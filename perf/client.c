#include "rdma-bench_cfg.h"
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#define _GNU_SOURCE
#include <libconfig.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "client.h"
#include "ib.h"
#include "log.h"
#include "qp.h"
#include "rdma_config.h"
#include "setup_ib.h"
#include "sock.h"
#include "utils.h"

#define NS_PER_SEC 1E9  /* Nano-seconds per second */
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
        ret = post_srq_recv(srq, buf_ptr, msg_size, lkey, (uint64_t)buf_ptr);
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
                post_srq_recv(srq, buf_base, msg_size, lkey, wc[i].wr_id);
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

    long int opt_count = 0;
    bool stop = false;
    while (!stop)
    {
        ret = post_write_signaled(*qp, buf_ptr, msg_size, lkey, 1, rptr, rkey);

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

    ret = post_send_signaled(qp[0], ib_res->ib_buf, 0, lkey, IB_WR_ID_STOP, MSG_CTL_STOP);
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

double calculate_timediff_nsec(struct timespec *end, struct timespec *start)
{
    long diff;

    diff = (end->tv_sec - start->tv_sec) * NS_PER_SEC;
    diff += end->tv_nsec;
    diff -= start->tv_nsec;

    return (double)diff;
}

void *client_thread_write_unsignaled(void *arg)
{
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    assert(ib_res->num_qps == 1);
    int ret = 0;
    int msg_size = config_info.msg_size;
    /* int num_concurr_msgs = config_info.num_concurr_msgs; */
    struct timespec start;
    struct timespec end;

    struct ibv_qp **qp = ib_res->qp;
    struct ibv_cq *cq = ib_res->cq;
    struct ibv_srq *srq = ib_res->srq;
    struct ibv_wc *wc = NULL;
    uint32_t lkey = ib_res->mr->lkey;

    int num_completion = 0;

    // remote key and address
    uint32_t rkey = ib_res->rkey;
    uint64_t raddr = ib_res->raddr;
    uint32_t rsize = ib_res->rsize;

    double duration = 0.0;
    double latency = 0.0;
    double rps = 0.0;

    long int opt_count = 0;
    char monitor = '1';
    uint64_t send_msg_buffer;
    uint64_t recv_msg_buffer;
    long int total_iter = config_info.total_iter;

    assert(ib_res->ib_buf_size == config_info.msg_size * 4);
    char* send_buf_ptr = ib_res->ib_buf;
    volatile char* recv_buf_ptr = ib_res->ib_buf + config_info.msg_size;

    char* two_side_send_buf_ptr = ib_res->ib_buf + config_info.msg_size * 2;
    char* two_side_recv_buf_ptr = ib_res->ib_buf + config_info.msg_size * 3;

    uint64_t remote_recv_buf_ptr = raddr + config_info.msg_size;
    uint64_t remote_send_buf_ptr = raddr;

    char *send_copy_buf = (char*)malloc(config_info.msg_size);
    memset(send_copy_buf, 0, config_info.msg_size);
    send_copy_buf[0] = monitor;
    char *recv_copy_buf = (char*)malloc(config_info.msg_size);
    memset(recv_copy_buf, 0, config_info.msg_size);
    recv_copy_buf[0] = monitor;

    log_info("msg_sz: %d", config_info.msg_size);
    log_info("buffersz: %d", ib_res->ib_buf_size);

    memset((void*)recv_buf_ptr, 0, config_info.msg_size);
    memset(send_buf_ptr, 0, config_info.msg_size);
    send_buf_ptr[0] = monitor;

    log_info("send buf: %s", send_buf_ptr);
    log_info("recv buf: %s", recv_buf_ptr);

    log_info("local send addr: %lu, local recv addr: %lu, remote send: %lu, remote recv: %lu", (uint64_t)send_buf_ptr, (uint64_t)recv_buf_ptr, remote_send_buf_ptr, remote_recv_buf_ptr);

    print_benchmark_cfg(&config_info);


    assert(rsize == ib_res->ib_buf_size);
    wc = (struct ibv_wc *)calloc(NUM_WC, sizeof(struct ibv_wc));
    check(wc != NULL, "thread: failed to allocate wc.");
    log_info("thread: ready to send");

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &start) != 0)
    {
        log_error("get time error");
    }
    log_info("send buf: %s", send_buf_ptr);
    log_info("recv buf: %s", recv_buf_ptr);

    int wr_id = 0;

    ret = post_srq_recv(srq, two_side_recv_buf_ptr, msg_size, lkey, wr_id++);
    if (unlikely(ret != 0))
    {
        log_error("post shared receive request fail");
        goto error;
    }

    while(opt_count < config_info.total_iter) {
        if (config_info.copy_mode == 0) {
            // log_info("!! post two side");
            ret = post_send_signaled(*qp, two_side_send_buf_ptr, msg_size, lkey, wr_id++, 0);
            if (unlikely(ret != 0))
            {
                log_error("post two side send fail");
                goto error;
            }
            do
            {
                num_completion = ibv_poll_cq(cq, 1, wc);
            } while (num_completion == 0);
            if (unlikely(num_completion < 0))
            {
                log_error("failed to poll cq");
                goto error;
            }

            do
            {
                num_completion = ibv_poll_cq(cq, 1, wc);
            } while (num_completion == 0);
            if (unlikely(num_completion < 0))
            {
                log_error("failed to poll cq");
                goto error;
            }
            ret = post_srq_recv(srq, two_side_recv_buf_ptr, msg_size, lkey, wr_id++);
            if (unlikely(ret != 0))
            {
                log_error("post shared receive request fail");
                goto error;
            }
        } else {
            memcpy(send_buf_ptr, send_copy_buf, config_info.msg_size);
        }


        // log_debug("send: %s", send_buf_ptr);
        ret = post_write_signaled(*qp, send_buf_ptr, msg_size, lkey, wr_id++, remote_recv_buf_ptr, rkey);
        if (ret != RDMA_SUCCESS) {
            log_error("post write failed");

        }

        // log_info("send buf: %s", send_buf_ptr);
        // log_info("recv buf: %s", recv_buf_ptr);
        do
        {
            num_completion = ibv_poll_cq(cq, 1, wc);
        } while (num_completion == 0);
        if (unlikely(num_completion < 0))
        {
            log_error("failed to poll cq");
            goto error;
        }
        // log_debug("get notification");

        if (config_info.copy_mode == 0) {
            do
            {
                num_completion = ibv_poll_cq(cq, 1, wc);
            } while (num_completion == 0);
            if (unlikely(num_completion < 0))
            {
                log_error("failed to poll cq");
                goto error;
            }
            ret = post_srq_recv(srq, two_side_recv_buf_ptr, msg_size, lkey, wr_id++);
            if (unlikely(ret != 0))
            {
                log_error("post shared receive request fail");
                goto error;
            }

            ret = post_send_signaled(*qp, two_side_send_buf_ptr, msg_size, lkey, wr_id++, 0);
            if (unlikely(ret != 0))
            {
                log_error("post two side send fail");
                goto error;
            }
            do
            {
                num_completion = ibv_poll_cq(cq, 1, wc);
            } while (num_completion == 0);
            if (unlikely(num_completion < 0))
            {
                log_error("failed to poll cq");
                goto error;
            }
        }
        // log_info("waiting");
        while (*recv_buf_ptr != monitor) {

        }
        if (config_info.copy_mode == 1) {
            memcpy(recv_copy_buf, (void*)recv_buf_ptr, config_info.msg_size);
        }
        // log_info("finished waiting");
        // reset the buf 
        memset((void*)recv_buf_ptr, 0, config_info.msg_size);
        opt_count++;

    }

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &end) != 0)
    {
        log_error("get time error");
    }


    // for (int j = 0; j < 40; j++)
    // {
    //     ret = post_srq_recv(srq, buf_ptr, msg_size, lkey, (uint64_t)buf_ptr);
    //     if (unlikely(ret != 0))
    //     {
    //         log_error("post shared receive request fail");
    //         goto error;
    //     }
    //     buf_offset = (buf_offset + msg_size) % buf_size;
    //     buf_ptr = buf_base + buf_offset;
    // }
    //
    //
    // buf_offset = 0;
    // roffset = 0;


    duration = calculate_timediff_nsec(&end, &start);
    latency = duration / total_iter / 1E3;

    rps = (double)(total_iter) / duration * 1E9;

    printf("round trip latency per request: %f usec\n", latency);

    printf("rps : %f\n", rps);

    free(wc);
    free(send_copy_buf);
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
        ret = post_srq_recv(srq, buf_ptr, msg_size, lkey, (uint64_t)buf_ptr);
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
                post_srq_recv(srq, buf_base, msg_size, lkey, wc[i].wr_id);

                if ((wc[i].wc_flags & IBV_WC_WITH_IMM) && (ntohl(wc[i].imm_data) == MSG_CTL_START))
                {
                    stop = true;
                }
            }
        }
    }

    log_debug("thread: ready to send");

    stop = false;
    buf_offset = 0;
    roffset = 0;
    while (!stop)
    {
        ret = post_write_imm_signaled(*qp, buf_ptr, msg_size, lkey, 0, rptr, rkey, 0);
        if (unlikely(ret != RDMA_SUCCESS))
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
            post_srq_recv(srq, buf_base, msg_size, lkey, wc[i].wr_id);
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
        ret = post_srq_recv(srq, buf_ptr, msg_size, lkey, (uint64_t)buf_ptr);
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
                post_srq_recv(srq, (char *)wc[i].wr_id, msg_size, lkey, wc[i].wr_id);

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

    ret = post_send_signaled(qp[0], buf_ptr, 0, lkey, IB_WR_ID_STOP, MSG_CTL_STOP);
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
    assert(ib_res->num_qps == 1);
    int ret = 0;
    int msg_size = config_info.msg_size;
    /* int num_concurr_msgs = config_info.num_concurr_msgs; */
    struct timespec start;
    struct timespec end;

    struct ibv_qp **qp = ib_res->qp;
    struct ibv_cq *cq = ib_res->cq;
    struct ibv_srq *srq = ib_res->srq;
    struct ibv_wc *wc = NULL;
    uint32_t lkey = ib_res->mr->lkey;

    int num_completion = 0;

    // remote key and address
    uint32_t rkey = ib_res->rkey;
    uint64_t raddr = ib_res->raddr;
    uint32_t rsize = ib_res->rsize;

    double duration = 0.0;
    double latency = 0.0;
    double rps = 0.0;

    long int opt_count = 0;
    char monitor = '1';
    uint64_t send_msg_buffer;
    uint64_t recv_msg_buffer;
    long int total_iter = config_info.total_iter;

    assert(ib_res->ib_buf_size == config_info.msg_size * 4);
    char* send_buf_ptr = ib_res->ib_buf;
    volatile char* recv_buf_ptr = ib_res->ib_buf + config_info.msg_size;

    char* two_side_send_buf_ptr = ib_res->ib_buf + config_info.msg_size * 2;
    char* two_side_recv_buf_ptr = ib_res->ib_buf + config_info.msg_size * 3;

    uint64_t remote_recv_buf_ptr = raddr + config_info.msg_size;
    uint64_t remote_send_buf_ptr = raddr;

    char *send_copy_buf = (char*)malloc(config_info.msg_size);
    memset(send_copy_buf, 0, config_info.msg_size);
    send_copy_buf[0] = monitor;
    char *recv_copy_buf = (char*)malloc(config_info.msg_size);
    memset(recv_copy_buf, 0, config_info.msg_size);
    recv_copy_buf[0] = monitor;

    log_info("msg_sz: %d", config_info.msg_size);
    log_info("buffersz: %d", ib_res->ib_buf_size);

    memset((void*)recv_buf_ptr, 0, config_info.msg_size);
    memset(send_buf_ptr, 0, config_info.msg_size);
    send_buf_ptr[0] = monitor;

    log_info("send buf: %s", send_buf_ptr);
    log_info("recv buf: %s", recv_buf_ptr);

    log_info("local send addr: %lu, local recv addr: %lu, remote send: %lu, remote recv: %lu", (uint64_t)send_buf_ptr, (uint64_t)recv_buf_ptr, remote_send_buf_ptr, remote_recv_buf_ptr);

    print_benchmark_cfg(&config_info);


    assert(rsize == ib_res->ib_buf_size);
    wc = (struct ibv_wc *)calloc(NUM_WC, sizeof(struct ibv_wc));
    check(wc != NULL, "thread: failed to allocate wc.");
    log_info("thread: ready to send");

    log_info("send buf: %s", send_buf_ptr);
    log_info("recv buf: %s", recv_buf_ptr);

    int wr_id = 0;

    ret = post_srq_recv(srq, two_side_recv_buf_ptr, msg_size, lkey, wr_id++);
    if (unlikely(ret != 0))
    {
        log_error("post shared receive request fail");
        goto error;
    }

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &start) != 0)
    {
        log_error("get time error");
    }
    while(opt_count < config_info.total_iter) {
            // log_info("!! post two side");
            ret = post_send_signaled(*qp, two_side_send_buf_ptr, msg_size, lkey, wr_id++, 0);
            if (unlikely(ret != 0))
            {
                log_error("post two side send fail");
                goto error;
            }
            do
            {
                num_completion = ibv_poll_cq(cq, 2, wc);
            } while (num_completion == 0);
            if (unlikely(num_completion < 0))
            {
                log_error("failed to poll cq");
                goto error;
            }

            do
            {
                num_completion = ibv_poll_cq(cq, 1, wc);
            } while (num_completion == 0);
            if (unlikely(num_completion < 0))
            {
                log_error("failed to poll cq");
                goto error;
            }
            ret = post_srq_recv(srq, two_side_recv_buf_ptr, msg_size, lkey, wr_id++);
            if (unlikely(ret != 0))
            {
                log_error("post shared receive request fail");
                goto error;
            }

        opt_count++;

    }

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &end) != 0)
    {
        log_error("get time error");
    }


    // for (int j = 0; j < 40; j++)
    // {
    //     ret = post_srq_recv(srq, buf_ptr, msg_size, lkey, (uint64_t)buf_ptr);
    //     if (unlikely(ret != 0))
    //     {
    //         log_error("post shared receive request fail");
    //         goto error;
    //     }
    //     buf_offset = (buf_offset + msg_size) % buf_size;
    //     buf_ptr = buf_base + buf_offset;
    // }
    //
    //
    // buf_offset = 0;
    // roffset = 0;


    duration = calculate_timediff_nsec(&end, &start);
    latency = duration / total_iter / 1E3;

    rps = (double)(total_iter) / duration * 1E9;

    printf("round trip latency per request: %f usec\n", latency);

    printf("rps : %f\n", rps);

    free(wc);
    free(send_copy_buf);
    pthread_exit((void *)0);

error:
    free(wc);
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
        ret = post_srq_recv(srq, buf_ptr, msg_size, lkey, (uint64_t)buf_ptr);
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
                post_srq_recv(srq, (char *)wc[i].wr_id, msg_size, lkey, wc[i].wr_id);

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
    long int warm_up_iter = config_info.warm_up_iter;
    long int total_iter = config_info.total_iter;
    int signal_freq = config_info.signal_freq;
    long int opt_count = 0;
    num_completion = 0;
    while (true)
    {
        for (int i = 0; i < signal_freq; i++)
        {
            ret = post_send_unsignaled(*qp, buf_ptr, msg_size, lkey, 1, 1);
            buf_offset = (buf_offset + msg_size) % buf_size;
            buf_ptr = buf_base + buf_offset;
        }

        ret = post_send_signaled(*qp, buf_ptr, msg_size, lkey, 1, 1);
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

    ret = post_send_signaled(qp[0], buf_ptr, 0, lkey, IB_WR_ID_STOP, MSG_CTL_STOP);
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
        log_info("\tLocal qp[%" PRIu32 "] <-> Remote qp[%" PRIu32 "]", ib_res->qp[peer_ind]->qp_num,
                 remote_qp_info[i].qp_num);
    }

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
