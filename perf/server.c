#include "rdma_config.h"
#include <libconfig.h>
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "ib.h"
#include "log.h"
#include "qp.h"
#include "rdma-bench_cfg.h"
#include "server.h"
#include "setup_ib.h"
#include "sock.h"
#include "utils.h"

struct args
{
    struct IBRes *ib_res;
};
void *server_thread_write_signaled(void *arg)
{
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    assert(ib_res->num_qps == 1);
    int ret = 0;
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

    /* signal the client to start */
    printf("signal the client to start...\n");

    ret = post_send_signaled(qp[0], buf_base, 0, lkey, 0, MSG_CTL_START);
    if (unlikely(ret != RDMA_SUCCESS))
    {
        log_error("post start fail");
        goto error;
    }
    // TODO add comfirmation of start

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
            if (wc[i].opcode == IBV_WC_RECV)
            {
                /* post a receive */
                post_srq_recv(srq, buf_base, msg_size, lkey, wc[i].wr_id);
                if ((wc[i].wc_flags & IBV_WC_WITH_IMM) && (ntohl(wc[i].imm_data) == MSG_CTL_STOP))
                {
                    finish = true;
                }
            }
        }
    }
    free(wc);
    pthread_exit((void *)0);
error:
    free(wc);
    pthread_exit((void *)-1);
}

void *server_thread_write_unsignaled(void *arg)
{
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    assert(ib_res->num_qps == 1);
    int ret = 0;
    int msg_size = config_info.msg_size;
    int num_concurr_msgs = config_info.num_concurr_msgs;

    log_info("!!!num_concurr_msgs", num_concurr_msgs);

    struct ibv_qp **qp = ib_res->qp;
    struct ibv_cq *cq = ib_res->cq;
    struct ibv_srq *srq = ib_res->srq;
    struct ibv_wc *wc = NULL;
    uint32_t lkey = ib_res->mr->lkey;
    uint32_t rkey = ib_res->rkey;
    // the remote memory's address
    uint64_t raddr = ib_res->raddr;
    // the remote memory
    uint64_t rptr = raddr;
    uint32_t rsize = ib_res->rsize;

    struct timespec start;
    struct timespec end;
    // the local memory
    //
    wc = (struct ibv_wc *)calloc(NUM_WC, sizeof(struct ibv_wc));

    char monitor = '1';
    int num_completion = 0;
    uint64_t send_msg_buffer;
    uint64_t recv_msg_buffer;

    log_info("local addr: %lu, remote addr: %lu", (uint64_t)ib_res->ib_buf, raddr);

    print_benchmark_cfg(&config_info);

    assert(ib_res->ib_buf_size == config_info.msg_size * 2);

    char* send_buf_ptr = ib_res->ib_buf;
    volatile char* recv_buf_ptr = ib_res->ib_buf + config_info.msg_size;

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
    log_info("send cpy: %s", send_copy_buf);
    log_info("recv cpy: %s", recv_copy_buf);

    log_info("local send addr: %lu, local recv addr: %lu, remote send: %lu, remote recv: %lu", (uint64_t)send_buf_ptr, (uint64_t)recv_buf_ptr, remote_send_buf_ptr, remote_recv_buf_ptr);

    assert(ib_res->ib_buf_size == config_info.msg_size * 2);
    assert(rsize == ib_res->ib_buf_size);
    int opt_count = 0;
    print_benchmark_cfg(&config_info);

    while(true) {
        if (config_info.copy_mode == 0) {
            sock_read(config_info.peer_sockfds, &recv_msg_buffer, sizeof(uint64_t));
            sock_write(config_info.peer_sockfds, &send_msg_buffer, sizeof(uint64_t));
        } 
        log_debug("waiting");
        while (*recv_buf_ptr != monitor) {
        }
        log_debug("not waiting");
        if (config_info.copy_mode == 1) {
            memcpy(recv_copy_buf, (void*)recv_buf_ptr, config_info.msg_size);
        }

        // reset the buf 
        memset((void*)recv_buf_ptr, 0, config_info.msg_size);

        if (config_info.copy_mode == 0) {
            sock_write(config_info.peer_sockfds, &send_msg_buffer, sizeof(uint64_t));
            sock_read(config_info.peer_sockfds, &recv_msg_buffer, sizeof(uint64_t));
        } else {
            memcpy(send_buf_ptr, send_copy_buf, config_info.msg_size);
        }


        log_debug("send buf: %s", send_buf_ptr);
        ret = post_write_signaled(*qp, send_buf_ptr, msg_size, lkey, opt_count, remote_recv_buf_ptr, rkey);
        if (ret != RDMA_SUCCESS) {
            log_error("post write failed");

        }
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
        log_debug("get notification");
        // ensure the wr_id is unique
        opt_count++;


    }



    free(wc);
    free(send_copy_buf);
    pthread_exit((void *)0);
error:
    free(wc);
    pthread_exit((void *)-1);
}

