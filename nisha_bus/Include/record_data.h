#ifndef _RECORD_DATA_H_
#define _RECORD_DATA_H_

#include "log.h"

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

#define FILENAME "recent.data"
#define RECORD_SIZE 50
#define MAX_RECORDS 25920  // 3个月数据量 (288*90)




void Record_file_init();

void Record_file_close();

void Record_data_write(const char *fmt, ...);

void Record_write_array(const void *arr, size_t len, size_t elem_size, const char *elem_fmt);

#define RECORD_ARRAY(arr, len, fmt) \
Record_write_array(arr, len, sizeof(*arr), fmt)


#define _RECORD_DATA_H_

#endif