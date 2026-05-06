#ifndef HANDLER_H
#define HANDLER_H

#include "http_parser.h"

extern char doc_root[MAX_PATH];

void handle_request(int fd);
void *client_thread(void *arg);

#endif