void *server_thread_write_imm_signaled(void *arg)
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

    bool stop = false;
    struct timeval start, end;
    double duration = 0.0;
    double throughput = 0.0;
    double latency = 0.0;

    int num_completion = 0;

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

    /* signal the client to start */
    printf("signal the client to start...\n");

    ret = post_send_signaled(qp[0], buf_base, 0, lkey, 0, MSG_CTL_START);
    check(ret == 0, "thread: failed to signal the client to start");

    printf("signaled the client to start...\n");
    long int ops_count = 0;

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
            if (wc[i].status != IBV_WC_SUCCESS)
            {
                log_error("wc failed status: %s.", ibv_wc_status_str(wc[i].status));
                goto error;
            }

            if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM)
            {
                /* uint32_t imm_data = ntohl(wc[i].imm_data); */
                ops_count++;
                if (ops_count == config_info.warm_up_iter)
                {
                    gettimeofday(&start, NULL);
                }
                if (ops_count == config_info.total_iter)
                {
                    gettimeofday(&end, NULL);
                    stop = true;
                    break;
                }
            }
            ret = post_srq_recv(srq, buf_ptr, msg_size, lkey, (uint64_t)buf_ptr);
            buf_offset = (buf_offset + msg_size) % buf_size;
            buf_ptr = buf_base + buf_offset;
        }
    }

    ret = post_send_signaled(qp[0], ib_res->ib_buf, 0, lkey, IB_WR_ID_STOP, MSG_CTL_STOP);
    check(ret == 0, "thread: failed to signal the client to stop");

    stop = false;
    while (!stop)
    {
        /* poll cq */
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
                if (wc[i].wr_id == IB_WR_ID_STOP)
                {
                    stop = true;
                }
            }
            ret = post_srq_recv(srq, buf_ptr, msg_size, lkey, (uint64_t)buf_ptr);
        }
    }

    /* dump statistics */
    duration = (double)((end.tv_sec - start.tv_sec) + (double)(end.tv_usec - start.tv_usec) / 1000000);
    throughput = (double)(ops_count - NUM_WARMING_UP_OPS) / duration;

    latency = duration * 1000000 / (double)(ops_count - NUM_WARMING_UP_OPS);
    log_info("thread: throughput = %f (ops/s)", throughput);
    printf("thread: throughput = %f (ops/s) %f (Bytes/s); ops_count:%ld, duration: %f seconds \n", throughput,
           throughput * msg_size, ops_count, duration);
    printf("latency: %f\n", latency);

    free(wc);
    pthread_exit((void *)0);
error:
    free(wc);
    pthread_exit((void *)-1);
}

void *server_thread_write_imm_unsignaled(void *arg)
{
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    assert(ib_res->num_qps == 1);
    int ret = 0;
    int msg_size = config_info.msg_size;
    int num_concurr_msgs = config_info.num_concurr_msgs;

    struct ibv_cq *cq = ib_res->cq;
    struct ibv_srq *srq = ib_res->srq;
    struct ibv_wc *wc = NULL;
    uint32_t lkey = ib_res->mr->lkey;

    struct ibv_qp **qp = ib_res->qp;
    char *buf_ptr = ib_res->ib_buf;
    char *buf_base = ib_res->ib_buf;
    int buf_offset = 0;
    size_t buf_size = ib_res->ib_buf_size;

    int num_completion = 0;

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

    ret = post_send_signaled(qp[0], buf_base, 0, lkey, 0, MSG_CTL_START);
    if (unlikely(ret != 0))
    {
        log_error("thread: failed to signal the client to start");
    }
    log_debug("start signal sent");

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
            if (wc[i].opcode == IBV_WC_RECV)
            {
                /* post a receive */
                if ((wc[i].wc_flags & IBV_WC_WITH_IMM) && (ntohl(wc[i].imm_data) == MSG_CTL_STOP))
                {
                    finish = true;
                }
            }
            post_srq_recv(srq, buf_base, msg_size, lkey, wc[i].wr_id);
        }
    }
    free(wc);
    pthread_exit((void *)0);
