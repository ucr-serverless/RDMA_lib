#include <arpa/inet.h>
#include <assert.h>
#include <infiniband/verbs.h>
#include <stdint.h>
#include <stdlib.h>

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "ib.h"
#include "log.h"
#include "mr.h"
#include "qp.h"
#include "rdma_config.h"
#include "sock.h"
#include "utils.h"

struct ibv_device* rdma_find_dev(const char *ib_devname)
{
	int num_of_device;
	struct ibv_device **dev_list;
	struct ibv_device *ib_dev = NULL;

	dev_list = ibv_get_device_list(&num_of_device);

	//coverity[uninit_use]
	if (num_of_device <= 0) {
		fprintf(stderr," Did not detect devices \n");
		fprintf(stderr," If device exists, check if driver is up\n");
        goto error;
	}

	if (!ib_devname) {
        goto error;
	} else {
		for (; (ib_dev = *dev_list); ++dev_list)
			if (!strcmp(ibv_get_device_name(ib_dev), ib_devname))
                goto success;
		if (!ib_dev) {
			fprintf(stderr, "IB device %s not found\n", ib_devname);
            goto error;
		}
	}
error:
    return NULL;
success:
	return ib_dev;
}

int init_ib_ctx(struct ib_ctx *ctx, struct rdma_param *params, void **local_buffers, void **remote_buffers)
{
    int num_of_device;
    struct ibv_device **dev_list;

    dev_list = ibv_get_device_list(&num_of_device);

    if (unlikely(num_of_device <= 0))
    {
        log_error(" Did not detect devices \n");
        log_error(" If device exists, check if driver is up\n");
        goto error;
    }
    ctx->device = dev_list[params->device_idx];
    if (unlikely(!(ctx->device)))
    {
        log_error("Can not open device %d", params->device_idx);
        goto error;
    }

    ctx->context = ibv_open_device(ctx->device);

    if (unlikely(!(ctx->context)))
    {
        log_error("Couldn't get context for the device\n");
        goto error;
    }

    ctx->device_idx = params->device_idx;

    ctx->pd = ibv_alloc_pd(ctx->context);

    if (unlikely(!(ctx->pd)))
    {
        log_error("Couldn't open protecttion domain\n");
        goto error;
    }

    if (unlikely(ibv_query_device(ctx->context, &(ctx->device_attr))))
    {
        log_error("Error, ibv_query_device");
        goto error;
    }

    if (unlikely(ibv_query_port(ctx->context, params->ib_port, &ctx->port_attr)))
    {
        log_error("Error, ibv_query_port");
        goto error;
    }

    ctx->lid = ctx->port_attr.lid;
    ctx->ib_port = params->ib_port;

    if (unlikely(ibv_query_gid(ctx->context, params->ib_port, params->sgid_idx, &ctx->gid)))
    {
        log_error("Error, ibv_query_gid");
        goto error;
    }

    ctx->sgid_idx = params->sgid_idx;

    ctx->send_channel = ibv_create_comp_channel(ctx->context);
    if (unlikely(!(ctx->send_channel)))
    {
        log_error("Error, ibv_create_comp_channel() failed\n");
        goto error;
    }
    size_t retry_cnt = 0;
    uint32_t init_cqe = params->init_cqe_num;
    do
    {
        ctx->send_cq = ibv_create_cq(ctx->context, init_cqe, NULL, ctx->send_channel, 0);
        init_cqe /= 2;
        retry_cnt++;
    } while (!ctx->send_cq && retry_cnt < RETRY_MAX);
    if (unlikely(!(ctx->send_cq)))
    {
        log_error("Error, ibv_create_qp() send completion queue failed\n");
        goto error;
    }

    ctx->send_cqe = ctx->send_cq->cqe;

    ctx->recv_channel = ibv_create_comp_channel(ctx->context);
    if (unlikely(!(ctx->recv_channel)))
    {
        log_error("Error, ibv_create_comp_channel() failed\n");
        goto error;
    }

    init_cqe = params->init_cqe_num;

    retry_cnt = 0;
    do
    {
        ctx->recv_cq = ibv_create_cq(ctx->context, init_cqe, NULL, ctx->recv_channel, 0);
        init_cqe /= 2;
        retry_cnt++;
    } while (!ctx->send_cq && retry_cnt < RETRY_MAX);
    if (unlikely(!(ctx->recv_cq)))
    {
        log_error("Error, ibv_create_qp() receive completion queue failed\n");
        goto error;
    }

    ctx->recv_cqe = ctx->recv_cq->cqe;

    ctx->srqe = ctx->device_attr.max_srq_wr - 1;
    struct ibv_srq_init_attr attr = {.attr = {/* when using sreq, rx_depth sets the max_wr */
                                              .max_wr = ctx->device_attr.max_srq_wr - 1,
                                              .max_sge = ctx->device_attr.max_srq_sge - 1}};
    retry_cnt = 0;

    do
    {
        ctx->srq = ibv_create_srq(ctx->pd, &attr);
        attr.attr.max_wr /= 2;
        attr.attr.max_sge /= 2;
        retry_cnt++;
    } while (!ctx->srq && retry_cnt < RETRY_MAX);

    ctx->srqe = attr.attr.max_wr;
    ctx->max_srq_sge = attr.attr.max_sge;

    if (unlikely(!(ctx->srq)))
    {
        log_error("Error, ibv_cratee_srq() failed\n");
        goto error;
    }

    if (unlikely(init_multiple_rc_qp_srq_unsignaled(ctx, params) == RDMA_FAILURE))
    {
        log_error("Error, init multiple qps\n");
        goto error;
    }
    ctx->local_mrs = NULL;

    if (local_buffers)
    {
        if (unlikely(register_multiple_local_mr(ctx, local_buffers, params->local_mr_size, params->local_mr_num,
                                                &ctx->local_mrs) == RDMA_FAILURE))
        {
            log_error("Error, register local mrs\n");
            goto error;
        }

        ctx->local_mrs_num = params->local_mr_num;
    }

    ctx->remote_mrs = NULL;

    if (remote_buffers)
    {
        if (unlikely(register_multiple_remote_mr(ctx, remote_buffers, params->remote_mr_size, params->remote_mr_num,
                                                 &ctx->remote_mrs) == RDMA_FAILURE))
        {
            log_error("Error, register remote mrs\n");
            goto error;
        }

        ctx->remote_mrs_num = params->remote_mr_num;
    }

    ctx->send_wc = (struct ibv_wc *)calloc(params->n_send_wc, sizeof(struct ibv_wc));

    if (unlikely(ctx->send_wc == NULL))
    {
        log_error("Error, allocate send wc fail");
        goto error;
    }

    ctx->recv_wc = (struct ibv_wc *)calloc(params->n_recv_wc, sizeof(struct ibv_wc));

    if (unlikely(ctx->recv_wc == NULL))
    {
        log_error("Error, allocate recv wc fail");
        goto error;
    }

    ctx->send_sg_list = NULL;

    // use srq if the srq is used
    ctx->recv_sg_list = NULL;

    ctx->srq_sg_list = NULL;

    ibv_free_device_list(dev_list);
    return RDMA_SUCCESS;
error:
    ibv_free_device_list(dev_list);
    destroy_ib_ctx(ctx);
    return RDMA_FAILURE;
}

