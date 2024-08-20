#ifndef MR_H_
#define MR_H_

#include "rdma_config.h"
#include "ib.h"
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <stdint.h>

int register_local_mr(struct ibv_pd *pd, void *addr, size_t length, struct ibv_mr **mr);
int register_remote_mr(struct ibv_pd *pd, void *addr, size_t length, struct ibv_mr **mr);

int register_multiple_mr(struct ib_ctx *ctx, struct rdma_param *params, void **buffers);

#endif // MR_H_
