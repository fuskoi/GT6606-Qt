#include "task.h"
task_chain_t nisha_sys_check_task = {
    .chainStatus = TASKCHAIN_WAITING,
    .chainStop_step = -1,
    .code = 0,
    .step = 0,
    .stepLength = 0,
    .taskStatus = TASK_INIT,
    .taskItem = NULL,
    .cleanupCallback = ChainCleanupHandler,
    .taskchain_manager = NULL,
    .pResource = NULL,
};

task_chain_t nisha_sys_data_collect_task = {
    .chainStatus = TASKCHAIN_WAITING,
    .chainStop_step = -1,
    .code = 0,
    .step = 0,
    .stepLength = 0,
    .taskStatus = TASK_INIT,
    .taskItem = NULL,
    .cleanupCallback = ChainCleanupHandler,
    .taskchain_manager = NULL,
    .pResource = NULL,
};

task_chain_t nisha_sys_proc_motor_error_task = {
    .chainStatus = TASKCHAIN_WAITING,
    .chainStop_step = -1,
    .code = 0,
    .step = 0,
    .stepLength = 0,
    .taskStatus = TASK_INIT,
    .taskItem = NULL,
    .cleanupCallback = ChainCleanupHandler,
    .taskchain_manager = NULL,
    .pResource = NULL,
};

ThreadPool *initializeThreadPool(int max_threads, size_t max_queue_size)
{
    ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
    if (pool == NULL)
    {
        fprintf(stderr, "Unable to allocate memory for the thread pool\n");
        return NULL;
    }

    pool->max_threads = max_threads;
    pool->num_threads = 0;
    pool->shutdown = 0;
    pool->max_queue_size = max_queue_size;
    pool->queue_size = 0;
    pool->queue = NULL;

    // 初始化互斥锁和条件变量
    if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0)
    {
        fprintf(stderr, "Unable to initialize the mutex lock\n");
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->queue_cond, NULL) != 0)
    {
        fprintf(stderr, "Unable to initialize the condition variable\n");
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool);
        return NULL;
    }

    // 分配内存给线程ID数组
    pool->threads = (pthread_t *)malloc(max_threads * sizeof(pthread_t));
    if (pool->threads == NULL)
    {
        fprintf(stderr, "Unable to allocate memory for the thread ID array\n");
        pthread_mutex_destroy(&pool->queue_mutex);
        pthread_cond_destroy(&pool->queue_cond);
        free(pool);
        return NULL;
    }

    // 创建并启动线程
    for (int i = 0; i < max_threads; ++i)
    {
        if (pthread_create(&pool->threads[i], NULL, workerThread, (void *)pool) != 0)
        {
            fprintf(stderr, "Unable to create the thread\n");
            // 清理已创建的线程和资源
            for (int j = 0; j < i; ++j)
            {
                pthread_cancel(pool->threads[j]);
                pthread_join(pool->threads[j], NULL);
            }
            free(pool->threads);
            pthread_mutex_destroy(&pool->queue_mutex);
            pthread_cond_destroy(&pool->queue_cond);
            free(pool);
            return NULL;
        }
        pool->num_threads++;
    }

    // 初始化任务队列
    pool->queue = (pool_task_t *)malloc(max_queue_size * sizeof(pool_task_t));
    if (pool->queue == NULL)
    {
        fprintf(stderr, "Unable to allocate memory for the task queue\n");
        // 清理已创建的线程和资源
        for (int j = 0; j < max_threads; ++j)
        {
            pthread_cancel(pool->threads[j]);
            pthread_join(pool->threads[j], NULL);
        }
        free(pool->threads);
        pthread_mutex_destroy(&pool->queue_mutex);
        pthread_cond_destroy(&pool->queue_cond);
        free(pool);
        return NULL;
    }

    return pool;
}

void *workerThread(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    pool_task_t task;

    while (1)
    {
        pthread_mutex_lock(&pool->queue_mutex);

        // 检查线程池是否关闭
        if (pool->shutdown)
        {
            pthread_mutex_unlock(&pool->queue_mutex);
            pthread_exit(NULL);
        }

        // 如果任务队列为空，等待任务
        while (pool->queue_size == 0)
        {
            pthread_cond_wait(&pool->queue_cond, &pool->queue_mutex);
            // 检查线程池是否关闭
            if (pool->shutdown)
            {
                pthread_mutex_unlock(&pool->queue_mutex);
                pthread_exit(NULL);
            }
        }

        // 取出任务
        task = pool->queue[0];
        memmove(pool->queue, pool->queue + 1, (pool->queue_size - 1) * sizeof(pool_task_t));
        pool->queue_size--;

        pthread_mutex_unlock(&pool->queue_mutex);

        // 执行任务回调函数
        if (task.task_function != NULL)
        {
            task.task_function(task.task_argument); // 动态调用不同处理逻辑
        }
    }

    pthread_exit(NULL);
}

