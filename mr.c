#include "mr.h"
#include "debug.h"
#include "utils.h"
#include <infiniband/verbs.h>
#include <stdbool.h>
#include <stdlib.h>

int register_local_mr(struct ibv_pd *pd, void *addr, size_t length, struct ibv_mr **mr)
{
    *mr = ibv_reg_mr(pd, addr, length, IBV_ACCESS_LOCAL_WRITE);
    if (unlikely(!(*mr)))
    {
        log_error("register local memory region fail");
        return RDMA_FAILURE;
    }
    return RDMA_SUCCESS;
}
int register_remote_mr(struct ibv_pd *pd, void *addr, size_t length, struct ibv_mr **mr)
{

    *mr = ibv_reg_mr(pd, addr, length, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (unlikely(!(*mr)))
    {
        log_error("register remote memory region fail");
        return RDMA_FAILURE;
    }
    return RDMA_SUCCESS;
}

int register_multiple_mr(struct ib_ctx *ctx, void **buffers, size_t buffer_size, size_t n_buffer,
                         bool is_local_buffer, struct ibv_mr ***mr_list)
{
    assert(buffer_size > 0);
    assert(n_buffer > 0);
    int ret = 0;
    if (unlikely(!buffers))
    {
        log_error("Error, buffers not passed\n");
        return RDMA_FAILURE;
    }

    *mr_list = (struct ibv_mr **)calloc(n_buffer, sizeof(struct ibv_mr *));

    if (unlikely(!(*mr_list)))
    {
        log_error("Error, allocate mrs failure\n");
        return RDMA_FAILURE;
    }

    for (size_t i = 0; i < n_buffer; i++)
    {
        if (!buffers[i])
        {
            log_error("Error, buffer %lu is NULL\n", i);
            return RDMA_FAILURE;
        }
        if (is_local_buffer)
        {
            ret = register_local_mr(ctx->pd, buffers[i], buffer_size, &((*mr_list)[i]));
        }
        else
        {
            ret = register_remote_mr(ctx->pd, buffers[i], buffer_size, &((*mr_list)[i]));
        }
        if (unlikely(ret == RDMA_FAILURE))
        {
            log_error("Error, register mr fail\n");
            return RDMA_FAILURE;
        }
    }
    return RDMA_SUCCESS;
}

int register_multiple_remote_mr(struct ib_ctx *ctx, void **buffers, size_t buffer_size, size_t n_buffer,
                                struct ibv_mr ***mr_list)
{
    return register_multiple_mr(ctx, buffers, buffer_size, n_buffer, false, mr_list);
}

int register_multiple_local_mr(struct ib_ctx *ctx, void **buffers, size_t buffer_size, size_t n_buffer,
                               struct ibv_mr ***mr_list)
{
    return register_multiple_mr(ctx, buffers, buffer_size, n_buffer, true, mr_list);
}
