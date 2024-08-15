#ifndef MM_H_
#define MM_H_

#include "bitmap.h"
#include "ib.h"
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>

int find_avaliable_slot(bitmap * bp, uint32_t message_size, uint32_t blk_size, struct mr_info *start, uint32_t mr_info_len, uint32_t *slot);

int slot_idx_to_addr(uint32_t psn, uint32_t blk_size, struct mr_info *start, uint32_t mr_info_len, uint32_t slot_idx, void **addr);

int send_release_signal(int sock_fd, void *addr, uint32_t len);

int receive_release_signal(int sock_fd, void **addr, uint32_t *len);
// need a psn to idx map


#endif // !MM_H_
