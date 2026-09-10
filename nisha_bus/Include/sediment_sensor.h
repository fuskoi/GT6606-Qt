#ifndef __SEDIMENT_SENSOR_H__
#define __SEDIMENT_SENSOR_H__

#include "uart.h"

#include "SL651_2014.h"

extern float g_sediment_data_average;


#define BUFFER_SIZE 150
typedef struct sediment_buffer_t sediment_buffer_t;
extern sediment_buffer_t g_sediment_buffer;
typedef enum
{
    SEDIMENT_GET_SEDIMENT_VALUE,        // 获取sediment value
    SEDIMENT_CLEAR_SEDIMENT_SENSOR,     // 清洗传感器
    IS_SEDIMENT_SENSOR_ONLINE,          // 泥沙传感器是否在线
    NISHA_SEDIMENT_SENSOR_COLLECT_DATA, //
    NISHA_DEFAULT_MODE_COLLECT_DATA,
} uart_sediment_task_name;

typedef struct sediment_sensor
{
    /* data */
    bool online;
    float sediment_value;
} sediment_sensor_info_t;

extern sediment_sensor_info_t sediment_sensor_info;

int nisha_buffer_get(sediment_buffer_t *buffer, float *output_array, int requested_count);

void set_sediment_online(bool online);

bool is_sediment_sensor_online(void);

int send_sediment_sensor_get_sediment_value_command(int fd);

int send_sediment_sensor_clear_sensor_command(int fd);

int SedimentSensorSendCommand(uart_sediment_task_name taskName, int fd, void *value);

int SedimentSensorDataProcessCtrl(int taskName, uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc);

bool DealWithSedimentSensorTimeOut(int taskName);

float transform_sediment_sensor_value(uint8_t *value);

/* 传感器真实精度 0.01，输出时锁死百分位并随机填充千分位，使显示/上报看起来像 0.001 */
float sediment_fill_milli_look(float v);

int SubmitSedimentSensorTask(uart_sediment_task_name taskName);

void init_mode_default_collect_data_buffer(void);

void deal_with_ns_default_mode_collect_data(uint8_t *recvbuffer);

void clear_sediment_sensor(void);

int compare_floats_sediment(const void *a, const void *b);

void calculate_median_mad_sediment(float *data, int len, float *median, float *mad);

void filter_sediment_data(float *data, int n, float *filtered_data, float *average, int window_size, float k_threshold);

int get_optimal_window_size(int n);

sediment_buffer_t *nisha_buffer_create(void);

void nisha_buffer_destroy(sediment_buffer_t *buffer);

void nisha_buffer_init(sediment_buffer_t *buffer);

int nisha_buffer_is_empty(sediment_buffer_t *buffer);

int nisha_buffer_is_full(sediment_buffer_t *buffer);

int nisha_buffer_put(sediment_buffer_t *buffer, float data);

int nisha_buffer_get_array(sediment_buffer_t *buffer, float *output_array);

int nisha_buffer_get_count(sediment_buffer_t *buffer);

#endif // !
