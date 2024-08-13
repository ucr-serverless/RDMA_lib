#ifndef SERVER_H_
#define SERVER_H_

#include "setup_ib.h"
int run_server(struct IBRes *ib_res);

int connect_qp_server(struct IBRes *ib_res);
#endif /* server.h */
