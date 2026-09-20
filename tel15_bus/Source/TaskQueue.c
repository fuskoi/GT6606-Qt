#include "TaskQueue.h"
#include "log.h"
#include "ringfifo.h"
#include "CRC.h"
#include "Relay.h"
#include "Encoder.h"
#include "Sensor.h"
#include "LinkList.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/reboot.h>
#include "zigbee_dataproc.h"
/* 全局任务队列 */
TaskQueue g_taskQueue;

/* 工作线程函数 */
static void *taskQueue_Worker(void *arg)
{
    TaskQueue *queue = (TaskQueue *)arg;
    TaskNode *node = NULL;

    while (1) {
        pthread_mutex_lock(&queue->mutex);

        /* 等待任务或关闭信号 */
        while (queue->count == 0 && !queue->shutdown) {
            pthread_cond_wait(&queue->cond, &queue->mutex);
        }

        if (queue->shutdown) {
            pthread_mutex_unlock(&queue->mutex);
            break;
        }

        /* 取出队列头节点 */
        node = queue->head;
        if (node != NULL) {
            queue->head = node->next;
            if (queue->head == NULL) {
                queue->tail = NULL;
            }
            queue->count--;
        }

        pthread_mutex_unlock(&queue->mutex);

        /* 执行任务 */
        if (node != NULL) {
            node->status = TASK_RUNNING;
            LOG(LOG_DEBUG, "TaskQueue: executing cmd=0x%x, seri=%d",
                node->commandCode, node->commandSeri);

            if (node->func != NULL) {
                node->func(node->arg);
            }

            node->status = TASK_COMPLETED;

            /* 释放节点内存 */
            if (node->arg != NULL) {
                CommonTaskArg *cmdArg = (CommonTaskArg *)node->arg;
                if (cmdArg->pData != NULL) {
                    free(cmdArg->pData);
                }
                free(cmdArg);
                node->arg = NULL;
            }
            free(node);
            node = NULL;
        }
    }

    return NULL;
}

/*
 * TaskQueue_Init - 初始化任务队列，创建工作线程
 */
int TaskQueue_Init(TaskQueue *queue)
{
    if (queue == NULL) return ERROR;

    memset(queue, 0, sizeof(TaskQueue));
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->shutdown = 0;

    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);

    /* 创建工作线程 */
    if (pthread_create(&queue->workerThread, NULL, taskQueue_Worker, queue) != 0) {
        LOG(LOG_ERROR, "TaskQueue: failed to create worker thread");
        return ERROR;
    }
    pthread_detach(queue->workerThread);

    LOG(LOG_INFO, "TaskQueue: initialized successfully");
    return SUCCESS;
}

/*
 * TaskQueue_Push - 向队列尾部添加一个任务（线程安全）
 */
int TaskQueue_Push(TaskQueue *queue, TaskNode *node)
{
    if (queue == NULL || node == NULL) return ERROR;

    pthread_mutex_lock(&queue->mutex);

    node->next = NULL;
    node->status = TASK_PENDING;

    if (queue->tail == NULL) {
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }
    queue->count++;

    /* 通知工作线程 */
    pthread_cond_signal(&queue->cond);

    pthread_mutex_unlock(&queue->mutex);
    return SUCCESS;
}

/*
 * TaskQueue_Pop - 从队列头部取出一个任务（线程安全）
 */
int TaskQueue_Pop(TaskQueue *queue, TaskNode **node)
{
    if (queue == NULL || node == NULL) return ERROR;

    pthread_mutex_lock(&queue->mutex);

    if (queue->head == NULL) {
        *node = NULL;
        pthread_mutex_unlock(&queue->mutex);
        return ERROR;
    }

    *node = queue->head;
    queue->head = queue->head->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    queue->count--;

    pthread_mutex_unlock(&queue->mutex);
    return SUCCESS;
}

/*
 * TaskQueue_Clear - 清空任务队列中所有未执行的任务（线程安全）
 * 注意：当前正在执行的任务不受影响
 */
