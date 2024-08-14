#ifndef QP_H_
#define QP_H_

#include "config.h"
#include "debug.h"
#include "ib.h"
#include "mr.h"
#include "utils.h"
#include <arpa/inet.h>
#include <infiniband/verbs.h>

#include <unistd.h>

#include <rdma/rdma_cma.h>

int init_rc_qp_srq_unsignaled(struct ib_ctx *ctx, struct ibv_qp **qp, uint32_t max_send_wr);

int init_multiple_rc_qp_srq_unsignaled(struct ib_ctx *ctx, struct user_param *params);

int modify_qp_init(struct ibv_qp *qp, struct ib_res *local_res);

int modify_qp_init_to_rtr_qp_num_idx(struct ibv_qp *qp, struct ib_res *local_res, struct ib_res *remote_res,
                                     size_t r_qp_num_idx);

int modify_qp_init_to_rtr_qp_num(struct ibv_qp *qp, struct ib_res *local_res, struct ib_res *remote_res,
                                 uint32_t r_qp_num);

int modify_qp_rtr_to_rts(struct ibv_qp *qp, struct ib_res *local_res);
#endif /* QP_H_ */
