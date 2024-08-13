#ifndef CONFIG_H_
#define CONFIG_H_

#include "utils.h"
#include <stdint.h>
#include <stdlib.h>
struct user_param
{
    uint32_t node_idx;
    uint32_t device_idx;
    uint32_t sgid_idx;
    uint32_t qp_num;
    uint32_t mr_num;
    uint64_t bf_size;
    uint32_t max_send_wr;
    uint8_t ib_port;
    void *extras;
};

#endif /* CONFIG_H_*/
