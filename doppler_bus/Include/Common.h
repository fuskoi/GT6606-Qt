#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <linux/watchdog.h>
#include "ringfifo.h"

#define MAX_PATH 260
#define CONFIG_FILE_PATH "/doppler.conf"
#define RECEVIE_BUFFER_LENGTH 4096
#define PORT_NAME_LENGTH 64
#define MAX_UART_NUM 6
#define RINGFIFO_SIZE 16384
#define MAX_FILE_LEN 16384

#define SUCCESS 0
#define ERROR (-1)

#define PRINTERROR                                                                            \
    do                                                                                        \
    {                                                                                         \
        printf("ERROR POSITION FILE:%s LINE:%d FUN:%s \n", __FILE__, __LINE__, __FUNCTION__); \
    } while (0)

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;

typedef enum UartType
{
    COMMON_TYPE = 0,
    DTU_TYPE,
    SENSOR_TYPE,
    DAM5213_TYPE
} UartType;

struct TaskItem
{
    enum UartType type;
    struct tm capturetime;
};

struct ListManagerInfo
{
    pthread_mutex_t* mutex;
    int nItemSize;
    BYTE newPos;
    BYTE orgPos;
    int itemCount;
    int dataItemCount;
    void* pValueArray;
};

struct Uart_Param
{
    char hName[PORT_NAME_LENGTH];
    int Baudrate;
    int databits;
    int stopbits;
    int parity;
};

struct Uart_Info
{
    int i32fd;
    struct Uart_Param uartpara;
    enum UartType staticUartType;
    enum UartType uType;
    uint8_t receiveBuffer[RECEVIE_BUFFER_LENGTH];
    uint16_t recvdataLength;
    KFIFO_ID ringfifo;
    struct ListManagerInfo taskManager;
};

typedef struct DEVICE_LIST
{
    int HOST_COMPUTER;
    int Sensor;
    int Dam5213;
    struct Uart_Info UartList[MAX_UART_NUM];
} DEVICE_LIST;

typedef struct DEVICE_ADDR
{
    uint8_t u8FlowMeter;
    uint8_t u8FlowMeter2;
    uint8_t u8Dam5213;
} DEVICE_ADDR;

typedef struct MEASURE_CONFIG
{
    uint32_t u32CollectIntervalSec;
    uint32_t u32ReportIntervalSec;
    uint8_t u8Sl651Enable;
    uint8_t u8WebReportEnable;
    uint8_t u8SensorSimulate;
    uint32_t u32SensorTimeoutSec;
    /* 网页链路：0=DTU串口  1=Socket以太网（全功能互斥切换） */
    uint8_t u8WebLinkMode;
    char szSocketServerIP[64];
    uint16_t u16SocketServerPort;
} MEASURE_CONFIG;

#define WEB_LINK_MODE_DTU    0
#define WEB_LINK_MODE_SOCKET 1

extern DEVICE_LIST g_stDevicelist;
extern DEVICE_ADDR g_stDeviceAddr;
extern MEASURE_CONFIG g_stMeasureConfig;
extern BYTE g_aStationID[12];
extern BYTE g_RTUID[5];
extern BYTE g_CenterStationID;
extern uint16_t g_u16Password;
extern uint32_t g_u32CommansSeri;
extern pthread_mutex_t g_lock[MAX_UART_NUM];
extern int g_WatchDogFd;

bool GetAppDir(char* appDir);
void TrimLeft(char* pString);
void TrimRight(char* pString);
void TrimStr(char* pStr);
int str_to_bcd(const char* str, uint8_t* buffer);
bool ReadConfig(void);
/* 热加载 conf：更新业务参数，保留已打开串口 fd/ringfifo（供 SETCONFIG 用） */
bool ReloadConfigKeepUart(void);
int OpenUart(uint8_t u8PortSeri, int baudrate);
int SetUartProperties(int32_t fd, uint32_t baudrate);
int InitAllUart(void);
int InitUartTask(void);
int InitDevice(void);
int InitWatchDog(void);
void FeedWatchDog(void* arg);
int UartWrite(int port_fd, const unsigned char* data, int length);
/* 带口锁发送；tcdrain 在锁外，发前/发后把内核 RX 转入 ringfifo（禁止 TCIFLUSH） */
int UartWritePort(int portIndex, const unsigned char* data, int length);
int UartRead(int port_fd, unsigned char* buffer, int length);
int32_t StartDealTaskThread(struct Uart_Info* pUartInfo);
int32_t startUartReceiveThread(struct DEVICE_LIST* devicelist);
void uartRecvFun(void* pParam);
int UartReceiveDataProc(enum UartType StaticType, KFIFO_ID pData, WORD* pUsed);

#endif
