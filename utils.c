#include "utils.h"

void print_ibv_gid(union ibv_gid gid)
{
    printf("Raw GID: ");
    for (int i = 0; i < 16; ++i)
    {
        printf("%02x", gid.raw[i]);
        if (i % 2 && i != 15)
        {
            printf(":");
        }
    }
    printf("\n");

    printf("Subnet Prefix: 0x%" PRIx64 "\n", (uint64_t)gid.global.subnet_prefix);
    printf("Interface ID: 0x%" PRIx64 "\n", (uint64_t)gid.global.interface_id);
}
