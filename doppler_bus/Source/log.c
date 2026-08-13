#define _POSIX_C_SOURCE 200809L
#include "log.h"

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int log_fd = -1;
static char *log_buffer = MAP_FAILED;

const char* level_strings[] = {
    "INFO",
    "DEBUG",
    "WARNING",
    "ERROR"
};

static LogLevel log_filter = LOG_INFO;
static int g_log_flash_quiet = 0;

/* magic 识别新头；filled 持久化，重启后仍可导出有效日志 */
#define LOG_FILE_MAGIC 0x31474F4Cu /* 'LOG1' */
/* 旧头仅 head+size，升级时需把环缓后移 */
#define LOG_HEADER_OLD_SIZE (2U * sizeof(size_t))

typedef struct {
    size_t head;      /* 当前写入位置 */
    size_t size;      /* 缓冲区大小 */
    size_t filled;    /* 有效字节 0..size，持久化，供重启后导出 */
    uint32_t magic;
} LogHeader;

static LogHeader *log_header = NULL;
static size_t g_log_filled = 0;

void set_log_level(LogLevel level)
{
    pthread_mutex_lock(&log_mutex);
    log_filter = level;
    pthread_mutex_unlock(&log_mutex);
}

void log_set_flash_quiet(int quiet)
{
    g_log_flash_quiet = quiet ? 1 : 0;
}

void log_init(void)
{
    log_fd = open(LOG_FILE, O_RDWR | O_CREAT, 0644);
    if (log_fd == -1) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    {
        off_t file_size = (off_t)(sizeof(LogHeader) + MAX_LOG_SIZE);
        if (ftruncate(log_fd, file_size) == -1) {
            perror("Failed to set log file size");
            close(log_fd);
            exit(EXIT_FAILURE);
        }
    }

    log_buffer = mmap(NULL, sizeof(LogHeader) + MAX_LOG_SIZE,
                      PROT_READ | PROT_WRITE, MAP_SHARED, log_fd, 0);
    if (log_buffer == MAP_FAILED) {
        perror("Failed to mmap log file");
        close(log_fd);
        exit(EXIT_FAILURE);
    }

    log_header = (LogHeader *)log_buffer;
    if (log_header->magic == LOG_FILE_MAGIC) {
        /*
         * size 必须与当前 MAX_LOG_SIZE 一致。
         * 若曾用 3KB 试传，头里仍是 3KB 时，导出会被 capped 成“最新 3KB”。
         */
        if (log_header->size != MAX_LOG_SIZE) {
            if (log_header->size > MAX_LOG_SIZE) {
                /* 缩容：超出部分丢弃 */
                if (log_header->head >= MAX_LOG_SIZE) {
                    log_header->head = 0;
                    log_header->filled = 0;
                } else if (log_header->filled > MAX_LOG_SIZE) {
                    log_header->filled = MAX_LOG_SIZE;
                }
            }
            log_header->size = MAX_LOG_SIZE;
        }
        if (log_header->head >= log_header->size) {
            log_header->head = 0;
        }
        if (log_header->filled > log_header->size) {
            log_header->filled = log_header->head;
        }
        g_log_filled = log_header->filled;
    } else {
        size_t old_head = 0;
        size_t old_size = 0;

        memcpy(&old_head, log_buffer, sizeof(old_head));
        memcpy(&old_size, log_buffer + sizeof(old_head), sizeof(old_size));

        if (old_size == MAX_LOG_SIZE && old_head < MAX_LOG_SIZE) {
            /* 旧格式：保留已有内容；未记绕环时有效长取 head（head==0 当空） */
            if (sizeof(LogHeader) > LOG_HEADER_OLD_SIZE) {
                memmove(log_buffer + sizeof(LogHeader),
                        log_buffer + LOG_HEADER_OLD_SIZE,
                        MAX_LOG_SIZE);
            }
            log_header->head = old_head;
            log_header->size = MAX_LOG_SIZE;
            log_header->filled = old_head;
            log_header->magic = LOG_FILE_MAGIC;
            g_log_filled = old_head;
        } else {
            log_header->head = 0;
            log_header->size = MAX_LOG_SIZE;
            log_header->filled = 0;
            log_header->magic = LOG_FILE_MAGIC;
            g_log_filled = 0;
        }
    }
}

void log_close(void)
{
    if (log_buffer != MAP_FAILED) {
        munmap(log_buffer, sizeof(LogHeader) + MAX_LOG_SIZE);
        log_buffer = MAP_FAILED;
    }
    if (log_fd != -1) {
        close(log_fd);
        log_fd = -1;
    }
}

static const char* basename_only(const char* path)
{
    const char* p = NULL;

    if (path == NULL) {
        return "unknown";
    }
    p = strrchr(path, '/');
    if (p == NULL) {
        p = strrchr(path, '\\');
    }
    return (p != NULL) ? (p + 1) : path;
}

