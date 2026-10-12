#include "depth_sensor.h"
depth_sensor_info_t depth_sensor_info = {
    .online = false,
    .depth = 0,
    .pressure = 0,
    .temperature = 0.0,
    .depth_task_status = DEPTH_TASK_IDLE,
};

int get_depth_sensor_depth(void)
{
    return depth_sensor_info.depth;
}
void set_depth_online(bool online)
{
    depth_sensor_info.online = online;
}

bool is_depth_sensor_online(void)
{
    return depth_sensor_info.online;
}
int send_depth_sensor_get_depth_wtemp_wpre_command(int fd)
{
    BYTE command[8] = {0x09, 0x03, 0x00, 0x01, 0x00, 0x05, 0xD5, 0x41};
    write(fd, command, 8);
    return 0;
}

int send_depth_sensor_get_zero_offset_command(int fd)
{
    BYTE command[8] = {0x09, 0x03, 0x00, 0x18, 0x00, 0x02, 0x45, 0x44};
    write(fd, command, 8);
    return 0;
}
int send_depth_sensor_set_zero_offset_command(int fd, int offset)
{
    printf("set depth sensor zero point offset:%d\n", offset);
    uint8_t command[13] = {0};
    uint8_t tempInfo[13] = {0x09, 0x10, 0x00, 0x18, 0x00, 0x02, 0x04};
    tempInfo[7] = (uint8_t)(offset >> 24) & 0xff;
    tempInfo[8] = (uint8_t)(offset >> 16) & 0xff;
    tempInfo[9] = (uint8_t)(offset >> 8) & 0xff;
    tempInfo[10] = (uint8_t)(offset) & 0xff;
    appendCRCtoBuffer(tempInfo, 11, command, 13);
    write(fd, command, 13);
    return 0;
}
int DepthSensorSendCommand(uart_depth_task_name taskName, int fd, void *value)
{
    switch (taskName)
    {
    case DEPTH_GET_DEPTH_WTEMP_WPRE:
    case IS_DEPTH_SENSOR_ONLINE:
    case NS_DEPTH_GET_DEPTH:
    case SETOFFSET_DEPTH_GET_DEPTH_WTEMP_WPRE:
        return send_depth_sensor_get_depth_wtemp_wpre_command(fd);
        break;
    case DEPTH_GET_ZERO_POINT_OFFSET:
        return send_depth_sensor_get_zero_offset_command(fd);
        break;
    case DEPTH_SET_ZERO_POINT_OFFSET:
        return send_depth_sensor_set_zero_offset_command(fd, depth_sensor_info.offset_value);
        break;
    default:
        break;
    }
    return 0;
}

int DepthSensorDataProcessCtrl(int taskName, uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc)
{
    int ret = 0;
    ret = checkFunc(recvBuffer, recvddatalen, 0x09);
    if (ret == DATA_PROC_ERROR)
        return DATA_PROC_ERROR;
    if (ret == DATA_PROC_INCOMPLETE)
        return DATA_PROC_INCOMPLETE;

    switch (taskName)
    {
    case SETOFFSET_DEPTH_GET_DEPTH_WTEMP_WPRE:
        set_offset_get_depth_sensor_depth(recvBuffer);
        break;
    case DEPTH_GET_DEPTH_WTEMP_WPRE:
        depth_sensor_info.depth = depth_data_process(recvBuffer + 3);

        break;
    case DEPTH_GET_ZERO_POINT_OFFSET:

        set_offset_get_depth_sensor_zero_point_offset(recvBuffer + 3);
        break;
    case DEPTH_SET_ZERO_POINT_OFFSET:
        ts_printf("[set success]\n");
        break;
    case IS_DEPTH_SENSOR_ONLINE:
        // ts_printf("depth:%d\n", depth_data_process(recvBuffer + 3));
        // print_data(recvddatalen, recvBuffer);
        NS_sys_check_depth_sensor();
        break;
    case NS_DEPTH_GET_DEPTH:
        depth_sensor_info.depth = depth_data_process(recvBuffer + 3);
        // printf("depth:%d\n", depth_sensor_info.depth);
        NiShaUD_depth_sensor_get_depth();
        break;

    default:
        break;
    }
    return ret;
}

