#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#define BUFFER_SIZE 8192
#define MAX_PATH 1024

static char doc_root[MAX_PATH];
static char log_file_path[MAX_PATH];
static FILE *log_fp = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ───── Logger ───── */
void log_msg(const char *level, const char *msg) {
    time_t now = time(NULL);
    char tbuf[64];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    pthread_mutex_lock(&log_mutex);
    fprintf(stdout, "[%s] [%s] %s\n", tbuf, level, msg);
    fflush(stdout);
    if (log_fp) {
        fprintf(log_fp, "[%s] [%s] %s\n", tbuf, level, msg);
        fflush(log_fp);
    }
    pthread_mutex_unlock(&log_mutex);
}

/* ───── MIME types ───── */
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

/* ───── Send helpers ───── */
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

void send_file_response(int fd, const char *filepath, const char *method, int code) {
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

/* ───── Request handler ───── */
void handle_request(int fd) {
    char raw[BUFFER_SIZE * 2];
    int total = 0;

    /* Read until \r\n\r\n or buffer full */
    while (total < (int)sizeof(raw) - 1) {
        ssize_t n = read(fd, raw + total, sizeof(raw) - 1 - total);
        if (n <= 0) break;
        total += n;
        raw[total] = '\0';
        if (strstr(raw, "\r\n\r\n")) break;
    }
    if (total == 0) return;
    raw[total] = '\0';

    char method[16], uri[MAX_PATH], version[16];
    if (sscanf(raw, "%15s %1023s %15s", method, uri, version) != 3) {
        send_error(fd, 400, "Bad Request");
        return;
    }

    /* Log request line */
    char logbuf[512];
    snprintf(logbuf, sizeof(logbuf), "%s %s %s", method, uri, version);
    log_msg("REQ", logbuf);

    /* Path traversal protection */
    if (strstr(uri, "..")) {
        send_error(fd, 400, "Bad Request");
        log_msg("WARN", "Path traversal attempt blocked");
        return;
    }

    /* Build file path */
    char filepath[MAX_PATH * 2];
    if (strcmp(uri, "/") == 0)
        snprintf(filepath, sizeof(filepath), "%s/index.html", doc_root);
    else
        snprintf(filepath, sizeof(filepath), "%s%s", doc_root, uri);

    if (strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0) {
        send_file_response(fd, filepath, method, 200);
        snprintf(logbuf, sizeof(logbuf), "Served: %s", filepath);
        log_msg("RES", logbuf);

    } else if (strcasecmp(method, "POST") == 0) {
        /* Parse Content-Length */
        char *cl_ptr = strcasestr(raw, "Content-Length:");
        int body_len = 0;
        if (cl_ptr) body_len = atoi(cl_ptr + 15);

        /* How many body bytes already in buffer */
        char *hdr_end = strstr(raw, "\r\n\r\n");
        int already = 0;
        char body_buf[BUFFER_SIZE] = {0};
        if (hdr_end) {
            hdr_end += 4;
            already = (raw + total) - hdr_end;
            if (already > 0)
                memcpy(body_buf, hdr_end, already < BUFFER_SIZE - 1 ? already : BUFFER_SIZE - 1);
        }
        /* Read remaining body */
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
        snprintf(logbuf, sizeof(logbuf), "Unknown method: %s", method);
        log_msg("WARN", logbuf);
    }
}

/* ───── Thread entry ───── */
void *client_thread(void *arg) {
    int fd = *(int *)arg;
    free(arg);
    handle_request(fd);
    close(fd);
    return NULL;
}

/* ───── Main ───── */
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <PORT> <LogFile> <DocumentRootFolder>\n", argv[0]);
        exit(1);
    }

    int port = atoi(argv[1]);
    strncpy(log_file_path, argv[2], MAX_PATH - 1);
    strncpy(doc_root, argv[3], MAX_PATH - 1);

    /* Remove trailing slash */
    int dl = strlen(doc_root);
    if (dl > 0 && doc_root[dl - 1] == '/') doc_root[dl - 1] = '\0';

    log_fp = fopen(log_file_path, "a");
    if (!log_fp) { perror("fopen log"); exit(1); }

    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(srv, 128) < 0) { perror("listen"); exit(1); }

    char msg[256];
    snprintf(msg, sizeof(msg), "TWS listening on port %d | root=%s | log=%s",
             port, doc_root, log_file_path);
    log_msg("INFO", msg);

    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int *fd = malloc(sizeof(int));
        if (!fd) continue;
        *fd = accept(srv, (struct sockaddr *)&cli, &cli_len);
        if (*fd < 0) { free(fd); continue; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        snprintf(msg, sizeof(msg), "New connection from %s", ip);
        log_msg("INFO", msg);

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, fd);
        pthread_detach(tid);
    }
    return 0;
}