void log_write(LogLevel level, const char* file, int line, const char* fmt, ...)
{
    if (level < log_filter) {
        return;
    }

    pthread_mutex_lock(&log_mutex);

    {
        struct timespec ts;
        struct tm *tm = NULL;
        char time_buf[64];
        char header[LOG_BUF_SIZE];
        char msg[LOG_BUF_SIZE];
        char log_line[LOG_BUF_SIZE * 2];
        int total_len = 0;
        va_list args;

        clock_gettime(CLOCK_REALTIME, &ts);
        tm = localtime(&ts.tv_sec);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);

        snprintf(header, sizeof(header), "[%s.%03ld] [%s:%d] [%s] ",
                 time_buf, ts.tv_nsec / 1000000,
                 basename_only(file), line, level_strings[level]);

        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);

        total_len = snprintf(log_line, sizeof(log_line), "%s%s\n", header, msg);
        if (total_len >= (int)sizeof(log_line)) {
            total_len = (int)sizeof(log_line) - 1;
            log_line[total_len] = '\n';
        }

        /* OTA 期间不碰 log.lg（YAFFS mmap），只打控制台 */
        if (!g_log_flash_quiet && log_buffer != MAP_FAILED && log_header != NULL) {
            char *buffer_start = log_buffer + sizeof(LogHeader);
            size_t remaining = MAX_LOG_SIZE - log_header->head;
            if (total_len <= (int)remaining) {
                memcpy(buffer_start + log_header->head, log_line, (size_t)total_len);
            } else {
                memcpy(buffer_start + log_header->head, log_line, remaining);
                memcpy(buffer_start, log_line + remaining, (size_t)total_len - remaining);
            }
            log_header->head = (log_header->head + (size_t)total_len) % MAX_LOG_SIZE;
            if ((size_t)total_len > remaining) {
                g_log_filled = MAX_LOG_SIZE;
            } else if (g_log_filled < MAX_LOG_SIZE) {
                g_log_filled = log_header->head;
            }
            log_header->filled = g_log_filled;
        }

        printf("%s", log_line);
    }

    pthread_mutex_unlock(&log_mutex);
}

/*
 * 导出最近 outMax 字节日志（环缓，以 head 为最新端，按时间正序拷出）。
 */
size_t log_export_recent(uint8_t* out, size_t outMax)
{
    size_t head = 0;
    size_t size = 0;
    size_t filled = 0;
    size_t want = 0;
    size_t start = 0;
    size_t first = 0;
    const char* buffer_start = NULL;

    if (out == NULL || outMax == 0) {
        return 0;
    }

    pthread_mutex_lock(&log_mutex);
    if (log_buffer == MAP_FAILED || log_header == NULL) {
        pthread_mutex_unlock(&log_mutex);
        return 0;
    }

    head = log_header->head;
    size = log_header->size;
    filled = g_log_filled;
    if (size == 0 || size > MAX_LOG_SIZE) {
        size = MAX_LOG_SIZE;
    }
    if (filled > size) {
        filled = size;
    }
    if (filled == 0) {
        pthread_mutex_unlock(&log_mutex);
        return 0;
    }

    buffer_start = log_buffer + sizeof(LogHeader);
    want = (outMax < filled) ? outMax : filled;
    start = (head + size - want) % size;

    first = size - start;
    if (first >= want) {
        memcpy(out, buffer_start + start, want);
    } else {
        memcpy(out, buffer_start + start, first);
        memcpy(out + first, buffer_start, want - first);
    }

    pthread_mutex_unlock(&log_mutex);
    return want;
}

void log_write_array(LogLevel level, const char* file, int line,
                     const void* arr, size_t len, size_t elem_size,
                     const char* elem_fmt)
{
    if (level < log_filter) {
        return;
    }

    {
        char buffer[8192];
        char *ptr = buffer;
        int remaining = (int)sizeof(buffer);
        int used = 0;
        size_t i = 0;

        used = snprintf(ptr, (size_t)remaining, "[");
        ptr += used;
        remaining -= used;

        for (i = 0; i < len; ++i) {
            const char* elem = (const char*)arr + i * elem_size;

            if (strchr(elem_fmt, 'f')) {
                used = snprintf(ptr, (size_t)remaining, elem_fmt, *(double*)elem);
            } else if (strchr(elem_fmt, 'd')) {
                used = snprintf(ptr, (size_t)remaining, elem_fmt, *(int*)elem);
            } else if (strchr(elem_fmt, 'x')) {
                used = snprintf(ptr, (size_t)remaining, elem_fmt, *(uint8_t*)elem);
            } else if (strchr(elem_fmt, 's')) {
                used = snprintf(ptr, (size_t)remaining, elem_fmt, *(char**)elem);
            } else if (strchr(elem_fmt, 'c')) {
                used = snprintf(ptr, (size_t)remaining, elem_fmt, *(char*)elem);
            } else {
                used = snprintf(ptr, (size_t)remaining, elem_fmt, *(void**)elem);
            }

            if (used >= remaining) {
                break;
            }

            ptr += used;
            remaining -= used;

            if (i != len - 1 && remaining > 2) {
                *ptr++ = ' ';
                *ptr++ = ' ';
                remaining -= 2;
            }
        }

        if (remaining > 2) {
            *ptr++ = ']';
            *ptr = '\0';
        } else {
            buffer[sizeof(buffer) - 3] = '.';
            buffer[sizeof(buffer) - 2] = '.';
            buffer[sizeof(buffer) - 1] = '\0';
        }

        log_write(level, file, line, "%s", buffer);
    }
}
