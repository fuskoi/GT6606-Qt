#include "log.h"



static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int log_fd = -1;
static char *log_buffer = MAP_FAILED;

const char* level_strings[] = {
    "INFO",
    "WARNING",
    "DEBUG",
    "ERROR"
};

static LogLevel log_filter = LOG_INFO;
static int g_log_flash_quiet = 0;

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
    if (log_header->size == 0 || log_header->size > MAX_LOG_SIZE ||
        log_header->head >= MAX_LOG_SIZE) {
        log_header->head = 0;
        log_header->size = MAX_LOG_SIZE;
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
             file, line, level_strings[level]);

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

    if (!g_log_flash_quiet && log_buffer != MAP_FAILED && log_header != NULL) {
        char *buffer_start = log_buffer + sizeof(LogHeader);
        size_t head = log_header->head;

        if (head >= MAX_LOG_SIZE) {
            head = 0;
            log_header->head = 0;
        }

        size_t remaining = MAX_LOG_SIZE - head;
        if (total_len <= remaining) {
            memcpy(buffer_start + head, log_line, total_len);
        } else {
            memcpy(buffer_start + head, log_line, remaining);
            memcpy(buffer_start, log_line + remaining, total_len - remaining);
        }

        log_header->head = (head + (size_t)total_len) % MAX_LOG_SIZE;
    }

    printf("%s", log_line);
    fflush(stdout);

    pthread_mutex_unlock(&log_mutex);
}

void log_write_array(LogLevel level, const char* file, int line, 
                     const void* arr, size_t len, size_t elem_size, 
                     const char* elem_fmt) {
    if (level < log_filter) return;

    char buffer[2024]; // 可根据需要调整大小
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
        } else if (strchr(elem_fmt, 'd')) {
            used = snprintf(ptr, remaining, elem_fmt, *(int*)elem);
        } else if (strchr(elem_fmt, 'x')) {
            used = snprintf(ptr, remaining, elem_fmt, *(uint8_t*)elem);
        } else {
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
    } else {
        buffer[sizeof(buffer)-3] = '.';
        buffer[sizeof(buffer)-2] = '.';
        buffer[sizeof(buffer)-1] = '\0';
    }

    log_write(level, file, line, "%s", buffer);
}