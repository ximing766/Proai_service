#include "../inc/log.h"
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>

#define LOG_DIR "log"
#define MAX_LOG_SIZE (2 * 1024 * 1024) // 2MB
#define MAX_LOG_FILES 5

static FILE *log_fp = NULL;
static int use_file = 0;

static int cmpstringp(const void *p1, const void *p2) {
    return strcmp(* (char * const *) p1, * (char * const *) p2);
}

static void rotate_logs() {
    DIR *dir = opendir(LOG_DIR);
    if (!dir) return;

    struct dirent *ent;
    char *files[64];
    int count = 0;

    // 读取所有的 proai_*.log 文件
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "proai_", 6) == 0 && strstr(ent->d_name, ".log") != NULL) {
            if (count < 64) {
                files[count++] = strdup(ent->d_name);
            }
        }
    }
    closedir(dir);

    // 如果文件数量达到或超过限制，按文件名（即时间）排序并删除最旧的
    if (count >= MAX_LOG_FILES) {
        qsort(files, count, sizeof(char *), cmpstringp);
        for (int i = 0; i <= count - MAX_LOG_FILES; i++) {
            char path[128];
            snprintf(path, sizeof(path), "%s/%s", LOG_DIR, files[i]);
            remove(path);
        }
    }

    for (int i = 0; i < count; i++) {
        free(files[i]);
    }
}

static void open_new_log_file() {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char filename[64];
    sprintf(filename, "%s/proai_%04d%02d%02d_%02d%02d%02d.log", 
            LOG_DIR,
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);

    log_fp = fopen(filename, "a");
    if (log_fp == NULL) {
        perror("Failed to open log file");
        use_file = 0;
    } else {
        printf("Log initialized. Output to: %s\n", filename);
    }
}

static void check_log_rotation() {
    if (!log_fp) return;
    
    long current_size = ftell(log_fp);
    if (current_size >= MAX_LOG_SIZE) {
        fclose(log_fp);
        rotate_logs();
        open_new_log_file();
    }
}

void log_init(int to_file) {
    use_file = to_file;
    if (!use_file) return;

    struct stat st = {0};
    if (stat(LOG_DIR, &st) == -1) {
        if (mkdir(LOG_DIR, 0755) != 0) {
            perror("Failed to create LOG directory");
            use_file = 0; // 回退到终端输出
            return;
        }
    }

    rotate_logs(); // 启动时清理多余的历史日志文件
    open_new_log_file();
}

void log_close(void) {
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
}

void log_write(LogLevel level, const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", t);

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

    // 1. 终端输出 (始终开启，带颜色)
    FILE *term_out = (level == LOG_LEVEL_ERROR) ? stderr : stdout;
    va_list args_term;
    va_start(args_term, fmt);
    fprintf(term_out, "%s[%s] [%s] ", color_start, time_buf, level_str);
    vfprintf(term_out, fmt, args_term);
    fprintf(term_out, "%s\n", color_end);
    fflush(term_out); // 强制刷新缓冲区，解决 journalctl 延迟问题
    va_end(args_term);

    // 2. 文件输出 (如果开启，无颜色)
    if (use_file && log_fp) {
        va_list args_file;
        va_start(args_file, fmt);
        fprintf(log_fp, "[%s] [%s] ", time_buf, level_str);
        vfprintf(log_fp, fmt, args_file);
        fprintf(log_fp, "\n");
        fflush(log_fp);
        va_end(args_file);

        // 检查是否需要轮转
        check_log_rotation();
    }
}
