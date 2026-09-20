#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "timer_manager.h"

#define THREAD_POOL_MAX_THREADS 32
#define THREAD_POOL_QUEUE_SIZE 1024
#define MAX_TIMERS_PER_TASK 64

// 任务状态
typedef enum {
    TASK_PENDING,    // 等待执行
    TASK_RUNNING,    // 正在执行
    TASK_COMPLETED,  // 已完成
    TASK_CANCELLED   // 已取消
} TaskState;

// 任务取消回调函数
typedef void (*TaskCancelCallback)(int task_id, void* arg);

// 任务结构
typedef struct ThreadPoolTask {
    void (*function)(void*);
    void* arg;
    int id;
    TaskState state;
    pthread_mutex_t state_lock;
    TaskCancelCallback cancel_callback;
    void* cancel_arg;
    
    // 定时器管理
    int timer_ids[MAX_TIMERS_PER_TASK];
    int timer_count;
    pthread_mutex_t timer_lock;
    
    // 取消标志（用于任务内部检测）
    volatile bool cancelled;
} ThreadPoolTask;

// 线程池结构
typedef struct {
    pthread_t threads[THREAD_POOL_MAX_THREADS];
    ThreadPoolTask* queue[THREAD_POOL_QUEUE_SIZE];
    ThreadPoolTask* active_tasks[THREAD_POOL_QUEUE_SIZE];  // 活动任务列表
    int thread_count;
    int queue_size;
    int queue_head;
    int queue_tail;
    int active_count;  // 活动任务计数
    int shutdown;
    int next_task_id;
    pthread_mutex_t lock;
    pthread_cond_t notify;
    pthread_mutex_t active_lock;  // 活动任务列表锁
} ThreadPool;
extern ThreadPool  * g_threadpool;




ThreadPool* thread_pool_create(int thread_count);
int thread_pool_submit(ThreadPool* pool, void (*function)(void*), void* arg, 
                      TaskCancelCallback cancel_cb, void* cancel_arg);
bool thread_pool_cancel_task(ThreadPool* pool, int task_id);
void thread_pool_wait_task(ThreadPool* pool, int task_id);
void thread_pool_destroy(ThreadPool* pool);
void thread_pool_add_timer_to_task(ThreadPool* pool, int task_id, int timer_id);
void thread_pool_remove_timer_from_task(ThreadPool* pool, int task_id, int timer_id);
bool thread_pool_is_task_cancelled(ThreadPoolTask* task);

#endif