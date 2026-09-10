#ifndef __hour_frame__
#define __hour_frame__


#include <pthread.h>
#include <stdbool.h>

#define QUEUE_SIZE 12

//此队列用于均匀报中的12个数据上报
typedef struct {
    float data[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t lock;  // 互斥锁
    pthread_cond_t not_empty;  // 条件变量
    pthread_cond_t not_full;
} hour_frame_FloatQueue;

hour_frame_FloatQueue g_hour_float;

void queue_init(hour_frame_FloatQueue *q);
void queue_destroy(hour_frame_FloatQueue *q);
bool enqueue(hour_frame_FloatQueue *q, float value);
bool dequeue(hour_frame_FloatQueue *q, float *value);
bool batch_dequeue(hour_frame_FloatQueue *q, float *buffer, int *count);  //一次性全部出队


#endif