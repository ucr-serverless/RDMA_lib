#define _GNU_SOURCE
#include "RDMA_c.h"
#include "ib.h"
#include "qp.h"
#include "rdma_config.h"
#include <arpa/inet.h>
#include <assert.h>
#include <bits/getopt_core.h>
#include <fstream>
#include <getopt.h>
#include <infiniband/sa.h>
#include <infiniband/verbs.h>
#include <iostream>
#include <memory>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#define MR_SIZE 10240
#define MAX_WC_NUM 10000

struct CpuTimes
{
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
};

CpuTimes getCpuTimes()
{
    std::ifstream file("/proc/stat");
    CpuTimes times{};
    std::string line;
    if (file.is_open())
    {
        std::getline(file, line);
        sscanf(line.c_str(), "cpu  %llu %llu %llu %llu %llu %llu %llu %llu", &times.user, &times.nice, &times.system,
               &times.idle, &times.iowait, &times.irq, &times.softirq, &times.steal);
    }
    return times;
}

double calculateCpuUsage(const CpuTimes &prev, const CpuTimes &curr)
{
    unsigned long long prevIdle = prev.idle + prev.iowait;
    unsigned long long currIdle = curr.idle + curr.iowait;

    unsigned long long prevNonIdle = prev.user + prev.nice + prev.system + prev.irq + prev.softirq + prev.steal;
    unsigned long long currNonIdle = curr.user + curr.nice + curr.system + curr.irq + curr.softirq + curr.steal;

    unsigned long long prevTotal = prevIdle + prevNonIdle;
    unsigned long long currTotal = currIdle + currNonIdle;

    unsigned long long totald = currTotal - prevTotal;
    unsigned long long idled = currIdle - prevIdle;

    return (totald - idled) * 100.0 / totald;
}

