#ifndef RDMA_CONFIG_H_
#define RDMA_CONFIG_H_

#include <stdint.h>
#include <stdlib.h>

#define RETRY_MAX 10
struct rdma_param
{
    uint32_t device_idx;
    uint32_t sgid_idx;
    uint32_t qp_num;
    uint32_t remote_mr_num;
    uint32_t local_mr_num;
    uint64_t remote_mr_size;
    uint64_t local_mr_size;
    uint32_t max_send_wr;
    uint8_t ib_port;
    uint32_t init_cqe_num;
    void *extras;
    uint32_t n_send_wc;
    uint32_t n_recv_wc;
};

enum rdma_status
{
    RDMA_SUCCESS = 0,
    RDMA_FAILURE = 1,
};
#endif /* RDMA_CONFIG_H_*/
