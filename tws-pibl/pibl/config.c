#include "config.h"
#include "balancer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int  listen_port = 8080;
int  cache_ttl   = 60;
char cache_dir[MAX_PATH] = "./cache";

void load_config(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen config"); exit(1); }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        if (strncmp(line, "port=", 5) == 0) {
            listen_port = atoi(line + 5);
        } else if (strncmp(line, "ttl=", 4) == 0) {
            cache_ttl = atoi(line + 4);
        } else if (strncmp(line, "backend=", 8) == 0) {
            if (backend_count >= MAX_BACKENDS) continue;
            char *val = line + 8;
            char *colon = strrchr(val, ':');
            if (!colon) continue;
            *colon = '\0';
            strncpy(backends[backend_count].host, val,
                    sizeof(backends[backend_count].host) - 1);
            backends[backend_count].port = atoi(colon + 1);
            backend_count++;
        }
    }
    fclose(f);
}