int submitTask(ThreadPool *pool, void *(*task_function)(void *), void *task_argument)
{
    pthread_mutex_lock(&pool->queue_mutex);

    // 检查线程池是否已关闭
    if (pool->shutdown)
    {
        pthread_mutex_unlock(&pool->queue_mutex);
        return -1;
    }

    // 检查任务队列是否已满
    if (pool->queue_size >= pool->max_queue_size)
    {
        pthread_mutex_unlock(&pool->queue_mutex);
        return -1;
    }

    // 将任务添加到队列末尾
    pool->queue[pool->queue_size].task_function = task_function;
    pool->queue[pool->queue_size].task_argument = task_argument;
    pool->queue_size++;

    // 通知等待的线程有新任务
    pthread_cond_signal(&pool->queue_cond);

    pthread_mutex_unlock(&pool->queue_mutex);

    return 0;
}

int SubmitTaskToGThreadPool(void *(*task_function)(void *), void *task_argument)
{
    return submitTask(g_threadPool, task_function, task_argument);
}

void shutdownThreadPool(ThreadPool *pool)
{
    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->queue_cond);
    pthread_mutex_unlock(&pool->queue_mutex);

    // 等待所有线程完成
    for (int i = 0; i < pool->num_threads; ++i)
    {
        pthread_join(pool->threads[i], NULL);
    }

    // 释放任务队列内存
    free(pool->queue);

    // 释放线程ID数组内存
    free(pool->threads);

    // 销毁互斥锁和条件变量
    pthread_mutex_destroy(&pool->queue_mutex);
    pthread_cond_destroy(&pool->queue_cond);

    // 释放线程池结构体内存
    free(pool);
}

