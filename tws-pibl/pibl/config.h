#ifndef CONFIG_H
#define CONFIG_H

#define MAX_PATH 1024
#define MAX_LINE 256

extern int  listen_port;
extern int  cache_ttl;
extern char cache_dir[MAX_PATH];

void load_config(const char *path);

#endif