void TaskQueue_Clear(TaskQueue *queue)
{
    if (queue == NULL) return;

    pthread_mutex_lock(&queue->mutex);

    TaskNode *cur = queue->head;
    while (cur != NULL) {
        TaskNode *next = cur->next;
        /* 释放任务参数 */
        if (cur->arg != NULL) {
            CommonTaskArg *cmdArg = (CommonTaskArg *)cur->arg;
            if (cmdArg->pData != NULL) free(cmdArg->pData);
            free(cmdArg);
        }
        free(cur);
        cur = next;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;

    pthread_mutex_unlock(&queue->mutex);

    LOG(LOG_DEBUG, "TaskQueue: cleared all pending tasks");
}

/*
 * TaskQueue_Destroy - 销毁任务队列
 */
void TaskQueue_Destroy(TaskQueue *queue)
{
    if (queue == NULL) return;

    /* 通知工作线程退出 */
    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = 1;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);

    /* 清理所有未完成的任务 */
    TaskNode *cur = queue->head;
    while (cur != NULL) {
        TaskNode *next = cur->next;
        if (cur->arg != NULL) {
            CommonTaskArg *cmdArg = (CommonTaskArg *)cur->arg;
            if (cmdArg->pData != NULL) free(cmdArg->pData);
            free(cmdArg);
        }
        free(cur);
        cur = next;
    }

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);

    memset(queue, 0, sizeof(TaskQueue));
}

/*
 * CommonTaskArg_Create - 创建通用任务参数
 */
CommonTaskArg* CommonTaskArg_Create(uint8_t *pData, uint16_t len, int type,
                                    uint32_t cmdCode, uint32_t cmdSeri)
{
    CommonTaskArg *arg = (CommonTaskArg *)malloc(sizeof(CommonTaskArg));
    if (arg == NULL) return NULL;

    arg->u16datalen = len;
    arg->Devicetype = type;
    arg->u32CommandCode = cmdCode;
    arg->u32CommandSeri = cmdSeri;

    /* 复制命令数据 */
    if (pData != NULL && len > 0) {
        arg->pData = (uint8_t *)malloc(len);
        if (arg->pData == NULL) {
            free(arg);
            return NULL;
        }
        memcpy(arg->pData, pData, len);
    } else {
        arg->pData = NULL;
    }

    return arg;
}

/*
 * CommonTaskArg_Destroy - 销毁通用任务参数
 */
void CommonTaskArg_Destroy(CommonTaskArg *arg)
{
    if (arg == NULL) return;
    if (arg->pData != NULL) {
        free(arg->pData);
    }
    free(arg);
}

/*
 * TaskQueue_SubmitSimple - 快捷创建并提交一个任务
 */
int TaskQueue_SubmitSimple(TaskQueue *queue, void (*func)(void*),
                           uint8_t *pData, uint16_t len,
                           int type, uint32_t cmdCode, uint32_t cmdSeri)
{
    /* 创建任务参数 */
    CommonTaskArg *arg = CommonTaskArg_Create(pData, len, type, cmdCode, cmdSeri);
    if (arg == NULL) return ERROR;

    /* 创建任务节点 */
    TaskNode *node = (TaskNode *)malloc(sizeof(TaskNode));
    if (node == NULL) {
        CommonTaskArg_Destroy(arg);
        return ERROR;
    }

    memset(node, 0, sizeof(TaskNode));
    node->func = func;
    node->arg = arg;
    node->commandCode = cmdCode;
    node->commandSeri = cmdSeri;
    node->deviceType = type;
    node->status = TASK_PENDING;
    node->next = NULL;

    return TaskQueue_Push(queue, node);
}

// ============================================================
// 命令处理函数（每个case封装为独立函数）
// 调用 Command_proc.c 中已存在的原始函数
// ============================================================

