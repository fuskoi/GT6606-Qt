#define _POSIX_C_SOURCE 200809L  // 添加在所有 #include 之前
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
/* 环缓冲是否已回绕过（启动时扫描一次，写日志时更新） */
static int g_log_wrapped = 0;

typedef struct {
    size_t head;      // 当前写入位置
    size_t size;      // 缓冲区大小
} LogHeader;

static LogHeader *log_header = NULL;

void set_log_level(LogLevel level) {
    pthread_mutex_lock(&log_mutex);
    log_filter = level;
    pthread_mutex_unlock(&log_mutex);
}

void log_set_flash_quiet(int quiet)
{
    g_log_flash_quiet = quiet ? 1 : 0;
}

void log_init() {
    log_fd = open(LOG_FILE, O_RDWR | O_CREAT, 0644);
    if (log_fd == -1) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    off_t file_size = sizeof(LogHeader) + MAX_LOG_SIZE;
    if (ftruncate(log_fd, file_size) == -1) {
        perror("Failed to set log file size");
        close(log_fd);
        exit(EXIT_FAILURE);
    }

    log_buffer = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, log_fd, 0);
    if (log_buffer == MAP_FAILED) {
        perror("Failed to mmap log file");
        close(log_fd);
        exit(EXIT_FAILURE);
    }

    log_header = (LogHeader *)log_buffer;
    if (log_header->size == 0) {
        log_header->head = 0;
        log_header->size = MAX_LOG_SIZE;
        g_log_wrapped = 0;
    } else {
        if (log_header->size != MAX_LOG_SIZE) {
            if (log_header->head >= MAX_LOG_SIZE) {
                log_header->head %= MAX_LOG_SIZE;
            }
            log_header->size = MAX_LOG_SIZE;
        }
        /* 已有文件：head 之后若有非零文本，视为曾回绕 */
        const char *buf = log_buffer + sizeof(LogHeader);
        size_t i;
        g_log_wrapped = 0;
        for (i = log_header->head; i < MAX_LOG_SIZE; i++) {
            if (buf[i] != '\0') {
                g_log_wrapped = 1;
                break;
            }
        }
    }
}

void log_close() {
    if (log_buffer != MAP_FAILED) {
        munmap(log_buffer, sizeof(LogHeader) + MAX_LOG_SIZE);
        log_buffer = MAP_FAILED;
    }
    if (log_fd != -1) {
        close(log_fd);
        log_fd = -1;
    }
}

/* 从完整路径中提取文件名 */
static const char* basename_only(const char* path) {
    if (path == NULL) return "unknown";
    const char* p = strrchr(path, '/');
    if (p == NULL) {
        p = strrchr(path, '\\');
    }
    return (p != NULL) ? (p + 1) : path;
}

void log_write(LogLevel level, const char* file, int line, const char* fmt, ...) {
    if (level < log_filter) return;

    pthread_mutex_lock(&log_mutex);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *tm = localtime(&ts.tv_sec);

    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);

    char header[LOG_BUF_SIZE];
    snprintf(header, sizeof(header), "[%s.%03ld] [%s:%d] [%s] ",
             time_buf, ts.tv_nsec / 1000000,
             basename_only(file), line, level_strings[level]);

    va_list args;
    va_start(args, fmt);
    char msg[LOG_BUF_SIZE];
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    char log_line[LOG_BUF_SIZE * 2];
    int total_len = snprintf(log_line, sizeof(log_line), "%s%s\n", header, msg);

    if (total_len >= sizeof(log_line)) {
        total_len = sizeof(log_line) - 1;
        log_line[total_len] = '\n';
    }

    /* OTA 期间不碰 log.lg（YAFFS mmap），只打控制台 */
    if (!g_log_flash_quiet && log_buffer != MAP_FAILED && log_header != NULL) {
        char *buffer_start = log_buffer + sizeof(LogHeader);
        size_t remaining = MAX_LOG_SIZE - log_header->head;
        size_t old_head = log_header->head;
        if (total_len <= (int)remaining) {
            memcpy(buffer_start + log_header->head, log_line, (size_t)total_len);
        } else {
            memcpy(buffer_start + log_header->head, log_line, remaining);
            memcpy(buffer_start, log_line + remaining, (size_t)total_len - remaining);
        }
        log_header->head = (old_head + (size_t)total_len) % MAX_LOG_SIZE;
        if (old_head + (size_t)total_len >= MAX_LOG_SIZE) {
            g_log_wrapped = 1;
        }
    }

    printf("%s", log_line);

    pthread_mutex_unlock(&log_mutex);
}