void *CommonTaskChainManager(void *arg)
{
    task_chain_t *chain = (task_chain_t *)arg;
    // 初始化时间戳
    clock_gettime(CLOCK_MONOTONIC, &chain->lastCycleEnd);
    // chain->startTime = time(NULL);
    ts_printf("TaskChainManager: [%s] Start ...\n", GetTaskChainName(chain->code));
    LOG(LOG_INFO, "TaskChainManager: [%s] Start ...", GetTaskChainName(chain->code));
    do
    {
        // 重置运行时状态
        LOCK(chain);
        chain->step = 0;
        chain->chainStatus = TASKCHAIN_RUNNING;
        UNLOCK(chain);
#ifdef DEBUG
        ts_printf("Remain Task Chain Running Count %d\n", chain->loopCount);
#endif // DEBUG

        // 单次任务链执行
        while (chain->step < chain->stepLength)
        {
            ts_printf("chain->step %d\n", chain->step);
            // 检查停止请求
            LOCK(chain);
            bool stop = chain->stopRequested;
            if (stop)
            {
#ifdef DEBUG
                ts_printf("Task Chain [%s] Stop Requested\n", GetTaskChainName(chain->code));
#endif // DEBUG
       // chain->chainStop_step = chain->step - 1;
                UNLOCK(chain);
                break;
            }
            UNLOCK(chain);

            // 设置任务状态并加入队列
            if (!chain->taskItem)
            {
                LOG(LOG_ERROR, "Task Chain [%s] step [%d] taskItem is NULL",
                    GetTaskChainName(chain->code), chain->step);
                chain->stopRequested = true;
                chain->taskStatus = TASK_ENQUEUE_FAILED;
                break;
            }
            chain->taskItem[chain->step].sendTime = time(NULL);
            chain->taskStatus = TASK_WAITING;
            {
                int port = chain->taskItem[chain->step].taskPort;
                if (port < 0 || port > 5)
                {
                    ts_printf("taskPort:%d\n", port);
                    LOG(LOG_ERROR, "Task Chain [%s] step [%d] Invalid task port",
                        GetTaskChainName(chain->code), chain->step);
                    chain->stopRequested = true;
                    chain->taskStatus = TASK_ENQUEUE_FAILED;
                    break;
                }
                /* COM1 未配 ROLE / SERVER 口无任务队列，入队会空指针崩溃 */
                if (g_uartWorkInfoArry[port].role != UART_ACTIVE_CONTROL
                    || g_uartWorkInfoArry[port].taskQueue.mutex == NULL
                    || g_uartWorkInfoArry[port].taskQueue.pItemArray == NULL)
                {
                    LOG(LOG_ERROR,
                        "Task Chain [%s] step [%d] COM%d not ACTIVE/queue not ready",
                        GetTaskChainName(chain->code), chain->step, port + 1);
                    chain->taskStatus = TASK_TIMEOUT;
                    chain->chainStop_step = chain->step;
                    chain->step++;
                    continue;
                }
            }

            if (!EnTaskQueue_List(
                    &g_uartWorkInfoArry[chain->taskItem[chain->step].taskPort].taskQueue,
                    &chain->taskItem[chain->step]))
            {
                LOG(LOG_ERROR, "Task Chain [%s] step [%d] EnTaskQueue_List Failed",
                    GetTaskChainName(chain->code), chain->step);
                chain->taskStatus = TASK_TIMEOUT;
                chain->chainStop_step = chain->step;
                chain->step++;
                continue;
            }
            // PrintTaskQueue_List(
            //     &g_uartWorkInfoArry[chain->taskItem[chain->step].taskPort].taskQueue);
            // ts_printf(" taskport:%d, priority:%d, sendtime:%ld\n",
            //           chain->taskItem[chain->step].taskPort,
            //           chain->taskItem[chain->step].priority,
            //           chain->taskItem[chain->step].sendTime);

            // 等待任务完成或超时
            struct timespec start, now;
            uint64_t elapsed;
            clock_gettime(CLOCK_MONOTONIC, &start);
            do
            {
                usleep(10000); // 10ms检查间隔

            } while (chain->taskStatus == TASK_WAITING);

            elapsed = CalcElapsed(&start);
            // ts_printf("elapsed  = %llu, taskINVms = %hu, INVms = %hu\n", elapsed, chain->taskIntervalMs, chain->intervalMs);
            if (elapsed < chain->taskIntervalMs) // 没有到达指定的时间间隔，则等待指定的时间间隔
            {
                uint64_t remain = chain->taskIntervalMs - elapsed;
                // ts_printf("task waiting remain %llu ms\n", remain);
                usleep(remain * 1000);
            }

            // if (chain->chainStatus == TASKCHAIN_STOPPED)
            // {
            //     break;
            // }
            chain->chainStop_step = chain->step;
            chain->step++; // 指向下一个任务
        }

        /* 循环控制 */
        LOCK(chain);
        if (chain->execMode == CHAIN_MODE_FINITE && chain->loopCount > 0)
        {
            clock_gettime(CLOCK_MONOTONIC, &chain->lastCycleEnd);
            chain->loopCount--;
        }
        bool need_continue = (chain->execMode == CHAIN_MODE_INFINITE) ||
                             (chain->execMode == CHAIN_MODE_FINITE && chain->loopCount > 0);
        bool stop = chain->stopRequested;
        UNLOCK(chain);
        // ts_printf("loopcount = %hu\n", chain->loopCount);
        // 处理循环间隔
        if (!stop && need_continue)
        {
            // ts_printf("need_continue = %d\n", need_continue);
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);

            // 计算需要等待的时间
            uint64_t sec_diff = now.tv_sec - chain->lastCycleEnd.tv_sec;
            int64_t nsec_diff = now.tv_nsec - chain->lastCycleEnd.tv_nsec;

            if (nsec_diff < 0)
            {
                // 如果纳秒差是负数，则借位
                nsec_diff += 1000000000; // 加上1秒的纳秒数
                sec_diff -= 1;           // 减去1秒
            }

            uint64_t elapsed = sec_diff * 1000 + nsec_diff / 1000000; // 转换为毫秒
            if (elapsed < chain->intervalMs)                          // 等待指定的任务链执行时间间隔
            {
                uint64_t remain = chain->intervalMs - elapsed;
                // ts_printf("remian = %llu\n", remain);
                usleep(remain * 1000);
            }

            // 更新结束时间戳
            // clock_gettime(CLOCK_MONOTONIC, &chain->lastCycleEnd);
        }

    } while (!chain->stopRequested &&
             (chain->execMode == CHAIN_MODE_INFINITE ||
              (chain->execMode == CHAIN_MODE_FINITE &&
               chain->loopCount > 0)));

    // 后处理
    chain->chainStatus = chain->stopRequested ? TASKCHAIN_STOPPED : TASKCHAIN_COMPLETED;

    // 执行清理回调
    if (chain->cleanupCallback != NULL)
    {
        // ts_printf("Task Chain Cleanup Callback ...\n");
        LOG(LOG_INFO, "TaskChainManager: [%s] End ...", GetTaskChainName(chain->code));
        chain->cleanupCallback(chain);
    }
