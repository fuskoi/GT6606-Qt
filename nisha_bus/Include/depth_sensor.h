#ifndef __DEPTH_SENSOR_H__
#define __DEPTH_SENSOR_H__
#include "uart.h"
// 地址0x09
typedef enum
{
    DEPTH_GET_ZERO_POINT_OFFSET,      // 获取零点偏移量
    DEPTH_GET_DEPTH_WTEMP_WPRE,
    SETOFFSET_DEPTH_GET_DEPTH_WTEMP_WPRE, // 获取深度、温度、压力
    DEPTH_SET_ZERO_POINT_OFFSET,          // 设置零点偏移量
    IS_DEPTH_SENSOR_ONLINE,               // 传感器是否在线
    // NS_DEFAULT_MODE_GET_DEPTH,   // 默认模式获取深度
    // NS_VMM_MODE_GET_DEPTH,       // VMM 模式获取深度
    NS_DEPTH_GET_DEPTH, // 泥沙系统获取深度

} uart_depth_task_name;

typedef enum
{
    DEPTH_TASK_SUBMIT = 0, // 提交深度传感器任务
    DEPTH_TASK_SUCCESS,    // 深度传感器任务成功
    DEPTH_TASK_TIMEOUT,    // 深度传感器任务超时
    DEPTH_TASK_IDLE,       // 深度传感器任务空闲
} depth_task_control_t;

typedef struct depth_sensor
{
    bool online;
    int depth;  // 毫米
    int offset; // 零点偏移量
    float temperature;
    int pressure;
    // int zero_point_offset;
    int depth_task_status;
    int offset_value; // 设置的零点偏移量值

} depth_sensor_info_t;

extern depth_sensor_info_t depth_sensor_info;

int get_depth_sensor_depth(void);

void set_depth_online(bool online);

bool is_depth_sensor_online(void);

int send_depth_sensor_get_depth_wtemp_wpre_command(int fd);

int send_depth_sensor_get_zero_offset_command(int fd);

int send_depth_sensor_set_zero_offset_command(int fd, int offset);

int DepthSensorSendCommand(uart_depth_task_name taskName, int fd, void *value);

int DepthSensorDataProcessCtrl(int taskName, uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc);

bool DealWithDepthSensorTimeOut(int taskName);

int depth_data_process(uint8_t *recvBuffer);

int SubmitDepthSensorTask(uart_depth_task_name taskName);

int set_depth_sensor_zero_point_offset(void *offset);

void set_offset_get_depth_sensor_depth(uint8_t *depth);

void set_offset_get_depth_sensor_depth_timeout(void);

void set_offset_get_depth_sensor_zero_point_offset(uint8_t *offset);

void set_offset_get_depth_sensor_zero_point_offset_timeout(void);

void CalibrationDepthSensor(void);

const char *get_dps_name(int name);

void get_depth_sensor_depth_timeout(void);

#endif // !
