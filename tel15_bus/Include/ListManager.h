#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H

#include <pthread.h>
#include <stdint.h>
#include "Common.h"
#define CHECK_CANCEL(task_id) \
    if (is_task_canceled(task_id)) { \
        LOG(LOG_DEBUG, "Task %d was canceled at line %d", task_id, __LINE__); \
        g_bRunning_State = false; \
        return ERROR; \
    }

typedef enum {
    TASK_PENDING,
    TASK_RUNNING,
    TASK_CANCELLED,
    TASK_COMPLETED
} TaskState;

typedef struct ThreadPoolTask {
    void (*func)(void*);           // 任务函数指针，用于执行具体任务
    void* arg;                     // 任务函数的参数
    TaskState state;               // 任务的当前状态
    int cancel_req;                // 任务取消请求标志
    int task_id;                   // 任务的唯一标识符
    int* timer_ids;         // 关联的定时器ID列表
    size_t timer_count;     // 定时器数量
    struct ThreadPoolTask* next;   // 指向下一个任务的指针，用于链表结构
} ThreadPoolTask;

// 线程池结构体定义
typedef struct {
    pthread_t* threads; // 线程池中的线程数组
    int thread_count; // 线程池中线程的数量
    int queue_capacity; // 任务队列的容量

    pthread_mutex_t lock; // 用于保护线程池的互斥锁
    pthread_cond_t not_empty; // 当任务队列为空时，用于通知等待的线程
    pthread_cond_t not_full; // 当任务队列为满时，用于通知等待的线程

    int shutdown; // 线程池关闭标志

    ThreadPoolTask* queue_head; // 任务队列的头指针
    ThreadPoolTask* queue_tail; // 任务队列的尾指针
    int queue_size; // 当前任务队列中的任务数量

    ThreadPoolTask* active_tasks; // 当前活动的任务指针
    int next_task_id; // 下一个任务的ID
} ThreadPool;

typedef struct TaskContext {
    int task_id;
    volatile bool running; // 每个任务独享的运行状态标志
} TaskContext;

//extern ThreadPool g_thread_pool;// 全局线程池

int thread_pool_init(int thread_num, int queue_size);
void* worker(void* arg);
int thread_pool_submit(void (*func)(void*), void* arg);
int thread_pool_cancel(int task_id);
void thread_pool_destroy();
int is_task_canceled(int taskid);
#endif