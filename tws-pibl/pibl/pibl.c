#include "config.h"
#include "balancer.h"
#include "logger.h"
#include "proxy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

int main(int argc, char *argv[]) {
    const char *config_path = "pibl/config.txt";
    const char *log_path    = "pibl/pibl.log";

    if (argc >= 2) config_path = argv[1];
    if (argc >= 3) log_path    = argv[2];

    load_config(config_path);

    mkdir(cache_dir, 0755);

    if (log_init(log_path) < 0) { perror("fopen log"); exit(1); }

    signal(SIGPIPE, SIG_IGN);

    char msg[512];
    snprintf(msg, sizeof(msg),
             "PIBL starting | port=%d | backends=%d | ttl=%ds | cache=%s",
             listen_port, backend_count, cache_ttl, cache_dir);
    log_msg("INFO", msg);

    for (int i = 0; i < backend_count; i++) {
        snprintf(msg, sizeof(msg), "  Backend[%d] = %s:%d",
                 i, backends[i].host, backends[i].port);
        log_msg("INFO", msg);
    }

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(listen_port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(srv, 128) < 0) { perror("listen"); exit(1); }

    log_msg("INFO", "PIBL ready — accepting connections");

    while (1) {
        ConnData *cd = malloc(sizeof(ConnData));
        if (!cd) continue;
        socklen_t cli_len = sizeof(cd->cli);
        cd->fd = accept(srv, (struct sockaddr *)&cd->cli, &cli_len);
        if (cd->fd < 0) { free(cd); continue; }

        char cip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cd->cli.sin_addr, cip, sizeof(cip));
        snprintf(msg, sizeof(msg), "Connection from %s", cip);
        log_msg("INFO", msg);

        pthread_t tid;
        pthread_create(&tid, NULL, proxy_thread, cd);
        pthread_detach(tid);
    }
    return 0;
}