void destroy_ib_ctx(struct ib_ctx *ctx)
{
    // caller is responsible for release the raw memory
    if (ctx->remote_mrs)
    {
        for (size_t i = 0; i < ctx->remote_mrs_num; i++)
        {
            if (ctx->remote_mrs[i])
            {
                ibv_dereg_mr(ctx->remote_mrs[i]);
            }
        }
        free(ctx->remote_mrs);
        ctx->remote_mrs = NULL;
    }

    if (ctx->local_mrs)
    {
        for (size_t i = 0; i < ctx->local_mrs_num; i++)
        {
            if (ctx->local_mrs[i])
            {
                ibv_dereg_mr(ctx->local_mrs[i]);
            }
        }
        free(ctx->local_mrs);
        ctx->local_mrs = NULL;
    }
    if (ctx->qps)
    {
        for (size_t i = 0; i < ctx->qp_num; i++)
        {
            if (ctx->qps[i] != NULL)
            {
                ibv_destroy_qp(ctx->qps[i]);
            }
        }
        free(ctx->qps);
        ctx->qps = NULL;
    }
    if (ctx->send_channel)
    {
        ibv_destroy_comp_channel(ctx->send_channel);
        ctx->send_channel = NULL;
    }
    if (ctx->recv_channel)
    {
        ibv_destroy_comp_channel(ctx->recv_channel);
        ctx->recv_channel = NULL;
    }
    if (ctx->send_cq)
    {
        ibv_destroy_cq(ctx->send_cq);
        ctx->send_cq = NULL;
    }
    if (ctx->recv_cq)
    {
        ibv_destroy_cq(ctx->recv_cq);
        ctx->recv_cq = NULL;
    }
    if (ctx->srq)
    {
        ibv_destroy_srq(ctx->srq);
        ctx->srq = NULL;
    }
    if (ctx->pd != NULL)
    {
        ibv_dealloc_pd(ctx->pd);
        ctx->pd = NULL;
    }

    if (ctx->context != NULL)
    {
        ibv_close_device(ctx->context);
        ctx->context = NULL;
    }
    if (ctx->send_wc)
    {
        free(ctx->send_wc);
        ctx->send_wc = NULL;
    }
    if (ctx->recv_wc)
    {
        free(ctx->recv_wc);
        ctx->recv_wc = NULL;
    }
}

