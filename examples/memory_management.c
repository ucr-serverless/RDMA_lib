#include "memory_management.h"
#include "debug.h"
#include "bitmap.h"
#include "ib.h"
#include "sock.h"
#include "utils.h"
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#define FIND_SLOT_RETRY_MAX 3

int init_qp_bitmap(uint32_t mr_num, uint32_t mr_len, uint32_t blk_len, bitmap **bp)
{
    *bp = bitmap_allocate(mr_num * mr_len / blk_len);
    if (!bp) {
        log_error("Error, allocate bitmap\n");
        exit(1);
    }
    return SUCCESS;

}

int find_avaliable_slot_inside_mr(bitmap *bp, uint32_t mr_bp_idx_start, uint32_t mr_blk_len, uint32_t msg_blk_len,
                                  uint32_t *slot_idx)
{
    if (mr_blk_len < msg_blk_len)
    {
        log_error("Error, meg size is larger than mr size\n");
        return FAILURE;
    }
    bool success = true;
    for (size_t i = 0; i <= mr_blk_len - msg_blk_len; i++)
    {
        for (size_t j = 0; j < msg_blk_len; j++)
        {
            if (bitmap_read(bp, mr_bp_idx_start + i + j) == 1)
            {
                i = i + j;
                success = false;
                break;
            }
        }
        if (success)
        {
            *slot_idx = mr_bp_idx_start + i;
            return SUCCESS;
        }
        success = true;
    }
    return FAILURE;
}

int find_avaliable_slot_try(bitmap *bp, uint32_t message_size, uint32_t blk_size, struct mr_info *start,
                            uint32_t mr_info_len, uint32_t *slot_idx)
{
    assert(mr_info_len > 0);
    assert(start);
    uint32_t result_slot_idx = 0;
    uint32_t bp_idx_start_per_mr = 0;
    uint32_t msg_blk_len = (message_size + blk_size - 1) / blk_size;
    int ret = 0;
    for (size_t i = 0; i < mr_info_len; i++)
    {
        size_t mr_blk_len = start[i].length / blk_size;
        assert(start[i].length % blk_size == 0);

        ret = find_avaliable_slot_inside_mr(bp, bp_idx_start_per_mr, mr_blk_len, msg_blk_len, &result_slot_idx);
        if (ret == SUCCESS)
        {
            *slot_idx = result_slot_idx;
            return SUCCESS;
        }
        bp_idx_start_per_mr += mr_blk_len;
    }
    return FAILURE;
}

int find_avaliable_slot(bitmap *bp, uint32_t message_size, uint32_t blk_size, struct mr_info *start,
                        uint32_t mr_info_len, uint32_t *slot_idx)
{
    int ret = 0;
    for (size_t i = 0; i < FIND_SLOT_RETRY_MAX; i++)
    {
        ret = find_avaliable_slot_try(bp, message_size, blk_size, start, mr_info_len, slot_idx);
        if (ret == SUCCESS)
        {
            return SUCCESS;
        }
    }
    log_error("Error, can not find avaliable slot in %d retries\n", FIND_SLOT_RETRY_MAX);
    exit(1);
}

int slot_idx_to_addr(uint32_t psn, uint32_t blk_size, struct mr_info *start, uint32_t mr_info_len, uint32_t slot_idx,
                     void **addr)
{
    assert(mr_info_len > 0);
    assert(start);
    uint32_t blk_len_per_mr = 0;
    size_t i = 0;
    for (; i < mr_info_len; i++)
    {
        blk_len_per_mr = start[i].length / blk_size;
        assert(blk_len_per_mr != 0);
        if (slot_idx >= blk_len_per_mr)
        {
            slot_idx -= blk_len_per_mr;
            continue;
        }
        else
        {
            break;
        }
    }
    if (i == mr_info_len)
    {
        return FAILURE;
    }
    *addr = start[i].addr + blk_size * slot_idx;
    return SUCCESS;
}

int send_release_signal(int sock_fd, void *addr, uint32_t len)
{
    if (sock_write(sock_fd, &addr, sizeof(void *)) != sizeof(void *))
    {
        log_error("Error, send addr\n");
        goto error;
    }
    if (sock_write(sock_fd, &len, sizeof(uint32_t)) != sizeof(uint32_t))
    {
        log_error("Error, send len\n");
        goto error;
    }

    return SUCCESS;
error:
    log_error("send_release_signal failed\n");
    return FAILURE;
}

int receive_release_signal(int sock_fd, void **addr, uint32_t *len)
{
    if (sock_read(sock_fd, addr, sizeof(void *)) != sizeof(void *))
    {
        log_error("Error, recv addr\n");
        goto error;
    }
    if (sock_read(sock_fd, len, sizeof(uint32_t)) != sizeof(uint32_t))
    {
        log_error("Error, recv len\n");
        goto error;
    }

    return SUCCESS;
error:
    log_error("recv_release_signal failed\n");
    return FAILURE;
}
