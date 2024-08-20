#define _GNU_SOURCE
#include <libconfig.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "client.h"
#include "rdma_config.h"
#include "debug.h"
#include "ib.h"
#include "qp.h"
#include "setup_ib.h"
#include "sock.h"
#include "utils.h"

struct args
{
    struct IBRes *ib_res;
};

void *client_thread_write_signaled(void *arg)
{

    int ret = 0;
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    assert(ib_res->num_qps == 1);
    int msg_size = config_info.msg_size;
    int num_concurr_msgs = config_info.num_concurr_msgs;

    struct ibv_qp **qp = ib_res->qp;
    struct ibv_cq *cq = ib_res->cq;
    struct ibv_srq *srq = ib_res->srq;
    struct ibv_wc *wc = NULL;
    uint32_t lkey = ib_res->mr->lkey;

    char *buf_ptr = ib_res->ib_buf;
    char *buf_base = ib_res->ib_buf;
    int buf_offset = 0;
    size_t buf_size = ib_res->ib_buf_size;
    int num_completion = 0;

    uint32_t rkey = ib_res->rkey;
    uint64_t raddr = ib_res->raddr;
    uint64_t rptr = raddr;
    uint32_t rsize = ib_res->rsize;
    int roffset = 0;

    struct timeval start, end;
    double duration = 0.0;
    double throughput = 0.0;
    double latency = 0.0;

    wc = (struct ibv_wc *)calloc(NUM_WC, sizeof(struct ibv_wc));
    check(wc != NULL, "thread: failed to allocate wc.");

    for (int j = 0; j < num_concurr_msgs; j++)
    {
        ret = post_srq_recv(msg_size, lkey, (uint64_t)buf_ptr, srq, buf_ptr);
        if (unlikely(ret != 0))
        {
            log_error("post shared receive request fail");
            goto error;
        }
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr = buf_base + buf_offset;
    }

    printf("Client thread wait for start signal...\n");
    /* wait for start signal */

    bool start_sending = false;
    while (!start_sending)
    {
        num_completion = ibv_poll_cq(cq, NUM_WC, wc);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (unlikely(wc[i].status != IBV_WC_SUCCESS))
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
            if (wc[i].opcode == IBV_WC_RECV)
            {
                /* post a receive */
                post_srq_recv(msg_size, lkey, wc[i].wr_id, srq, buf_base);
                if ((wc[i].wc_flags & IBV_WC_WITH_IMM) && (ntohl(wc[i].imm_data) == MSG_CTL_START))
                {
                    start_sending = true;
                }
            }
        }
    }

    log_debug("thread: ready to send");

    buf_offset = 0;
    roffset = 0;
    debug("buf_ptr = %" PRIx64 "", (uint64_t)buf_ptr);

    long int opt_count = 0;
    bool stop = false;
    while (!stop)
    {
        ret = post_write_signaled(msg_size, lkey, 1, *qp, buf_ptr, rptr, rkey);

        while ((num_completion = ibv_poll_cq(cq, NUM_WC, wc)) == 0)
        {
        };
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (unlikely(wc[i].status != IBV_WC_SUCCESS))
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
            opt_count++;
            if (opt_count == NUM_WARMING_UP_OPS)
            {
                gettimeofday(&start, NULL);
            }
            if (opt_count == TOT_NUM_OPS)
            {
                gettimeofday(&end, NULL);
                stop = true;
            }
        }
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr = buf_base + buf_offset;
        roffset = (roffset + msg_size) % rsize;
        rptr = raddr + roffset;
    }

    duration = (double)((end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec) / 1000000);
    throughput = (double)(opt_count - NUM_WARMING_UP_OPS) / duration;
    latency = duration * 1000000 / (double)(opt_count - NUM_WARMING_UP_OPS);

    log_info("thread: throughput = %f (ops/s)", throughput);
    printf("thread: throughput = %f (ops/s) %f (Bytes/s); ops_count:%ld, duration: %f seconds \n", throughput,
           throughput * msg_size, opt_count - NUM_WARMING_UP_OPS, duration);
    printf("latency: %f\n", latency);

    ret = post_send_signaled(0, lkey, IB_WR_ID_STOP, MSG_CTL_STOP, qp[0], ib_res->ib_buf);
    bool finish = false;
    while (!finish)
    {
        num_completion = ibv_poll_cq(cq, NUM_WC, wc);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (wc[i].status != IBV_WC_SUCCESS)
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
            if (wc[i].opcode == IBV_WC_SEND)
            {
                finish = true;
            }
        }
    }
    free(wc);
    pthread_exit((void *)0);

