#ifndef LOGGER_H
#define LOGGER_H

int  log_init(const char *path);
void log_msg(const char *level, const char *msg);
void log_close(void);

#endif
