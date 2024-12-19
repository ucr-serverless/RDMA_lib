#include <arpa/inet.h>
#ifdef USE_RTE_MEMPOOL
#include <rte_branch_prediction.h>
#include <rte_errno.h>
#include <rte_mempool.h>
#endif /* ifdef USE_RTE_MEMPOOL */
#include <malloc.h>
#include <unistd.h>

#include "log.h"
#include "mr.h"
#include "setup_ib.h"

#ifdef USE_RTE_MEMPOOL
#define MEMPOOL_NAME "SPRIGHT_MEMPOOL"

static void *rte_shm_mgr(size_t ib_buf_size)
{
    int ret;
    void *buffer;

    config_info.mempool =
        rte_mempool_create(MEMPOOL_NAME, 1, ib_buf_size, 0, 0, NULL, NULL, NULL, NULL, rte_socket_id(), 0);
    if (unlikely(config_info.mempool == NULL))
    {
        fprintf(stderr, "rte_mempool_create() error: %s\n", rte_strerror(rte_errno));
        goto error_0;
    }

    // Allocate DPDK memory and register it
    ret = rte_mempool_get(config_info.mempool, (void **)&buffer);
    if (unlikely(ret < 0))
    {
        fprintf(stderr, "rte_mempool_get() error: %s\n", rte_strerror(-ret));
        goto error_1;
    }

    return buffer;

error_1:
    rte_mempool_put(config_info.mempool, buffer);
error_0:
    rte_mempool_free(config_info.mempool);
    return NULL;
}
#endif

int sock_set_qp_info(int sock_fd, struct QPInfo *qp_info)
{
    int n;

    n = sock_write(sock_fd, (char *)qp_info, sizeof(struct QPInfo));
    check(n == sizeof(struct QPInfo), "write qp_info to socket.");

    return 0;

error:
    return -1;
}

int sock_get_qp_info(int sock_fd, struct QPInfo *qp_info)
{
    int n;

    n = sock_read(sock_fd, (char *)qp_info, sizeof(struct QPInfo));
    check(n == sizeof(struct QPInfo), "read qp_info from socket.");

    return 0;

error:
    return -1;
}
void print_qp_info(struct QPInfo *qp_info)
{
    printf("LID: %u\n", qp_info->lid);
    printf("QP Number: %u\n", qp_info->qp_num);
    printf("GID Index: %u\n", qp_info->sgid_index);
    print_ibv_gid(qp_info->gid);
    printf("ib_port: %u\n", qp_info->ib_port);
    printf("rkey: %u\n", qp_info->rkey);
    printf("raddr: %ld\n", qp_info->raddr);
    printf("rsize: %d\n", qp_info->rsize);
    printf("psn: %d\n", qp_info->psn);
}

