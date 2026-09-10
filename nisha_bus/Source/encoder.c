#include "encoder.h"
encoder_info_t encoder = {.online = false,
                          .wheel_circumference = WHEEL_CIRCUM_METAL,
                          .resolution = COMMON_RESOLUTION,
                          .value = 0,
                          .distance = 0,
                          .isEncoderOnline = IsEncoderOnline};

void *IsEncoderOnline(bool *online)
{
    *online = encoder.online;
}
bool isEncoderOnline(void)
{
    return encoder.online;
}
void GetEncoderDistance(float *distance)
{
    *distance = encoder.distance;
}
void set_encoder_online(bool online)
{
    encoder.online = online;
}
// 01 06 00 A3 00 01 B8 28 改为无校验
int PassiveEncoderDataProcess(uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc)
{
    int ret = 0;
    ret = checkFunc(recvBuffer, recvddatalen, 0x01);
    if (ret == DATA_PROC_ERROR)
        return DATA_PROC_ERROR;
    if (ret == DATA_PROC_INCOMPLETE)
        return DATA_PROC_INCOMPLETE;

    return 0;
}

int send_encodercmd_getvalue(int fd)
{
    BYTE cmd[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
    write(fd, cmd, sizeof(cmd) / sizeof(cmd[0]));
    return 0;
}

int send_encodercmd_setcountingmode(int fd)
{
    // 01 06 00 46 00 01 a9 df - 逆时针+
    // 0x01, 0x06, 0x00, 0x46, 0x00, 0x00, 0x68, 0x1F - 顺时针+
    BYTE cmd[] = {0x01, 0x06, 0x00, 0x46, 0x00, 0x00, 0x68, 0x1F};
    write(fd, cmd, sizeof(cmd) / sizeof(cmd[0]));
    return 0;
}
// 设置255为零点
int send_encodercmd_clearvalue(int fd)
{
    // 01 10 00 4A 00 02 04 00 00 00 FF 37 A0
    BYTE cmd[] = {0x01, 0x10, 0x00, 0x4A, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0xFF, 0x37, 0xA0};
    write(fd, cmd, sizeof(cmd) / sizeof(cmd[0]));
    return 0;
}

int EncoderSendCmd(uart_encoder_task_name taskName, int comfd)
{
    switch (taskName)
    {
    case ENCODER_GETDATA:
    case ENCODER_HEARTBEAT:
        /* code */
        send_encodercmd_getvalue(comfd);
        break;
    case ENCODER_SETCOUNTINGMODE:
        /* code */
        send_encodercmd_setcountingmode(comfd);
        break;
    case ENCODER_CLEARDATA:
        send_encodercmd_clearvalue(comfd);
        break;
    default:
        break;
    }
    return 0;
}

int ActiveEncoderDataProcess(int taskName, uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc)
{
    int ret = 0;
    ret = checkFunc(recvBuffer, recvddatalen, 0x01);
    if (ret == DATA_PROC_ERROR)
        return DATA_PROC_ERROR;
    if (ret == DATA_PROC_INCOMPLETE)
        return DATA_PROC_INCOMPLETE;

    switch (taskName)
    {
    // case constant expression:
    //     /* code */
    //     break;
    case ENCODER_GETDATA:
        // 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B

        break;
    case ENCODER_SETCOUNTINGMODE:
        break;
    case ENCODER_HEARTBEAT:
        NS_sys_check_encoder();
        break;
    case ENCODER_CLEARDATA:
        break;
    default:
        break;
    }

    return ret;
}
bool DealWithEncoderTimeOut(int taskName)
{
    bool ret = true;
    switch (taskName)
    {
    case ENCODER_GETDATA:
        break;
    case ENCODER_HEARTBEAT:
        NS_sys_check_encoder_timeout();
        break;
    case ENCODER_CLEARDATA:
        break;
    default:
        break;
    }
    return ret;
}

int SubmitEncoderTask(int taskName)
{
    int index = get_device_index(ENCODER);
    if (index < 0 || index >= MAX_UART_NUM)
        return -3;
    bool online;
    if (!get_device_online_status(g_device_list, ENCODER, &online))
        return -2;
    if (!online)
    {
#ifdef DEBUG
        printf("ENCODER is offline\n");
#endif // DEBUG

        return -1;
    }
    task_item_t tempTask;
    tempTask.priority = 1;
    tempTask.sendTime = time(NULL);
    tempTask.taskName.encoder_task = taskName;
    tempTask.deviceName = ENCODER;
    EnTaskQueue_List(&g_uartWorkInfoArry[index].taskQueue, &tempTask);
    return 0;
}

void InitEncoderTask(void)
{
    SubmitEncoderTask(ENCODER_SETCOUNTINGMODE);
}

int process_encoder_data(BYTE *pBuffer)
{
    int wTemp = 0;
    // int wMax = 4194303;
    wTemp |= pBuffer[3];
    wTemp |= pBuffer[2] << 8;
    wTemp |= pBuffer[1] << 16;
    wTemp |= pBuffer[0] << 24;
    // print_data(4, pBuffer);
    // if (pBuffer[1] == 0x3F)
    // {
    //     wTemp = wTemp - wMax;
    // }
    // printf("wTemp = %d\n", wTemp);
    return wTemp;
}
