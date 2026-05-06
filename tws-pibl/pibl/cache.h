#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>

void uri_to_filename(const char *uri, char *out, size_t outlen);
int  cache_is_valid(const char *cache_path);
void cache_write(const char *cache_path, const char *data, size_t len);

#endif