int modify_qp_to_rts(struct ibv_qp *qp, struct QPInfo *local, struct QPInfo *remote)
{
    int ret = 0;

    /* change QP state to INIT */
    {
        struct ibv_qp_attr qp_attr = {
            .qp_state = IBV_QPS_INIT,
            .pkey_index = 0,
            .port_num = local->ib_port,
            .qp_access_flags =
                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_WRITE,
        };

        ret = ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
        check(ret == 0, "Failed to modify qp to INIT.");
    }

    /* Change QP state to RTR */
    {
        struct ibv_qp_attr qp_attr = {
            .qp_state = IBV_QPS_RTR,
            .path_mtu = IBV_MTU_1024,
            .dest_qp_num = remote->qp_num,
            .rq_psn = remote->psn,
            .max_dest_rd_atomic = 1,
            .min_rnr_timer = 12,
            .ah_attr.is_global = 0,      // grh is invalid for none RoCE
            .ah_attr.dlid = remote->lid, // Not used for RoCEv2
            .ah_attr.sl = 0,             // Service level
            .ah_attr.src_path_bits = 0,
            .ah_attr.port_num = local->ib_port,
        };

        if (qp_attr.ah_attr.dlid == 0)
        {
            printf("Using RoCEv2 transport\n");
            qp_attr.ah_attr.is_global = 1; // grh should be configured for RoCEv2
            qp_attr.ah_attr.grh.sgid_index = local->sgid_index;
            qp_attr.ah_attr.grh.dgid = remote->gid;
            qp_attr.ah_attr.grh.hop_limit = 0xFF;
            qp_attr.ah_attr.grh.traffic_class = 0;
            qp_attr.ah_attr.grh.flow_label = 0;
        }
        ret = ibv_modify_qp(qp, &qp_attr,
                            IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                                IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER | 0);
        check(ret == 0, "Failed to change qp to rtr.");
    }

    /* Change QP state to RTS */
    {
        struct ibv_qp_attr qp_attr = {
            .qp_state = IBV_QPS_RTS,
            .timeout = 14,
            .retry_cnt = 7,
            .rnr_retry = 7,
            .sq_psn = local->psn,
            .max_rd_atomic = 1,
        };

        ret = ibv_modify_qp(qp, &qp_attr,
                            IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                                IBV_QP_MAX_QP_RD_ATOMIC);
        check(ret == 0, "Failed to modify qp to RTS.");
    }

    return 0;
error:
    return -1;
}
int setup_ib(struct IBRes *ib_res)
{
    int ret = 0;
    int i = 0;
    int num_devices = 0;
    struct ibv_device **dev_list = NULL;
    memset(ib_res, 0, sizeof(struct IBRes));

    ib_res->num_qps = 1;
    /* get IB device list */
    dev_list = ibv_get_device_list(&num_devices);
    check(dev_list != NULL, "Failed to get ib device list.");

    /* create IB context */
    ib_res->ctx = ibv_open_device(dev_list[config_info.dev_index]);
    check(ib_res->ctx != NULL, "Failed to open ib device.");

    /* allocate protection domain */
    ib_res->pd = ibv_alloc_pd(ib_res->ctx);
    check(ib_res->pd != NULL, "Failed to allocate protection domain.");

    /* query IB port attribute */
    ret = ibv_query_port(ib_res->ctx, config_info.ib_port, &ib_res->port_attr);
    check(ret == 0, "Failed to query IB port information.");

    /* query GID (RoCEv2) */
    if (ib_res->port_attr.lid == 0 && ib_res->port_attr.link_layer == IBV_LINK_LAYER_ETHERNET)
    {
        ret = ibv_query_gid(ib_res->ctx, config_info.ib_port, config_info.sgid_index, &ib_res->sgid);
        check(!ret, "Failed to query GID.");

        print_ibv_gid(ib_res->sgid);
    }

    /* register mr */
    /* set the buf_size twice as large as msg_size * num_concurr_msgs */
    /* the recv buffer occupies the first half while the sending buffer */
    /* occupies the second half */
    /* assume all msgs are of the same content */
    ib_res->ib_buf_size = config_info.msg_size * config_info.num_concurr_msgs * ib_res->num_qps;
#ifdef USE_RTE_MEMPOOL
    ib_res->ib_buf = (char *)rte_shm_mgr(ib_res->ib_buf_size);
#else
    ib_res->ib_buf = (char *)memalign(4096, ib_res->ib_buf_size);
#endif
    check(ib_res->ib_buf != NULL, "Failed to allocate ib_buf");

    ib_res->mr = ibv_reg_mr(ib_res->pd, (void *)ib_res->ib_buf, ib_res->ib_buf_size,
                            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    check(ib_res->mr != NULL, "Failed to register mr");

    /* ret = register_remote_mr(ib_res->pd, (void *)ib_res->ib_buf, ib_res->ib_buf_size, &(ib_res->mr)); */
    /* query IB device attr */
    ret = ibv_query_device(ib_res->ctx, &ib_res->dev_attr);
    check(ret == 0, "Failed to query device");

    /* create cq */
    ib_res->cq = ibv_create_cq(ib_res->ctx, ib_res->dev_attr.max_cqe - 1, NULL, NULL, 0);
    check(ib_res->cq != NULL, "Failed to create cq");

    assert(ib_res->dev_attr.max_srq != 0);
    /* create srq */
    struct ibv_srq_init_attr srq_init_attr = {
        .attr.max_wr = ib_res->dev_attr.max_srq_wr,
        .attr.max_sge = 1,
    };

    ib_res->srq = ibv_create_srq(ib_res->pd, &srq_init_attr);
    if (unlikely(!ib_res->srq))
    {
        log_error("Failed to create shared receive queue");
        goto error;
    }

    /* create qp */
    // when srq is used, the max_recv_wr and max_recv_sge is ignored
    struct ibv_qp_init_attr qp_init_attr = {
        .send_cq = ib_res->cq,
        .recv_cq = ib_res->cq,
        .srq = ib_res->srq,
        .cap =
            {
                // TODO add retry to determine the max_send_wr
                .max_send_wr = 64,
                .max_recv_wr = 64,
                /* .max_recv_wr = ib_res->dev_attr.max_qp_wr, */
                .max_send_sge = 1,
                .max_recv_sge = 1,
                /* .max_recv_sge = 1, */
            },
        .qp_type = IBV_QPT_RC,
    };

    ib_res->qp = (struct ibv_qp **)calloc(ib_res->num_qps, sizeof(struct ibv_qp *));
    check(ib_res->qp != NULL, "Failed to allocate qp array");

    for (i = 0; i < ib_res->num_qps; i++)
    {
        ib_res->qp[i] = ibv_create_qp(ib_res->pd, &qp_init_attr);
        check(ib_res->qp[i] != NULL, "Failed to create qp[%d]", i);
    }

    ibv_free_device_list(dev_list);
    return 0;

error:
    if (dev_list != NULL)
    {
        ibv_free_device_list(dev_list);
    }
    return -1;
}

void close_ib_connection(struct IBRes *ib_res)
{
    int i;

    if (ib_res->qp != NULL)
    {
        for (i = 0; i < ib_res->num_qps; i++)
        {
            if (ib_res->qp[i] != NULL)
            {
                ibv_destroy_qp(ib_res->qp[i]);
            }
        }
        free(ib_res->qp);
    }

    if (ib_res->srq != NULL)
    {
        ibv_destroy_srq(ib_res->srq);
    }

    if (ib_res->cq != NULL)
    {
        ibv_destroy_cq(ib_res->cq);
    }

    if (ib_res->mr != NULL)
    {
        ibv_dereg_mr(ib_res->mr);
    }

    if (ib_res->pd != NULL)
    {
        ibv_dealloc_pd(ib_res->pd);
    }

    if (ib_res->ctx != NULL)
    {
        ibv_close_device(ib_res->ctx);
    }

    if (config_info.peer_sockfds != NULL)
    {
        for (i = 0; i < 1; i++)
        {
            if (config_info.peer_sockfds[i] > 0)
            {
                close(config_info.peer_sockfds[i]);
            }
        }
        free(config_info.peer_sockfds);
    }
    if (config_info.self_sockfd > 0)
    {
        close(config_info.self_sockfd);
    }

    if (ib_res->ib_buf != NULL)
    {
#ifdef USE_RTE_MEMPOOL
        rte_mempool_put(config_info.mempool, ib_res->ib_buf);
#else
        free(ib_res->ib_buf);
#endif
    }

#ifdef USE_RTE_MEMPOOL
    /* Clean up rte mempool */
    rte_mempool_free(config_info.mempool);
#endif
}