#ifdef DEBUG
    ts_printf("Task Chain [%s] Exit ...\n", GetTaskChainName(chain->code));
#endif // DEBUG
    return NULL;
}

// 初始化任务链和启动线程
bool SubmitTaskToThreadPool(ThreadPool *threadPool, task_chain_t *taskChain, void *(*task_function)(void *), void *task_argument)
{
    if (submitTask(threadPool, task_function, task_argument) != 0)
    {
        ts_printf("cant submit task!\n");
        return false;
    }
    else
    {
        ts_printf("submit task success!\n");
        return true;
    }
}

ThreadPool *g_threadPool = NULL;
bool StartTaskThread(void)
{
    g_threadPool = initializeThreadPool(MAX_THREADS, MAX_THREAD_POOL_SIZE);
    if (g_threadPool == NULL)
    {
        fprintf(stderr, "cant initialize thread pool\n");
        return false;
    }
    ts_printf("thread pool %X init success!\n", g_threadPool);
    return true;
}

bool isTaskChainRunning(task_chain_t *taskChain)
{
    if (!taskChain)
        return false;
    if (taskChain->chainStatus == TASKCHAIN_RUNNING || taskChain->chainStatus == TASKCHAIN_SUBMIT)
        return true;
    return false;
}

void changeTaskStatus(task_chain_t *taskChain, int status)
{
    if (!taskChain)
        return;
    LOCK(taskChain);
    taskChain->taskStatus = status;
    UNLOCK(taskChain);
}
// 单个任务完成时 更改任务状态为TASK_READY
void chain_task_complete(task_chain_name_t chain_code)
{
    task_chain_t *taskChain = GetTaskChain(chain_code);
    if (taskChain && isTaskChainRunning(taskChain))
        changeTaskStatus(taskChain, TASK_READY);
}
// 单个任务完成时 自定义任务状态(taskStatus = TASK_FINISHED 可以不用等待到下一步的间隔时间，快速执行下一步),
void chain_task_complete_choosestatus(task_chain_name_t chain_code, task_status_t taskStatus)
{
    task_chain_t *taskChain = GetTaskChain(chain_code);
    if (taskChain && isTaskChainRunning(taskChain))
        changeTaskStatus(taskChain, taskStatus);
}
// 单个任务完成时 更改任务状态 并切换到上一步
void chain_task_switch_previous_step(task_chain_name_t chain_code)
{
    task_chain_t *taskChain = GetTaskChain(chain_code);
    if (taskChain && isTaskChainRunning(taskChain))
    {
        changeTaskStatus(taskChain, TASK_READY);
        loadTaskChainPreviousStep(taskChain);
    }
}
void chain_task_timeout(task_chain_name_t chain_code)
{
    task_chain_t *taskChain = GetTaskChain(chain_code);
    if (taskChain && isTaskChainRunning(taskChain))
        changeTaskStatus(taskChain, TASK_TIMEOUT);
}

void chain_task_stop(task_chain_name_t chain_code)
{
    task_chain_t *taskChain = GetTaskChain(chain_code);
    if (taskChain && isTaskChainRunning(taskChain))
        stopTaskChain(taskChain);
}

void stopTaskChain(task_chain_t *taskChain)
{
    LOCK(taskChain);
    taskChain->stopRequested = true;
    UNLOCK(taskChain);
}

void loadTaskChainPreviousStep(task_chain_t *taskChain)
{

    LOCK(taskChain);
    taskChain->step--;
    UNLOCK(taskChain);
}
/// @brief 回到指定步骤
/// @param taskChain
/// @param step
void loadTaskChainStep(task_chain_t *taskChain, int step)
{
    LOCK(taskChain);
    taskChain->step = step - 1;
    UNLOCK(taskChain);
}

/// @brief 回到指定步骤
/// @param chain_code
/// @param step
void chain_task_goto_step(task_chain_name_t chain_code, int step)
{
    task_chain_t *taskChain = GetTaskChain(chain_code);
    if (isTaskChainRunning(taskChain))
    {
        loadTaskChainStep(taskChain, step);
    }
}

