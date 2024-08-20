#include "unity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE *log_fp = NULL;

void setUp(void)
{
}

void tearDown(void)
{
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <config_file_path>\n", argv[0]);
        return 1;
    }
    return UNITY_END();
}