error:
    free(wc);
    pthread_exit((void *)-1);
}

void *client_thread_write_unsignaled(void *arg)
{
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    assert(ib_res->num_qps == 1);
    int ret = 0;
    int msg_size = config_info.msg_size;
    /* int num_concurr_msgs = config_info.num_concurr_msgs; */

    struct ibv_qp **qp = ib_res->qp;
    struct ibv_cq *cq = ib_res->cq;
    struct ibv_srq *srq = ib_res->srq;
    struct ibv_wc *wc = NULL;
    uint32_t lkey = ib_res->mr->lkey;

    char *buf_ptr = ib_res->ib_buf;
    char *buf_base = ib_res->ib_buf;
    int buf_offset = 0;
    size_t buf_size = ib_res->ib_buf_size;
    int num_completion = 0;

    // remote key and address
    uint32_t rkey = ib_res->rkey;
    uint64_t raddr = ib_res->raddr;
    uint64_t rptr = raddr;
    uint32_t rsize = ib_res->rsize;
    int roffset = 0;

    struct timeval start, end;
    double duration = 0.0;
    double latency = 0.0;
    double throughput = 0.0;

    wc = (struct ibv_wc *)calloc(NUM_WC, sizeof(struct ibv_wc));
    check(wc != NULL, "thread: failed to allocate wc.");

    for (int j = 0; j < 40; j++)
    {
        ret = post_srq_recv(msg_size, lkey, (uint64_t)buf_ptr, srq, buf_ptr);
        if (unlikely(ret != 0))
        {
            log_error("post shared receive request fail");
            goto error;
        }
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr = buf_base + buf_offset;
    }

    log_debug("thread: ready to send");

    buf_offset = 0;
    roffset = 0;
    debug("buf_ptr = %" PRIx64 "", (uint64_t)buf_ptr);
    long int warm_up_iter = config_info.warm_up_iter;
    long int total_iter = config_info.total_iter;
    int signal_freq = config_info.signal_freq;
    long int opt_count = 0;
    while (true)
    {
        for (int i = 0; i < signal_freq; i++)
        {
            ret = post_write_unsignaled(msg_size, lkey, 1, *qp, buf_ptr, rptr, rkey);
            roffset = (roffset + msg_size) % rsize;
            rptr = raddr + roffset;
        }

        ret = post_write_signaled(msg_size, lkey, 1, *qp, buf_ptr, rptr, rkey);
        roffset = (roffset + msg_size) % rsize;
        rptr = raddr + roffset;

        do
        {
            num_completion = ibv_poll_cq(cq, NUM_WC, wc);
        } while (num_completion == 0);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (unlikely(wc[i].status != IBV_WC_SUCCESS))
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
        }
        opt_count++;
        if (opt_count == warm_up_iter)
        {
            gettimeofday(&start, NULL);
        }
        if (opt_count == total_iter)
        {
            gettimeofday(&end, NULL);
            break;
        }
    }

    duration = (double)((end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec) / 1000000);
    latency = duration * 1000000 / (double)(total_iter - warm_up_iter);

    throughput = (double)(total_iter - warm_up_iter) * (signal_freq + 1) * msg_size / duration;
    printf("latency: %f for %d unsignaled operations plus a signaled operation\n", latency, signal_freq);

    printf("throughput (Bytes/s): %f\n", throughput);
    ret = post_send_signaled(0, lkey, IB_WR_ID_STOP, MSG_CTL_STOP, qp[0], ib_res->ib_buf);
    bool finish = false;
    while (!finish)
    {
        num_completion = ibv_poll_cq(cq, NUM_WC, wc);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (wc[i].status != IBV_WC_SUCCESS)
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
            if (wc[i].opcode == IBV_WC_SEND)
            {
                finish = true;
            }
        }
    }
    free(wc);
    pthread_exit((void *)0);