task_chain_t *GetTaskChain(task_chain_name_t chain_code)
{
    switch (chain_code)
    {
    case NISHA_SYS_CHECK_TASK:
        return &nisha_sys_check_task;
    case NISHA_SYS_DATA_COLLECT_TASK:
        return &nisha_sys_data_collect_task;
    case NISHA_SYS_PROC_MOTOR_ERROR_TASK:
        return &nisha_sys_proc_motor_error_task;
    default:
        return NULL;
    }
}
const char *GetTaskChainName(uint8_t chain)
{
    static const char *names[] = {
        [NISHA_SYS_CHECK_TASK] = "nisha system check task",
        [NISHA_SYS_DATA_COLLECT_TASK] = "nisha system data collect task",
        [NISHA_SYS_PROC_MOTOR_ERROR_TASK] = "nisha system process motor error task",

        // ...其他设备名称
    };
    return (chain < MAX_DEVICES) ? names[chain] : "Unknown";
}

task_chain_status_t GetChainStatus(task_chain_t *chain)
{
    return chain->chainStatus; // 返回枚举值：运行中/停止/完成等
}

void ChainCleanupHandler(task_chain_t *chain)
{
    pthread_mutex_destroy(&chain->lock);
    free(chain->taskItem);
    chain->taskItem = NULL;
    // chain.
}

// 返回毫秒
uint64_t CalcElapsed(struct timespec *start)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t sec_diff = now.tv_sec - start->tv_sec;
    int64_t nsec_diff = now.tv_nsec - start->tv_nsec;
    if (nsec_diff < 0)
    {
        // 如果纳秒差是负数，则借位
        nsec_diff += 1000000000; // 加上1秒的纳秒数
        sec_diff -= 1;           // 减去1秒
    }

    return (sec_diff * 1000 + nsec_diff / 1000000); // 转换为毫秒
}

void load_task_item(task_item_t *dest_task, task_item_t *src_task)
{
    memcpy(dest_task, src_task, sizeof(task_item_t));
}
#define TASK_PRIORITY 1
// -------------------------------  任务0 系统初始化 ------------------------------------- //
#define NISHA_SYS_CHECK_TASK_COUNT 5

/* 仅 ACTIVE 且已建队列的口可入自检链（COM1 无 ROLE 时勿绑外设） */
static int check_port_ready(int port)
{
    if (port < 0 || port > 5)
        return 0;
    if (g_uartWorkInfoArry[port].role != UART_ACTIVE_CONTROL)
        return 0;
    if (g_uartWorkInfoArry[port].taskQueue.mutex == NULL
        || g_uartWorkInfoArry[port].taskQueue.pItemArray == NULL)
        return 0;
    return 1;
}

