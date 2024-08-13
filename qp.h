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

#endif /* QP_H_ */