error:
    free(wc);
    pthread_exit((void *)-1);
}

void *client_thread_write_imm_signaled(void *arg)
{
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    assert(ib_res->num_qps == 1);
    int ret = 0;
    int msg_size = config_info.msg_size;

    struct ibv_qp **qp = ib_res->qp;
    struct ibv_cq *cq = ib_res->cq;
    struct ibv_srq *srq = ib_res->srq;
    struct ibv_wc *wc = NULL;
    uint32_t lkey = ib_res->mr->lkey;

    char *buf_ptr = ib_res->ib_buf;
    char *buf_base = ib_res->ib_buf;
    int buf_offset = 0;
    size_t buf_size = ib_res->ib_buf_size;

    uint32_t rkey = ib_res->rkey;
    uint64_t raddr = ib_res->raddr;
    uint64_t rptr = raddr;
    uint32_t rsize = ib_res->rsize;
    int roffset = 0;

    bool stop = false;

    wc = (struct ibv_wc *)calloc(NUM_WC, sizeof(struct ibv_wc));
    check(wc != NULL, "thread: failed to allocate wc.");

    for (int j = 0; j < config_info.num_concurr_msgs; j++)
    {
        ret = post_srq_recv(msg_size, lkey, (uint64_t)buf_ptr, srq, buf_ptr);
        if (unlikely(ret != 0))
        {
            log_error("post shared receive request fail");
            goto error;
        }
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr = buf_base + buf_offset;
    }

    printf("Client thread- wait for start signal...\n");
    /* wait for start signal */

    int num_completion = 0;
    stop = false;
    while (!stop)
    {
        num_completion = ibv_poll_cq(cq, NUM_WC, wc);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (unlikely(wc[i].status != IBV_WC_SUCCESS))
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
            if (wc[i].opcode == IBV_WC_RECV)
            {
                /* post a receive */
                post_srq_recv(msg_size, lkey, wc[i].wr_id, srq, buf_base);

                if ((wc[i].wc_flags & IBV_WC_WITH_IMM) && (ntohl(wc[i].imm_data) == MSG_CTL_START))
                {
                    stop = true;
                }
            }
        }
    }

    log_debug("thread: ready to send");

    debug("buf_ptr = %" PRIx64 "", (uint64_t)buf_ptr);

    stop = false;
    buf_offset = 0;
    roffset = 0;
    while (!stop)
    {
        ret = post_write_imm_signaled(*qp, buf_ptr, msg_size, lkey, 0, rptr, rkey, 0);
        if (unlikely(ret != 0))
        {
            log_error("send write imme_data failed, error ret: %d", ret);
            goto error;
        }

        while ((num_completion = ibv_poll_cq(cq, NUM_WC, wc)) == 0)
        {
        };
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (unlikely(wc[i].status != IBV_WC_SUCCESS))
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
            if (wc[i].opcode == IBV_WC_RDMA_WRITE)
            {
            }
            if (wc[i].opcode == IBV_WC_RECV)
            {
                if ((wc[i].wc_flags & IBV_WC_WITH_IMM) && ntohl(wc[i].imm_data) == MSG_CTL_STOP)
                {
                    stop = true;
                }
            }
            post_srq_recv(msg_size, lkey, wc[i].wr_id, srq, buf_base);
        }
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr = buf_base + buf_offset;
        roffset = (roffset + msg_size) % rsize;
        rptr = raddr + roffset;
    }

    pthread_exit((void *)0);

error:
    pthread_exit((void *)-1);
    return NULL;
}

