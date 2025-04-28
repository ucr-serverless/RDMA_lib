#pragma once

#include "ib.h"
#include "mr.h"
#include "qp.h"


#ifdef __cplusplus
extern "C" {
#endif

struct ibv_device* rdma_find_dev(const char *ib_devname);

int connect_rc_qp(struct ibv_qp *qp, uint32_t r_qp_num, uint32_t r_psn, uint16_t r_lid,
                                  union ibv_gid r_gid, uint32_t l_psn, uint8_t l_ib_port, uint8_t l_gid_idx);

int init_cq(struct ibv_context *context, uint32_t init_cqe, struct ibv_comp_channel* channel, struct ibv_cq **cq);

int init_multi_rc_qp(struct ibv_pd *pd, struct ibv_cq* send_cq, struct ibv_cq* recv_cq, struct ibv_srq* srq ,uint32_t max_send_wr, struct ibv_qp **qp, uint16_t n_qp);

int init_rc_qp(struct ibv_pd *pd, struct ibv_cq* send_cq, struct ibv_cq* recv_cq, struct ibv_srq* srq ,uint32_t max_send_wr, struct ibv_qp **qp);

int init_srq(struct ibv_pd* pd, struct ibv_device_attr* device_attr, int max_wr, int max_sge, struct ibv_srq** srq);

#ifdef __cplusplus
}
#endif
