
#include "ib.h"
#include "sock.h"
#include <arpa/inet.h>
#include <assert.h>
#include <bits/getopt_core.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char *argv[])
{

    static struct option long_options[] = {
        {"server_ip", required_argument, NULL, 1},
        {"port", required_argument, NULL, 2},
        {"local_ip", required_argument, NULL, 3},
    };

    char buffer[128];
    char *message = "hello from server";
    int ch = 0;
    bool is_server = true;
    char *server_name = NULL;
    char *local_ip = NULL;

    char *port = NULL;
    while ((ch = getopt_long(argc, argv, "", long_options, NULL)) != -1)
    {
        switch (ch)
        {
        case 1:
            is_server = false;
            server_name = strdup(optarg);
            break;
        case 3:
            local_ip = strdup(optarg);
            break;
        case 2:
            port = strdup(optarg);
            break;
        case '?':
            printf("options error\n");
            exit(1);
        }
    }
    printf("Hello, World!\n");

    int self_fd = 0;
    int peer_fd = 0;
    struct sockaddr_in peer_addr;

    socklen_t peer_addr_len = sizeof(struct sockaddr_in);
    if (is_server)
    {

        self_fd = sock_create_bind(local_ip, port);
        assert(self_fd > 0);
        listen(self_fd, 5);
        peer_fd = accept(self_fd, (struct sockaddr *)&peer_addr, &peer_addr_len);
        assert(peer_fd > 0);
        sock_write(peer_fd, message, strlen(message));
        close(peer_fd);
    }
    else
    {
        peer_fd = sock_create_connect(server_name, port);
        sock_read(peer_fd, buffer, strlen(message));
        printf("%s\n", buffer);
        close(peer_fd);
    }
    free(server_name);
    free(local_ip);
    free(port);
}
