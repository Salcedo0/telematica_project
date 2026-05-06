#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <stddef.h>

void send_all(int fd, const char *buf, size_t len);
void send_error(int fd, int code, const char *reason);

#endif