error:
    free(wc);
    pthread_exit((void *)-1);
}

void *server_thread_send_signaled(void *arg)
{
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    int ret = 0, i = 0, j = 0, n = 0;
    int num_concurr_msgs = config_info.num_concurr_msgs;
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

    bool stop = false;

    wc = (struct ibv_wc *)calloc(NUM_WC, sizeof(struct ibv_wc));
    check(wc != NULL, "thread: failed to allocate wc.");

    /* pre-post recvs */
    wc = (struct ibv_wc *)calloc(NUM_WC, sizeof(struct ibv_wc));
    check(wc != NULL, "thread: failed to allocate wc.");

    for (j = 0; j < num_concurr_msgs; j++)
    {
        ret = post_srq_recv(srq, buf_ptr, msg_size, lkey, (uint64_t)buf_ptr);
        buf_offset = (buf_offset + msg_size) % buf_size;
        buf_ptr = buf_base + buf_offset;
    }

    /* signal the client to start */
    printf("signal the client to start...\n");

    ret = post_send_signaled(*qp, buf_base, 0, lkey, 0, MSG_CTL_START);
    check(ret == 0, "thread: failed to signal the client to start");
    log_debug("wait for client");

    while (stop != true)
    {
        /* poll cq */
        n = ibv_poll_cq(cq, NUM_WC, wc);
        if (n < 0)
        {
            check(0, "thread: Failed to poll cq");
        }

        for (i = 0; i < n; i++)
        {
            if (wc[i].status != IBV_WC_SUCCESS)
            {
                if (wc[i].opcode == IBV_WC_SEND)
                {
                    check(0, "thread: send failed status: %s", ibv_wc_status_str(wc[i].status));
                }
                else
                {
                    check(0, "thread: recv failed status: %s", ibv_wc_status_str(wc[i].status));
                }
            }

            if (wc[i].opcode == IBV_WC_RECV)
            {
                if ((wc[i].wc_flags & IBV_WC_WITH_IMM) && (ntohl(wc[i].imm_data) == MSG_CTL_STOP))
                {
                    stop = true;
                }
            }
            post_srq_recv(srq, buf_ptr, msg_size, lkey, wc[i].wr_id);
            buf_offset = (buf_offset + msg_size) % buf_size;
            buf_ptr = buf_base + buf_offset;
        }
    }
    free(wc);
    pthread_exit((void *)0);

error:
    if (wc != NULL)
    {
        free(wc);
    }
    pthread_exit((void *)-1);
}

void *server_thread_send_unsignaled(void *arg)
{
    struct args *args = (struct args *)arg;
    struct IBRes *ib_res = args->ib_res;
    assert(ib_res->num_qps == 1);
    int ret = 0;
    int msg_size = config_info.msg_size;
    int num_concurr_msgs = config_info.num_concurr_msgs;

    struct ibv_cq *cq = ib_res->cq;
    struct ibv_srq *srq = ib_res->srq;
    struct ibv_wc *wc = NULL;
    uint32_t lkey = ib_res->mr->lkey;

    struct ibv_qp **qp = ib_res->qp;
    char *buf_ptr = ib_res->ib_buf;
    char *buf_base = ib_res->ib_buf;
    int buf_offset = 0;
    size_t buf_size = ib_res->ib_buf_size;

    int num_completion = 0;

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

    ret = post_send_signaled(qp[0], buf_base, 0, lkey, 0, MSG_CTL_START);
    if (unlikely(ret != 0))
    {
        log_error("thread: failed to signal the client to start");
    }
    log_debug("start signal sent");

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
            if (wc[i].opcode == IBV_WC_RECV)
            {
                /* post a receive */
                if ((wc[i].wc_flags & IBV_WC_WITH_IMM) && (ntohl(wc[i].imm_data) == MSG_CTL_STOP))
                {
                    finish = true;
                }
            }
            post_srq_recv(srq, buf_base, msg_size, lkey, wc[i].wr_id);
        }
    }
    free(wc);
    pthread_exit((void *)0);
error:
    free(wc);
    pthread_exit((void *)-1);
}

