#include "logger.h"
#include <stdio.h>
#include <pthread.h>
#include <time.h>

static FILE *log_fp = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

int log_init(const char *path) {
    log_fp = fopen(path, "a");
    return log_fp ? 0 : -1;
}

void log_msg(const char *level, const char *msg) {
    time_t now = time(NULL);
    char tbuf[64];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    pthread_mutex_lock(&log_mutex);
    fprintf(stdout, "[%s] [%s] %s\n", tbuf, level, msg);
    fflush(stdout);
    if (log_fp) {
        fprintf(log_fp, "[%s] [%s] %s\n", tbuf, level, msg);
        fflush(log_fp);
    }
    pthread_mutex_unlock(&log_mutex);
}

void log_close(void) {
    if (log_fp) { fclose(log_fp); log_fp = NULL; }
}
