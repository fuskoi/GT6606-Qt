#ifndef _LOG_H
#define _LOG_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>  // uint8_t 支持
#include <sys/mman.h>


#include "error.h"

#define LOG_FILE "log.lg"
#define MAX_LOG_SIZE (2 * 1024 * 1024) // 2MB
#define LOG_BUF_SIZE 1024

typedef enum {
    LOG_INFO,
    LOG_WARNING,
    LOG_DEBUG,
    LOG_ERROR
} LogLevel;

extern const char* level_strings[];

void log_set_flash_quiet(int quiet);

void log_init();
void set_log_level(LogLevel level);
void log_close();
void log_write(LogLevel level, const char* file, int line, const char* fmt, ...);
#define LOG(level, fmt, ...) log_write(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

void log_write_array(LogLevel level, const char* file, int line, 
                     const void* arr, size_t len, size_t elem_size, 
                     const char* elem_fmt);

#define LOG_ARRAY(level, arr, len, fmt) \
    log_write_array(level, __FILE__, __LINE__, arr, len, sizeof(*arr), fmt)

#endif
