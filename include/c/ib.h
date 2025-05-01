#ifndef IB_H_
#define IB_H_

#include "rdma_config.h"
#include <arpa/inet.h>
#include <byteswap.h>
#include <endian.h>
#include <infiniband/verbs.h>
#include <inttypes.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>
#include <sys/types.h>

#define MAX_N_SGE 50

#ifdef __cplusplus
extern "C" {
#endif

struct ib_ctx
{
    struct ibv_device *device;
    // if the device_str is used, the device_idx is not meaningful
    int device_idx;
    struct ibv_context *context;
    struct ibv_pd *pd;
    uint32_t qp_num;
    struct ibv_qp **qps;
    uint32_t remote_mrs_num;
    uint32_t local_mrs_num;
    struct ibv_mr **remote_mrs;
    struct ibv_mr **local_mrs;
    struct ibv_srq *srq;
    struct ibv_cq *send_cq;
    struct ibv_cq *recv_cq;
    struct ibv_comp_channel *send_channel;
    struct ibv_comp_channel *recv_channel;
    struct ibv_device_attr device_attr;
    struct ibv_port_attr port_attr;
    int sgid_idx;
    union ibv_gid gid;
    uint16_t lid;
    uint32_t send_cqe;
    uint32_t recv_cqe;
    uint32_t srqe;
    uint32_t max_srq_sge;
    uint8_t ib_port;
    struct ibv_wc *send_wc;
    struct ibv_wc *recv_wc;
    uint32_t n_send_wc;
    uint32_t n_recv_wc;
    uint32_t max_send_wr;
    uint32_t max_send_sge;
    uint32_t max_recv_sge;
    struct ibv_sge *send_sg_list;
    struct ibv_sge *recv_sg_list;
    struct ibv_sge *srq_sg_list;
    struct ibv_recv_wr *bad_recv_wr;
};

int init_ib_ctx(struct ib_ctx *ctx, struct rdma_param *params, void **local_buffers, void **remote_buffers);
void destroy_ib_ctx(struct ib_ctx *ctx);

struct mr_info
{
    uint64_t addr;
    size_t length;
    uint32_t lkey;
    uint32_t rkey;
} __attribute__((packed));

struct ib_res
{
    union ibv_gid gid;
    struct mr_info *mrs;
    uint32_t *qp_nums;
    uint32_t psn;
    uint32_t n_mr;
    uint32_t n_qp;
    uint16_t lid;
    uint8_t sgid_idx;
    uint8_t ib_port;
} __attribute__((packed));

void print_ib_res(struct ib_res *res);
int init_local_ib_res(struct ib_ctx *ctx, struct ib_res *res);
// the local ib_res should be initialized first
int send_ib_res(struct ib_res *local_ib_res, int sock_fd);
int recv_ib_res(struct ib_res *remote_ib_res, int sock_fd);
void destroy_ib_res(struct ib_res *res);

int post_send_signaled(struct ibv_qp *qp, uint64_t buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                       uint32_t imm_data);

int post_send_unsignaled(struct ibv_qp *qp, uint64_t buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                         uint32_t imm_data);

int post_send_sg_list_signaled(struct ibv_qp *qp, struct ibv_sge *sg_list, uint32_t sg_list_len, uint64_t wr_id,
                               uint32_t imm_data);

int post_send_sg_list_unsignaled(struct ibv_qp *qp, struct ibv_sge *sg_list, uint32_t sg_list_len, uint64_t wr_id,
                                 uint32_t imm_data);

int post_srq_recv(struct ibv_srq *srq, uint64_t buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id);

int pre_post_dumb_srq_recv(struct ibv_srq *srq, uint64_t buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                           uint32_t num);

int post_srq_recv_sg_list(struct ibv_srq *srq, struct ibv_sge *sg_list, uint32_t sg_list_len, uint64_t wr_id);

int post_dumb_srq_recv(struct ibv_srq *srq, uint64_t buf, uint32_t buf_size, uint32_t lkey, uint64_t wr_id);

int post_multiple_dumb_srq_recv(struct ibv_srq *srq, uint64_t buf, uint32_t buf_size, uint32_t lkey, size_t n_srq_recv);

int post_write_signaled(struct ibv_qp *qp, uint64_t buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id, uint64_t raddr,
                        uint32_t rkey);

int post_write_unsignaled(struct ibv_qp *qp, uint64_t buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                          uint64_t raddr, uint32_t rkey);

int post_write_imm_signaled(struct ibv_qp *qp, uint64_t buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                            uint64_t raddr, uint32_t rkey, uint32_t imm_data);

int post_write_imm_unsignaled(struct ibv_qp *qp, uint64_t buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                              uint64_t raddr, uint32_t rkey, uint32_t imm_data);

#ifdef __cplusplus
}
#endif

#endif /*ib.h*/
