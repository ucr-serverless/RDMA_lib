#include "debug.h"
#include "examples/bitmap.h"
#include "ib.h"
#include "memory_management.h"
#include "qp.h"
#include "rdma_config.h"
#include "sock.h"
#include "utils.h"
#include <arpa/inet.h>
#include <bits/getopt_core.h>
#include <getopt.h>
#include <infiniband/verbs.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    struct ib_ctx ctx;

    static struct option long_options[] = {
        {"server_ip", required_argument, NULL, 1},
        {"sock_port", required_argument, NULL, 2},
    };

    int ch = 0;
    bool is_server = true;
    char *server_name = NULL;

    char *port = NULL;
    while ((ch = getopt_long(argc, argv, "", long_options, NULL)) != -1)
    {
        switch (ch)
        {
        case 1:
            is_server = false;
            server_name = strdup(optarg);
            break;
        case 2:
            port = strdup(optarg);
            break;
        case '?':
            printf("options error\n");
            exit(1);
        }
    }

    struct rdma_param params = {
        .device_idx = 2,
        .sgid_idx = 3,
        .ib_port = 1,
        .qp_num = 1,
        .remote_mr_num = 4,
        .remote_mr_size = 4 * sizeof(uint32_t),
        .init_cqe_num = 128,
        .max_send_wr = 100,
        .n_send_wc = 10,
        .n_recv_wc = 10,
    };

    void **buffers = calloc(params.remote_mr_num, sizeof(void *));
    assert(buffers);
    void *buf = calloc(params.remote_mr_num, params.remote_mr_size);
    assert(buf);

    for (size_t i = 0; i < params.remote_mr_num; i++)
    {
        buffers[i] = buf + i * params.remote_mr_size;
    }
    init_ib_ctx(&ctx, &params, NULL, buffers);

#ifdef DEBUG

    printf("max_mr: %d\n", ctx.device_attr.max_mr);
    printf("max_mr_size: %lu\n", ctx.device_attr.max_mr_size);
    printf("page_size_cap: %lu\n", ctx.device_attr.page_size_cap);
#endif
    printf("Hello, World!\n");

    int self_fd = 0;
    int peer_fd = 0;
    struct sockaddr_in peer_addr;

    socklen_t peer_addr_len = sizeof(struct sockaddr_in);
    struct ib_res remote_res;
    struct ib_res local_res;
    init_local_ib_res(&ctx, &local_res);
    if (is_server)
    {

        self_fd = sock_create_bind(port);
        assert(self_fd > 0);
        listen(self_fd, 5);
        peer_fd = accept(self_fd, (struct sockaddr *)&peer_addr, &peer_addr_len);
        assert(peer_fd > 0);

        send_ib_res(&local_res, peer_fd);
        recv_ib_res(&remote_res, peer_fd);
    }
    else
    {
        peer_fd = sock_create_connect(server_name, port);
        recv_ib_res(&remote_res, peer_fd);
        send_ib_res(&local_res, peer_fd);
    }

#ifdef DEBUG
    printf("srq sg_list size\n");
    printf("\t%d", ctx.max_srq_sge);

    printf("send sg_list size\n");
    printf("\t%d", ctx.max_send_sge);

    printf("remote qp_nums\n");
    for (size_t i = 0; i < remote_res.n_qp; i++)
    {
        printf("%d\n", remote_res.qp_nums[i]);
    }
    printf("local qp_nums\n");
    for (size_t i = 0; i < ctx.qp_num; i++)
    {
        printf("%d\n", local_res.qp_nums[i]);
    }
    printf("remote mr info\n\n");
    for (size_t i = 0; i < remote_res.n_mr; i++)
    {
        printf("mr length %lu\n", remote_res.mrs[i].length);
        printf("mr addrs %p\n", remote_res.mrs[i].addr);
        printf("mr lkey %d\n", remote_res.mrs[i].lkey);
        printf("mr rkey %d\n", remote_res.mrs[i].rkey);
    }
    printf("local mr len\n\n");
    for (size_t i = 0; i < ctx.qp_num; i++)
    {
        printf("mr length %lu\n", local_res.mrs[i].length);
        printf("mr addrs %p\n", local_res.mrs[i].addr);
        printf("mr lkey %d\n", local_res.mrs[i].lkey);
        printf("mr rkey %d\n", local_res.mrs[i].rkey);
    }

    /* uint32_t msg_size = 2048; */

