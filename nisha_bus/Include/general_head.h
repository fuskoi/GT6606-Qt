#ifndef __GENERAL_HEAD_H__
#define __GENERAL_HEAD_H__
#include <stdio.h>
#include <stdbool.h>
#define DEBUG
// #define DEBUG_0
// #define DEBUG_1
// #define DEBUG_2
// #define DEBUG_3
// #define DEBUG_4
// #define DEBUG_5
// #define DEBUG_6
// #define DEBUG_7
// #define DEBUG_8

/* ENUM */
typedef enum // 用于区分串口设备的任务
{
    ACTIVETYPE = 1000, // 赋值给staticUartType表示这个串口主动执行任务
    PASSIVE_TYPE,
    ZIGBEE_DEV,
    WJ1000_DEV,
    END
} uart_task_type_t;

typedef struct uarttask
{
    int dc_motor_task;
    int relay_module_task;
    int depth_sensor_task;
    int sediment_sensor_task;
    int encoder_task;
} uart_task_name;

typedef enum
{
    UART_PASSIVE_LISTEN = 997, // 被动监听型（仅接收数据）
    UART_ACTIVE_CONTROL,       // 主动控制型（发送指令+接收响应）
    UART_SERVER_COMM,          // 服务器通信型（双向透传）
    UART_DEFAULT,
} uart_role_t;

typedef enum
{
    LEVEL_INDICATOR = 0,   // 水位计
    LOAD_CELL,             // 重量计
    METEOROLOGICAL_SENSOR, // 气象传感器
    SOIL_MOISTURE_SENSOR1, // 土壤湿度传感器PH
    SOIL_MOISTURE_SENSOR2, // 土壤湿度传感器TMP
    WIND_SENSOR,           // 风速计
    RELAY_MODULE,          // 继电器模块
    ENCODER,               // 编码器
    STEPMOTOR,             // 步进电机
    RADARVELOCITY,         // 雷达流速仪
    COULOMB_COUNTER,       // 库仑计
    DC_MOTOR,              // 直流电机
    SEDIMENT_SENSOR,       // 泥沙传感器
    DEPTH_SENSOR,          // 深度传感器
    ZIGBEE,
    DTU,
    LORA,
    MAX_DEVICES
} device_name_t;

typedef enum
{
    MODBUS_RTU = 1100,
    ZIGBEE_USR,
    DTU_651_USR,
    LORA_SCREEN,
} uart_protocol_t;

/* 宏函数 */
/**
 * @brief 	 【简介】输出该函数所在文件名函数名以及行数
 */
#define PRINTERROR                                                                            \
    do                                                                                        \
    {                                                                                         \
        printf("ERROR POSITION FILE:%s LINE:%d FUN:%s \n", __FILE__, __LINE__, __FUNCTION__); \
    } while (0)

/* 工程中的全局变量 */
extern int fdw;

#endif