void *client_thread_write_imm_unsignaled(void *arg)
{
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    assert(ib_res->num_qps == 1);
    int ret = 0;
    int msg_size = config_info.msg_size;
    /* int num_concurr_msgs = config_info.num_concurr_msgs; */

    struct ibv_qp **qp = ib_res->qp;
    struct ibv_cq *cq = ib_res->cq;
    struct ibv_srq *srq = ib_res->srq;
    struct ibv_wc *wc = NULL;
    uint32_t lkey = ib_res->mr->lkey;

    char *buf_ptr = ib_res->ib_buf;
    char *buf_base = ib_res->ib_buf;
    int buf_offset = 0;
    size_t buf_size = ib_res->ib_buf_size;
    assert(buf_size % msg_size == 0);
    int num_completion = 0;

    int rbuf_offset = 0;
    uint32_t rbuf_size = ib_res->rsize;
    uint64_t rbuf_ptr = ib_res->raddr;
    uint64_t rbuf_base = ib_res->raddr;
    uint32_t rkey = ib_res->rkey;
    assert(rbuf_size % msg_size == 0);
    struct timeval start, end;
    double duration = 0.0;
    double latency = 0.0;
    double throughput = 0.0;

    wc = (struct ibv_wc *)calloc(NUM_WC, sizeof(struct ibv_wc));
    check(wc != NULL, "thread: failed to allocate wc.");

    for (int j = 0; j < config_info.num_concurr_msgs; j++)
    {
        ret = post_srq_recv(msg_size, lkey, (uint64_t)buf_ptr, srq, buf_ptr);
        if (unlikely(ret != 0))
        {
            log_error("post shared receive request fail");
            goto error;
        }
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr = buf_base + buf_offset;
    }

    log_debug("thread: ready to send");

    bool start_sending = false;

    while (start_sending != true)
    {
        num_completion = ibv_poll_cq(cq, NUM_WC, wc);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (unlikely(wc[i].status != IBV_WC_SUCCESS))
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
            if (wc[i].opcode == IBV_WC_RECV)
            {
                /* post a receive */
                post_srq_recv(msg_size, lkey, wc[i].wr_id, srq, (char *)wc[i].wr_id);

                if (ntohl(wc[i].imm_data) == MSG_CTL_START)
                {
                    log_debug("received start signal");
                    start_sending = true;
                    break;
                }
            }
        }
    }

    buf_offset = 0;
    debug("buf_ptr = %" PRIx64 "", (uint64_t)buf_ptr);
    long int warm_up_iter = config_info.warm_up_iter;
    long int total_iter = config_info.total_iter;
    int signal_freq = config_info.signal_freq;
    long int opt_count = 0;
    num_completion = 0;
    while (true)
    {
        for (int i = 0; i < signal_freq; i++)
        {
            ret = post_write_imm_unsignaled(*qp, buf_ptr, msg_size, lkey, 1, rbuf_ptr, rkey, 1);
            buf_offset = (buf_offset + msg_size) % buf_size;
            buf_ptr = buf_base + buf_offset;
            rbuf_offset = (rbuf_offset + msg_size) % rbuf_size;
            rbuf_ptr = rbuf_base + rbuf_offset;
        }

        ret = post_write_imm_signaled(*qp, buf_ptr, msg_size, lkey, 1, rbuf_ptr, rkey, 1);
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr = buf_base + buf_offset;
        rbuf_offset = (rbuf_offset + msg_size) % rbuf_size;
        rbuf_ptr = rbuf_base + rbuf_offset;
        do
        {
            num_completion = ibv_poll_cq(cq, NUM_WC, wc);
        } while (num_completion == 0);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (unlikely(wc[i].status != IBV_WC_SUCCESS))
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
        }
        opt_count++;
        if (opt_count == warm_up_iter)
        {
            gettimeofday(&start, NULL);
        }
        if (opt_count == total_iter)
        {
            gettimeofday(&end, NULL);
            break;
        }
    }

    duration = (double)((end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec) / 1000000);
    latency = duration * 1000000 / (double)(total_iter - warm_up_iter);

    throughput = (double)(total_iter - warm_up_iter) * (signal_freq + 1) * msg_size / duration;
    printf("latency: %f for %d unsignaled operations plus a signaled operation\n", latency, signal_freq);
    printf("throughput: %f (Bytes/s)\n", throughput);

    ret = post_send_signaled(0, lkey, IB_WR_ID_STOP, MSG_CTL_STOP, qp[0], buf_ptr);
    bool finish = false;
    while (!finish)
    {
        num_completion = ibv_poll_cq(cq, NUM_WC, wc);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (wc[i].status != IBV_WC_SUCCESS)
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
            if (wc[i].opcode == IBV_WC_SEND)
            {
                finish = true;
            }
        }
    }
    free(wc);
    pthread_exit((void *)0);

