#ifndef COMMON_H
#define COMMON_H
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <signal.h>
#include <linux/watchdog.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <string.h>
#include "ringfifo.h"

// #include <asm-generic/siginfo.h>
#define MAX_PATH 260
#define CONFIG_FILE_PATH "/tyls.conf" // 配置文件路径
#define RECEVIE_BUFFER_LENGTH 1024
#define PORT_NAME_LENGTH 64
#define MAX_UART_NUM 6 // 最大串口个数
#define FISH_BODY_HIGHT 20
#define MAX_FILE_LEN 4096 // 最大配置文件长度

#define MODBUS_READ_CODE 0x03       // 读取数据
#define MODBUS_WRITE_CODE 0x06      // 写数据
#define MODBUS_READ_MULTI_CODE 0x0A // 批量读取数据

#define MAX_DATA_LEN 1024

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
#pragma pack(push, 1)

typedef struct
{
    uint8_t u8SlaveAddr;
    uint8_t u8FunctionCode;
    uint16_t u16StartAddr;
    uint16_t u16data;
    uint16_t u16CRC;
} MODBUS_FRAME;

typedef struct
{
    uint8_t u8SlaveAddr;
    uint8_t u8FunctionCode;
    uint8_t u8Datalen;
    uint8_t *u8pdata;
    uint16_t u16CRC;
} MODBUS_RECFRAME;

typedef enum UartType
{
    COMMON_TYPE = 0,
    DTU_TYPE,
    ZIGBEE_TYPE,
    DC_MOTOR_TYPE,
    ENCODER_TYPE,
    RELAY_TYPE,
    SENSOR_TYPE
} UartType;

struct TaskItem
{
    enum UartType type;
    struct tm capturetime;
};

struct ListManagerInfo //
{
    pthread_mutex_t *mutex;
    int nItemSize;
    BYTE newPos;
    BYTE orgPos;
    int itemCount;
    int dataItemCount;
    void *pValueArray;
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

typedef struct DEVICE_LIST // 设备列表，需要设备在里面添加
{
    int DC_Motor; // 直流电机
    int ZigBee;   //
    int DTU;      // 主机
    int Encoder;  // 编码器
    int Relay;    // 继电器
    int Sensor;   // 传感器
    // int Sediment;//泥沙
    struct Uart_Info UartList[MAX_UART_NUM];

} DEVICE_LIST;

typedef struct _DEVICE_ADDR // modbus设备地址列表,增加设备在里面添加
{
    uint8_t u8Motor_Drive_car;     // 小车电机驱动器
    uint8_t u8Motor_Drive_fish;    // 铅鱼绞盘驱动器
    uint8_t u8Encoder_car;         // 小车编码器
    uint8_t u8Encoder_fish;        // 铅鱼绞盘编码器
    uint8_t u8Relay_2_port_power;  // 控制小车继电器
    uint8_t u8Relay_2_port2_break; // 控制铅鱼继电器
    uint8_t u8Relay_8_port;        // 8口继电器
    uint8_t u8Rotator_addr;        // 转子流速仪
    uint8_t u8WaterLevel;          // 水位计
    uint8_t u8CoulombAddr;         // 库仑
    uint8_t u8TouchScreenAddr;     // 触摸屏
    uint8_t u8SedimentAddr;        // 泥沙
    uint8_t u8deepth_temp;         // 深度温度
} DEVICE_ADDR;

typedef struct _WATER_LEVEL_CONFIG
{
    uint16_t u16WaterBase; // 雷达水位计基值
    uint16_t u16WaterType;
} WATER_LEVEL_CONFIG;

typedef enum WATER_TYPE
{
    WATER_TYPE_RADAR = 0,     //  雷达水位计
    WATER_TYPE_PASSIVE = 0XFF // 被动式水位计

} WATER_TYPE;

typedef struct _DI_STATUS_FLAG
{

    uint8_t u8Top_limit_flag;           //到顶限位
    uint8_t u8Bottom_limit_flag;        //触底限位
    uint8_t u8Original_flag;            //水平原点限位
    uint8_t u8Farest_flag;              //水平最远限位
    uint8_t u8UP_WaterSurface_flag;     //上水面标志
    uint8_t u8Down_WaterSurface_flag;   //下水面标志

}DI_STATUS_FLAG;  //采集端子状态

typedef struct _MEASURETIME
{
    uint8_t u8Rotator;
} MEASURETIME;

#pragma pack(pop)
extern MEASURETIME g_stMeasureTime;
extern DEVICE_ADDR g_stDeviceAddr;

extern struct DEVICE_LIST g_stDevicelist;
extern BYTE g_aStationID[12];
extern BYTE g_CenterStationID;
extern BYTE g_RTUID[5];
extern pthread_mutex_t g_lock[MAX_UART_NUM];

extern int g_EncoderTimer;
extern int g_WatchDogFd;
extern uint64_t g_timeout_Intervel;
extern uint32_t g_u32CommansSeri;
extern uint8_t g_ReverseDirection ;

bool GetAppDir(char *appDir);
void TrimLeft(char *pString);
void TrimRight(char *pString);
void TrimStr(char *pStr);
// bool ReadConfig(void);
int OpenUart(uint8_t u8PortSeri, int baudrate);
// int SetUartProperties (int32_t fd, uint32_t baudrate);//
int InitAllUart();
int InitUartTask();

int InitDevice();
int InitMeasureConfig();
void GlobalInit();
// void FeedWatchDog(void *arg);
// int InitWatchDog();

#endif