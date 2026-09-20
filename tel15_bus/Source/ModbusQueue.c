#include "ModbusQueue.h"
#include "log.h"
#include "CRC.h"
#include "ringfifo.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 发送队列FIFO大小（必须是2的n次幂）
 * sizeof(MODBUS_CMD_PARAM) ≈ 40字节，8192 / 40 ≈ 204条指令缓冲
 * 20ms周期×2条=50条/秒，204条≈4秒缓冲，足够 */
#define SEND_FIFO_SIZE    8192

/* 发送线程间隔（微秒） */
#define SEND_THREAD_INTERVAL_US 100

/* 发送队列（环形FIFO，存储 MODBUS_CMD_PARAM 结构体） */
static KFIFO_ID g_stSendFifo = NULL;
static pthread_mutex_t g_stSendMutex = PTHREAD_MUTEX_INITIALIZER;

/* 发送线程 */
static pthread_t g_stSendThreadId = 0;
static bool g_bThreadRunning = false;
static bool g_bInitialized = false;

/**
 * @brief 发送线程函数：尽可能快地从队列取指令发送
 */
static void *ModbusSendThreadFunc(void *arg)
{
    (void)arg;
    LOG(LOG_INFO, "Modbus send thread started");

    while (g_bThreadRunning) {
        MODBUS_CMD_PARAM stCmd;
        bool hasCmd = false;

        /* 从发送队列取一个指令（非阻塞） */
        pthread_mutex_lock(&g_stSendMutex);
        if (g_stSendFifo != NULL &&
            kfifo_len(g_stSendFifo) >= sizeof(MODBUS_CMD_PARAM)) {

            if (kfifo_get(g_stSendFifo, (uint8_t *)&stCmd, sizeof(MODBUS_CMD_PARAM))
                == sizeof(MODBUS_CMD_PARAM)) {
                hasCmd = true;
            }
        }
        pthread_mutex_unlock(&g_stSendMutex);

        if (hasCmd) {
            /* 发送帧（内联存储，无需释放内存） */
            int ret = write(stCmd.i32Fd, stCmd.pu8RawFrame, stCmd.u16RawFrameLen);
            if (ret != (int)stCmd.u16RawFrameLen) {
                LOG(LOG_ERROR, "ModbusSendThread: write failed, "
                    "expected %d bytes, wrote %d", stCmd.u16RawFrameLen, ret);
            }
        }

        /* 固定10ms间隔发送（无论是否有指令，保证串口节奏稳定） */
        usleep(50000);
    }

    LOG(LOG_INFO, "Modbus send thread stopped");
    return NULL;
}

/**
 * @brief 初始化Modbus队列系统
 */
int ModbusQueue_Init(void)
{
    if (g_bInitialized) {
        return SUCCESS;
    }

    if (pthread_mutex_init(&g_stSendMutex, NULL) != 0) {
        LOG(LOG_ERROR, "ModbusQueue_Init: failed to init send mutex");
        return ERROR;
    }

    /* 创建发送队列（环形FIFO）
     * SEND_FIFO_SIZE = 1024 * sizeof(MODBUS_CMD_PARAM) ≈ 1024 * (4+1+1+32+2) ≈ 40KB
     * 可存储1024条待发送指令 */
    g_stSendFifo = kfifo_init(SEND_FIFO_SIZE);
    if (g_stSendFifo == NULL) {
        LOG(LOG_ERROR, "ModbusQueue_Init: failed to create send fifo");
        pthread_mutex_destroy(&g_stSendMutex);
        return ERROR;
    }

    /* 启动发送线程 */
    g_bThreadRunning = true;
    if (pthread_create(&g_stSendThreadId, NULL, ModbusSendThreadFunc, NULL) != 0) {
        LOG(LOG_ERROR, "ModbusQueue_Init: failed to create send thread");
        g_bThreadRunning = false;
        kfifo_free(g_stSendFifo);
        g_stSendFifo = NULL;
        pthread_mutex_destroy(&g_stSendMutex);
        return ERROR;
    }
    pthread_detach(g_stSendThreadId);

    g_bInitialized = true;
    LOG(LOG_INFO, "ModbusQueue initialized");
    return SUCCESS;
}

/**
 * @brief 销毁Modbus队列系统
 */
void ModbusQueue_Destroy(void)
{
    if (!g_bInitialized) {
        return;
    }

    /* 停止发送线程 */
    g_bThreadRunning = false;
    usleep(20000);

    /* 清空发送队列中剩余的命令 */
    pthread_mutex_lock(&g_stSendMutex);
    if (g_stSendFifo != NULL) {
        kfifo_clear(g_stSendFifo);
        kfifo_free(g_stSendFifo);
        g_stSendFifo = NULL;
    }
    pthread_mutex_unlock(&g_stSendMutex);

    pthread_mutex_destroy(&g_stSendMutex);
    g_bInitialized = false;

    LOG(LOG_INFO, "ModbusQueue destroyed");
}

/**
 * @brief 将Modbus命令添加到发送队列
 *
 * 调用方只管往队列里添加指令，由独立发送线程发送。
 * 帧数据内联存储（不malloc），队列中直接保存结构体副本。
 */
int ModbusQueue_AddCommand(MODBUS_CMD_PARAM *pParam)
{
    if (!g_bInitialized) {
        LOG(LOG_ERROR, "ModbusQueue_AddCommand: queue not initialized");
        return ERROR;
    }

    if (pParam == NULL) {
        LOG(LOG_ERROR, "ModbusQueue_AddCommand: null parameter");
        return ERROR;
    }

    if (pParam->i32Fd < 0) {
        LOG(LOG_ERROR, "ModbusQueue_AddCommand: invalid file descriptor %d", pParam->i32Fd);
        return ERROR;
    }

    /* 入队到发送FIFO（复制整个结构体，内联帧数据） */
    pthread_mutex_lock(&g_stSendMutex);

    if (g_stSendFifo == NULL) {
        LOG(LOG_ERROR, "ModbusQueue_AddCommand: send fifo not initialized");
        pthread_mutex_unlock(&g_stSendMutex);
        return ERROR;
    }

    /* 检查队列是否已满 */
    unsigned int freeSpace = kfifo_lenLeft(g_stSendFifo);
    if (freeSpace < sizeof(MODBUS_CMD_PARAM)) {
        LOG(LOG_WARNING, "ModbusQueue_AddCommand: send fifo full, command dropped! FD=%d",
            pParam->i32Fd);
        pthread_mutex_unlock(&g_stSendMutex);
        return ERROR;
    }

    unsigned int ret = kfifo_put(g_stSendFifo, (uint8_t *)pParam, sizeof(MODBUS_CMD_PARAM));
    pthread_mutex_unlock(&g_stSendMutex);

    if (ret != sizeof(MODBUS_CMD_PARAM)) {
        LOG(LOG_ERROR, "ModbusQueue_AddCommand: kfifo_put failed");
        return ERROR;
    }

    return SUCCESS;
}