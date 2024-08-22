#ifndef SOCK_H_
#define SOCK_H_

#include <inttypes.h>
#include <stdlib.h>

#define SOCK_SYNC_MSG "sync"

ssize_t sock_read(int sock_fd, void *buffer, size_t len);
ssize_t sock_write(int sock_fd, void *buffer, size_t len);

int sock_create_bind(char *port);
int sock_create_connect(char *server_name, char *port);

#endif /* SOCK_H_ */