int run_server(struct IBRes *ib_res)
{
    int ret = 0;

    pthread_t *threads = NULL;
    pthread_attr_t attr;
    void *(*server_thread_func)(void *) = NULL;
    int benchmark_type = config_info.benchmark_type;
    void *status;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    threads = (pthread_t *)malloc(sizeof(pthread_t));
    check(threads != NULL, "Failed to allocate threads.");

    if (benchmark_type == SEND_SIGNALED)
    {
        server_thread_func = server_thread_send_signaled;
    }
    else if (benchmark_type == SEND_UNSIGNALED)
    {
        server_thread_func = server_thread_send_unsignaled;
    }
    else if (benchmark_type == WRITE_SIGNALED)
    {
        server_thread_func = server_thread_write_signaled;
    }
    else if (benchmark_type == WRITE_UNSIGNALED)
    {
        server_thread_func = server_thread_write_unsignaled;
    }
    else if (benchmark_type == WRITE_IMM_SIGNALED)
    {
        server_thread_func = server_thread_write_imm_signaled;
    }
    else if (benchmark_type == WRITE_IMM_UNSIGNALED)
    {
        server_thread_func = server_thread_write_imm_unsignaled;
    }
    else
    {
        log_error("The benchmark_type is illegal, %d", benchmark_type);
    }

    struct args args = {.ib_res = ib_res};
    ret = pthread_create(threads, &attr, server_thread_func, &args);
    check(ret == 0, "Failed to create server_thread");

    bool thread_ret_normally = true;
    ret = pthread_join(*threads, &status);
    check(ret == 0, "Failed to join thread.");
    if ((long)status != 0)
    {
        thread_ret_normally = false;
        log_debug("server_thread: failed to execute");
    }

    if (thread_ret_normally == false)
    {
        goto error;
    }

    pthread_attr_destroy(&attr);
    free(threads);

    return 0;

error:
    if (threads != NULL)
    {
        free(threads);
    }
    pthread_attr_destroy(&attr);

    return -1;
}

int connect_qp_server(struct IBRes *ib_res)
{
    int ret = 0, n = 0, i = 0;
    /* int num_peers = config_info.num_clients; */
    int num_peers = 1;
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(struct sockaddr_in);
    char sock_buf[64] = {'\0'};
    struct QPInfo *local_qp_info = NULL;
    struct QPInfo *remote_qp_info = NULL;

    config_info.self_sockfd = sock_create_bind(NULL, config_info.sock_port);
    check(config_info.self_sockfd > 0, "Failed to create server socket.");
    listen(config_info.self_sockfd, 5);

    config_info.peer_sockfds = (int *)calloc(num_peers, sizeof(int));
    check(config_info.peer_sockfds != NULL, "Failed to allocate peer_sockfd");

    for (i = 0; i < num_peers; i++)
    {
        config_info.peer_sockfds[i] = accept(config_info.self_sockfd, (struct sockaddr *)&peer_addr, &peer_addr_len);
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

    /* get qp_info from client */
    remote_qp_info = (struct QPInfo *)calloc(num_peers, sizeof(struct QPInfo));
    check(remote_qp_info != NULL, "Failed to allocate remote_qp_info");

    for (i = 0; i < num_peers; i++)
    {
        ret = sock_get_qp_info(config_info.peer_sockfds[i], &remote_qp_info[i]);
        check(ret == 0, "Failed to get qp_info from client[%d]", i);
    }
    // TODO temporary setting for one server one client benchmark
    assert(num_peers == 1);
    ib_res->raddr = remote_qp_info[0].raddr;
    ib_res->rkey = remote_qp_info[0].rkey;
    ib_res->rsize = remote_qp_info[0].rsize;

    /* send qp_info to client */
    int peer_ind = -1;
    for (i = 0; i < num_peers; i++)
    {
        peer_ind = 0;
        ret = sock_set_qp_info(config_info.peer_sockfds[i], &local_qp_info[peer_ind]);
        check(ret == 0, "Failed to send qp_info to client[%d]", peer_ind);
    }

    /* change send QP state to RTS */
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

    /* sync with clients */
    for (i = 0; i < num_peers; i++)
    {
        n = sock_read(config_info.peer_sockfds[i], sock_buf, sizeof(SOCK_SYNC_MSG));
        check(n == sizeof(SOCK_SYNC_MSG), "Failed to receive sync from client");
    }

    for (i = 0; i < num_peers; i++)
    {
        n = sock_write(config_info.peer_sockfds[i], sock_buf, sizeof(SOCK_SYNC_MSG));
        check(n == sizeof(SOCK_SYNC_MSG), "Failed to write sync to client");
    }

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
    if (config_info.self_sockfd > 0)
    {
        close(config_info.self_sockfd);
    }

    return -1;
}
