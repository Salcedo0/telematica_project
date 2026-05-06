#include "net_utils.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

void send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n <= 0) break;
        sent += n;
    }
}

void send_error(int fd, int code, const char *reason) {
    char body[256], header[512];
    int blen = snprintf(body, sizeof(body),
        "<html><body><h1>%d %s</h1></body></html>", code, reason);
    snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n", code, reason, blen);
    send_all(fd, header, strlen(header));
    send_all(fd, body, blen);
}
