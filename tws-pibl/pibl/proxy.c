#include "proxy.h"
#include "balancer.h"
#include "cache.h"
#include "config.h"
#include "logger.h"
#include "net_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 8192

void *proxy_thread(void *arg) {
    ConnData *cd = (ConnData *)arg;
    int client_fd = cd->fd;
    free(cd);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((struct sockaddr_in *)&cd)->sin_addr, ip, sizeof(ip));

    /* ── Read request from client ── */
    char req_buf[BUFFER_SIZE * 2];
    int  req_total = 0;

    while (req_total < (int)sizeof(req_buf) - 1) {
        ssize_t n = read(client_fd, req_buf + req_total,
                         sizeof(req_buf) - 1 - req_total);
        if (n <= 0) break;
        req_total += n;
        req_buf[req_total] = '\0';
        if (strstr(req_buf, "\r\n\r\n")) break;
    }

    if (req_total == 0) { close(client_fd); return NULL; }
    req_buf[req_total] = '\0';

    /* Parse method + URI */
    char method[16], uri[MAX_PATH], version[16];
    if (sscanf(req_buf, "%15s %1023s %15s", method, uri, version) != 3) {
        send_error(client_fd, 400, "Bad Request");
        close(client_fd);
        return NULL;
    }

    char logbuf[512];
    snprintf(logbuf, sizeof(logbuf), "REQ %s %s from client", method, uri);
    log_msg("PROXY", logbuf);

    /* ── Check cache (only GET) ── */
    char cache_path[MAX_PATH * 2];
    uri_to_filename(uri, cache_path, sizeof(cache_path));

    if (strcasecmp(method, "GET") == 0 && cache_is_valid(cache_path)) {
        FILE *cf = fopen(cache_path, "rb");
        if (cf) {
            char cbuf[BUFFER_SIZE];
            size_t nr;
            while ((nr = fread(cbuf, 1, sizeof(cbuf), cf)) > 0)
                send_all(client_fd, cbuf, nr);
            fclose(cf);
            log_msg("CACHE", "HIT — served from cache");
            close(client_fd);
            return NULL;
        }
    }

    /* ── Forward to backend ── */
    Backend *b = next_backend();
    if (!b) {
        send_error(client_fd, 503, "No backends available");
        close(client_fd);
        return NULL;
    }

    snprintf(logbuf, sizeof(logbuf), "Forwarding to %s:%d", b->host, b->port);
    log_msg("PROXY", logbuf);

    int back_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (back_fd < 0) {
        send_error(client_fd, 502, "Bad Gateway");
        close(client_fd);
        return NULL;
    }

    struct sockaddr_in baddr;
    memset(&baddr, 0, sizeof(baddr));
    baddr.sin_family = AF_INET;
    baddr.sin_port   = htons(b->port);
    inet_pton(AF_INET, b->host, &baddr.sin_addr);

    if (connect(back_fd, (struct sockaddr *)&baddr, sizeof(baddr)) < 0) {
        snprintf(logbuf, sizeof(logbuf), "Cannot connect to backend %s:%d", b->host, b->port);
        log_msg("ERROR", logbuf);
        send_error(client_fd, 502, "Bad Gateway");
        close(back_fd);
        close(client_fd);
        return NULL;
    }

    /* Read POST body if needed */
    char *cl_ptr = strcasestr(req_buf, "Content-Length:");
    int body_len = 0;
    if (cl_ptr) body_len = atoi(cl_ptr + 15);

    char *hdr_end = strstr(req_buf, "\r\n\r\n");
    int already_body = 0;
    if (hdr_end) already_body = (req_buf + req_total) - (hdr_end + 4);

    /* Send headers already read */
    send_all(back_fd, req_buf, req_total);

    /* Forward remaining body bytes from client → backend */
    int remaining = body_len - already_body;
    while (remaining > 0) {
        char tmp[BUFFER_SIZE];
        int to_read = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;
        ssize_t n = read(client_fd, tmp, to_read);
        if (n <= 0) break;
        send_all(back_fd, tmp, n);
        remaining -= n;
    }

    /* ── Read response from backend ── */
    size_t resp_cap  = BUFFER_SIZE * 4;
    size_t resp_size = 0;
    char  *resp_buf  = malloc(resp_cap);
    if (!resp_buf) {
        send_error(client_fd, 500, "Internal Server Error");
        close(back_fd); close(client_fd);
        return NULL;
    }

    char tmp[BUFFER_SIZE];
    ssize_t n;
    while ((n = read(back_fd, tmp, sizeof(tmp))) > 0) {
        if (resp_size + n > resp_cap) {
            resp_cap *= 2;
            char *nb = realloc(resp_buf, resp_cap);
            if (!nb) break;
            resp_buf = nb;
        }
        memcpy(resp_buf + resp_size, tmp, n);
        resp_size += n;
        send_all(client_fd, tmp, n);
    }
    close(back_fd);

    /* ── Store in cache (GET only, if we got a response) ── */
    if (strcasecmp(method, "GET") == 0 && resp_size > 0) {
        if (strncmp(resp_buf, "HTTP/1.1 200", 12) == 0 ||
            strncmp(resp_buf, "HTTP/1.0 200", 12) == 0) {
            cache_write(cache_path, resp_buf, resp_size);
            log_msg("CACHE", "MISS — response cached");
        }
    }

    free(resp_buf);
    close(client_fd);
    return NULL;
}
