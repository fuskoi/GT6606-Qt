#ifndef __UART_H__
#define __UART_H__

#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/epoll.h>
#include <ctype.h>

#include "general_head.h"
#include "data_proc.h"
#include "src_uthash.h"
#include "taskqueue.h"
#include "config.h"
#include "crc_modbus.h"
#include "task.h"
#include "relay_module.h"
#include "log.h"
#include "watch_dog.h"
#include "linklist.h"
#include "timertask.h"
#include "encoder.h"
#include "zigbee_com.h"
#include "dc_motor.h"
#include "depth_sensor.h"
#include "sediment_sensor.h"
//#include "protocol_651.h"
#include "SL651_2014.h"
#include "nishatask.h"
#include "lora_screen.h"
// #include "data_type.h"
// #include "config.h"


#include "hour_frame.h"  //小时报专用队列，只能存放12个报文数据，用于SL651协议，泥沙数据保存在这个队列里面

#define DEVICECOUNT 7
#define RECEIVE_BUFFER_LENGTH 4096
#define MAX_QUEUE_LOCK 6
#ifdef BOARD_GT6606
/* GT6606：COM1..6 → /dev/ttyAMA0..5（与 Doppler GT6606 一致） */
#define COM1_NAME "/dev/ttyAMA0"
#define COM2_NAME "/dev/ttyAMA1"
#define COM3_NAME "/dev/ttyAMA2"
#define COM4_NAME "/dev/ttyAMA3"
#define COM5_NAME "/dev/ttyAMA4"
#define COM6_NAME "/dev/ttyAMA5"
#else
#define COM1_NAME "/dev/ttyS1" // 串口1
#define COM2_NAME "/dev/ttyS2" // 串口2
#define COM3_NAME "/dev/ttyS3" // 串口3
#define COM4_NAME "/dev/ttyS4" // 串口4
#define COM5_NAME "/dev/ttyS5" // 串口5
#define COM6_NAME "/dev/ttyS6" // 串口6
#endif
#define HTSETRXFIFO 0xAA01
#define RECV_FIFOSIZE 14 // 串口接收缓冲区大小，默认4字节，可设置为1,4,8,14字节
#define SET_RECV_FIFOSIZE     \
    fifoSize = RECV_FIFOSIZE; \
    ioctl(com_fd, HTSETRXFIFO, fifoSize) // 设置串口接收缓冲区大小
#define MAX_DEVICES_PER_UART 3
    typedef int (*ModbusFuncPtr)(uint8_t *, uint16_t, WORD *);
// 串口结构
typedef struct
{
    int baudrate; // 波特率
    int databits; // 数据位
    int stopbits; // 停止位
    char parity;  // 奇偶校验位
} port_info_t, *pport_info_t;

typedef struct device_state
{
    /* data */
    uint16_t deviceName;
    bool is_online;
    // uint8_t retry_count;
} device_state_t;

// 控制串口设备的结构
typedef struct
{
    int protocol;
    int comfd; // 串口的文件描述符
    int comID; // 串口序号
    uint8_t recvBuffer[RECEIVE_BUFFER_LENGTH];
    uint16_t recvddatalen;
    uart_role_t role;
    uart_task_name taskName;
    int deviceName;
    list_queue_t taskQueue; // task队列
    bool bRunTask;          // 标记当前是否有任务正在执行
    time_t tsend_time;      // 任务发送时间
    void *value;            // 发送给串口设备的值(06功能码时要设置的值)
} uart_work_info_t, *puart_work_info_t;

typedef enum
{
    DATA_PROC_ERROR = -1,
    DATA_PROC_OK,
    DATA_PROC_INCOMPLETE,
} dataproc_return_t;

// 设备配置映射结构体
typedef struct
{
    const char *config_key;    // 配置项名称
    int *config_var;           // 对应的全局变量
    device_name_t device_name; // 设备枚举类型
} DeviceConfigMapping;

typedef struct
{
    /* data */
    int level_indicator;
    int load_cell;
    int meteorological_sensor;
    int soil_moisture_sensorA;
    int soil_moisture_sensorB;
    int wind_sensor;
    int communicate;
    int relay_module;
    int encoder;
    int radar_velocity;
    int coulomb_counter;
    int step_motor_4aix;
    int zigbee;
    int dc_motor;
    int depth_sensor;
    int sediment_sensor;
    int dtu;
    int lora;
} DeviceIndex;

extern port_info_t g_portinfos;
extern uart_work_info_t g_uartWorkInfoArry[6];
extern pthread_mutex_t g_lock[MAX_QUEUE_LOCK];

// extern int g_level_indicator;
// extern int g_load_cell;
// extern int g_meteorological_sensor;
// extern int g_soil_moisture_sensorA;
// extern int g_soil_moisture_sensorB;
// extern int g_wind_sensor;
// extern int g_communicate;
// extern int g_relay_module;
extern Node *g_device_list;
/**
 * @brief 	 【简介】打开串口 并获得串口的默认属性
 * @param 	 devpath [in/out], 串口设备的路径
 * @param 	 old_cfg [in/out], 获取串口的默认属性
 * @return 	 【解释返回值】 成功返回串口设备的文件描述符 失败返回-1
 */
int get_device_port_fd(int device_name);
int uart_init(const char *devpath, struct termios *old_cfg);
/**
 * @brief 	 【简介】设置串口的属性
 * @param 	 com_fd [in/out], 想要进行设置的串口的文件描述符
 * @param 	 cfg [in/out], 用于配置串口属性的结构体，把串口属性信息传入该函数
 * @return 	 【解释返回值】 成功返回0 失败返回-1
 */
