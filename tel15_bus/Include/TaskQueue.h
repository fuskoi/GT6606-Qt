#ifndef _TASK_QUEUE_H
#define _TASK_QUEUE_H

#include "Common.h"
#include "WebSeriver.h"

/* 任务状态 */
typedef enum {
    TASK_PENDING = 0,
    TASK_RUNNING,
    TASK_COMPLETED,
    TASK_ERROR
} TaskStatus;

/* 通用任务节点 */
typedef struct TaskNode {
    void    (*func)(void *arg);    /* 任务执行函数 */
    void    *arg;                   /* 任务参数 */
    size_t   argSize;              /* 参数数据大小 */
    uint32_t commandSeri;          /* 命令序号 */
    int      deviceType;           /* 设备类型 DTU/ZIGBEE */
    uint32_t commandCode;          /* 命令字 */
    TaskStatus status;             /* 任务状态 */
    struct TaskNode *next;         /* 链表指针 */
} TaskNode;

/* 任务队列结构体 */
typedef struct {
    TaskNode    *head;             /* 队列头 */
    TaskNode    *tail;             /* 队列尾 */
    int         count;             /* 队列中的任务数 */
    pthread_mutex_t mutex;         /* 互斥锁 */
    pthread_cond_t  cond;          /* 条件变量（通知工作线程） */
    volatile int    shutdown;      /* 关闭标志 */
    pthread_t   workerThread;      /* 工作线程ID */
} TaskQueue;

/* 任务队列API */
int  TaskQueue_Init(TaskQueue *queue);
int  TaskQueue_Push(TaskQueue *queue, TaskNode *node);
int  TaskQueue_Pop(TaskQueue *queue, TaskNode **node);
void TaskQueue_Clear(TaskQueue *queue);
void TaskQueue_Destroy(TaskQueue *queue);

/* 通用任务封装 */
typedef struct {
    uint8_t     *pData;            /* 命令数据（已分配） */
    uint16_t    u16datalen;        /* 数据长度 */
    int         Devicetype;        /* 设备类型 */
    uint32_t    u32CommandCode;    /* 命令字 */
    uint32_t    u32CommandSeri;    /* 命令序号 */
} CommonTaskArg;

CommonTaskArg* CommonTaskArg_Create(uint8_t *pData, uint16_t len, int type,
                                    uint32_t cmdCode, uint32_t cmdSeri);
void CommonTaskArg_Destroy(CommonTaskArg *arg);

/* 注册快捷任务（封装创建+入队） */
int TaskQueue_SubmitSimple(TaskQueue *queue, void (*func)(void*),
                           uint8_t *pData, uint16_t len,
                           int type, uint32_t cmdCode, uint32_t cmdSeri);

/* 全局任务队列 */
extern TaskQueue g_taskQueue;

/* 命令处理函数注册 */
typedef void (*CmdHandlerFunc)(void *arg);

/* 将CommandCodeProc的每个case封装为独立函数 */
void Cmd_SetSection(void *arg);
void Cmd_SetSection_new(void *arg);
void Cmd_SetCableway(void *arg);
void Cmd_SetAutoMeasure(void *arg);
void Cmd_SwitchMode(void *arg);
void Cmd_Restart(void *arg);
void Cmd_UpFish(void *arg);
void Cmd_CarBackHome(void *arg);
void Cmd_RotatorStart(void *arg);
void Cmd_RotatorStop(void *arg);
void Cmd_CarMove(void *arg);
void Cmd_FishMove(void *arg);
void Cmd_GetCarConfig(void *arg);
void Cmd_AddMeasure(void *arg);
void Cmd_EmergencyStop(void *arg);
void Cmd_SetRotatorPara(void *arg);
void Cmd_PauseMeasure(void *arg);
void Cmd_ResumeMeasure(void *arg);
void Cmd_ProcFinalResult(void *arg);
void Cmd_SendSubDeviceConfigFile(void *arg);
void Cmd_SetSubDeviceConfigFile(void *arg);
void Cmd_SendLogFile(void *arg);

#endif /* _TASK_QUEUE_H */