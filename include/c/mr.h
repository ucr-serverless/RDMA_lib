#ifndef MR_H_
#define MR_H_

#include "ib.h"
#include "rdma_config.h"
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


int deregister_mr(struct ibv_mr* mr);
int register_local_mr(struct ibv_pd *pd, void *addr, size_t length, struct ibv_mr **mr);
int register_remote_mr(struct ibv_pd *pd, void *addr, size_t length, struct ibv_mr **mr);

int register_multiple_remote_mr(struct ib_ctx *ctx, void **buffers, size_t buffer_size, size_t buffers_len,
                                struct ibv_mr ***mr_list);

int register_multiple_local_mr(struct ib_ctx *ctx, void **buffers, size_t buffer_size, size_t buffers_len,
                               struct ibv_mr ***mr_list);


#ifdef __cplusplus
}
#endif

#endif // MR_H_