extern int SetSectionPara(BYTE *pData, SetSection *pstSection, uint16_t u16datalen);
extern int ReadSectionPara(SetSection *pstSection);
extern int SetCablewayPara(BYTE *pData, SetCableway *pstSetCableway, uint16_t u16datalen);
extern int ReadCablewayPara(SetCableway *pstSetCableway);
extern int SetAutoMeasurePara(BYTE *pData, AutoMeasure *pstAutoMeasure, uint16_t u16datalen);
extern int ReadAutoMeasurePara(AutoMeasure *pstAutoMeasure);
extern int SwitchoperaMode(BYTE *pData, uint16_t u16datalen);
extern void ReStart(BYTE *pData);
extern int Fish_up_nonblock(void);
extern int Car_Goback_Home_nonblock(void);
extern int CarMovetoAim_nonblock(BYTE *pData);
extern int FishMovetoAim_nonblock(BYTE *pData);
extern int Rotator_manuMeasureStart(BYTE *pData, uint16_t u16datalen);
extern int Rotator_manuMeasureStop(BYTE *pData, uint16_t u16datalen, int type);
extern int GetCarConfigFile(uint32_t commandseri, int type);
extern int SetRotatorpara(BYTE *pData, ROTATOR_CONFIG *pstRotator, uint16_t u16datalen);
extern int ReadRotatorConfig(ROTATOR_CONFIG *pstRotatorconfig);
extern void ProcFinalResult(BYTE *pData, uint16_t u16datalen);
extern void SendSubDeviceConfigFile(uint8_t subDeviceType, const char* filename, uint32_t command_seri, int type);
extern int SaveConfigFile(const char* filename, uint16_t file_len, uint8_t* file_data);
extern void* AddMeauCtrl(void *arg);
extern void PauseAddMeasure(void);
extern void ResumeAddMeasure(void);
extern int SendResultFrame(uint32_t ExeCommand, uint32_t commandseri, uint8_t resaut, int type);
extern SetSection g_stSection;
extern SetCableway g_stSetCableway;
extern AutoMeasure g_stAutoMeasure;
extern ROTATOR_CONFIG g_stRotatorConfig;

/* ------------------------------------------------------------------ */
void Cmd_SetSection(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    int ret = SetSectionPara(ca->pData, &g_stSection, ca->u16datalen);
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, ret, ca->Devicetype);
    ReadSectionPara(&g_stSection);
}

void Cmd_SetSection_new(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    int ret = SetSectionPara_new(ca->pData, &g_stSection_new, ca->u16datalen);
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, ret, ca->Devicetype);
    ReadSectionPara_new(&g_stSection_new);
}


/* ------------------------------------------------------------------ */
void Cmd_SetCableway(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    int ret = SetCablewayPara(ca->pData, &g_stSetCableway, ca->u16datalen);
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, ret, ca->Devicetype);
    ReadCablewayPara(&g_stSetCableway);
    sync();
    reboot(RB_AUTOBOOT);
}

/* ------------------------------------------------------------------ */
void Cmd_SetAutoMeasure(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    int ret = SetAutoMeasurePara(ca->pData, &g_stAutoMeasure, ca->u16datalen);
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, ret, ca->Devicetype);
    ReadAutoMeasurePara(&g_stAutoMeasure);
}

/* ------------------------------------------------------------------ */
void Cmd_SwitchMode(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    int ret = SwitchoperaMode(ca->pData, ca->u16datalen);
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, ret, ca->Devicetype);
    (void)ret;
}

/* ------------------------------------------------------------------ */
void Cmd_Restart(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    ReStart(ca->pData);
}

/* ------------------------------------------------------------------ */
void Cmd_UpFish(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    (void)ca;
    
    Fish_up_nonblock();
}

/* ------------------------------------------------------------------ */
void Cmd_CarBackHome(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    (void)ca;
    
    Car_Goback_Home_nonblock();
}

/* ------------------------------------------------------------------ */
void Cmd_RotatorStart(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    Rotator_manuMeasureStart(ca->pData, ca->u16datalen);
}

/* ------------------------------------------------------------------ */
void Cmd_RotatorStop(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    Rotator_manuMeasureStop(ca->pData, ca->u16datalen, ca->Devicetype);
    ResetRotatorValue(NULL);
}

/* ------------------------------------------------------------------ */
void Cmd_CarMove(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    CarMovetoAim_nonblock(ca->pData);
}

/* ------------------------------------------------------------------ */
void Cmd_FishMove(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    FishMovetoAim_nonblock(ca->pData);
}

/* ------------------------------------------------------------------ */
void Cmd_GetCarConfig(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    GetCarConfigFile(ca->u32CommandSeri, ca->Devicetype);
}

/* ------------------------------------------------------------------ */
void Cmd_AddMeasure(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    CommandProcParam stCommandPara;
    memset(&stCommandPara, 0, sizeof(CommandProcParam));
    stCommandPara.pData = ca->pData;
    stCommandPara.u16datalen = ca->u16datalen;
    stCommandPara.Devicetype = ca->Devicetype;
    AddMeauCtrl(&stCommandPara);
}

/* ------------------------------------------------------------------ */
void Cmd_EmergencyStop(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    (void)arg;
    emergency_stop_handler(NULL);
}

