#include "RDMA_cpp.h"
#include <iostream>



RDMA_base::RDMA_base(const std::string && device_name, uint8_t ib_port, uint32_t gid_idx) {
    this->device = rdma_find_dev(device_name.c_str());
    if (!this->device) {
        throw std::runtime_error("device name not valid");
    }
    this->context = ibv_open_device(this->device);

    if (!this->context)
    {
        throw std::runtime_error("Couldn't get context for the device\n");
    }

    this->pd = ibv_alloc_pd(this->context);

    if (!(this->pd))
    {
        throw std::runtime_error("Couldn't open protecttion domain\n");
    }

    if (ibv_query_device(this->context, &(this->device_attr)))
    {
        throw std::runtime_error("Error, ibv_query_device");
    }

    if (ibv_query_port(this->context, ib_port, &this->port_attr))
    {
        throw std::runtime_error("Error, ibv_query_port");
    }

    this->lid = this->port_attr.lid;
    this->ib_port = ib_port;
    this->gid_idx = gid_idx;

    if (ibv_query_gid(this->context, ib_port, gid_idx, &this->gid))
    {
        throw std::runtime_error("Error, ibv_query_gid");
    }
}

RDMA_base::~RDMA_base()
{
    ibv_dealloc_pd(this->pd);
    ibv_close_device(this->context);

}
