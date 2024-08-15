#include "qp.h"
#include "debug.h"
#include "utils.h"
#include <infiniband/verbs.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

int init_rc_qp_srq_unsignaled(struct ib_ctx *ctx, struct ibv_qp **qp, uint32_t max_send_wr)
{
    assert(max_send_wr != 0);
    struct ibv_qp_init_attr qp_init_attr = {
        .send_cq = ctx->send_cq,
        .recv_cq = ctx->recv_cq,
        .srq = ctx->srq,
        .cap =
            {
                .max_send_wr = MIN(max_send_wr, ctx->device_attr.max_qp_wr - 1),
                .max_recv_wr = 64,
                .max_send_sge = 1,
                .max_recv_sge = 1,
            },
        .qp_type = IBV_QPT_RC,
        .sq_sig_all = 0,
    };

    *qp = ibv_create_qp(ctx->pd, &qp_init_attr);
    if (unlikely(!(*qp)))
    {
        log_error("Error init qp, current max_send_wr: %d", MIN(max_send_wr, ctx->device_attr.max_qp_wr - 1));
        goto error;
    }

    return SUCCESS;
error:
    return FAILURE;
}

int init_multiple_rc_qp_srq_unsignaled(struct ib_ctx *ctx, struct user_param *params)
{
    assert(params->qp_num > 0);
    ctx->qp_num = params->qp_num;
    ctx->qps = (struct ibv_qp **)calloc(ctx->qp_num, sizeof(struct ib_qp *));
    if (unlikely(!ctx->qps))
    {
        log_error("Error, allocate qps failure\n");
        return FAILURE;
    }
    for (size_t i = 0; i < ctx->qp_num; i++)
    {
        if (unlikely(init_rc_qp_srq_unsignaled(ctx, &(ctx->qps[i]), UINT8_MAX) == FAILURE))
        {
            log_error("Error, allocate the %lu th qp", i);
            return FAILURE;
        }
    }
    return SUCCESS;
}

int modify_qp_init_verbose(struct ibv_qp *qp, uint8_t l_ib_port)
{
    int ret = 0;
    struct ibv_qp_attr qp_attr = {
        .qp_state = IBV_QPS_INIT,
        .pkey_index = 0,
        .port_num = l_ib_port,
        .qp_access_flags =
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_WRITE,
    };

    ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
    if (unlikely(ret != 0))
    {
        log_error("Failed to modify qp to INIT.");
        return FAILURE;
    }
    return SUCCESS;
}

int modify_qp_init(struct ibv_qp *qp, struct ib_res *local_res)
{
    return modify_qp_init_verbose(qp, local_res->ib_port);
}

int modify_qp_init_to_rtr_verbose(struct ibv_qp *qp, uint32_t r_qp_num, uint32_t r_psn, uint16_t r_lid,
                                  union ibv_gid r_gid, uint8_t l_ib_port, uint8_t l_sgid_index)

{
    int ret = 0;
    struct ibv_qp_attr qp_attr = {
        .qp_state = IBV_QPS_RTR,
        .path_mtu = IBV_MTU_1024,
        .dest_qp_num = r_qp_num,
        .rq_psn = r_psn,
        .max_dest_rd_atomic = 1,
        .min_rnr_timer = 12,
        .ah_attr.is_global = 0, // grh is invalid for none RoCE
        .ah_attr.dlid = r_lid,  // Not used for RoCEv2
        .ah_attr.sl = 0,        // Service level
        .ah_attr.src_path_bits = 0,
        .ah_attr.port_num = l_ib_port,
    };

    if (qp_attr.ah_attr.dlid == 0)
    {
        log_info("Using RoCEv2 transport\n");
        qp_attr.ah_attr.is_global = 1; // grh should be configured for RoCEv2
        qp_attr.ah_attr.grh.sgid_index = l_sgid_index;
        qp_attr.ah_attr.grh.dgid = r_gid;
        qp_attr.ah_attr.grh.hop_limit = 0xFF;
        qp_attr.ah_attr.grh.traffic_class = 0;
        qp_attr.ah_attr.grh.flow_label = 0;
    }
    ret = ibv_modify_qp(qp, &qp_attr,
                        IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                            IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER | 0);
    if (unlikely(ret != 0))
    {
        log_error("Failed to change qp to rtr");
        return FAILURE;
    }
    return SUCCESS;
}

int modify_qp_init_to_rtr_qp_num_idx(struct ibv_qp *qp, struct ib_res *local_res, struct ib_res *remote_res,
                                     size_t r_qp_num_idx)
{
    return modify_qp_init_to_rtr_verbose(qp, remote_res->qp_nums[r_qp_num_idx], remote_res->psn, remote_res->lid,
                                         remote_res->gid, local_res->ib_port, local_res->sgid_idx);
}

int modify_qp_init_to_rtr_qp_num(struct ibv_qp *qp, struct ib_res *local_res, struct ib_res *remote_res,
                                 uint32_t r_qp_num)
{
    return modify_qp_init_to_rtr_verbose(qp, r_qp_num, remote_res->psn, remote_res->lid, remote_res->gid,
                                         local_res->ib_port, local_res->sgid_idx);
}

int modify_qp_rtr_to_rts_verbose(struct ibv_qp *qp, uint32_t l_psn)
{
    int ret = 0;
    struct ibv_qp_attr qp_attr = {
        .qp_state = IBV_QPS_RTS,
        .timeout = 14,
        .retry_cnt = 7,
        .rnr_retry = 7,
        .sq_psn = l_psn,
        .max_rd_atomic = 1,
    };

    ret = ibv_modify_qp(qp, &qp_attr,
                        IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                            IBV_QP_MAX_QP_RD_ATOMIC);
    if (unlikely(ret != 0))
    {
        log_error("Failed to change qp to rts");
        return FAILURE;
    }
    return SUCCESS;
}

int modify_qp_rtr_to_rts(struct ibv_qp *qp, struct ib_res *local_res)
{
    return modify_qp_rtr_to_rts_verbose(qp, local_res->psn);
}

int modify_qp_init_to_rts(struct ibv_qp *qp, struct ib_res *local_res, struct ib_res *remote_res, uint32_t r_qp_num)
{
    int ret = 0;
    ret = modify_qp_init(qp, local_res);
    if (unlikely(ret != SUCCESS))
    {
        return FAILURE;
    }
    ret = modify_qp_init_to_rtr_qp_num(qp, local_res, remote_res, r_qp_num);
    if (unlikely(ret != SUCCESS))
    {
        return FAILURE;
    }
    ret = modify_qp_rtr_to_rts(qp, local_res);
    if (unlikely(ret != SUCCESS))
    {
        return FAILURE;
    }
    return SUCCESS;
}