void log_write_array(LogLevel level, const char* file, int line, 
                     const void* arr, size_t len, size_t elem_size, 
                     const char* elem_fmt) {
    if (level < log_filter) return;

    char buffer[8192]; // 可根据需要调整大小
    char *ptr = buffer;
    int remaining = sizeof(buffer);

    int used = snprintf(ptr, remaining, "[");
    ptr += used;
    remaining -= used;
    size_t i = 0;
    for (i = 0; i < len; ++i) {
        const char* elem = (const char*)arr + i * elem_size;

        if (strchr(elem_fmt, 'f')) {
            used = snprintf(ptr, remaining, elem_fmt, *(double*)elem);
        } 
        else if (strchr(elem_fmt, 'd')) {
            used = snprintf(ptr, remaining, elem_fmt, *(int*)elem);
        } 
        else if (strchr(elem_fmt, 'x')) {
            used = snprintf(ptr, remaining, elem_fmt, *(uint8_t*)elem);
        } 
        else if (strchr(elem_fmt, 's')) {
            // 处理字符串数组
            used = snprintf(ptr, remaining, elem_fmt, *(char**)elem);
        } 
        else if (strchr(elem_fmt, 'c')) {
            // 处理字符数组
            used = snprintf(ptr, remaining, elem_fmt, *(char*)elem);
        }
        else {
            used = snprintf(ptr, remaining, elem_fmt, *(void**)elem);
        }

        if (used >= remaining) break;

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
    } 
    else {
        buffer[sizeof(buffer)-3] = '.';
        buffer[sizeof(buffer)-2] = '.';
        buffer[sizeof(buffer)-1] = '\0';
    }

    log_write(level, file, line, "%s", buffer);
}

size_t log_get_valid_size(void)
{
    size_t n = 0;

    pthread_mutex_lock(&log_mutex);
    if (log_buffer != MAP_FAILED && log_header != NULL) {
        n = g_log_wrapped ? MAX_LOG_SIZE : log_header->head;
    }
    pthread_mutex_unlock(&log_mutex);
    return n;
}

size_t log_copy_linear(size_t offset, void *dst, size_t len)
{
    char *buf;
    size_t valid;
    size_t start;
    size_t ncopy;
    size_t first;
    uint8_t *out = (uint8_t *)dst;

    if (dst == NULL || len == 0) {
        return 0;
    }

    pthread_mutex_lock(&log_mutex);
    if (log_buffer == MAP_FAILED || log_header == NULL) {
        pthread_mutex_unlock(&log_mutex);
        return 0;
    }

    valid = g_log_wrapped ? MAX_LOG_SIZE : log_header->head;
    if (offset >= valid) {
        pthread_mutex_unlock(&log_mutex);
        return 0;
    }
    ncopy = len;
    if (offset + ncopy > valid) {
        ncopy = valid - offset;
    }

    buf = log_buffer + sizeof(LogHeader);
    if (g_log_wrapped) {
        start = (log_header->head + offset) % MAX_LOG_SIZE;
        first = MAX_LOG_SIZE - start;
        if (first >= ncopy) {
            memcpy(out, buf + start, ncopy);
        } else {
            memcpy(out, buf + start, first);
            memcpy(out + first, buf, ncopy - first);
        }
    } else {
        memcpy(out, buf + offset, ncopy);
    }
    pthread_mutex_unlock(&log_mutex);
    return ncopy;
}

static int log_line_match_level(const char *line, size_t line_len, uint8_t levelSel)
{
    char tmp[512];
    size_t n = line_len;
    int is_err, is_warn, is_dbg;

    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    memcpy(tmp, line, n);
    tmp[n] = '\0';

    is_err = (strstr(tmp, "[ERROR]") != NULL);
    is_warn = (strstr(tmp, "[WARNING]") != NULL);
    is_dbg = (strstr(tmp, "[DEBUG]") != NULL);

    switch (levelSel) {
    case 1:
        return 1;
    case 2:
        return (is_warn || is_err) ? 1 : 0;
    case 3:
        return is_err ? 1 : 0;
    case 4:
        return is_dbg ? 1 : 0;
    default:
        return 1;
    }
}

int log_export_filtered(uint8_t levelSel, uint8_t **out, size_t *out_len)
{
    size_t valid_len;
    size_t export_offset;
    size_t raw_len;
    uint8_t *raw = NULL;
    uint8_t *filt = NULL;
    size_t filt_len = 0;
    size_t pos = 0;

    if (out == NULL || out_len == NULL) {
        return -1;
    }
    *out = NULL;
    *out_len = 0;

    valid_len = log_get_valid_size();
    if (valid_len == 0) {
        return 0;
    }

    /* 只取时间线上最新的 LOG_UPLOAD_MAX_SIZE 字节 */
    if (valid_len > LOG_UPLOAD_MAX_SIZE) {
        export_offset = valid_len - LOG_UPLOAD_MAX_SIZE;
        raw_len = LOG_UPLOAD_MAX_SIZE;
    } else {
        export_offset = 0;
        raw_len = valid_len;
    }

    raw = (uint8_t *)malloc(raw_len);
    if (raw == NULL) {
        return -1;
    }
    if (log_copy_linear(export_offset, raw, raw_len) != raw_len) {
        free(raw);
        return -1;
    }

    if (levelSel <= 1) {
        *out = raw;
        *out_len = raw_len;
        return 0;
    }

    filt = (uint8_t *)malloc(raw_len);
    if (filt == NULL) {
        free(raw);
        return -1;
    }

    while (pos < raw_len) {
        size_t line_start = pos;
        size_t line_len;

        while (pos < raw_len && raw[pos] != '\n') {
            pos++;
        }
        if (pos < raw_len) {
            pos++; /* include newline */
        }
        line_len = pos - line_start;
        if (line_len == 0) {
            continue;
        }
        if (log_line_match_level((const char *)(raw + line_start), line_len, levelSel)) {
            memcpy(filt + filt_len, raw + line_start, line_len);
            filt_len += line_len;
        }
    }

    free(raw);
    if (filt_len == 0) {
        free(filt);
        return 0;
    }
    *out = filt;
    *out_len = filt_len;
    return 0;
}