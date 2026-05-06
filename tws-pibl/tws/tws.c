#include "handler.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <PORT> <LogFile> <DocumentRootFolder>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    const char *log_file_path = argv[2];
    strncpy(doc_root, argv[3], MAX_PATH - 1);

    int dl = strlen(doc_root);
    if (dl > 0 && doc_root[dl - 1] == '/') doc_root[dl - 1] = '\0';

    if (log_init(log_file_path) < 0) { perror("fopen log"); exit(1); }

    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(srv, 128) < 0) { perror("listen"); exit(1); }

    char msg[256];
    snprintf(msg, sizeof(msg), "TWS listening on port %d | root=%s | log=%s",
             port, doc_root, log_file_path);
    log_msg("INFO", msg);

    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int *fd = malloc(sizeof(int));
        if (!fd) continue;
        *fd = accept(srv, (struct sockaddr *)&cli, &cli_len);
        if (*fd < 0) { free(fd); continue; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        snprintf(msg, sizeof(msg), "New connection from %s", ip);
        log_msg("INFO", msg);

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, fd);
        pthread_detach(tid);
    }
    return 0;
}