void print_ib_res(struct ib_res *res)
{
    printf("n_mr: %u\n\n", res->n_mr);
    for (size_t i = 0; i < res->n_mr; i++)
    {
        printf("mr: %lu\n", i);
        printf("\tlength: %lu\n", res->mrs[i].length);
        printf("\tlkey: %u\n", res->mrs[i].lkey);
        printf("\trkey: %u\n", res->mrs[i].rkey);
        printf("\taddr: %p\n", res->mrs[i].addr);
    }
    printf("n_qp: %u\n", res->n_mr);
    for (size_t i = 0; i < res->n_qp; i++)
    {
        printf("qp: %lu\n", i);
        printf("\tqp_num: %u\n", res->qp_nums[i]);
    }
}

int init_local_ib_res(struct ib_ctx *ctx, struct ib_res *res)
{

    res->gid = ctx->gid;
    res->psn = 0;
    res->n_mr = ctx->remote_mrs_num;
    res->n_qp = ctx->qp_num;
    res->lid = ctx->lid;
    res->sgid_idx = ctx->sgid_idx;
    res->ib_port = ctx->ib_port;

    res->qp_nums = (uint32_t *)calloc(res->n_qp, sizeof(uint32_t));
    if (!res->qp_nums)
    {
        log_error("Error, fail to allocate mem for qps");
        goto error;
    }

    res->mrs = (struct mr_info *)calloc(res->n_mr, sizeof(struct mr_info));
    if (!res->mrs)
    {
        log_error("Error, fail to allocate mem for mrs");
        goto error;
    }

    for (size_t i = 0; i < res->n_qp; i++)
    {
        res->qp_nums[i] = ctx->qps[i]->qp_num;
    }

    for (size_t i = 0; i < res->n_mr; i++)
    {
        res->mrs[i].length = ctx->remote_mrs[i]->length;
        res->mrs[i].lkey = ctx->remote_mrs[i]->lkey;
        res->mrs[i].rkey = ctx->remote_mrs[i]->rkey;
        res->mrs[i].addr = ctx->remote_mrs[i]->addr;
    }
    return RDMA_SUCCESS;
error:
    log_error("init local ib res failed\n");
    destroy_ib_res(res);
    return RDMA_FAILURE;
}

int send_ib_res(struct ib_res *res, int sock_fd)
{
    if (sock_write(sock_fd, res, sizeof(struct ib_res)) != sizeof(struct ib_res))
    {
        log_error("Error, Send ib res\n");
        goto error;
    }
    for (size_t i = 0; i < res->n_qp; i++)
    {

        if (sock_write(sock_fd, &(res->qp_nums[i]), sizeof(uint32_t)) != sizeof(uint32_t))
        {
            log_error("Error, Send qp_num at index %lu\n", i);
            goto error;
        }
    }

    for (size_t i = 0; i < res->n_mr; i++)
    {

        if (sock_write(sock_fd, &(res->mrs[i]), sizeof(struct mr_info)) != sizeof(struct mr_info))
        {
            log_error("Error, Send ibv_mr at index %lu\n", i);
            goto error;
        }
    }

    return RDMA_SUCCESS;

error:
    destroy_ib_res(res);
    log_error("Error, recv remote ib res");
    return RDMA_FAILURE;
}