bool DealWithDepthSensorTimeOut(int taskName)
{
    bool ret = true;
    switch (taskName)
    {
    case SETOFFSET_DEPTH_GET_DEPTH_WTEMP_WPRE:
        set_offset_get_depth_sensor_depth_timeout();
        break;
    case DEPTH_GET_DEPTH_WTEMP_WPRE:
        set_depth_online(false);
        break;
    case DEPTH_GET_ZERO_POINT_OFFSET:
        set_offset_get_depth_sensor_zero_point_offset_timeout();
        break;
    case DEPTH_SET_ZERO_POINT_OFFSET:
        break;
    case IS_DEPTH_SENSOR_ONLINE:
        NS_sys_check_depth_sensor_timeout();
        break;
    case NS_DEPTH_GET_DEPTH:
        NiShaUD_depth_sensor_get_depth_timeout();
        break;

    default:
        break;
    }
    return ret;
}

int depth_data_process(uint8_t *recvBuffer)
{
    int depth_data = recvBuffer[0] << 24 | recvBuffer[1] << 16 | recvBuffer[2] << 8 | recvBuffer[3];
    return depth_data;
}

int SubmitDepthSensorTask(uart_depth_task_name taskName)
{
    int index;

    index = get_device_index(DEPTH_SENSOR);
    if (index < 0 || index >= MAX_UART_NUM)
        return -3;
    // ts_printf("[depth sensor] submit task:%s\n", get_dps_name(taskName));
    task_item_t tempTask;
    tempTask.priority = 1;
    tempTask.sendTime = time(NULL);
    tempTask.taskName.depth_sensor_task = taskName;
    tempTask.deviceName = DEPTH_SENSOR;
    EnTaskQueue_List(&g_uartWorkInfoArry[index].taskQueue, &tempTask);
    return 0;
}

void set_offset_get_depth_sensor_depth(uint8_t *depth)
{
    depth_sensor_info.depth = depth_data_process(depth + 3);
    // printf("depth:%d\n", depth_sensor_info.depth);
    depth_sensor_info.depth_task_status = DEPTH_TASK_SUCCESS;
    set_depth_online(true);
}

void set_offset_get_depth_sensor_depth_timeout(void)
{
    depth_sensor_info.depth_task_status = DEPTH_TASK_TIMEOUT;
    set_depth_online(false);
}

void set_offset_get_depth_sensor_zero_point_offset(uint8_t *offset)
{
    if (offset == NULL)
        return;
    depth_sensor_info.offset = offset[0] << 24 | offset[1] << 16 | offset[2] << 8 | offset[3];
    depth_sensor_info.depth_task_status = DEPTH_TASK_SUCCESS;
    // printf("offset:%d\n", depth_sensor_info.offset);
    set_depth_online(true);
}

void set_offset_get_depth_sensor_zero_point_offset_timeout(void)
{
    depth_sensor_info.depth_task_status = DEPTH_TASK_TIMEOUT;
    set_depth_online(false);
}

void CalibrationDepthSensor(void)
{
    depth_sensor_info.depth_task_status = DEPTH_TASK_SUBMIT;
    // 1.查询当前深度
    SubmitDepthSensorTask(SETOFFSET_DEPTH_GET_DEPTH_WTEMP_WPRE);
    while (depth_sensor_info.depth_task_status == DEPTH_TASK_SUBMIT)
    {
        /* code */
        sleep_ms(1000);
    }

    depth_sensor_info.depth_task_status = DEPTH_TASK_SUBMIT;
    // 2.查询零点偏移量
    SubmitDepthSensorTask(DEPTH_GET_ZERO_POINT_OFFSET);
    while (depth_sensor_info.depth_task_status == DEPTH_TASK_SUBMIT)
    {
        /* code */
        sleep_ms(1000);
    }

    // 3.设置零点偏移量
    depth_sensor_info.offset_value = depth_sensor_info.depth + depth_sensor_info.offset;
    ts_printf("depth:%d, offset:%d, value:%d\n", depth_sensor_info.depth, depth_sensor_info.offset, depth_sensor_info.offset_value);
    SubmitDepthSensorTask(DEPTH_SET_ZERO_POINT_OFFSET);
}

const char *get_dps_name(int name)
{
    static const char *names[] = {
        [DEPTH_GET_DEPTH_WTEMP_WPRE] = "depth_get_depth_wtemp_wpre",
        [DEPTH_GET_ZERO_POINT_OFFSET] = "depth_get_zero_point_offset",
        [SETOFFSET_DEPTH_GET_DEPTH_WTEMP_WPRE] = "set_offset_get_depth_sensor_depth_wtemp_wpre",
        [DEPTH_SET_ZERO_POINT_OFFSET] = "depth_set_zero_point_offset",
        [IS_DEPTH_SENSOR_ONLINE] = "is_depth_sensor_online",
        [NS_DEPTH_GET_DEPTH] = "ns_depth_get_depth",
        // ...其他设备名称
    };
    return (name < MAX_DEVICES) ? names[name] : "Unknown";
}