/* ------------------------------------------------------------------ */
void Cmd_SetRotatorPara(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    int ret = SetRotatorpara(ca->pData, &g_stRotatorConfig, ca->u16datalen);
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    sync();
    ReadRotatorConfig(&g_stRotatorConfig);
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, ret, ca->Devicetype);
}

/* ------------------------------------------------------------------ */
void Cmd_PauseMeasure(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    uint8_t action = 1; /* 缺省按暂停，兼容旧包无动作字节 */

    if (ca != NULL && ca->pData != NULL && ca->u16datalen >= 1) {
        action = ca->pData[0];
    }

    LOG(LOG_INFO, "PAUSE_CONTINUE_MEASURE action=%u (1暂停 2继续 3门切换)",
        (unsigned)action);

    switch (action) {
    case 1:
        PauseAddMeasure();
        break;
    case 2:
        ResumeAddMeasure();
        break;
    case 3:
        /* 先回平台 ACK，再静默开关门（可能等到位数秒，避免平台判超时） */
        if (ca != NULL) {
            SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS,
                            ca->Devicetype);
            ca = NULL;
        }
        Door_Toggle_cmd();
        break;
    default:
        LOG(LOG_WARNING, "PAUSE_CONTINUE_MEASURE unknown action=%u",
            (unsigned)action);
        break;
    }

    if (ca != NULL) {
        SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS,
                        ca->Devicetype);
    }
}

/* ------------------------------------------------------------------ */
void Cmd_ResumeMeasure(void *arg)
{
    (void)arg;
    ResumeAddMeasure();
}

/* ------------------------------------------------------------------ */
void Cmd_ProcFinalResult(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    SendResultFrame(ca->u32CommandCode, ca->u32CommandSeri, SUCCESS, ca->Devicetype);
    ProcFinalResult(ca->pData, ca->u16datalen);
}

/* ------------------------------------------------------------------ */
void Cmd_SendSubDeviceConfigFile(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    // tyls.conf 是主机的配置文件，子设备类型固定为 3
    uint8_t subDeviceType = 3;
    
    // 获取配置文件路径
    char configFilePath[256] = {0};
    GetAppDir(configFilePath);
    strncat(configFilePath, CONFIG_FILE_PATH, strlen(CONFIG_FILE_PATH));
    
    SendSubDeviceConfigFile(subDeviceType, configFilePath, ca->u32CommandSeri, ca->Devicetype);
}

/* ------------------------------------------------------------------ */
void Cmd_SendLogFile(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    uint8_t deviceType = LOG_DEVICE_CAR_HOST;
    uint8_t logLevel = LOG_LEVEL_ALL;

    if (ca->pData != NULL && ca->u16datalen >= 2) {
        deviceType = ca->pData[0];
        logLevel = ca->pData[1];
    }
    SendLogFile(deviceType, logLevel, ca->u32CommandSeri, ca->Devicetype);
}

/* ------------------------------------------------------------------ */
void Cmd_SetSubDeviceConfigFile(void *arg)
{
    CommonTaskArg *ca = (CommonTaskArg *)arg;
    // pData 中包含 SetSubDeviceConfig 结构体的数据
    // SeriverCommandHead(20)已由CommandCodeProc解析，pData从站码后开始
    // 但TaskQueue_SubmitSimple传入的是pData + paraStartdiff整个后续数据
    if (ca->pData == NULL || ca->u16datalen < sizeof(uint8_t) + sizeof(uint16_t))
    {
        LOG(LOG_ERROR, "Cmd_SetSubDeviceConfigFile: invalid data");
        SendResultFrame(SET_SUB_DEVICE_CONFIG, ca->u32CommandSeri, ERROR, ca->Devicetype);
        return;
    }
    
    // 解析: u8SubDevice(1) + u16FileLen(2) + fileData
    uint8_t subDevice = ca->pData[0];
    uint16_t fileLen = 0;
    memcpy(&fileLen, &ca->pData[1], sizeof(uint16_t));
    uint8_t *fileData = &ca->pData[1 + sizeof(uint16_t)];
    
    // 保存配置文件（内部会执行备份+写入）
    int ret = SaveConfigFile(CONFIG_FILE_PATH, fileLen, fileData);
    
    // 发送执行结果
    SendResultFrame(SET_SUB_DEVICE_CONFIG, ca->u32CommandSeri, 
                    (ret == 0) ? SUCCESS : ERROR, ca->Devicetype);
}
