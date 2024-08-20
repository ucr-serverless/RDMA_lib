#ifndef UTILS_H_
#define UTILS_H_

#include <assert.h>
#include <infiniband/verbs.h>
#include <inttypes.h>
#include <stdio.h>

#ifdef USE_RTE_MEMPOOL
#include <rte_branch_prediction.h>
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifndef USE_RTE_MEMPOOL
#define unlikely(x) (!!(x))
#endif // !USE_RTE_MEMPOOL


void print_ibv_gid(union ibv_gid gid);
#endif /* UTILS_H_ */
