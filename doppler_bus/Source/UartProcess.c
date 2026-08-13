#include "Common.h"
#include "TaskList.h"
#include "log.h"
#include "DopplerProtocol.h"
#include "Sensor.h"
#include "Dam5213.h"
#include "Command_proc.h"
#include "SocketLink.h"
#include <errno.h>
#include <termios.h>

int UartWrite(int port_fd, const unsigned char* data, int length)
{
    int written = write(port_fd, data, length);
    if (written == -1) {
        perror("write");
        return ERROR;
    }
    return written;
}

/* 内核 RX → ringfifo；禁止 TCIFLUSH。调用方已持口锁。 */
static void uart_drain_rx_locked(int portIndex, int fd, KFIFO_ID fifo, uint8_t* rxbuf)
{
    if (fifo == NULL || rxbuf == NULL || fd <= 0) {
        return;
    }
    for (;;) {
        int n = UartRead(fd, rxbuf, RECEVIE_BUFFER_LENGTH);
        if (n <= 0) {
            break;
        }
        {
            unsigned int putn = kfifo_put(fifo, rxbuf, (unsigned int)n);
            if (putn < (unsigned int)n) {
                LOG(LOG_WARNING,
                    "uart%d fifo overflow after tx drop=%u",
                    portIndex + 1,
                    (unsigned int)n - putn);
                break;
            }
        }
    }
}

/*
 * 带口锁发送。tcdrain 放在锁外：等待硬件发完期间收线程可继续抽 RX，
 * 减轻「正文对、CRC 尾错 / OTA 停包」。仍禁止 TCIFLUSH。
 */
int UartWritePort(int portIndex, const unsigned char* data, int length)
{
    int fd = 0;
    int written = 0;
    KFIFO_ID fifo = NULL;
    uint8_t* rxbuf = NULL;

    if (portIndex < 0 || portIndex >= MAX_UART_NUM || data == NULL || length <= 0) {
        return ERROR;
    }
    fd = g_stDevicelist.UartList[portIndex].i32fd;
    if (fd <= 0) {
        return ERROR;
    }

    fifo = g_stDevicelist.UartList[portIndex].ringfifo;
    rxbuf = g_stDevicelist.UartList[portIndex].receiveBuffer;

    pthread_mutex_lock(&g_lock[portIndex]);
    written = write(fd, data, (size_t)length);
    if (written > 0) {
        /* 先收走已进内核的下行，再放锁去 tcdrain */
        uart_drain_rx_locked(portIndex, fd, fifo, rxbuf);
    }
    pthread_mutex_unlock(&g_lock[portIndex]);

    if (written > 0) {
        (void)tcdrain(fd);
        pthread_mutex_lock(&g_lock[portIndex]);
        uart_drain_rx_locked(portIndex, fd, fifo, rxbuf);
        pthread_mutex_unlock(&g_lock[portIndex]);
    }

    if (written == -1) {
        perror("write");
        return ERROR;
    }
    return written;
}

int UartRead(int port_fd, unsigned char* buffer, int length)
{
    int bytes_read = read(port_fd, buffer, length);
    if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        perror("read");
        return ERROR;
    }
    return bytes_read;
}

int32_t StartDealTaskThread(struct Uart_Info* pUartInfo)
{
    pthread_t threadId;
    if (pthread_create(&threadId, NULL, (void*)DealTaskWork, pUartInfo) == SUCCESS) {
        pthread_detach(threadId);
        return SUCCESS;
    }
    return ERROR;
}

int32_t startUartReceiveThread(struct DEVICE_LIST* devicelist)
{
    pthread_t receiveId;
    if (pthread_create(&receiveId, NULL, (void*)uartRecvFun, devicelist) == SUCCESS) {
        pthread_detach(receiveId);
        return SUCCESS;
    }
    return ERROR;
}

void uartRecvFun(void* pParam)
{
    int i = 0;
    int iReadByte = 0;
    struct DEVICE_LIST* devicelist = (struct DEVICE_LIST*)pParam;

    while (1) {
        for (i = 0; i < MAX_UART_NUM; i++) {
            if (devicelist->UartList[i].i32fd <= 0) {
                continue;
            }
            /* 与 UartWritePort 互斥，发送时由 UartWritePort 代为排空内核 RX */
            pthread_mutex_lock(&g_lock[i]);
            iReadByte = UartRead(devicelist->UartList[i].i32fd,
                                 devicelist->UartList[i].receiveBuffer,
                                 RECEVIE_BUFFER_LENGTH);
            if (iReadByte > 0) {
                unsigned int putn = kfifo_put(devicelist->UartList[i].ringfifo,
                                             devicelist->UartList[i].receiveBuffer,
                                             (unsigned int)iReadByte);
                /* 分片 RAW 不打；拼好整帧后在 Command_proc [DOWNLINK] 打印 */
                if (putn < (unsigned int)iReadByte) {
                    LOG(LOG_WARNING,
                        "uart%d fifo overflow drop=%u remain=%u",
                        i + 1,
                        (unsigned int)iReadByte - putn,
                        kfifo_len(devicelist->UartList[i].ringfifo));
                }
            }
            pthread_mutex_unlock(&g_lock[i]);
        }
        usleep(1000);
    }
}

int UartReceiveDataProc(enum UartType staticType, KFIFO_ID pData, WORD* pUsed)
{
    switch (staticType) {
    case DTU_TYPE:
        /* Socket 全链路模式：网页走 TCP，串口 DTU 数据丢弃以免双通道抢解析 */
        if (WebLink_IsSocketMode()) {
            uint8_t dump[256];
            while (kfifo_len(pData) > 0U) {
                unsigned int n = kfifo_len(pData);
                if (n > sizeof(dump)) {
                    n = (unsigned int)sizeof(dump);
                }
                (void)kfifo_get(pData, dump, n);
            }
            return SUCCESS;
        }
        return Proc_DTU_RecData(pData, pUsed, DTU_TYPE);
    case SENSOR_TYPE:
        if (kfifo_len(pData) >= MODBUS_BATCH_RSP_LEN) {
            return SensorRecDataProc(pData, pUsed);
        }
        break;
    case DAM5213_TYPE:
        if (kfifo_len(pData) >= DAM5213_RSP_LEN) {
            return Dam5213_RecDataProc(pData, pUsed);
        }
        break;
    default:
        break;
    }
    return SUCCESS;
}
