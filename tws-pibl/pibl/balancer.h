#ifndef BALANCER_H
#define BALANCER_H

#define MAX_BACKENDS 16

typedef struct {
    char host[128];
    int  port;
} Backend;

extern Backend backends[MAX_BACKENDS];
extern int     backend_count;

Backend *next_backend(void);

#endif
