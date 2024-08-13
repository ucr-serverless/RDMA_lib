#ifndef SETUP_IB_H_
#define SETUP_IB_H_

#include "rdma-bench_cfg.h"
#include "sock.h"
#include <assert.h>
#include <infiniband/verbs.h>
struct IBRes
{
    struct ibv_context *ctx;
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *cq;
    struct ibv_qp **qp;
    struct ibv_srq *srq;
    struct ibv_port_attr port_attr;
    struct ibv_device_attr dev_attr;

    int num_qps;
    char *ib_buf;
    size_t ib_buf_size;

    union ibv_gid sgid;

    /* Used for one-sided WRITE */
    uint32_t rkey;
    uint64_t raddr;
    uint32_t rsize;
};

struct QPInfo
{
    uint16_t lid;
    uint32_t qp_num;
    union ibv_gid gid;
    uint8_t sgid_index;
    uint8_t ib_port;
    uint32_t rkey;
    uint64_t raddr;
    uint32_t rsize;
    uint32_t psn;
} __attribute__((packed));

int setup_ib(struct IBRes *ib_res);
void close_ib_connection(struct IBRes *ib_res);

int sock_set_qp_info(int sock_fd, struct QPInfo *qp_info);
int sock_get_qp_info(int sock_fd, struct QPInfo *qp_info);
void print_qp_info(struct QPInfo *qp_info);

int modify_qp_to_rts(struct ibv_qp *qp, struct QPInfo *local, struct QPInfo *remote);
#endif /*setup_ib.h*/
