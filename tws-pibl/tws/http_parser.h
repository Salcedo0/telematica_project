#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#define BUFFER_SIZE 8192
#define MAX_PATH    1024

typedef struct {
    char raw[BUFFER_SIZE * 2];
    int  raw_len;
    char method[16];
    char uri[MAX_PATH];
    char version[16];
} HttpRequest;

/* Returns 1 on success, 0 if connection closed before any data, -1 on parse error */
int parse_request(int fd, HttpRequest *req);

#endif