error:
    free(wc);
    pthread_exit((void *)-1);
}
void *client_thread_send_signaled(void *arg)
{
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    int ret = 0, n = 0;
    int msg_size = config_info.msg_size;
    int num_concurr_msgs = config_info.num_concurr_msgs;

    struct ibv_qp **qp = ib_res->qp;
    struct ibv_cq *cq = ib_res->cq;
    struct ibv_srq *srq = ib_res->srq;
    struct ibv_wc *wc = NULL;
    uint32_t lkey = ib_res->mr->lkey;

    char *buf_ptr = ib_res->ib_buf;
    char *buf_base = ib_res->ib_buf;
    int buf_offset = 0;
    size_t buf_size = ib_res->ib_buf_size;

    bool start_sending = false;
    struct timeval start, end;
    long ops_count = 0;
    double duration = 0.0;
    double throughput = 0.0;
    double latency = 0.0;

    /* pre-post recvs */
    wc = (struct ibv_wc *)calloc(NUM_WC, sizeof(struct ibv_wc));
    check(wc != NULL, "thread: failed to allocate wc.");

    for (int j = 0; j < num_concurr_msgs; j++)
    {
        ret = post_srq_recv(msg_size, lkey, (uint64_t)buf_ptr, srq, buf_ptr);
        if (unlikely(ret != 0))
        {
            log_error("post shared receive request fail");
            goto error;
        }
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr = buf_base + buf_offset;
    }

    printf("Client thread wait for start signal...\n");
    /* wait for start signal */
    while (start_sending != true)
    {
        do
        {
            n = ibv_poll_cq(cq, NUM_WC, wc);
        } while (n < 1);
        check(n > 0, "thread: failed to poll cq");

        for (int i = 0; i < n; i++)
        {
            if (wc[i].status != IBV_WC_SUCCESS)
            {
                check(0, "thread: wc failed status: %s.", ibv_wc_status_str(wc[i].status));
            }
            if (wc[i].opcode == IBV_WC_RECV)
            {
                /* post a receive */
                post_srq_recv(msg_size, lkey, wc[i].wr_id, srq, buf_ptr);

                if (ntohl(wc[i].imm_data) == MSG_CTL_START)
                {
                    log_debug("received start signal");
                    start_sending = true;
                    break;
                }
            }
        }
    }

    log_debug("thread: ready to send");

    /* pre-post sends */
    buf_offset = 0;
    log_debug("buf_ptr = %" PRIx64 "", (uint64_t)buf_ptr);

    bool stop = false;
    while (!stop)
    {
        ret = post_send_signaled(msg_size, lkey, (uint64_t)buf_ptr, 1, *qp, buf_ptr);
        /* poll cq */
        do
        {
            n = ibv_poll_cq(cq, NUM_WC, wc);
        } while (n == 0);
        if (n < 0)
        {
            check(0, "thread: Failed to poll cq");
        }

        for (int i = 0; i < n; i++)
        {
            if (wc[i].status != IBV_WC_SUCCESS)
            {
                if (wc[i].opcode == IBV_WC_SEND)
                {
                    check(0, "thread: send failed status: %s; wr_id = %" PRIx64 "", ibv_wc_status_str(wc[i].status),
                          wc[i].wr_id);
                }
                else
                {
                    check(0, "thread: recv failed status: %s; wr_id = %" PRIx64 "", ibv_wc_status_str(wc[i].status),
                          wc[i].wr_id);
                }
            }

            if (wc[i].opcode == IBV_WC_SEND)
            {
                ops_count += 1;

                if (ops_count == config_info.warm_up_iter)
                {
                    gettimeofday(&start, NULL);
                }

                if (ops_count == config_info.total_iter)
                {
                    gettimeofday(&end, NULL);
                    ret = post_send_signaled(0, lkey, IB_WR_ID_STOP, MSG_CTL_STOP, qp[0], buf_ptr);
                }
                if (wc[i].wr_id == IB_WR_ID_STOP)
                {
                    stop = true;
                    break;
                }

                /* post a new receive */
            }
            /* ret = post_srq_recv(msg_size, lkey, wc[i].wr_id, srq, buf_ptr); */
        } /* loop through all wc */
    }

    /* dump statistics */
    duration = (double)((end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec) / 1000000);
    throughput = (double)(config_info.total_iter - config_info.warm_up_iter) / duration;
    latency = duration * 1000000 / (double)(config_info.total_iter - config_info.warm_up_iter);

    log("thread: throughput = %f (ops/s)", throughput);
    printf("thread: throughput = %f (ops/s) %f (Bytes/s)\n", throughput, throughput * msg_size);
    printf("latency: %f usec", latency);

    free(wc);
    pthread_exit((void *)0);

error:
    if (wc != NULL)
    {
        free(wc);
    }
    pthread_exit((void *)-1);
}

