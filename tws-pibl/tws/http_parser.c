#include "http_parser.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int parse_request(int fd, HttpRequest *req) {
    req->raw_len = 0;
    req->raw[0] = '\0';

    while (req->raw_len < (int)sizeof(req->raw) - 1) {
        ssize_t n = read(fd, req->raw + req->raw_len,
                         sizeof(req->raw) - 1 - req->raw_len);
        if (n <= 0) break;
        req->raw_len += n;
        req->raw[req->raw_len] = '\0';
        if (strstr(req->raw, "\r\n\r\n")) break;
    }

    if (req->raw_len == 0) return 0;
    req->raw[req->raw_len] = '\0';

    if (sscanf(req->raw, "%15s %1023s %15s",
               req->method, req->uri, req->version) != 3) {
        return -1;
    }
    return 1;
}
