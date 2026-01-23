#include "../inc/log.h"
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LOG_DIR "LOG"
static FILE *log_fp = NULL;
static int use_file = 0;

void log_init(int to_file) {
    use_file = to_file;
    if (!use_file) return;

    // 1. 确保 LOG 目录存在
    struct stat st = {0};
    if (stat(LOG_DIR, &st) == -1) {
        if (mkdir(LOG_DIR, 0755) != 0) {
            perror("Failed to create LOG directory");
            use_file = 0; // 回退到终端输出
            return;
        }
    }

    // 2. 生成文件名: LOG/slave_YYYYMMDD_HHMMSS.log
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char filename[64];
    sprintf(filename, "%s/slave_%04d%02d%02d_%02d%02d%02d.log", 
            LOG_DIR,
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);

    // 3. 打开文件
    log_fp = fopen(filename, "a");
    if (log_fp == NULL) {
        perror("Failed to open log file");
        use_file = 0;
    } else {
        printf("Log initialized. Output to: %s\n", filename);
    }
}

void log_close(void) {
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
}

void log_write(LogLevel level, const char *fmt, ...) {
    // 获取当前时间
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", t);

    // 转换日志级别字符串
    const char *level_str;
    const char *color_start = "";
    const char *color_end = "\033[0m";

    switch (level) {
        case LOG_LEVEL_INFO:  
            level_str = "INFO"; 
            color_start = "\033[32m"; // Green
            break;
        case LOG_LEVEL_DEBUG: 
            level_str = "DEBG"; 
            color_start = "\033[34m"; // Blue
            break;
        case LOG_LEVEL_WARN:  
            level_str = "WARN"; 
            color_start = "\033[33m"; // Yellow
            break;
        case LOG_LEVEL_ERROR: 
            level_str = "EROR"; 
            color_start = "\033[31m"; // Red
            break;
        default:              
            level_str = "UNKNOWN"; 
            break;
    }

    // 准备输出流
    FILE *out = (use_file && log_fp) ? log_fp : stdout;
    if (level == LOG_LEVEL_ERROR && !use_file) {
        out = stderr;
    }

    // 组装日志信息
    // 格式: [时间] [级别] 内容
    // 文件输出时不带颜色，终端输出带颜色
    
    va_list args;
    va_start(args, fmt);

    if (use_file && log_fp) {
        // 文件输出 (无颜色)
        fprintf(log_fp, "[%s] [%s] ", time_buf, level_str);
        vfprintf(log_fp, fmt, args);
        fprintf(log_fp, "\n");
        fflush(log_fp); // 确保立即写入磁盘
    } else {
        // 终端输出 (带颜色)
        fprintf(out, "%s[%s] [%s] ", color_start, time_buf, level_str);
        vfprintf(out, fmt, args);
        fprintf(out, "%s\n", color_end);
    }

    va_end(args);
}
