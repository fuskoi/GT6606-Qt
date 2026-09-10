#ifndef __TASK_H__
#define __TASK_H__
// #define __USE_POSIX199309
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include "general_head.h"
#include "uart.h"
#include "taskqueue.h"
#define MAX_THREAD_POOL_SIZE 10
#define MAX_THREADS 5
#define MAX_TASK_ITEM 32
#define CLOCK_MONOTONIC 1

typedef enum
{
    TASKCHAIN_SUBMIT,
    TASKCHAIN_WAITING,   // 等待中
    TASKCHAIN_RUNNING,   // 运行中
    TASKCHAIN_COMPLETED, // 正常完成
    TASKCHAIN_STOPPED    // 被手动停止
} task_chain_status_t;

typedef enum
{
    TASK_INIT = 0,
    TASK_WAITING,        // 等待状态返回
    TASK_READY,          // 可以执行下一个任务
    TASK_FINISHED,       // 任务结束
    TASK_TIMEOUT,        // 任务失败
    TASK_ENQUEUE_FAILED, // 任务入队失败
    TASK_EXEC_FAILED,    // 任务执行失败
} task_status_t;
typedef enum
{
    CHAIN_MODE_SINGLE,   // 单次执行
    CHAIN_MODE_INFINITE, // 无限循环
    CHAIN_MODE_FINITE    // 有限次数循环
} ChainExecMode;

typedef enum
{
    // CARSYSTEMINIT_TASK_CHAIN = 0,        // 小车系统初始化任务
    // CARSYSTEM_STATECHECK_TASKCHAIN = 1,  // 小车系统状态检测任务, 出发前检测
    // CARSYS_WATERFLOWDET_TASKCHAIN = 2,   // 小车系统流速检测任务
    // CARSYS_BACKHOME_TASKCHAIN = 3,       // 小车系统返回家任务
    // CARSYS_MOTORRESTART_TASKCHAIN = 4,   // 小车系统驱动重启任务
    // CARSYS_CARINHOME_TASKCHAIN = 5,      // 小车系统是否在家任务
    // CARSYS_CLEARENCODER_TASKCHAIN = 6,   // 小车系统清除编码器任务
    // CARSYS_CLEARMOTORSTEP_TASKCHAIN = 7, // 小车系统清除步进电机步数任务
    // CARSYS_STOPMOTOR_TASKCHAIN = 8,      // 小车系统停止电机任务
    NISHA_SYS_CHECK_TASK,        // 泥沙系统自检任务
    NISHA_SYS_DATA_COLLECT_TASK, // 泥沙系统数据采集任务
    NISHA_SYS_PROC_MOTOR_ERROR_TASK, // 泥沙系统处理电机异常任务
    TASK_CHAIN_END,
} task_chain_name_t;

// 定义任务结构体
typedef struct
{
    void *(*task_function)(void *); // 任务函数指针
    void *task_argument;            // 任务函数参数
} pool_task_t;

typedef struct task_chain
{
    // 基础参数
    uint8_t code;          // 任务链标识码
    uint8_t stepLength;    // 总步骤数
    task_item_t *taskItem; // 任务项数组

    // 运行时状态
    task_chain_status_t chainStatus; // 任务链状态
    task_status_t taskStatus;        // 任务状态
    int step;                        // 当前步骤
    int chainStop_step;              // 导致任务链停止的步骤

    // 循环控制 -> 任务链
    ChainExecMode execMode; // 执行模式
    uint16_t loopCount;     // 任务链剩余循环次数（仅FINITE模式有效）
    _Bool stopRequested;    // 停止请求标志
    pthread_mutex_t lock;   // 互斥锁

    // 时间控制
    uint32_t intervalMs;          // 任务链 循环间隔（毫秒）
    struct timespec lastCycleEnd; // 任务链 上次循环结束时间
    uint32_t taskIntervalMs;      // 任务间隔时间

    // 任务需要的外部资源
    void *pResource;
    // 资源清理回调
    void (*cleanupCallback)(struct task_chain *);
    // 任务链管理函数
    void *(*taskchain_manager)(void *);
} task_chain_t;

// 定义线程池结构体
typedef struct ThreadPool
{
    pthread_mutex_t queue_mutex; // 任务队列互斥锁
    pthread_cond_t queue_cond;   // 任务队列条件变量
    pool_task_t *queue;          // 任务队列
    size_t queue_size;           // 任务队列当前大小
    size_t max_queue_size;       // 任务队列最大容量
    int shutdown;                // 线程池关闭标志
    pthread_t *threads;          // 线程ID数组
    int num_threads;             // 线程池中的线程数
    int max_threads;             // 线程池最大线程数
} ThreadPool;

