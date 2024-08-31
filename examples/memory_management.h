#ifndef MM_H_
#define MM_H_

#include "bitmap.h"
#include "ib.h"
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>

int init_qp_bitmap(uint32_t mr_per_qp, uint32_t mr_len, uint32_t slot_size, bitmap **bp);

int find_avaliable_slot(bitmap * bp, uint32_t message_size, uint32_t slot_size, struct mr_info *start, uint32_t mr_info_len, uint32_t *slot_idx, uint32_t *slot_num, void **raddr, uint32_t *rkey);


int remote_addr_convert_slot_idx(void *remote_addr, uint32_t remote_len, struct mr_info *start, uint32_t mr_info_len, uint32_t slot_size, uint32_t *slot_idx, uint32_t *slot_num);

int qp_num_to_idx(struct ib_res * res, uint32_t qp_num, uint32_t *idx);

int slot_idx_to_addr(struct ib_res *local_res, uint32_t local_qp_num, uint32_t slot_idx, uint32_t mr_info_num,  uint32_t blk_size, void **addr);

uint32_t memory_len_to_slot_len(uint32_t len, uint32_t slot_size);

int send_release_signal(int sock_fd, void *addr, uint32_t len);

int receive_release_signal(int sock_fd, void **addr, uint32_t *len);


#endif // !MM_H_
