#ifndef CLIENT_H_
#define CLIENT_H_

#include "rdma-bench_cfg.h"
#include "setup_ib.h"
int run_client(struct IBRes *ib_res);

int connect_qp_client(struct IBRes *ib_res);
#endif /* client.h */