// 互斥锁操作宏（简化代码）
#define LOCK(chain) pthread_mutex_lock(&(chain)->lock)
#define UNLOCK(chain) pthread_mutex_unlock(&(chain)->lock)
extern ThreadPool *g_threadPool;

ThreadPool *initializeThreadPool(int max_threads, size_t max_queue_size);

void *workerThread(void *arg);

int submitTask(ThreadPool *pool, void *(*task_function)(void *), void *task_argument);

int SubmitTaskToGThreadPool(void *(*task_function)(void *), void *task_argument);

void shutdownThreadPool(ThreadPool *pool);

void *CommonTaskChainManager(void *arg);

bool SubmitTaskToThreadPool(ThreadPool *threadPool, task_chain_t *taskChain, void *(*task_function)(void *), void *task_argument);

void *TaskChainCommHandle(void *arg);

// bool SubmitTaskToThreadPool(ThreadPool *threadPool, task_chain_t *taskChain);

bool StartTaskThread(void);

void load_task_item(task_item_t *dest_task, task_item_t *src_task);

int NiShaSystemCheckTask(void);

void NS_sys_check_relay_module(void);

void NS_sys_check_relay_module_timeout(void);

void NS_sys_check_depth_sensor(void);

void NS_sys_check_depth_sensor_timeout(void);

void NS_sys_check_sediment_sensor(void);

void NS_sys_check_sediment_sensor_timeout(void);

void NS_sys_check_dc_motor(void);

void NS_sys_check_dc_motor_timeout(void);

void NS_sys_check_encoder(void);

void NS_sys_check_encoder_timeout(void);

bool isNiShaSystemCheckTaskRunning(void);

void NS_sys_data_collect_sediment_sensor(uint8_t *data);

void NS_sys_data_collect_sediment_sensor_timeout(void);

bool isNiShaSystemDataCollectTaskRunning(void);

void stop_NiShaSystemDataCollectTask(void);

int get_Ns_sys_data_collect_sediment_sensor_taskStatus(void);

int NiShaSystemDataCollectTask(uint32_t intervalMs, int execMode, uint16_t loopCount);

void NS_sys_proc_motor_error_dc_motor(uint8_t *recvbuffer);

void NS_sys_proc_motor_error_dc_motor_timeout(void);

void NS_sys_reset_motor_error_dc_motor(void);

void NS_sys_reset_motor_error_dc_motor_timeout(void);

int NiShaSystemProcMotorErrorTask(void);

int CarSystemInitTask(void);

bool isCarSystemInitTaskRunning(void);

task_chain_status_t getCarSystemInitTaskStatus(void);

int getCarSystemInitTaskStoppedStep(void);

void *CarSystemInitTaskChainManager(void *arg);

int CarSystemStateCheckTask(void);

bool isCarSystemStateCheckTaskRunning(void);

task_chain_status_t getCarSystemStateCheckTaskStatus(void);

int getCarSystemStateCheckTaskStoppedStep(void);

int CarSystemBackHomeTask(void);

bool isCarSystemBackHomeTaskRunning(void);

task_chain_status_t getCarSystemBackHomeTaskStatus(void);

int getCarSystemBackHomeTaskStoppedStep(void);

void *BackHomeTaskChainManager(void *arg);

int CarSystemMotorReStartTask(uint32_t re_time);

bool isCarSystemMotorRestartRunning(void);

void *TimeCtrlTaskChainManager(void *arg);

bool isCarSystemCarInHomeTaskRunning(void);

int CarSystemStopMotorTask(void);

int CarSystemWaterFlowDetTask(void *arg);

bool isWaterFlowDetTaskRunning(void);

task_chain_status_t getWaterFlowDetTaskStatus(void);

void *WaterFlowDetTaskChainManager(void *arg);

bool isTaskChainRunning(task_chain_t *taskChain);

void changeTaskStatus(task_chain_t *taskChain, int status);

void chain_task_complete(task_chain_name_t chain_code);

void chain_task_complete_choosestatus(task_chain_name_t chain_code, task_status_t taskStatus);

// void changeTaskChainStatus(task_chain_t *taskChain, int status);

void chain_task_switch_previous_step(task_chain_name_t chain_code);

void chain_task_timeout(task_chain_name_t chain_code);

void chain_task_stop(task_chain_name_t chain_code);

void stopTaskChain(task_chain_t *taskChain);

void loadTaskChainPreviousStep(task_chain_t *taskChain);

void loadTaskChainStep(task_chain_t *taskChain, int step);

void chain_task_goto_step(task_chain_name_t chain_code, int step);

task_chain_t *GetTaskChain(task_chain_name_t chain_code);

task_chain_status_t GetChainStatus(task_chain_t *chain);

void ChainCleanupHandler(task_chain_t *chain);

const char *GetTaskChainName(uint8_t chain);

uint64_t CalcElapsed(struct timespec *start);

#endif
