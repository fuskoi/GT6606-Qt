#include "threadpool.h"
#include "timer_manager.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include "log.h"

ThreadPool *  g_threadpool;
// 工作线程函数
static void* thread_pool_worker(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    while (1) {
        pthread_mutex_lock(&pool->lock);
        
        // 等待队列中有任务或关闭线程池
        while (pool->queue_size == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->notify, &pool->lock);
        }
        
        // 检查是否关闭线程池
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->lock);
            pthread_exit(NULL);
        }
        
        // 获取任务
        ThreadPoolTask* task = pool->queue[pool->queue_head];
        pool->queue[pool->queue_head] = NULL;
        pool->queue_head = (pool->queue_head + 1) % THREAD_POOL_QUEUE_SIZE;
        pool->queue_size--;
        
        pthread_mutex_unlock(&pool->lock);
        
        // 添加到活动任务列表
        pthread_mutex_lock(&pool->active_lock);
        int i = 0;
        for (i = 0; i < THREAD_POOL_QUEUE_SIZE; i++) {
            if (pool->active_tasks[i] == NULL) {
                pool->active_tasks[i] = task;
                pool->active_count++;
                break;
            }
        }
        pthread_mutex_unlock(&pool->active_lock);
        
        // 更新任务状态
        pthread_mutex_lock(&task->state_lock);
        if (task->state == TASK_PENDING) {
            task->state = TASK_RUNNING;
        }
        pthread_mutex_unlock(&task->state_lock);
        
        // 检查任务是否已被取消
        pthread_mutex_lock(&task->state_lock);
        if (task->state == TASK_CANCELLED) {
            pthread_mutex_unlock(&task->state_lock);
            
            // 从活动任务列表中移除
            pthread_mutex_lock(&pool->active_lock);
            int i = 0;
            for (i = 0; i < THREAD_POOL_QUEUE_SIZE; i++) {
                if (pool->active_tasks[i] == task) {
                    pool->active_tasks[i] = NULL;
                    pool->active_count--;
                    break;
                }
            }
            pthread_mutex_unlock(&pool->active_lock);
            
            continue;
        }
        pthread_mutex_unlock(&task->state_lock);
        
        // 执行任务
        task->function(task->arg);
        
        // 再次检查状态（可能在执行过程中被取消）
        pthread_mutex_lock(&task->state_lock);
        if (task->state == TASK_RUNNING) {
            task->state = TASK_COMPLETED;
        }
        pthread_mutex_unlock(&task->state_lock);
        
        // 从活动任务列表中移除
        pthread_mutex_lock(&pool->active_lock);
        for (i = 0; i < THREAD_POOL_QUEUE_SIZE; i++) {
            if (pool->active_tasks[i] == task) {
                pool->active_tasks[i] = NULL;
                pool->active_count--;
                break;
            }
        }
        pthread_mutex_unlock(&pool->active_lock);
    }
    return NULL;
}

// 创建线程池
ThreadPool* thread_pool_create(int thread_count) {
    if (thread_count <= 0 || thread_count > THREAD_POOL_MAX_THREADS) {
        return NULL;
    }
    
    ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
    if (!pool) {
        return NULL;
    }
    
    memset(pool, 0, sizeof(ThreadPool));
    pool->thread_count = thread_count;
    pool->next_task_id = 1;
    
    // 初始化互斥锁和条件变量
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->notify, NULL);
    pthread_mutex_init(&pool->active_lock, NULL);
    
    // 创建工作线程
    int i = 0;
    for (i = 0; i < pool->thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, thread_pool_worker, pool) != 0) {
            thread_pool_destroy(pool);
            return NULL;
        }
    }
    return pool;
}

// 提交任务
int thread_pool_submit(ThreadPool* pool, void (*function)(void*), void* arg, 
                      TaskCancelCallback cancel_cb, void* cancel_arg) {
    if (!pool || !function) {
        return -1;
    }
    
    pthread_mutex_lock(&pool->lock);
    
    // 检查队列是否已满
    if (pool->queue_size == THREAD_POOL_QUEUE_SIZE) {
        pthread_mutex_unlock(&pool->lock);
        return -1;
    }
    
    // 创建任务
    ThreadPoolTask* task = (ThreadPoolTask*)malloc(sizeof(ThreadPoolTask));
    if (!task) {
        pthread_mutex_unlock(&pool->lock);
        return -1;
    }
    
    // 初始化任务
    memset(task, 0, sizeof(ThreadPoolTask));
    task->function = function;
    task->arg = arg;
    task->id = pool->next_task_id++;
    task->state = TASK_PENDING;
    task->cancel_callback = cancel_cb;
    task->cancel_arg = cancel_arg;
    task->cancelled = false;
    task->timer_count = 0;
    pthread_mutex_init(&task->state_lock, NULL);
    pthread_mutex_init(&task->timer_lock, NULL);
    
    // 添加到任务队列
    pool->queue[pool->queue_tail] = task;
    pool->queue_tail = (pool->queue_tail + 1) % THREAD_POOL_QUEUE_SIZE;
    pool->queue_size++;
    
    // 通知工作线程
    pthread_cond_signal(&pool->notify);
    pthread_mutex_unlock(&pool->lock);
    
    return task->id;
}