int recv_ib_res(struct ib_res *res, int sock_fd)
{
    if (sock_read(sock_fd, res, sizeof(struct ib_res)) != sizeof(struct ib_res))
    {
        log_error("Error, recv ib res\n");
        goto error;
    }
    res->qp_nums = (uint32_t *)calloc(res->n_qp, sizeof(uint32_t));
    if (!res->qp_nums)
    {
        log_error("Error, fail to allocate mem for qps");
        goto error;
    }
    res->mrs = (struct mr_info *)calloc(res->n_mr, sizeof(struct mr_info));
    if (!res->mrs)
    {
        log_error("Error, fail to allocate mem for mrs");
        goto error;
    }
    for (size_t i = 0; i < res->n_qp; i++)
    {

        if (sock_read(sock_fd, &(res->qp_nums[i]), sizeof(uint32_t)) != sizeof(uint32_t))
        {
            log_error("Error, Recv qp_num at index %lu\n", i);
            goto error;
        }
    }

    for (size_t i = 0; i < res->n_mr; i++)
    {

        if (sock_read(sock_fd, &(res->mrs[i]), sizeof(struct mr_info)) != sizeof(struct mr_info))
        {
            log_error("Error, Recv ibv_mr at index %lu\n", i);
            goto error;
        }
    }

    return RDMA_SUCCESS;
error:
    destroy_ib_res(res);
    log_error("Error, recv remote ib res");
    return RDMA_FAILURE;
}

void destroy_ib_res(struct ib_res *res)
{
    if (res)
    {
        if (res->qp_nums)
        {

            free(res->qp_nums);
            res->qp_nums = NULL;
        }
        if (res->mrs)
        {

            free(res->mrs);
            res->mrs = NULL;
        }
    }
}

int post_send_sg_list(struct ibv_qp *qp, struct ibv_sge *sg_list, uint32_t sg_list_len, uint64_t wr_id,
                      uint32_t imm_data, int flag)
{
    int ret = 0;
    struct ibv_send_wr *bad_send_wr;

    struct ibv_send_wr send_wr = {.wr_id = wr_id,
                                  .sg_list = sg_list,
                                  .num_sge = sg_list_len,
                                  .opcode = IBV_WR_SEND_WITH_IMM,
                                  .send_flags = flag,
                                  .imm_data = htonl(imm_data)};

    ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
    if (ret != 0)
    {
        log_error("post send sg_list fail, %s", strerror(ret));
        return RDMA_FAILURE;
    }
    return RDMA_SUCCESS;
}

int post_send(struct ibv_qp *qp, char *buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id, uint32_t imm_data,
              int flag)
{
    struct ibv_sge list = {.addr = (uintptr_t)buf, .length = req_size, .lkey = lkey};

    return post_send_sg_list(qp, &list, 1, wr_id, imm_data, flag);
}

int post_send_signaled(struct ibv_qp *qp, char *buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                       uint32_t imm_data)

{
    return post_send(qp, buf, req_size, lkey, wr_id, imm_data, IBV_SEND_SIGNALED);
}

int post_send_unsignaled(struct ibv_qp *qp, char *buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                         uint32_t imm_data)
{
    return post_send(qp, buf, req_size, lkey, wr_id, imm_data, 0);
}

int post_send_sg_list_signaled(struct ibv_qp *qp, struct ibv_sge *sg_list, uint32_t sg_list_len, uint64_t wr_id,
                               uint32_t imm_data)

{
    return post_send_sg_list(qp, sg_list, sg_list_len, wr_id, imm_data, IBV_SEND_SIGNALED);
}

int post_send_sg_list_unsignaled(struct ibv_qp *qp, struct ibv_sge *sg_list, uint32_t sg_list_len, uint64_t wr_id,
                                 uint32_t imm_data)
{
    return post_send_sg_list(qp, sg_list, sg_list_len, wr_id, imm_data, 0);
}

int post_srq_recv_sg_list(struct ibv_srq *srq, struct ibv_sge *sg_list, uint32_t sg_list_len, uint64_t wr_id)
{
    int ret = 0;

    struct ibv_recv_wr *bad_recv_wr;

    struct ibv_recv_wr recv_wr = {.wr_id = wr_id, .next = NULL, .sg_list = sg_list, .num_sge = sg_list_len};

    ret = ibv_post_srq_recv(srq, &recv_wr, &bad_recv_wr);
    if (ret != 0)
    {
        log_error("post srq failed: %s\n", strerror(ret));
        return RDMA_FAILURE;
    }
    return RDMA_SUCCESS;
}

