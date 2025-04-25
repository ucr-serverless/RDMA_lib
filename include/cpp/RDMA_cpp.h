#pragma once

#include "RDMA_c.h"
#include <string>

struct RDMA_base {
    struct ibv_device *device;
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_srq *srq;
    union ibv_gid gid;
    struct ibv_cq *send_cq;
    struct ibv_cq *recv_cq;
    struct ibv_device_attr device_attr;
    struct ibv_port_attr port_attr;
    uint16_t lid;
    uint8_t ib_port;
    uint32_t gid_idx;
    RDMA_base(const std::string && device_name, uint8_t ib_port, uint32_t gid_idx);
    RDMA_base() = delete;
    ~RDMA_base();
};