int uart_cfg_set(int com_fd, const pport_info_t cfg);
/**
 * @brief 	 【简介】向串口发送数据
 * @param 	 com_fd [in/out], 用文件描述符指定串口
 * @param 	 data [in/out], 想要发送的数据
 * @param 	 len [in/out], 想要发送的数据的长度
 * @return 	 【解释返回值】 成功返回的长度 失败返回-1
 */
int uart_send(int com_fd, const void *data, int len);
/**
 * @brief 	 【简介】从串口中接收数据
 * @param 	 com_fd [in/out], 用文件描述符指定串口
 * @param 	 buf [in/out], 用来保存接收到的数据
 * @param 	 len [in/out], buf的长度
 * @return 	 【解释返回值】 成功返回接收的长度 失败返回-1
 */
int uart_recv(int com_fd, void *buf, int len);
/**
 * @brief 	 【简介】以16进制输出BYTE数组中的数据
 * @param 	 size [in/out], 数组大小
 * @param 	 data [in/out], 要输出的数组
 */
void print_data(int size, const BYTE *data);
/**
 * @brief 	 DTU 上下行十六进制日志（控制台 + log）
 * @param 	 dir  "UP" / "DOWN"
 * @param 	 tag  如 "0x32" / "0x31" / "raw" / "ota"
 */
void log_dtu_frame(const char *dir, const char *tag, const uint8_t *data, int len);
/**
 * @brief 	 【简介】以16进制输出char数组中的数据
 * @param 	 size [in/out], 数组大小
 * @param 	 data [in/out], 要输出的数组
 */
void print_cdata(int size, const char *data);
/**
 * @brief 	 【简介】打开串口设备，并对串口属性进行配置
 * @param 	 devpath [in/out], 串口设备的路径
 * @param 	 portinfo [in/out], 配置串口属性的结构体，将想要配置的属性传入函数 进行配置
 * @return 	 【解释返回值】 成功返回0 失败返回-1
 */
int SetUartProperties(const char *devpath, port_info_t portinfo);
/**
 * @brief 	 【简介】向串口发送指令 并等待接收响应 有超时重传机制 等待5秒 重复3次
 * @param 	 com_fd [in/out], 串口的文件描述符
 * @param 	 command [in/out], 想要发送的指令
 * @param 	 response [in/out], 接收响应的数组
 * @param 	 len [in/out], 接收响应的数组的长度
 * @return 	 【解释返回值】成功返回0 失败返回-1
 */
int GetDataFromUart(int com_fd, const BYTE *command, BYTE *response, int len);

bool ExecTaskByUart(device_name_t deviceName, uart_task_name taskName, uint32_t comfd, void *value);

int ProcUartData(uart_task_name taskName, int deviceName, uint8_t *recvBuffer, uint16_t recvddatalen);

/**
 * @brief 	 【简介】根据配置文件初始化连接设备的串口(配置串口属性、打开串口、创建任务数组、初始化每个串口的锁资源、初始化串口的任务队列、开启线程(串口工作函数))
 * @return 	 【解释返回值】成功返回0 失败返回-1
 */
int InitUartDeviceFromConfig(void);
/**
 * @brief 	 【简介】串口功能函数; 根据串口设备类型不同执行不同功能：1.被动接收数据 2.主动发送数据再接收响应
 * @param 	 arg [in], 该函数工作时所需要的所有资源：uartWorkInfo_t类型的结构体
 * @return 	 【解释返回值】无返回值
 */
void *UartWorker(void *arg);
/**
 * @brief
 *
 * @param pUartWorkInfo
 */
void ActiveSendSerialPort(uart_work_info_t *pUartWorkInfo);
/**
 * @brief
 *
 * @param pUartWorkInfo
 */
void PassiveReceiveSerialPort(uart_work_info_t *pUartWorkInfo);
/**
 * @brief 	 【简介】将实现串口功能的函数加入到线程中执行
 * @param 	 pUartWorkInfo [in], 串口功能函数工作需要的串口资源集合的地址
 * @return 	 【解释返回值】成功返回0 系统资源不足返回-11
 */
int StartUartWork(puart_work_info_t pUartWorkInfo);

/**
 * @brief 转义配置文件中的内容，使程序按照配置文件来对某些参数进行配置或改变运行逻辑
 *
 * @return int
 */
int EscapeConfig(void);

void ReStart(void);
/**
 * @brief 检查设备是否在线
 *
 * @param deviceName
 * @return true
 * @return false
 */
bool CheckDeviceOnline(uint8_t deviceName);

int CheckModbusResData(BYTE *pData, int nLength, BYTE nDeviceSite);

void clear_line();

void move_cursor_to_first_line();

void move_cursor_to_second_line();

void clear_current_line();

void UpdateConfigValue(const char *key, int *value_ptr, myhash_t *(*find)(char *), int subtract_one);

void SetConfigParameters(void);


int ProcUartData_Passive(int protocol, uint8_t *recvBuffer, uint16_t recvddatalen, WORD *wUsed);

void SetUartProtocol(void);

void print_device(void *data);

void change_device_status(void *data, device_name_t deviceName);

const char *get_device_name(device_name_t name);

bool update_device_status(Node *head, device_name_t target_name, bool new_status);

bool get_device_online_status(const Node *head, device_name_t target, bool *out_online);

void DealWithTimeOutTask(int deviceName, uart_task_name taskName);

// char get_device_index(device_name_t deviceName);

void DealUartTaskTimeOut(uart_work_info_t *pUartWorkInfo, uint8_t *retry_count, time_t time_interval);

void ts_printf(const char *format, ...);

int IsDtDataPacketComplete(uint8_t *packet, uint16_t packet_size);

int get_device_index(device_name_t deviceName);

#endif
