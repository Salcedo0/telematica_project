#include "handler.h"
#include "logger.h"
#include "mime.h"
#include "net_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

char doc_root[MAX_PATH];

static void send_file_response(int fd, const char *filepath, const char *method, int code) {
    struct stat st;
    if (stat(filepath, &st) < 0 || !S_ISREG(st.st_mode)) {
        send_error(fd, 404, "Not Found");
        return;
    }
    const char *mime = get_mime(filepath);
    char header[512];
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        code, mime, (long)st.st_size);
    send_all(fd, header, strlen(header));

    if (strcasecmp(method, "HEAD") == 0) return;

    int f = open(filepath, O_RDONLY);
    if (f < 0) { send_error(fd, 404, "Not Found"); return; }
    char buf[BUFFER_SIZE];
    ssize_t n;
    while ((n = read(f, buf, sizeof(buf))) > 0)
        send_all(fd, buf, (size_t)n);
    close(f);
}

void handle_request(int fd) {
    HttpRequest req;
    int rc = parse_request(fd, &req);
    if (rc == 0) return;
    if (rc < 0) {
        send_error(fd, 400, "Bad Request");
        return;
    }

    char logbuf[512];
    snprintf(logbuf, sizeof(logbuf), "%s %s %s", req.method, req.uri, req.version);
    log_msg("REQ", logbuf);

    if (strstr(req.uri, "..")) {
        send_error(fd, 400, "Bad Request");
        log_msg("WARN", "Path traversal attempt blocked");
        return;
    }

    char filepath[MAX_PATH * 2];
    if (strcmp(req.uri, "/") == 0)
        snprintf(filepath, sizeof(filepath), "%s/index.html", doc_root);
    else
        snprintf(filepath, sizeof(filepath), "%s%s", doc_root, req.uri);

    if (strcasecmp(req.method, "GET") == 0 || strcasecmp(req.method, "HEAD") == 0) {
        send_file_response(fd, filepath, req.method, 200);
        snprintf(logbuf, sizeof(logbuf), "Served: %s", filepath);
        log_msg("RES", logbuf);

    } else if (strcasecmp(req.method, "POST") == 0) {
        char *cl_ptr = strcasestr(req.raw, "Content-Length:");
        int body_len = 0;
        if (cl_ptr) body_len = atoi(cl_ptr + 15);

        char *hdr_end = strstr(req.raw, "\r\n\r\n");
        int already = 0;
        char body_buf[BUFFER_SIZE] = {0};
        if (hdr_end) {
            hdr_end += 4;
            already = (req.raw + req.raw_len) - hdr_end;
            if (already > 0)
                memcpy(body_buf, hdr_end, already < BUFFER_SIZE - 1 ? already : BUFFER_SIZE - 1);
        }
        while (already < body_len && already < BUFFER_SIZE - 1) {
            ssize_t n = read(fd, body_buf + already, body_len - already);
            if (n <= 0) break;
            already += n;
        }

        const char *resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n\r\nOK";
        send_all(fd, resp, strlen(resp));
        log_msg("RES", "POST 200 OK");

    } else {
        send_error(fd, 400, "Bad Request");
        snprintf(logbuf, sizeof(logbuf), "Unknown method: %s", req.method);
        log_msg("WARN", logbuf);
    }
}

void *client_thread(void *arg) {
    int fd = *(int *)arg;
    free(arg);
    handle_request(fd);
    close(fd);
    return NULL;
}
