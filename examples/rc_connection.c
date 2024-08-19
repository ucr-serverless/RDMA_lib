#include "config.h"
#include "debug.h"
#include "examples/bitmap.h"
#include "ib.h"
#include "memory_management.h"
#include "qp.h"
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

    struct user_param params = {
        .device_idx = 2,
        .sgid_idx = 3,
        .ib_port = 1,
        .mr_num = 2,
        .qp_num = 2,
        .bf_size = 2048,
        .init_cqe_num = 128,
    };

    void **buffers = calloc(params.mr_num, sizeof(void *));
    assert(buffers);
    void *buf = calloc(params.mr_num, params.bf_size);
    assert(buf);
    for (size_t i = 0; i < params.mr_num; i++)
    {
        buffers[i] = buf + i * params.bf_size;
    }
    init_ib_ctx(&ctx, &params, buffers);
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

    printf("remote qp_nums\n");
    for (size_t i = 0; i < remote_res.qp_num; i++)
    {
        printf("%d\n", remote_res.qp_nums[i]);
    }
    printf("local qp_nums\n");
    for (size_t i = 0; i < ctx.qp_num; i++)
    {
        printf("%d\n", local_res.qp_nums[i]);
    }
    printf("remote mr info\n\n");
    for (size_t i = 0; i < remote_res.mr_num; i++)
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
    void *raddr = NULL;
    uint32_t blk_size = 4;
    void *laddr = local_res.mrs[0].addr;
    uint32_t rkey = 0;
    uint32_t msg_size = 2048;
    uint32_t mr_info_len = 1;
    int ret = 0;
    if (is_server)
    {
        modify_qp_init_to_rts(ctx.qps[0], &local_res, &remote_res, remote_res.qp_nums[0]);
        /* modify_qp_init(ctx.qps[0], &local_res); */
        /* modify_qp_init_to_rtr_qp_num_idx(ctx.qps[0], &local_res, &remote_res, 0); */
        /* modify_qp_rtr_to_rts(ctx.qps[0], &local_res); */
        bitmap *bp;
        uint32_t slot;
        uint32_t slot_num;
        init_qp_bitmap(ctx.mr_num / ctx.qp_num, params.bf_size, blk_size, &bp);
        find_avaliable_slot(bp, msg_size, blk_size, remote_res.mrs, mr_info_len, &slot, &slot_num, &raddr, &rkey);
        printf("slot: %d\n", slot);
        printf("slot_num: %d\n", slot_num);
        printf("rkey: %d\n", rkey);
        printf("raddr: %p\n", raddr);

        *(char *)laddr = '1';
        printf("local content: %c\n", *(char *)laddr);

        ret = pre_post_dumb_srq_recv(ctx.srq, local_res.mrs[0].addr, local_res.mrs[0].length, local_res.mrs[0].lkey, 0,
                                     ctx.srqe);

        ret = post_write_imm_signaled(ctx.qps[0], local_res.mrs[0].addr, msg_size, local_res.mrs[0].lkey, 0,
                                      (uint64_t)raddr, rkey, slot);

        printf("%d\n", ret);
        /* int ret = 0; */

        printf("addr: %p\n", raddr);
        printf("slot: %d\n", slot);
        bitmap_set_consecutive(bp, slot, slot_num);
        bitmap_print_bit(bp);
        void * recv_addr = NULL;
        uint32_t recv_len = 0;
        receive_release_signal(peer_fd, &recv_addr, &recv_len);
        printf("recv_addr: %p\n", recv_addr);
        printf("recv_len: %d\n", recv_len);
        remote_addr_convert_slot_idx(recv_addr, recv_len, remote_res.mrs, mr_info_len, blk_size, &slot, &slot_num);
        printf("slot_idx: %d\n", slot);
        printf("slot_num: %d\n", slot_num);
        bitmap_clear_consecutive(bp, slot, slot_num);
        bitmap_print_bit(bp);

        close(self_fd);
        close(peer_fd);
    }
    else
    {
        modify_qp_init_to_rts(ctx.qps[0], &local_res, &remote_res, remote_res.qp_nums[0]);
        pre_post_dumb_srq_recv(ctx.srq, local_res.mrs[0].addr, local_res.mrs[0].length, local_res.mrs[0].lkey, 0,
                               ctx.srqe);
        /* modify_qp_init(ctx.qps[0], &local_res); */
        /* modify_qp_init_to_rtr_qp_num_idx(ctx.qps[0], &local_res, &remote_res, 0); */
        /* modify_qp_rtr_to_rts(ctx.qps[0], &local_res); */
        struct ibv_wc wc;
        int wc_num = 0;
        do
        {
        } while ((wc_num = ibv_poll_cq(ctx.recv_cq, 1, &wc) == 0));

        uint32_t slot_idx = wc.imm_data;
        printf("slot: %d\n", slot_idx);
        printf("length: %d\n", wc.byte_len);
        printf("optcode: %d\n", wc.opcode);
        printf("qp_num: %d\n", wc.qp_num);
        void *addr;
        local_slot_idx_convert(&local_res, wc.qp_num, slot_idx, ctx.mr_num / ctx.qp_num, blk_size, &addr);
        printf("received addr: %p\n", addr);
        printf("received content: %c\n", *(char*)addr);
        send_release_signal(peer_fd, addr, wc.byte_len);

        close(peer_fd);
    }

printf("finished setup connection\n");

destroy_ib_ctx(&ctx);
free(buf);
free(buffers);
return 0;
}
