#include "cache.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void meta_filename(const char *cache_path, char *meta, size_t metalen) {
    snprintf(meta, metalen, "%s.meta", cache_path);
}

void uri_to_filename(const char *uri, char *out, size_t outlen) {
    snprintf(out, outlen, "%s", cache_dir);
    size_t base = strlen(out);
    out[base++] = '/';
    for (int i = 0; uri[i] && base < outlen - 1; i++) {
        char c = uri[i];
        if (c == '/' || c == '?' || c == '&' || c == '=')
            out[base++] = '_';
        else
            out[base++] = c;
    }
    out[base] = '\0';
    strncat(out, ".cache", outlen - strlen(out) - 1);
}

int cache_is_valid(const char *cache_path) {
    char meta[MAX_PATH * 2];
    meta_filename(cache_path, meta, sizeof(meta));

    FILE *mf = fopen(meta, "r");
    if (!mf) return 0;
    time_t expiry = 0;
    fscanf(mf, "%ld", &expiry);
    fclose(mf);

    if (time(NULL) > expiry) {
        remove(cache_path);
        remove(meta);
        return 0;
    }
    return 1;
}

void cache_write(const char *cache_path, const char *data, size_t len) {
    FILE *cf = fopen(cache_path, "wb");
    if (!cf) return;
    fwrite(data, 1, len, cf);
    fclose(cf);

    char meta[MAX_PATH * 2];
    meta_filename(cache_path, meta, sizeof(meta));
    FILE *mf = fopen(meta, "w");
    if (!mf) return;
    fprintf(mf, "%ld\n", (long)(time(NULL) + cache_ttl));
    fclose(mf);
}