void *client_thread_send_unsignaled(void *arg)
{
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    assert(ib_res->num_qps == 1);
    int ret = 0;
    int msg_size = config_info.msg_size;
    /* int num_concurr_msgs = config_info.num_concurr_msgs; */

    struct ibv_qp **qp = ib_res->qp;
    struct ibv_cq *cq = ib_res->cq;
    struct ibv_srq *srq = ib_res->srq;
    struct ibv_wc *wc = NULL;
    uint32_t lkey = ib_res->mr->lkey;

    char *buf_ptr = ib_res->ib_buf;
    char *buf_base = ib_res->ib_buf;
    int buf_offset = 0;
    size_t buf_size = ib_res->ib_buf_size;
    assert(buf_size % msg_size == 0);
    int num_completion = 0;

    struct timeval start, end;
    double duration = 0.0;
    double latency = 0.0;
    double throughput = 0.0;

    wc = (struct ibv_wc *)calloc(NUM_WC, sizeof(struct ibv_wc));
    check(wc != NULL, "thread: failed to allocate wc.");

    for (int j = 0; j < config_info.num_concurr_msgs; j++)
    {
        ret = post_srq_recv(msg_size, lkey, (uint64_t)buf_ptr, srq, buf_ptr);
        if (unlikely(ret != 0))
        {
            log_error("post shared receive request fail");
            goto error;
        }
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr = buf_base + buf_offset;
    }

    log_debug("thread: ready to send");

    bool start_sending = false;

    while (start_sending != true)
    {
        num_completion = ibv_poll_cq(cq, NUM_WC, wc);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (unlikely(wc[i].status != IBV_WC_SUCCESS))
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
            if (wc[i].opcode == IBV_WC_RECV)
            {
                /* post a receive */
                post_srq_recv(msg_size, lkey, wc[i].wr_id, srq, (char *)wc[i].wr_id);

                if (ntohl(wc[i].imm_data) == MSG_CTL_START)
                {
                    log_debug("received start signal");
                    start_sending = true;
                    break;
                }
            }
        }
    }

    buf_offset = 0;
    debug("buf_ptr = %" PRIx64 "", (uint64_t)buf_ptr);
    long int warm_up_iter = config_info.warm_up_iter;
    long int total_iter = config_info.total_iter;
    int signal_freq = config_info.signal_freq;
    long int opt_count = 0;
    num_completion = 0;
    while (true)
    {
        for (int i = 0; i < signal_freq; i++)
        {
            ret = post_send_unsignaled(msg_size, lkey, 1, 1, *qp, buf_ptr);
            buf_offset = (buf_offset + msg_size) % buf_size;
            buf_ptr = buf_base + buf_offset;
        }

        ret = post_send_signaled(msg_size, lkey, 1, 1, *qp, buf_ptr);
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr = buf_base + buf_offset;

        do
        {
            num_completion = ibv_poll_cq(cq, NUM_WC, wc);
        } while (num_completion == 0);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (unlikely(wc[i].status != IBV_WC_SUCCESS))
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
        }
        opt_count++;
        if (opt_count == warm_up_iter)
        {
            gettimeofday(&start, NULL);
        }
        if (opt_count == total_iter)
        {
            gettimeofday(&end, NULL);
            break;
        }
    }

    duration = (double)((end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec) / 1000000);
    latency = duration * 1000000 / (double)(total_iter - warm_up_iter);

    throughput = (double)(total_iter - warm_up_iter) * (signal_freq + 1) * msg_size / duration;
    printf("latency: %f for %d unsignaled operations plus a signaled operation\n", latency, signal_freq);
    printf("throughput: %f (Bytes/s)\n", throughput);

    ret = post_send_signaled(0, lkey, IB_WR_ID_STOP, MSG_CTL_STOP, qp[0], buf_ptr);
    bool finish = false;
    while (!finish)
    {
        num_completion = ibv_poll_cq(cq, NUM_WC, wc);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        for (int i = 0; i < num_completion; i++)
        {
            if (wc[i].status != IBV_WC_SUCCESS)
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }
            if (wc[i].opcode == IBV_WC_SEND)
            {
                finish = true;
            }
        }
    }
    free(wc);
    pthread_exit((void *)0);

