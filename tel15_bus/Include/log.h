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
#include <stdint.h>  // uint8_t 支持
#include <sys/mman.h>


#include "error.h"

#define LOG_FILE "log.lg"
#define MAX_LOG_SIZE (2 * 1024 * 1024)       /* 本地环缓冲 2MB */
#define LOG_UPLOAD_MAX_SIZE (256 * 1024)     /* 平台上报最新 256KB */
#define LOG_BUF_SIZE 8192

typedef enum {
    LOG_INFO = 0, 
    LOG_DEBUG,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

extern const char* level_strings[];

void log_init();
void set_log_level(LogLevel level);
void log_close();
/* OTA 期间：跳过 log.lg mmap 写，只 printf，避免与升级抢 NAND */
void log_set_flash_quiet(int quiet);
void log_write(LogLevel level, const char* file, int line, const char* fmt, ...);
#define LOG(level, fmt, ...) log_write(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

void log_write_array(LogLevel level, const char* file, int line, 
                     const void* arr, size_t len, size_t elem_size, 
                     const char* elem_fmt);

#define LOG_ARRAY(level, arr, len, fmt) \
    log_write_array(level, __FILE__, __LINE__, arr, len, sizeof(*arr), fmt)

/* 环缓冲有效字节数（未回绕=head，已回绕=MAX_LOG_SIZE） */
size_t log_get_valid_size(void);
/* 按时间顺序从 offset 拷贝最多 len 字节到 dst，返回实际拷贝长度 */
size_t log_copy_linear(size_t offset, void *dst, size_t len);
/*
 * 导出按级别过滤后的线性日志（最多 LOG_UPLOAD_MAX_SIZE 最新内容）；*out 由调用方 free。
 * levelSel: 1全部 2警告(+错误) 3仅错误 4仅debug
 * 成功返回 0，失败返回 -1；空日志时 *out=NULL,*out_len=0 仍算成功。
 */
int log_export_filtered(uint8_t levelSel, uint8_t **out, size_t *out_len);

#endif
