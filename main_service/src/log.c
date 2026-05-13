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
#include <sys/time.h>
#include <pthread.h>

#define LOG_DIR "log"
#define MAX_LOG_SIZE (2 * 1024 * 1024) // 2MB
#define MAX_LOG_FILES 5

static FILE *log_fp = NULL;
static int use_file = 0;
static LogLevel g_log_level = LOG_LEVEL_DEBUG;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_set_level(LogLevel level) {
    g_log_level = level;
}

LogLevel log_get_level(void) {
    return g_log_level;
}

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
    pthread_mutex_lock(&g_log_mutex);
    use_file = to_file;
    if (!use_file) {
        pthread_mutex_unlock(&g_log_mutex);
        return;
    }

    struct stat st = {0};
    if (stat(LOG_DIR, &st) == -1) {
        if (mkdir(LOG_DIR, 0755) != 0) {
            perror("Failed to create LOG directory");
            use_file = 0;
            pthread_mutex_unlock(&g_log_mutex);
            return;
        }
    }

    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }

    rotate_logs();
    open_new_log_file();
    pthread_mutex_unlock(&g_log_mutex);
}

void log_close(void) {
    pthread_mutex_lock(&g_log_mutex);
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void log_write(LogLevel level, const char *fmt, ...) {
    if (level < g_log_level) return;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *t = localtime(&tv.tv_sec);

    char time_buf[64];
    int len = strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", t);
    snprintf(time_buf + len, sizeof(time_buf) - len, ".%03ld", tv.tv_usec / 1000);

    const char *level_str;
    const char *color_start = "";
    const char *color_end = "\033[0m";

    switch (level) {
        case LOG_LEVEL_INFO:
            level_str = "INFO";
            color_start = "\033[32m";
            break;
        case LOG_LEVEL_DEBUG:
            level_str = "DEBG";
            color_start = "\033[34m";
            break;
        case LOG_LEVEL_WARN:
            level_str = "WARN";
            color_start = "\033[33m";
            break;
        case LOG_LEVEL_ERROR:
            level_str = "EROR";
            color_start = "\033[31m";
            break;
        default:
            level_str = "UNKNOWN";
            break;
    }

    FILE *term_out = (level == LOG_LEVEL_ERROR) ? stderr : stdout;
    va_list args_term;
    va_start(args_term, fmt);
    fprintf(term_out, "%s[%s] [%s] ", color_start, time_buf, level_str);
    vfprintf(term_out, fmt, args_term);
    fprintf(term_out, "%s\n", color_end);
    fflush(term_out);
    va_end(args_term);

    pthread_mutex_lock(&g_log_mutex);
    if (use_file && log_fp) {
        va_list args_file;
        va_start(args_file, fmt);
        fprintf(log_fp, "[%s] [%s] ", time_buf, level_str);
        vfprintf(log_fp, fmt, args_file);
        fprintf(log_fp, "\n");
        fflush(log_fp);
        va_end(args_file);

        check_log_rotation();
    }
    pthread_mutex_unlock(&g_log_mutex);
}
