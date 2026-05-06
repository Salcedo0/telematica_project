#include "mime.h"
#include <string.h>
#include <strings.h>

const char *get_mime(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) return "text/html";
    if (strcasecmp(ext, ".css")  == 0) return "text/css";
    if (strcasecmp(ext, ".js")   == 0) return "application/javascript";
    if (strcasecmp(ext, ".jpg")  == 0 || strcasecmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, ".png")  == 0) return "image/png";
    if (strcasecmp(ext, ".gif")  == 0) return "image/gif";
    if (strcasecmp(ext, ".ico")  == 0) return "image/x-icon";
    if (strcasecmp(ext, ".txt")  == 0) return "text/plain";
    if (strcasecmp(ext, ".pdf")  == 0) return "application/pdf";
    return "application/octet-stream";
}