int NiShaSystemCheckTask(void)
{
    if (isTaskChainRunning(&nisha_sys_check_task))
    {
        fprintf(stderr, "[Error] Task chain is already running\n");
        return -1;
    }

    task_item_t *tasks = malloc(sizeof(task_item_t) * NISHA_SYS_CHECK_TASK_COUNT);
    if (!tasks)
    {
        fprintf(stderr, "[Error] Failed to allocate memory for tasks\n");
        return -1;
    }

    int step_count = 0;
    task_item_t temp_task;
    int port;

    nisha_sys_check_task.chainStatus = TASKCHAIN_SUBMIT;

    port = get_device_index(RELAY_MODULE);
    if (check_port_ready(port))
    {
        memset(&temp_task, 0, sizeof(temp_task));
        temp_task.deviceName = RELAY_MODULE;
        temp_task.taskName.relay_module_task = RELAY_MODULE_HEARTBEAT;
        temp_task.priority = TASK_PRIORITY;
        temp_task.value = NULL;
        temp_task.taskPort = port;
        load_task_item(&tasks[step_count++], &temp_task);
    }
    else if (port != -1)
    {
        LOG(LOG_WARNING, "NiShaSystemCheckTask: skip RELAY_MODULE, COM%d not ACTIVE", port + 1);
    }

    port = get_device_index(DEPTH_SENSOR);
    if (check_port_ready(port))
    {
        memset(&temp_task, 0, sizeof(temp_task));
        temp_task.deviceName = DEPTH_SENSOR;
        temp_task.taskName.depth_sensor_task = IS_DEPTH_SENSOR_ONLINE;
        temp_task.priority = TASK_PRIORITY;
        temp_task.value = NULL;
        temp_task.taskPort = port;
        load_task_item(&tasks[step_count++], &temp_task);
    }
    else if (port != -1)
    {
        LOG(LOG_WARNING, "NiShaSystemCheckTask: skip DEPTH_SENSOR, COM%d not ACTIVE", port + 1);
    }

    port = get_device_index(SEDIMENT_SENSOR);
    if (check_port_ready(port))
    {
        memset(&temp_task, 0, sizeof(temp_task));
        temp_task.deviceName = SEDIMENT_SENSOR;
        temp_task.taskName.sediment_sensor_task = IS_SEDIMENT_SENSOR_ONLINE;
        temp_task.priority = TASK_PRIORITY;
        temp_task.value = NULL;
        temp_task.taskPort = port;
        load_task_item(&tasks[step_count++], &temp_task);
    }
    else if (port != -1)
    {
        LOG(LOG_WARNING, "NiShaSystemCheckTask: skip SEDIMENT_SENSOR, COM%d not ACTIVE", port + 1);
    }

    port = get_device_index(DC_MOTOR);
    if (check_port_ready(port))
    {
        memset(&temp_task, 0, sizeof(temp_task));
        temp_task.deviceName = DC_MOTOR;
        temp_task.taskName.dc_motor_task = IS_DC_MOTOR_ONLINE;
        temp_task.priority = TASK_PRIORITY;
        temp_task.value = NULL;
        temp_task.taskPort = port;
        load_task_item(&tasks[step_count++], &temp_task);
    }
    else if (port != -1)
    {
        LOG(LOG_WARNING, "NiShaSystemCheckTask: skip DC_MOTOR, COM%d not ACTIVE", port + 1);
    }

    port = get_device_index(ENCODER);
    if (check_port_ready(port))
    {
        memset(&temp_task, 0, sizeof(temp_task));
        temp_task.deviceName = ENCODER;
        temp_task.taskName.encoder_task = ENCODER_HEARTBEAT;
        temp_task.priority = TASK_PRIORITY;
        temp_task.value = NULL;
        temp_task.taskPort = port;
        load_task_item(&tasks[step_count++], &temp_task);
    }
    else if (port != -1)
    {
        LOG(LOG_WARNING, "NiShaSystemCheckTask: skip ENCODER, COM%d not ACTIVE", port + 1);
    }

    if (step_count == 0)
    {
        free(tasks);
        nisha_sys_check_task.chainStatus = TASKCHAIN_WAITING;
        LOG(LOG_ERROR, "NiShaSystemCheckTask: no configured device to check");
        return -1;
    }

    nisha_sys_check_task.code = NISHA_SYS_CHECK_TASK;
    nisha_sys_check_task.taskItem = tasks;
    nisha_sys_check_task.taskStatus = TASK_INIT;
    nisha_sys_check_task.stepLength = step_count;
    nisha_sys_check_task.step = 0;
    nisha_sys_check_task.intervalMs = 0; // 任务链间隔
    nisha_sys_check_task.execMode = CHAIN_MODE_SINGLE;
    nisha_sys_check_task.loopCount = 0;
    nisha_sys_check_task.taskIntervalMs = 200; // 任务间隔
    nisha_sys_check_task.stopRequested = false;
    nisha_sys_check_task.taskchain_manager = CommonTaskChainManager;
    pthread_mutex_init(&nisha_sys_check_task.lock, NULL);
    if (-1 == submitTask(g_threadPool, nisha_sys_check_task.taskchain_manager, &nisha_sys_check_task))
    {
        nisha_sys_check_task.chainStatus = TASKCHAIN_WAITING;
        free(tasks);
        LOG(LOG_ERROR, "Failed to submit [%s] to thread pool\n", GetTaskChainName(nisha_sys_check_task.code));
        return -1;
    }
    return 0;
}

void NS_sys_check_relay_module(void)
{
    SetRelayModuleOnline(true);
    chain_task_complete(NISHA_SYS_CHECK_TASK);
}

void NS_sys_check_relay_module_timeout(void)
{
    SetRelayModuleOnline(false);
    chain_task_timeout(NISHA_SYS_CHECK_TASK);
}

void NS_sys_check_depth_sensor(void)
{
    set_depth_online(true);
    chain_task_complete(NISHA_SYS_CHECK_TASK);
}

