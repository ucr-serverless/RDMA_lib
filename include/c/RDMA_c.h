#pragma once

#include "ib.h"
#include "mr.h"
#include "qp.h"


#ifdef __cplusplus
extern "C" {
#endif

struct ibv_device* rdma_find_dev(const char *ib_devname);

#ifdef __cplusplus
}
#endif
