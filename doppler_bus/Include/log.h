#ifndef _LOG_H
#define _LOG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include "error.h"

#define LOG_FILE "log.lg"
#define MAX_LOG_SIZE (2 * 1024 * 1024) /* 本地环缓 2MB */
#define LOG_BUF_SIZE 8192

typedef enum {
    LOG_INFO = 0,
    LOG_DEBUG,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

extern const char* level_strings[];

void log_init(void);
void set_log_level(LogLevel level);
void log_close(void);
/* OTA 期间：跳过 log.lg mmap 写，只 printf，避免与升级抢 NAND */
void log_set_flash_quiet(int quiet);
/* 导出环形 log.lg 中最近 outMax 字节（时间正序），返回实际长度；失败返回 0 */
size_t log_export_recent(uint8_t* out, size_t outMax);
void log_write(LogLevel level, const char* file, int line, const char* fmt, ...);
#define LOG(level, fmt, ...) log_write(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

void log_write_array(LogLevel level, const char* file, int line,
                     const void* arr, size_t len, size_t elem_size,
                     const char* elem_fmt);

#define LOG_ARRAY(level, arr, len, fmt) \
    log_write_array(level, __FILE__, __LINE__, arr, len, sizeof(*arr), fmt)

#endif
