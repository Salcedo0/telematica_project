#ifndef PROXY_H
#define PROXY_H

#include <netinet/in.h>

typedef struct {
    int fd;
    struct sockaddr_in cli;
} ConnData;

void *proxy_thread(void *arg);

#endif