void NS_sys_check_depth_sensor_timeout(void)
{
    set_depth_online(false);
    chain_task_timeout(NISHA_SYS_CHECK_TASK);
}

void NS_sys_check_sediment_sensor(void)
{
    set_sediment_online(true);
    chain_task_complete(NISHA_SYS_CHECK_TASK);
}

void NS_sys_check_sediment_sensor_timeout(void)
{
    set_sediment_online(false);
    chain_task_timeout(NISHA_SYS_CHECK_TASK);
}

void NS_sys_check_dc_motor(void)
{
    set_dc_motor_online(true);
    chain_task_complete(NISHA_SYS_CHECK_TASK);
}

void NS_sys_check_dc_motor_timeout(void)
{
    set_dc_motor_online(false);
    chain_task_timeout(NISHA_SYS_CHECK_TASK);
}

void NS_sys_check_encoder(void)
{
    set_encoder_online(true);
    chain_task_complete(NISHA_SYS_CHECK_TASK);
}

void NS_sys_check_encoder_timeout(void)
{
    set_encoder_online(false);
    chain_task_timeout(NISHA_SYS_CHECK_TASK);
}

bool isNiShaSystemCheckTaskRunning(void)
{
    return isTaskChainRunning(&nisha_sys_check_task);
}

// -------------------------------  任务1 泥沙数据采集 ------------------------------------- //
#define NISHA_SYS_DATACOLLECT_TASK_COUNT 1

void NS_sys_data_collect_sediment_sensor(uint8_t *data)
{
    if (data == NULL)
    {
        fprintf(stderr, "[Error] Data is NULL\n");
        return;
    }

    nisha_buffer_put(&g_sediment_buffer, transform_sediment_sensor_value(data));

    chain_task_complete(NISHA_SYS_DATA_COLLECT_TASK);
}

void NS_sys_data_collect_sediment_sensor_timeout(void)
{
    set_sediment_online(false);
    chain_task_timeout(NISHA_SYS_DATA_COLLECT_TASK);
}

bool isNiShaSystemDataCollectTaskRunning(void)
{
    return isTaskChainRunning(&nisha_sys_data_collect_task);
}
void stop_NiShaSystemDataCollectTask(void)

{
    if (isNiShaSystemDataCollectTaskRunning())
    {
        stopTaskChain(&nisha_sys_data_collect_task);
    }
}

int get_Ns_sys_data_collect_sediment_sensor_taskStatus(void)
{
    return nisha_sys_data_collect_task.taskStatus;
}

int NiShaSystemDataCollectTask(uint32_t intervalMs, int execMode, uint16_t loopCount)
{
    if (isTaskChainRunning(&nisha_sys_data_collect_task))
    {
        fprintf(stderr, "[Error] Task chain is already running\n");
        return -1;
    }

    task_item_t *tasks = malloc(sizeof(task_item_t) * NISHA_SYS_DATACOLLECT_TASK_COUNT);
    if (!tasks)
    {
        fprintf(stderr, "[Error] Failed to allocate memory for tasks\n");
        return -1;
    }
    nisha_sys_data_collect_task.chainStatus = TASKCHAIN_SUBMIT;
    task_item_t temp_task = {.deviceName = SEDIMENT_SENSOR,
                             .taskName.sediment_sensor_task = NISHA_SEDIMENT_SENSOR_COLLECT_DATA,
                             .priority = TASK_PRIORITY,
                             .value = NULL,
                             .taskPort = get_device_index(SEDIMENT_SENSOR)};

    load_task_item(&tasks[0], &temp_task); // 检查继电器在不在线

    nisha_sys_data_collect_task.code = NISHA_SYS_DATA_COLLECT_TASK;
    nisha_sys_data_collect_task.taskItem = tasks;
    nisha_sys_data_collect_task.taskStatus = TASK_INIT;
    nisha_sys_data_collect_task.stepLength = NISHA_SYS_DATACOLLECT_TASK_COUNT;
    nisha_sys_data_collect_task.step = 0;
    nisha_sys_data_collect_task.intervalMs = intervalMs; // 任务链间隔
    nisha_sys_data_collect_task.execMode = execMode;     // 循环模式
    nisha_sys_data_collect_task.loopCount = loopCount;
    nisha_sys_data_collect_task.taskIntervalMs = 200; // 任务间隔
    nisha_sys_data_collect_task.stopRequested = false;
    nisha_sys_data_collect_task.taskchain_manager = CommonTaskChainManager;
    pthread_mutex_init(&nisha_sys_data_collect_task.lock, NULL);
    if (-1 == submitTask(g_threadPool, nisha_sys_data_collect_task.taskchain_manager, &nisha_sys_data_collect_task))
    {
        nisha_sys_data_collect_task.chainStatus = TASKCHAIN_WAITING;
        free(tasks);
        LOG(LOG_ERROR, "Failed to submit [%s] to thread pool\n", GetTaskChainName(nisha_sys_data_collect_task.code));
        return -1;
    }
}