error:
    free(wc);
    pthread_exit((void *)-1);
}
int run_client(struct IBRes *ib_res)
{
    int ret = 0;

    pthread_t *client_threads = NULL;
    pthread_attr_t attr;
    void *status;
    void *(*client_thread_func)(void *) = NULL;
    int benchmark_type = config_info.benchmark_type;

    log(LOG_SUB_HEADER, "Run Client");

    /* initialize threads */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    client_threads = (pthread_t *)malloc(sizeof(pthread_t));
    check(client_threads != NULL, "Failed to allocate client_threads.");

    if (benchmark_type == SEND_SIGNALED)
    {
        client_thread_func = client_thread_send_signaled;
    }
    else if (benchmark_type == SEND_UNSIGNALED)
    {
        client_thread_func = client_thread_send_unsignaled;
    }
    else if (benchmark_type == WRITE_SIGNALED)
    {
        client_thread_func = client_thread_write_signaled;
    }
    else if (benchmark_type == WRITE_UNSIGNALED)
    {
        client_thread_func = client_thread_write_unsignaled;
    }
    else if (benchmark_type == WRITE_IMM_SIGNALED)
    {
        client_thread_func = client_thread_write_imm_signaled;
    }
    else if (benchmark_type == WRITE_IMM_UNSIGNALED)
    {
        client_thread_func = client_thread_write_imm_unsignaled;
    }
    else
    {
        log_error("The benchmark_type is illegal, %d", benchmark_type);
    }
    struct args args = {.ib_res = ib_res};

    ret = pthread_create(client_threads, &attr, client_thread_func, &args);
    if (unlikely(ret != 0))
    {
        log_error("Failed to create client thread");
    }

    ret = pthread_join(*client_threads, &status);
    if ((long)status != 0)
    {
        goto error;
    }

    pthread_attr_destroy(&attr);
    free(client_threads);
    return 0;

error:
    if (client_threads != NULL)
    {
        free(client_threads);
    }

    pthread_attr_destroy(&attr);
    return -1;
}