int post_constructed(uint32_t req_size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, uint64_t buf, uint64_t raddr,
               uint32_t rkey, int send_flag)
{
    int ret = 0;
    struct ibv_send_wr *bad_send_wr;

    struct ibv_sge list = {.addr = (uintptr_t)buf, .length = req_size, .lkey = lkey};

    struct ibv_send_wr send_wr = {
        .wr_id = wr_id,
        .sg_list = &list,
        .num_sge = 1,
        .opcode = IBV_WR_RDMA_WRITE,
        .send_flags = send_flag,
        // .wr.rdma = {.remote_addr = raddr, .rkey = rkey};
        // .wr.rdma.rkey = rkey,
    };
    send_wr.wr.rdma = {.remote_addr = raddr, .rkey = rkey};

    ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
    if (ret != 0)
    {
        return RDMA_FAILURE;
    }
    return RDMA_SUCCESS;
}
int probing(const int n_hosts, const int n_qp, struct ib_ctx &ctx, const struct ib_res &local_res,
            const struct ib_res *host_ptr, struct ibv_wc *wc)
{
    int ret = 0;
    int tt_wc_num = 0;
    int wc_num = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < n_hosts; i++)
    {
        for (size_t j = 0; j < n_qp; j++)
        {
            ret = post_write_signaled(ctx.qps[i * n_qp + j], local_res.mrs[0].addr, 0, local_res.mrs[0].lkey, 1,
                                      host_ptr[i].mrs[0].addr, host_ptr[i].mrs[0].rkey);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Time spent in post write: " << duration.count() << " microseconds" << std::endl;
    while (tt_wc_num < n_hosts * n_qp)
    {
        wc_num = ibv_poll_cq(ctx.send_cq, MAX_WC_NUM, wc);
        assert(wc_num >= 0);
        tt_wc_num += wc_num;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    struct ib_ctx ctx;

    static struct option long_options[] = {{"server_ip", required_argument, NULL, 'H'},
                                           {"port", required_argument, NULL, 'p'},
                                           {"local_ip", required_argument, NULL, 'L'},
                                           {"sgid_index", required_argument, 0, 'x'},
                                           {"help", no_argument, 0, 'h'},
                                           {"device_index", required_argument, 0, 'd'},
                                           {"ib_port", required_argument, 0, 'i'},
                                           {"n_qp", required_argument, 0, 'q'},
                                           {"n_hosts", required_argument, 0, 'o'},
                                           {"n_iter", required_argument, 0, 't'},
                                           {"n_interval", required_argument, 0, 'l'},
                                           {0, 0, 0, 0}};
    int option_index = 0;

    int ch = 0;
    bool is_server = true;
    char *server_name = nullptr;
    char *local_ip = nullptr;
    char *usage = nullptr;
    int ib_port = 0;
    int device_idx = 0;
    int sgid_idx = 0;
    int n_qp = 1;
    int n_hosts = 1;
    int n_interval = 1;
    int n_iter = 1000;

    char *port = NULL;
    while ((ch = getopt_long(argc, argv, "H:p:L:hi:d:x:q:o:t:l:", long_options, &option_index)) != -1)
    {
        switch (ch)
        {
        case 'H':
            is_server = false;
            server_name = strdup(optarg);
            break;
        case 'q':
            n_qp = atoi(optarg);
            break;
        case 'l':
            n_interval = atoi(optarg);
            break;
        case 't':
            n_iter = atoi(optarg);
            break;
        case 'o':
            n_hosts = atoi(optarg);
            break;
        case 'L':
            local_ip = strdup(optarg);
            break;
        case 'p':
            port = strdup(optarg);
            break;
        case 'h':
            printf("usage: %s", usage);
            break;
        case 'i':
            ib_port = atoi(optarg);
            break;
        case 'd':
            device_idx = atoi(optarg);
            break;
        case 'x':
            sgid_idx = atoi(optarg);
            break;
        case '?':
            printf("options error\n");
            exit(1);
        }
    }
    // on xl170, the device_idx should be 3, on c6525-25g, the device_idx should be 2.
    assert(n_hosts);
    assert(n_qp);

    struct rdma_param rparams = {
        .device_idx = device_idx,
        .sgid_idx = sgid_idx,
        .qp_num = 0,
        .remote_mr_num = 1,
        .remote_mr_size = MR_SIZE,
        .max_send_wr = 100,
        .ib_port = ib_port,
        .init_cqe_num = 128,
        .n_send_wc = 10,
        .n_recv_wc = 10,
    };

    if (is_server)
    {
        rparams.qp_num = n_qp * n_hosts;
    }
    else
    {
        rparams.qp_num = n_qp;
    }

    auto buf = std::make_unique<std::byte[]>(rparams.remote_mr_size);
    // cudaMalloc(&buf, rparams.remote_mr_num * rparams.remote_mr_size);
    // void *buf = (void *)calloc(rparams.remote_mr_num, rparams.remote_mr_size);
    assert(buf);
    void *ptr = buf.get();
    init_ib_ctx(&ctx, &rparams, NULL, &ptr);

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
    size_t current_host = 0;
    // on host
    auto host_ptr = std::make_unique<struct ib_res[]>(n_hosts);
    auto host_fd = std::make_unique<int[]>(n_hosts);
    // on client
    size_t client_id = 0;
    struct ib_res remote_res;
    struct ib_res local_res;
    init_local_ib_res(&ctx, &local_res);
    if (is_server)
    {

        self_fd = sock_create_bind(local_ip, port);
        assert(self_fd > 0);
        listen(self_fd, 5);
        while (current_host < n_hosts)
        {
            peer_fd = accept(self_fd, (struct sockaddr *)&peer_addr, &peer_addr_len);
            assert(peer_fd > 0);

            host_fd[current_host] = peer_fd;
            sock_write(peer_fd, &current_host, sizeof(size_t));
            recv_ib_res(&host_ptr[current_host], peer_fd);
            struct ib_res &client_res = host_ptr[current_host];
            send_ib_res(&local_res, peer_fd);
            std::cout << "received: " << current_host << std::endl;
            int ret;
            for (size_t i = 0; i < n_qp; i++)
            {
                ret =
                    connect_rc_qp(ctx.qps[current_host * n_qp + i], client_res.qp_nums[i], client_res.psn,
                                  client_res.lid, client_res.gid, local_res.psn, local_res.ib_port, local_res.sgid_idx);
                if (ret != RDMA_SUCCESS)
                {
                    printf("connect rc qp failure\n");
                }
            }
            current_host++;
        }
    }
    else
    {
        peer_fd = sock_create_connect(server_name, port);
        assert(peer_fd);
        sock_read(peer_fd, &client_id, sizeof(size_t));
        std::cout << "current client id is " << client_id << std::endl;
        send_ib_res(&local_res, peer_fd);
        recv_ib_res(&remote_res, peer_fd);
        int ret = 0;
        for (size_t i = 0; i < n_qp; i++)
        {
            ret = connect_rc_qp(ctx.qps[i], remote_res.qp_nums[client_id * n_qp + i], remote_res.psn, remote_res.lid,
                                remote_res.gid, local_res.psn, local_res.ib_port, local_res.sgid_idx);
            if (ret != RDMA_SUCCESS)
            {
                printf("connect rc qp failure\n");
            }
        }
        std::cout << "client id: " << client_id << "connected all qps" << std::endl;
    }

#ifdef DEBUG

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

#endif /* ifdef DEBUG */

    int ret = 0;
    if (is_server)
    {
        CpuTimes prevTimes = getCpuTimes();
        double totalUsage = 0.0;
        struct ibv_wc wc[MAX_WC_NUM];
        for (size_t i = 0; i < n_iter; i++)
        {
            auto start = std::chrono::high_resolution_clock::now();
            probing(n_hosts, n_qp, ctx, local_res, host_ptr.get(), wc);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            std::cout << "Time spent in function: " << duration.count() << " microseconds" << std::endl;
            std::this_thread::sleep_for(std::chrono::microseconds(n_interval));
            if (i % 1000)
            {
                continue;
            }
            CpuTimes currTimes = getCpuTimes();
            double usage = calculateCpuUsage(prevTimes, currTimes);
            std::cout << "CPU Usage: " << usage << "%" << std::endl;
            totalUsage += usage;
            prevTimes = currTimes;
        }
        std::cout << "Average CPU Usage: " << (totalUsage / n_iter * 1000) << "%" << std::endl;
    }
    else
    {
        // prepost receive request
        // ret = post_srq_recv(ctx.srq, local_res.mrs[0].addr, local_res.mrs[0].length, local_res.mrs[0].lkey, 0);
        // if (ret != RDMA_SUCCESS)
        // {
        //     printf("post recv request failed\n");
        // }
        // printf("wait for incoming request\n");
        //
        // struct ibv_wc wc;
        // int wc_num = 0;
        // do
        // {
        // } while ((wc_num = ibv_poll_cq(ctx.recv_cq, 1, &wc) == 0));
        //
        // printf("recv ibv wc status %s\n", ibv_wc_status_str(wc.status));
        //
        // ret = post_srq_recv(ctx.srq, local_res.mrs[0].addr, local_res.mrs[0].length, local_res.mrs[0].lkey, 0);
        // if (ret != RDMA_SUCCESS)
        // {
        //     printf("post recv request failed");
        // }
        //
        // ret =
        //     post_send_signaled(ctx.qps[0], local_res.mrs[0].addr, local_res.mrs[0].length, local_res.mrs[0].lkey, 0,
        //     0);
        // // poll the send_cq for the ack of send.
        // do
        // {
        // } while ((wc_num = ibv_poll_cq(ctx.send_cq, 1, &wc) == 0));
        // printf("send ibv wc status %s\n", ibv_wc_status_str(wc.status));

        while (true)
        {
            sleep(1);
        }
        close(peer_fd);
    }
    free(server_name);
    free(local_ip);
    free(port);
    destroy_ib_res((&local_res));
    if (is_server)
    {
    }
    else
    {
        destroy_ib_res((&remote_res));
    }
    destroy_ib_ctx(&ctx);
    return 0;
}
