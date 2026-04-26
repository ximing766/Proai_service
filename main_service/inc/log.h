#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>

typedef enum {
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

// to_file: 1=输出到文件, 0=输出到终端
void log_init(int to_file);
void log_close(void);
void log_write(LogLevel level, const char *fmt, ...);

#define LOG_I(fmt, ...) log_write(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_D(fmt, ...) log_write(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) log_write(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) log_write(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

#endif // _LOG_H_