#endif /* ifdef DEBUG */
    // server would post recv sg-list
    if (is_server)
    {
        modify_qp_init_to_rts(ctx.qps[0], &local_res, &remote_res, remote_res.qp_nums[0]);

        for (size_t i = 0; i < params.remote_mr_num; i++)
        {
            for (size_t j = 0; j < params.remote_mr_size / sizeof(uint32_t); j++)
            {

                printf("%d, ", *((uint32_t *)buffers[i] + j));
            }
            printf("\n");
        }
        ctx.srq_sg_list[0].addr = (uint64_t)buffers[2];
        ctx.srq_sg_list[0].length = 2 * sizeof(uint32_t);
        ctx.srq_sg_list[0].lkey = ctx.remote_mrs[2]->lkey;

        ctx.srq_sg_list[1].addr = (uint64_t)buffers[3];
        ctx.srq_sg_list[1].length = 2 * sizeof(uint32_t);
        ctx.srq_sg_list[1].lkey = ctx.remote_mrs[3]->lkey;

        post_srq_recv_sg_list(ctx.srq, ctx.srq_sg_list, 2, 1);

        log_debug("post sg_list success");

        struct ibv_wc wc;
        int wc_num = 0;
        do
        {
        } while ((wc_num = ibv_poll_cq(ctx.recv_cq, 1, &wc) == 0));

        printf("receved\n");
        printf("wc status: %s\n", ibv_wc_status_str(wc.status));

        for (size_t i = 0; i < params.remote_mr_num; i++)
        {
            for (size_t j = 0; j < params.remote_mr_size / sizeof(uint32_t); j++)
            {

                printf("%d, ", *((uint32_t *)buffers[i] + j));
            }
            printf("\n");
        }
        close(self_fd);
        close(peer_fd);
    }
    else
    {
        modify_qp_init_to_rts(ctx.qps[0], &local_res, &remote_res, remote_res.qp_nums[0]);

        ((uint32_t *)buffers[0])[0] = 1;
        ctx.send_sg_list[0].addr = (uint64_t)buffers[0];
        ctx.send_sg_list[0].length = sizeof(uint32_t);
        ctx.send_sg_list[0].lkey = ctx.remote_mrs[0]->lkey;

        ((uint32_t *)buffers[1])[0] = 1;
        ctx.send_sg_list[1].addr = (uint64_t)buffers[1];
        ctx.send_sg_list[1].length = sizeof(uint32_t);
        ctx.send_sg_list[1].lkey = ctx.remote_mrs[1]->lkey;

        for (size_t i = 0; i < params.remote_mr_num; i++)
        {
            for (size_t j = 0; j < params.remote_mr_size / sizeof(uint32_t); j++)
            {

                printf("%d, ", *((uint32_t *)buffers[i] + j));
            }
            printf("\n");
        }
        post_send_sg_list_signaled(ctx.qps[0], ctx.send_sg_list, 2, 0, 0);

        log_debug("post sg_list success");
        struct ibv_wc wc;
        int wc_num = 0;
        do
        {
        } while ((wc_num = ibv_poll_cq(ctx.send_cq, 1, &wc) == 0));
        printf("get ack\n");
        printf("wc status: %s\n", ibv_wc_status_str(wc.status));

        for (size_t i = 0; i < params.remote_mr_num; i++)
        {
            for (size_t j = 0; j < params.remote_mr_size / sizeof(uint32_t); j++)
            {

                printf("%d, ", *((uint32_t *)buffers[i] + j));
            }
            printf("\n");
        }
        close(peer_fd);
    }

    printf("finished setup connection\n");

    free(server_name);
    free(port);
    destroy_ib_res((&local_res));
    destroy_ib_res((&remote_res));
    destroy_ib_ctx(&ctx);
    free(buf);
    free(buffers);
    return 0;
}