// 添加定时器到任务
void thread_pool_add_timer_to_task(ThreadPool* pool, int task_id, int timer_id) {
    // 在活动任务列表中查找任务
    pthread_mutex_lock(&pool->active_lock);
    int i = 0;
    for (i = 0; i < THREAD_POOL_QUEUE_SIZE; i++) {
        if (pool->active_tasks[i] && pool->active_tasks[i]->id == task_id) {
            ThreadPoolTask* task = pool->active_tasks[i];
            pthread_mutex_lock(&task->timer_lock);
            if (task->timer_count < MAX_TIMERS_PER_TASK) {
                task->timer_ids[task->timer_count++] = timer_id;
            }
            pthread_mutex_unlock(&task->timer_lock);
            break;
        }
    }
    pthread_mutex_unlock(&pool->active_lock);
}

// 检查任务是否被取消
bool thread_pool_is_task_cancelled(ThreadPoolTask* task) {
    if (!task) {
        return true;
    }
    
    pthread_mutex_lock(&task->state_lock);
    bool cancelled = (task->state == TASK_CANCELLED) || task->cancelled;
    pthread_mutex_unlock(&task->state_lock);
    
    return cancelled;
}

// 取消任务
bool thread_pool_cancel_task(ThreadPool* pool, int task_id) {
    ThreadPoolTask* task = NULL;
    
    // 在活动任务列表中查找
    pthread_mutex_lock(&pool->active_lock);
    int i = 0;
    for (i = 0; i < THREAD_POOL_QUEUE_SIZE; i++) {
        if (pool->active_tasks[i] && pool->active_tasks[i]->id == task_id) {
            task = pool->active_tasks[i];
            break;
        }
    }
    pthread_mutex_unlock(&pool->active_lock);
    
    if (!task) {
        LOG(LOG_ERROR, "Cancel failed: Task %d not found", task_id);
        return false;
    }
    
    pthread_mutex_lock(&task->state_lock);
    
    // 根据任务状态处理
    switch (task->state) {
        case TASK_PENDING:
            // 标记为已取消，任务不会被执行
            task->state = TASK_CANCELLED;
            task->cancelled = true;
            LOG(LOG_DEBUG, "Task %d cancelled (pending)", task_id);
            break;
            
        case TASK_RUNNING:
            // 标记任务为已取消
            task->cancelled = true;
            
            // 调用取消回调函数
            if (task->cancel_callback) {
                task->cancel_callback(task_id, task->cancel_arg);
            }
            
            // 取消任务关联的所有定时器
            pthread_mutex_lock(&task->timer_lock);
            int i = 0;
    for (i = 0; i < task->timer_count; i++) {
                remove_timer(task->timer_ids[i]);
            }
            task->timer_count = 0;
            pthread_mutex_unlock(&task->timer_lock);
            
            // 更新状态
            task->state = TASK_CANCELLED;
            LOG(LOG_DEBUG, "Task %d cancelled (running)", task_id);
            break;
            
        case TASK_COMPLETED:
        case TASK_CANCELLED:
            // 任务已完成或已取消，无法再次取消
            pthread_mutex_unlock(&task->state_lock);
            LOG(LOG_DEBUG, "Cancel failed: Task %d already completed/cancelled", task_id);
            return false;
    }
    
    pthread_mutex_unlock(&task->state_lock);
    return true;
}

// 等待任务完成
void thread_pool_wait_task(ThreadPool* pool, int task_id) {
    ThreadPoolTask* task = NULL;
    
    while (1) {
        // 在活动任务列表中查找
        pthread_mutex_lock(&pool->active_lock);
        int i = 0;
    for (i = 0; i < THREAD_POOL_QUEUE_SIZE; i++) {
            if (pool->active_tasks[i] && pool->active_tasks[i]->id == task_id) {
                task = pool->active_tasks[i];
                break;
            }
        }
        pthread_mutex_unlock(&pool->active_lock);
        
        if (!task) {
            // 任务不存在或已完成
            break;
        }
        
        // 检查任务状态
        pthread_mutex_lock(&task->state_lock);
        if (task->state == TASK_COMPLETED || task->state == TASK_CANCELLED) {
            pthread_mutex_unlock(&task->state_lock);
            break;
        }
        pthread_mutex_unlock(&task->state_lock);
        
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 }; // 10ms
        nanosleep(&ts, NULL);
    }
}

// 销毁线程池
void thread_pool_destroy(ThreadPool* pool) {
    if (!pool) return;
    
    // 设置关闭标志
    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->notify);
    pthread_mutex_unlock(&pool->lock);
    
    // 等待所有线程退出
    int i = 0;
    for (i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    // 清理任务资源
    pthread_mutex_lock(&pool->active_lock);
    for (i = 0; i < THREAD_POOL_QUEUE_SIZE; i++) {
        if (pool->active_tasks[i]) {
            ThreadPoolTask* task = pool->active_tasks[i];
            
            // 取消所有关联的定时器
            pthread_mutex_lock(&task->timer_lock);
            int j = 0;
            for (j = 0; j < task->timer_count; j++) {
                remove_timer(task->timer_ids[j]);
            }
            pthread_mutex_unlock(&task->timer_lock);
            
            // 销毁任务
            pthread_mutex_destroy(&task->state_lock);
            pthread_mutex_destroy(&task->timer_lock);
            free(task);
        }
    }
    pthread_mutex_unlock(&pool->active_lock);
    
    // 清理队列中的任务
    pthread_mutex_lock(&pool->lock);
    for (i = 0; i < THREAD_POOL_QUEUE_SIZE; i++) {
        if (pool->queue[i]) {
            ThreadPoolTask* task = pool->queue[i];
            pthread_mutex_destroy(&task->state_lock);
            pthread_mutex_destroy(&task->timer_lock);
            free(task);
        }
    }
    pthread_mutex_unlock(&pool->lock);
    
    // 销毁锁和条件变量
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->notify);
    pthread_mutex_destroy(&pool->active_lock);
    free(pool);
}