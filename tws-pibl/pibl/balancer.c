#include "balancer.h"
#include <pthread.h>

Backend backends[MAX_BACKENDS];
int     backend_count = 0;

static int rr_index = 0;
static pthread_mutex_t rr_mutex = PTHREAD_MUTEX_INITIALIZER;

Backend *next_backend(void) {
    if (backend_count == 0) return NULL;
    pthread_mutex_lock(&rr_mutex);
    Backend *b = &backends[rr_index % backend_count];
    rr_index = (rr_index + 1) % backend_count;
    pthread_mutex_unlock(&rr_mutex);
    return b;
}