int post_srq_recv(struct ibv_srq *srq, char *buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id)
{
    int ret = 0;
    struct ibv_recv_wr *bad_recv_wr;

    struct ibv_sge list = {.addr = (uintptr_t)buf, .length = req_size, .lkey = lkey};

    struct ibv_recv_wr recv_wr = {.wr_id = wr_id, .sg_list = &list, .num_sge = 1};

    ret = ibv_post_srq_recv(srq, &recv_wr, &bad_recv_wr);
    if (ret != 0)
    {
        return RDMA_FAILURE;
    }
    return RDMA_SUCCESS;
}

int post_dumb_srq_recv(struct ibv_srq *srq, void *buf, uint32_t buf_size, uint32_t lkey, uint64_t wr_id)
{
    int ret = 0;
    ret = post_srq_recv(srq, buf, buf_size, lkey, wr_id);
    if (unlikely(ret == RDMA_FAILURE))
    {
        log_error("Error, pre post srq requests fail\n");
        return RDMA_FAILURE;
    }
    return RDMA_SUCCESS;
}
int pre_post_dumb_srq_recv(struct ibv_srq *srq, char *buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                           uint32_t num)
{
    int ret = 0;
    for (size_t i = 0; i < num; i++)
    {
        ret = post_srq_recv(srq, buf, req_size, lkey, i);
        if (unlikely(ret != 0))
        {
            log_error("Error, pre post srq requests fail\n");
            return RDMA_FAILURE;
        }
    }
    return RDMA_SUCCESS;
}
int post_write(uint32_t req_size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, char *buf, uint64_t raddr,
               uint32_t rkey, int send_flag)
{
    int ret = 0;
    struct ibv_send_wr *bad_send_wr;

    struct ibv_sge list = {.addr = (uintptr_t)buf, .length = req_size, .lkey = lkey};

    struct ibv_send_wr send_wr = {
        .wr_id = wr_id,
        .sg_list = &list,
        .num_sge = 1,
        .opcode = IBV_WR_RDMA_WRITE,
        .send_flags = send_flag,
        .wr.rdma.remote_addr = raddr,
        .wr.rdma.rkey = rkey,
    };

    ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
    if (ret != 0)
    {
        return RDMA_FAILURE;
    }
    return RDMA_SUCCESS;
}

int post_write_signaled(struct ibv_qp *qp, char *buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id, uint64_t raddr,
                        uint32_t rkey)
{
    return post_write(req_size, lkey, wr_id, qp, buf, raddr, rkey, IBV_SEND_SIGNALED);
}

int post_write_unsignaled(struct ibv_qp *qp, char *buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                          uint64_t raddr, uint32_t rkey)
{
    return post_write(req_size, lkey, wr_id, qp, buf, raddr, rkey, 0);
}

int post_write_imm(uint32_t req_size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, char *buf, uint64_t raddr,
                   uint32_t rkey, uint32_t imm_data, int flag)
{
    int ret = 0;
    struct ibv_send_wr *bad_send_wr;

    struct ibv_sge sg_list = {.addr = (uintptr_t)buf, .length = req_size, .lkey = lkey};

    struct ibv_send_wr send_wr = {
        .wr_id = wr_id,
        .sg_list = &sg_list,
        .num_sge = 1,
        .opcode = IBV_WR_RDMA_WRITE_WITH_IMM,
        .send_flags = flag,
        .imm_data = htonl(imm_data),
        .wr.rdma.remote_addr = raddr,
        .wr.rdma.rkey = rkey,
    };

    ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
    if (ret != 0)
    {
        return RDMA_FAILURE;
    }
    return RDMA_SUCCESS;
}

int post_write_imm_signaled(struct ibv_qp *qp, void *buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                            uint64_t raddr, uint32_t rkey, uint32_t imm_data)
{
    return post_write_imm(req_size, lkey, wr_id, qp, buf, raddr, rkey, imm_data, IBV_SEND_SIGNALED);
}

int post_write_imm_unsignaled(struct ibv_qp *qp, void *buf, uint32_t req_size, uint32_t lkey, uint64_t wr_id,
                              uint64_t raddr, uint32_t rkey, uint32_t imm_data)
{
    return post_write_imm(req_size, lkey, wr_id, qp, buf, raddr, rkey, imm_data, 0);
}
