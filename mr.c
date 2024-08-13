#include "mr.h"
#include "debug.h"
#include "utils.h"
#include <infiniband/verbs.h>
#include <stdlib.h>

int register_local_mr(struct ibv_pd *pd, void *addr, size_t length, struct ibv_mr **mr)
{
    *mr = ibv_reg_mr(pd, addr, length, IBV_ACCESS_LOCAL_WRITE);
    if (unlikely(!(*mr)))
    {
        log_error("register local memory region fail");
        return FAILURE;
    }
    return SUCCESS;
}
int register_remote_mr(struct ibv_pd *pd, void *addr, size_t length, struct ibv_mr **mr)
{

    *mr = ibv_reg_mr(pd, addr, length, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (unlikely(!(*mr)))
    {
        log_error("register remote memory region fail");
        return FAILURE;
    }
    return SUCCESS;
}

int register_multiple_mr(struct ib_ctx *ctx, struct user_param *params, void **buffers)
{
    assert(params->mr_num > 0);
    assert(params->mr_num < ctx->device_attr.max_mr);
    assert(params->bf_size > 0);
    ctx->mr_num = params->mr_num;
    ctx->buffers = buffers;
    ctx->bf_size = params->bf_size;
    if (unlikely(!ctx->buffers))
    {
        log_error("Error, buffers not passed\n");
        return FAILURE;
    }

    ctx->mrs = (struct ibv_mr **)calloc(ctx->mr_num, sizeof(struct ibv_mr *));

    if (unlikely(!ctx->mrs))
    {
        log_error("Error, allocate mrs failure\n");
        return FAILURE;
    }

    for (size_t i = 0; i < ctx->mr_num; i++)
    {
        if (!ctx->buffers[i])
        {
            log_error("Error, buffer %lu is NULL\n", i);
            return FAILURE;
        }
        if (unlikely(register_remote_mr(ctx->pd, ctx->buffers[i], ctx->bf_size, &(ctx->mrs[i])) == FAILURE))
        {
            log_error("Error, register mr fail\n");
            return FAILURE;
        }
    }
    return SUCCESS;
}
