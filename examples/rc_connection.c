#include "config.h"
#include "ib.h"
#include "sock.h"
#include "utils.h"
#include <arpa/inet.h>
#include <bits/getopt_core.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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
        .device_idx = 3,
        .sgid_idx = 3,
        .ib_port = 1,
        .mr_num = 2,
        .qp_num = 2,
        .bf_size = 2048,
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

#endif /* ifdef DEBUG */
    if (is_server)
    {
        close(self_fd);
        close(peer_fd);
    }
    else
    {
        close(peer_fd);
    }
    destroy_ib_ctx(&ctx);
    free(buf);
    free(buffers);
    return 0;
}
