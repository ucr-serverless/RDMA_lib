#include "rdma-bench_cfg.h"

struct ConfigInfo config_info;

void init_config_info(struct ConfigInfo *config_info)
{
    config_info->is_server = true;
}

void print_benchmark_cfg(struct ConfigInfo *config)
{

    printf("server_ip: %s\n", config->server_ip);

    printf("self_sockfd: %d\n", config->self_sockfd);

    printf("is_server: %s\n", config->is_server ? "true" : "false");
    printf("name: %s\n", config->name);
    printf("msg_size: %d\n", config->msg_size);
    printf("num_concurr_msgs: %d\n", config->num_concurr_msgs);
    printf("n_nodes: %d\n", config->n_nodes);

    printf("Nodes:\n");
    for (int i = 0; i < config->n_nodes; i++)
    {
        printf("  Node %d:\n", i);
        printf("    id: %d\n", config->nodes[i].id);
        printf("    hostname: %s\n", config->nodes[i].hostname);
        printf("    n_peers: %d\n", config->nodes[i].n_peers);
        printf("    peers:\n");
        for (int j = 0; j < config->nodes[i].n_peers; j++)
        {
            printf("      %d\n", config->nodes[i].peers[j]);
        }
    }

    printf("sock port: %s\n", config->sock_port);
    printf("current_node_idx: %d\n", config->current_node_idx);
    printf("benchmark_type: %d\n", config->benchmark_type);
    printf("sgid_index: %d\n", config->sgid_index);
    printf("dev_index: %d\n", config->dev_index);
    printf("ib_port: %d\n", config->ib_port);
    printf("msg_size: %d\n", config->msg_size);
    printf("num_concurr_msgs: %d\n", config->num_concurr_msgs);
    printf("warm_up_iter: %d\n", config->warm_up_iter);
    printf("total_iter: %d\n", config->total_iter);
    printf("signal_freq: %d\n", config->signal_freq);
}

int parse_benchmark_cfg(char *cfg_file, struct ConfigInfo *config_info)
{
    config_t config;
    int ret = 0;
    char hostname[MAX_HOSTNAME_LEN];
    int is_hostname_matched = 0;
    const char *name;
    const char *node_hostname;
    config_setting_t *nodes = NULL;
    config_setting_t *node = NULL;
    config_setting_t *peers = NULL;

    if (unlikely(gethostname(hostname, MAX_HOSTNAME_LEN) == -1))
    {
        log_error("gethostname failt");
        goto error_1;
    }

    config_init(&config);
    ret = config_read_file(&config, cfg_file);
    if (unlikely(ret == CONFIG_FALSE))
    {
        log_error("parse_benchmark_cfg() error: line %d: %s", config_error_line(&config), config_error_text(&config));
        goto error_1;
    }

    ret = config_lookup_string(&config, "name", &name);
    if (unlikely(ret == CONFIG_FALSE))
    {
        /* TODO: Error message */
        goto error_1;
    }

    strcpy(config_info->name, name);

    ret = config_lookup_int(&config, "num_concurr_msgs", &config_info->num_concurr_msgs);
    if (unlikely(ret == CONFIG_FALSE))
    {
        log_error("parse_benchmark_cfg() error: ");
        goto error_1;
    }

    ret = config_lookup_int(&config, "msg_size", &config_info->msg_size);
    if (unlikely(ret == CONFIG_FALSE))
    {
        log_error("parse_benchmark_cfg() error: ");
        goto error_1;
    }

    nodes = config_lookup(&config, "nodes");
    if (unlikely(nodes == NULL))
    {
        goto error_1;
    }

    ret = config_setting_is_list(nodes);
    if (unlikely(ret == CONFIG_FALSE))
    {
        goto error_1;
    }

    config_info->n_nodes = config_setting_length(nodes);

    for (int i = 0; i < config_info->n_nodes; i++)
    {
        node = config_setting_get_elem(nodes, i);
        // Get the node id
        ret = config_setting_lookup_int(node, "id", &config_info->nodes[i].id);
        if (unlikely(ret == CONFIG_FALSE))
        {
            goto error_1;
        }

        ret = config_setting_lookup_string(node, "hostname", &node_hostname);
        if (unlikely(ret == CONFIG_FALSE))
        {
            goto error_1;
        }
        strcpy(config_info->nodes[i].hostname, node_hostname);
        if (strcmp(hostname, config_info->nodes[i].hostname) == 0)
        {
            config_info->current_node_idx = i;
            is_hostname_matched = 1;
            log_debug("Hostnames match: %s, node index: %u", node_hostname, i);
        }
        else
        {
            log_debug("Hostnames do not match. Got: %s, Expected: %s", node_hostname, hostname);
        }

        // Get the peers array
        peers = config_setting_lookup(node, "peers");
        if (unlikely(peers == NULL))
        {
            goto error_1;
        }

        ret = config_setting_is_array(peers);
        if (unlikely(ret == CONFIG_FALSE))
        {
            goto error_1;
        }

        config_info->nodes[i].n_peers = config_setting_length(peers);

        for (int j = 0; j < config_info->nodes[i].n_peers; j++)
        {
            config_info->nodes[i].peers[j] = config_setting_get_int_elem(peers, j);
        }
    }
    if (unlikely(!is_hostname_matched))
    {
        log_error("hostname not matched");
        goto error_1;
    }

    ret = config_lookup_int(&config, "benchmark_type", &config_info->benchmark_type);
    if (unlikely(ret == CONFIG_FALSE))
    {
        log_error("parse_benchmark_cfg() error: ");
        goto error_1;
    }

    config_destroy(&config);
    return 0;

error_1:
    return -1;
}

void free_config_info(struct ConfigInfo *config_info)
{
    free(config_info->server_ip);
    free(config_info->sock_port);
}

void print_config_info()
{

    if (config_info.is_server)
    {
        log_info("is_server = %s", "true");
    }
    log_info("msg_size                  = %d", config_info.msg_size);
    log_info("num_concurr_msgs          = %d", config_info.num_concurr_msgs);
    log_info("sock_port                 = %s", config_info.sock_port);
}
