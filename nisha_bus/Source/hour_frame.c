#include "hour_frame.h"

void queue_init(hour_frame_FloatQueue *q) {
    q->head = q->tail = q->count = 0;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void queue_destroy(hour_frame_FloatQueue *q) {
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

bool enqueue(hour_frame_FloatQueue *q, float value) {
    pthread_mutex_lock(&q->lock);
    while (q->count >= QUEUE_SIZE) {
        pthread_cond_wait(&q->not_full, &q->lock);
    }
    
    q->data[q->tail] = value;
    q->tail = (q->tail + 1) % QUEUE_SIZE;
    q->count++;
    
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
    return true;
}

bool dequeue(hour_frame_FloatQueue *q, float *value) {
    pthread_mutex_lock(&q->lock);
    while (q->count <= 0) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    
    *value = q->data[q->head];
    q->head = (q->head + 1) % QUEUE_SIZE;
    q->count--;
    
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return true;
}

bool batch_dequeue(hour_frame_FloatQueue *q, float *buffer, int *count) {
    pthread_mutex_lock(&q->lock);
    while (q->count <= 0) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }
    
    *count = q->count;
    int index = q->head;
    for (int i = 0; i < *count; i++) {
        buffer[i] = q->data[index];
        index = (index + 1) % QUEUE_SIZE;
    }
    
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    memset(q->data, 0, QUEUE_SIZE);  //发送一次均匀报后，数据全清0
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return true;
}