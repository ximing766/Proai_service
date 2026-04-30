#ifndef _LOG_H_
#define _LOG_H_

#include <stdio.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_NONE
} LogLevel;

// 设置全局日志级别
void log_set_level(LogLevel level);
LogLevel log_get_level(void);

// to_file: 1=输出到文件, 0=输出到终端
void log_init(int to_file);
void log_close(void);
void log_write(LogLevel level, const char *fmt, ...);

#define LOG_D(fmt, ...) do { if (LOG_LEVEL_DEBUG >= log_get_level()) log_write(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__); } while(0)
#define LOG_I(fmt, ...) do { if (LOG_LEVEL_INFO  >= log_get_level()) log_write(LOG_LEVEL_INFO,  fmt, ##__VA_ARGS__); } while(0)
#define LOG_W(fmt, ...) do { if (LOG_LEVEL_WARN  >= log_get_level()) log_write(LOG_LEVEL_WARN,  fmt, ##__VA_ARGS__); } while(0)
#define LOG_E(fmt, ...) do { if (LOG_LEVEL_ERROR >= log_get_level()) log_write(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__); } while(0)

#endif // _LOG_H_
