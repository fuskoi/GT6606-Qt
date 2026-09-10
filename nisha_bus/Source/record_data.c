#include "record_data.h"

static pthread_mutex_t recmutex = PTHREAD_MUTEX_INITIALIZER;
static int record_fd = -1;
static char *record_buffer = MAP_FAILED;

// 文件头结构（存储元数据）
typedef struct {
    size_t head;      // 当前写入位置
    size_t size;      // 缓冲区大小
} FileHeader;

static FileHeader *Record_header = NULL;


void Record_file_init() {
    record_fd = open(FILENAME, O_RDWR | O_CREAT, 0644);
    if (record_fd == -1) {
        perror("Failed to open recent.data file");
        exit(EXIT_FAILURE);
    }

    off_t file_size = sizeof(FileHeader) + MAX_RECORDS;
    if (ftruncate(record_fd, file_size) == -1) {
        perror("Failed to set recent.data file size");
        close(record_fd);
        exit(EXIT_FAILURE);
    }

    record_buffer = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, record_fd, 0);
    if (record_buffer == MAP_FAILED) {
        perror("Failed to mmap recent.data file");
        close(record_fd);
        exit(EXIT_FAILURE);
    }

    Record_header = (FileHeader *)record_buffer;
    if (Record_header->size == 0) {
        Record_header->head = 0;
        Record_header->size = MAX_RECORDS;
    }
}


void Record_file_close() {
    if (record_buffer != MAP_FAILED) {
        munmap(record_buffer, sizeof(FileHeader) + MAX_RECORDS);
        record_buffer = MAP_FAILED;
    }
    if (record_fd != -1) {
        close(record_fd);
        record_fd = -1;
    }
}




void Record_data_write(const char* fmt, ...) {
   
    pthread_mutex_lock(&recmutex);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *tm = localtime(&ts.tv_sec);

    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);

    char header[LOG_BUF_SIZE];
    snprintf(header, sizeof(header), "[%s.%03ld]",
             time_buf, ts.tv_nsec / 1000000);

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

    char *buffer_start = record_buffer + sizeof(Record_header);

    size_t remaining = MAX_RECORDS - Record_header->head;
    if (total_len <= remaining) {
        memcpy(buffer_start + Record_header->head, log_line, total_len);
    } else {
        memcpy(buffer_start + Record_header->head, log_line, remaining);
        memcpy(buffer_start, log_line + remaining, total_len - remaining);
    }

    Record_header->head = (Record_header->head + total_len) % MAX_RECORDS;

    printf("%s", log_line);

    pthread_mutex_unlock(&recmutex);
}



void Record_write_array(const void* arr, size_t len, size_t elem_size, const char* elem_fmt) 
{

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

    Record_data_write("%s", buffer);
}