// -------------------------------  任务2 电机异常处理 ------------------------------------- //
#define NISHA_SYS_MOTOR_PROC_ERROR_TASK_COUNT 2

void NS_sys_proc_motor_error_dc_motor(uint8_t *recvbuffer)
{
    process_dc_motor_error_code(recvbuffer);
    chain_task_complete(NISHA_SYS_PROC_MOTOR_ERROR_TASK);
}

void NS_sys_proc_motor_error_dc_motor_timeout(void)
{
    set_dc_motor_online(false);
    chain_task_timeout(NISHA_SYS_PROC_MOTOR_ERROR_TASK);
}

void NS_sys_reset_motor_error_dc_motor(void)
{
    chain_task_complete(NISHA_SYS_PROC_MOTOR_ERROR_TASK);
}

void NS_sys_reset_motor_error_dc_motor_timeout(void)
{
    set_dc_motor_online(false);
    chain_task_timeout(NISHA_SYS_PROC_MOTOR_ERROR_TASK);
}

int NiShaSystemProcMotorErrorTask(void)
{
    if (isTaskChainRunning(&nisha_sys_proc_motor_error_task))
    {
        fprintf(stderr, "[Error] Task chain is already running\n");
        return -1;
    }

    task_item_t *tasks = malloc(sizeof(task_item_t) * NISHA_SYS_MOTOR_PROC_ERROR_TASK_COUNT);
    if (!tasks)
    {
        fprintf(stderr, "[Error] Failed to allocate memory for tasks\n");
        return -1;
    }
    nisha_sys_proc_motor_error_task.chainStatus = TASKCHAIN_SUBMIT;
    task_item_t temp_task = {.deviceName = DC_MOTOR,
                             .taskName.dc_motor_task = NS_DC_MOTOR_GET_ERROR_CODE,
                             .priority = TASK_PRIORITY,
                             .value = NULL,
                             .taskPort = get_device_index(DC_MOTOR)};

    load_task_item(&tasks[0], &temp_task); //

    temp_task.deviceName = DC_MOTOR;
    temp_task.taskName.dc_motor_task = NS_DC_MOTOR_RESET_ERROR_CODE;
    temp_task.taskPort = get_device_index(DC_MOTOR);
    load_task_item(&tasks[1], &temp_task);

    nisha_sys_proc_motor_error_task.code = NISHA_SYS_PROC_MOTOR_ERROR_TASK;
    nisha_sys_proc_motor_error_task.taskItem = tasks;
    nisha_sys_proc_motor_error_task.taskStatus = TASK_INIT;
    nisha_sys_proc_motor_error_task.stepLength = NISHA_SYS_MOTOR_PROC_ERROR_TASK_COUNT;
    nisha_sys_proc_motor_error_task.step = 0;
    nisha_sys_proc_motor_error_task.intervalMs = 200;             // 任务链间隔
    nisha_sys_proc_motor_error_task.execMode = CHAIN_MODE_SINGLE; // 无限循环
    nisha_sys_proc_motor_error_task.loopCount = 0;
    nisha_sys_proc_motor_error_task.taskIntervalMs = 200; // 任务间隔
    nisha_sys_proc_motor_error_task.stopRequested = false;
    nisha_sys_proc_motor_error_task.taskchain_manager = CommonTaskChainManager;
    pthread_mutex_init(&nisha_sys_proc_motor_error_task.lock, NULL);
    if (-1 == submitTask(g_threadPool, nisha_sys_proc_motor_error_task.taskchain_manager, &nisha_sys_proc_motor_error_task))
    {
        nisha_sys_proc_motor_error_task.chainStatus = TASKCHAIN_WAITING;
        free(tasks);
        LOG(LOG_ERROR, "Failed to submit [%s] to thread pool\n", GetTaskChainName(nisha_sys_proc_motor_error_task.code));
        return -1;
    }
}