int connect_qp_client(struct IBRes *ib_res)
{
    int ret = 0, n = 0, i = 0;
    int num_peers = 1;

    config_info.self_sockfd = -1;
    char sock_buf[64] = {'\0'};

    struct QPInfo *local_qp_info = NULL;
    struct QPInfo *remote_qp_info = NULL;

    config_info.peer_sockfds = (int *)calloc(num_peers, sizeof(int));
    check(config_info.peer_sockfds != NULL, "Failed to allocate peer_sockfd");

    for (i = 0; i < num_peers; i++)
    {
        config_info.peer_sockfds[i] = sock_create_connect(config_info.server_ip, config_info.sock_port);
        check(config_info.peer_sockfds[i] > 0, "Failed to create peer_sockfd[%d]", i);
    }

    /* init local qp_info */
    local_qp_info = (struct QPInfo *)calloc(num_peers, sizeof(struct QPInfo));
    check(local_qp_info != NULL, "Failed to allocate local_qp_info");

    for (i = 0; i < num_peers; i++)
    {
        local_qp_info[i].lid = ib_res->port_attr.lid;
        local_qp_info[i].qp_num = ib_res->qp[i]->qp_num;
        /* local_qp_info[i].rank = config_info.rank; */
        local_qp_info[i].sgid_index = config_info.sgid_index;
        local_qp_info[i].gid = ib_res->sgid;
        local_qp_info[i].ib_port = config_info.ib_port;
        local_qp_info[i].rkey = ib_res->mr->rkey;
        local_qp_info[i].raddr = (uint64_t)ib_res->mr->addr;
        local_qp_info[i].rsize = ib_res->mr->length;
        local_qp_info[i].psn = 0;
    }

    /* send qp_info to server */
    for (i = 0; i < num_peers; i++)
    {
        ret = sock_set_qp_info(config_info.peer_sockfds[i], &local_qp_info[i]);
        check(ret == 0, "Failed to send qp_info[%d] to server", i);
    }

    /* get qp_info from server */
    remote_qp_info = (struct QPInfo *)calloc(num_peers, sizeof(struct QPInfo));
    check(remote_qp_info != NULL, "Failed to allocate remote_qp_info");

    for (i = 0; i < num_peers; i++)
    {
        ret = sock_get_qp_info(config_info.peer_sockfds[i], &remote_qp_info[i]);
        check(ret == 0, "Failed to get qp_info[%d] from server", i);
    }

    // TODO temporary setting for one server one client benchmark
    assert(num_peers == 1);
    ib_res->raddr = remote_qp_info[0].raddr;
    ib_res->rkey = remote_qp_info[0].rkey;
    ib_res->rsize = remote_qp_info[0].rsize;

    /* change QP state to RTS */
    /* send qp_info to client */
    int peer_ind = -1;
    log(LOG_SUB_HEADER, "IB Config");
    for (i = 0; i < num_peers; i++)
    {
        peer_ind = 0;

        printf("Loca qp_num: %" PRIu32 ", Remote qp_num %" PRIu32 "\n", local_qp_info[peer_ind].qp_num,
               remote_qp_info[i].qp_num);

        printf("Local QP info: \n");
        print_qp_info(&local_qp_info[peer_ind]);
        printf("\n");
        printf("Remote QP info: \n");
        print_qp_info(&remote_qp_info[i]);
        printf("\n");

        ret = modify_qp_to_rts(ib_res->qp[peer_ind], &local_qp_info[peer_ind], &remote_qp_info[i]);
        check(ret == 0, "Failed to modify qp[%d] to rts", peer_ind);
        log("\tLocal qp[%" PRIu32 "] <-> Remote qp[%" PRIu32 "]", ib_res->qp[peer_ind]->qp_num,
            remote_qp_info[i].qp_num);
    }
    log(LOG_SUB_HEADER, "End of IB Config");

    /* sync with server */
    for (i = 0; i < num_peers; i++)
    {
        n = sock_write(config_info.peer_sockfds[i], sock_buf, sizeof(SOCK_SYNC_MSG));
        check(n == sizeof(SOCK_SYNC_MSG), "Failed to write sync to client[%d]", i);
    }

    for (i = 0; i < num_peers; i++)
    {
        n = sock_read(config_info.peer_sockfds[i], sock_buf, sizeof(SOCK_SYNC_MSG));
        check(n == sizeof(SOCK_SYNC_MSG), "Failed to receive sync from client");
    }

    free(local_qp_info);
    free(remote_qp_info);
    return 0;

error:
    if (config_info.peer_sockfds != NULL)
    {
        for (i = 0; i < num_peers; i++)
        {
            if (config_info.peer_sockfds[i] > 0)
            {
                close(config_info.peer_sockfds[i]);
            }
        }
        free(config_info.peer_sockfds);
    }

    if (local_qp_info != NULL)
    {
        free(local_qp_info);
    }

    if (remote_qp_info != NULL)
    {
        free(remote_qp_info);
    }

    return -1;
}
