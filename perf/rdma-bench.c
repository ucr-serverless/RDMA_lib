#define _GNU_SOURCE
#include "client.h"
#include "log.h"
#include "rdma-bench_cfg.h"
#include "server.h"
#include "setup_ib.h"
#include <getopt.h>
#include <libconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef USE_RTE_MEMPOOL
#include <rte_branch_prediction.h>
#include <rte_eal.h>
#include <rte_errno.h>
#endif /* ifdef USE_RTE_MEMPOOL */
FILE *log_fp;

int init_env(struct ConfigInfo *config_info);
void destroy_env();

int main(int argc, char *argv[])
{
    int ret = 0;
    init_config_info(&config_info);
#ifdef USE_RTE_MEMPOOL
    ret = rte_eal_init(argc, argv);
    if (unlikely(ret == -1))
    {
        fprintf(stderr, "rte_eal_init() error: %s\n", rte_strerror(rte_errno));
        return 1;
    }

    argc -= ret;
    argv += ret;
#endif

    static struct option long_options[] = {
        {"server_ip", required_argument, NULL, 'H'},    {"dev_index", required_argument, NULL, 'd'},
        {"sgid_index", required_argument, NULL, 'x'},   {"ib_port", required_argument, NULL, 'i'},
        {"sock_port", required_argument, NULL, 'p'},    {"benchmark_type", required_argument, NULL, 't'},
        {"msg_size", required_argument, NULL, 's'},     {"num_concurr_msgs", required_argument, NULL, 'c'},
        {"warm_up_iter", required_argument, NULL, 9}, {"total_iter", required_argument, NULL, 'n'},
        {"signal_freq", required_argument, NULL, 11}};

    int ch = 0;
    while ((ch = getopt_long(argc, argv, "", long_options, NULL)) != -1)
    {
        switch (ch)
        {
        case 'H':
            config_info.is_server = false;
            config_info.server_ip = strdup(optarg);
            break;
        case 'd':
            config_info.dev_index = atoi(optarg);
            break;
        case 'x':
            config_info.sgid_index = atoi(optarg);
            break;
        case 'i':
            config_info.ib_port = atoi(optarg);
            break;
        case 'p':
            config_info.sock_port = strdup(optarg);
            break;
        case 't':
            config_info.benchmark_type = atoi(optarg);
            break;
        case 's':
            config_info.msg_size = atoi(optarg);
            break;
        case 'c':
            config_info.num_concurr_msgs = atoi(optarg);
            break;
        case 9:
            config_info.warm_up_iter = atoi(optarg);
            break;
        case 'n':
            config_info.total_iter = atoi(optarg);
            break;
        case 11:
            config_info.signal_freq = atoi(optarg);
            break;
        case '?':
#ifdef USE_RTE_MEMPOOL
            printf("Usage: %s -l 0 --file-prefix=$UNIQUE_NAME --proc-type=primary --no-telemetry --no-pci -- [config "
                   "options]\n",
                   argv[0]);
#else
            printf("Usage: %s [config options]\n", argv[0]);
#endif
            goto error;
        }
    }
    print_benchmark_cfg(&config_info);

    ret = init_env(&config_info);
    check(ret == 0, "Failed to init env");

    struct IBRes ib_res;
    ret = setup_ib(&ib_res);
    check(ret == 0, "Failed to setup IB");

    /* connect QP */
    if (config_info.is_server)
    {
        ret = connect_qp_server(&ib_res);
    }
    else
    {
        ret = connect_qp_client(&ib_res);
    }

    check(ret == 0, "Failed to connect qp");

    if (config_info.is_server)
    {
        printf("Running Server...\n");
        ret = run_server(&ib_res);
    }
    else
    {
        printf("Running Client...\n");
        ret = run_client(&ib_res);
    }
    check(ret == 0, "Failed to run workload");

error:
    close_ib_connection(&ib_res);
    destroy_env();
    free_config_info(&config_info);
#ifdef USE_RTE_MEMPOOL
    rte_eal_cleanup();
#endif
    return ret;
}

int init_env(struct ConfigInfo *config_info)
{
    char fname[64] = {'\0'};

    if (config_info->is_server)
    {
        sprintf(fname, "server.log");
    }
    else
    {
        sprintf(fname, "client.log");
    }
    log_fp = fopen(fname, "w");
    check(log_fp != NULL, "Failed to open log file");

    return 0;
error:
    return -1;
}

void destroy_env()
{
    if (log_fp != NULL)
    {
        fclose(log_fp);
    }
}
