#pragma once

#include "ib.h"
#include "mr.h"
#include "qp.h"


#ifdef __cplusplus
extern "C" {
#endif

struct ibv_device* rdma_find_dev(const char *ib_devname);

int connect_rc_qp(struct ibv_qp *qp, uint32_t r_qp_num, uint32_t r_psn, uint16_t r_lid,
                                  union ibv_gid r_gid, uint32_t l_psn, uint8_t l_ib_port, uint8_t l_gid_idx);

#ifdef __cplusplus
}
#endif
