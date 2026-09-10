#include "SL651_2014.h"
#include "CRC.h"
#include "log.h"
#include "math.h"
#include "record_data.h"
#include "UplinkOffline.h"
#include "OtaUpgrade.h"
#include "SocketLink.h"

uint32_t g_u32CommansSeri = 0;

uint8_t g_RespondFlag = 0;
BYTE g_RTUID[5] = {0};
BYTE g_CenterStationID = 0;
BYTE g_aStationID[12];
M2UPFramePara g_stM2UpFramepara;
// ------------------------- 时间转换函数 -------------------------
void get_bcd_time(uint8_t *buffer)
{
    time_t now = time(NULL);
    if (now == (time_t)(-1))
    {
        // 处理时间获取失败的情况
        return;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *tm = localtime(&ts.tv_sec);
    if (tm == NULL)
    {
        // 处理时间转换失败的情况
        return;
    }

    // 确保缓冲区足够大
    if (buffer == NULL)
    {
        return;
    }

    // 将时间转换为BCD格式
    buffer[0] = (uint8_t)((tm->tm_year % 100) / 10) << 4 | (tm->tm_year % 10);   // YY
    buffer[1] = (uint8_t)((tm->tm_mon + 1) / 10) << 4 | ((tm->tm_mon + 1) % 10); // MM
    buffer[2] = (uint8_t)(tm->tm_mday / 10) << 4 | (tm->tm_mday % 10);           // DD
    buffer[3] = (uint8_t)(tm->tm_hour / 10) << 4 | (tm->tm_hour % 10);           // HH
    buffer[4] = (uint8_t)(tm->tm_min / 10) << 4 | (tm->tm_min % 10);             // mm
    buffer[5] = (uint8_t)(tm->tm_sec / 10) << 4 | (tm->tm_sec % 10);             // SS
}

// 示例函数：将浮点数转换为HEX编码的16位整数
uint16_t float_to_hex(float value, float scale)
{
    return (uint16_t)(value * scale);
}

unsigned char *float_to_bcd(float originalData, int datalen, int ndp)
{
    // 1. 计算缩放因子并四舍五入到整数
    double scale = pow(10, ndp);
    long long scaled_value = (long long)llround(originalData * scale);

    // 2. 计算需要的总位数并验证长度
    int total_digits = 2 * datalen; // 每个字节存储2位BCD码
    ts_printf("scaled_value = %lld\n", scaled_value);
    if (scaled_value < 0)
    {
        fprintf(stderr, "Error: Negative values not supported\n");
        return NULL;
    }

    // 3. 将整数转换为字符串并补零
    char buffer[total_digits + 1]; // +1 for null terminator
    snprintf(buffer, sizeof(buffer), "%0*lld", total_digits, scaled_value);
    // ts_printf("data:%s ", buffer);

    // 4. 转换为压缩BCD码
    unsigned char *bcd_array = (unsigned char *)malloc(datalen);
    int i = 0;
    // ts_printf("\nbcd_array=");
    for (i = 0; i < datalen; i++)
    {
        char high_nibble = buffer[2 * i] - '0';
        char low_nibble = buffer[2 * i + 1] - '0';
        bcd_array[i] = (high_nibble << 4) | low_nibble;
        // ts_printf("%d", bcd_array[i]);
    }
    // ts_printf("\n");
    return bcd_array;
}

void sl651_send_report_structure(void *arg, uint8_t *inputdata, int inputdatalen)
{
    pthread_mutex_lock(&g_lock[1]);
    uint8_t tempbuffer[1024];
    HexFrameHeader head;     // 651上行帧头
    ReportBodyHead bodyhead; // 正文结构通用帧头
    VoltageElement voltage;
    uint8_t end_marker;                                  // 结束符ETX/ETB
    uint16_t crc16;                                      // CRC16校验（大端序）
    M2UPFramePara stUpframepara = *(M2UPFramePara *)arg; // 上行参数
    int datalen = 0;

    head.frame_start = HEX_FRAME_START;
    head.center_addr = stUpframepara.center_addr;
    memcpy(head.telemetry_addr, stUpframepara.tele_addr, ADDR_BCD_LEN);
    stUpframepara.password = swap_u16(stUpframepara.password);
    memcpy(head.password, &stUpframepara.password, PASSWORD_HEX_LEN);

    head.func_code = FUNCTION_CODE_TIMED_REPORT;                                                           // 功能码 0x32
    head.msg_flag_len = (0x0000 << 12) | (sizeof(ReportBodyHead) + inputdatalen + sizeof(VoltageElement)); // 上行标识
    head.msg_flag_len = swap_u16(head.msg_flag_len);
    head.stx = STX;
    // LOG(LOG_DEBUG, "head.msg_flag_len is %04x", head.msg_flag_len);
    ts_printf("head.msg_flag_len is %04x\n", head.msg_flag_len);

    // 填充报帧头

    // 报正文
    if (g_u32CommansSeri > 65535)
        g_u32CommansSeri = 0;
    bodyhead.serial_num = ++g_u32CommansSeri;
    bodyhead.serial_num = swap_u16(bodyhead.serial_num);
    get_bcd_time(bodyhead.report_time);
    bodyhead.addr_identifier = 0xF1F1; // 地址标识符
    memcpy(&bodyhead.Stationid, &stUpframepara.tele_addr, ADDR_BCD_LEN);
    bodyhead.station_type = 0x48;
    bodyhead.obs_identifier = 0xF0F0;                   // 观测时间标识符
    memcpy(bodyhead.obs_time, bodyhead.report_time, 5); // 观测时间

    voltage.u8Element_identifier = POWER_VOLTAGE; // 0x38
    voltage.DataFmt.dataSize = 2;
    voltage.DataFmt.decimalPlaces = 2;
    unsigned char *tempvol = float_to_bcd((float)g_relay_module_volt, 2, 2); // g_relay_module_volt 继电器的电压
    memcpy(voltage.data, tempvol, 2);

    memcpy(tempbuffer, (uint8_t *)&head, sizeof(HexFrameHeader)); // 填充帧头
    datalen = sizeof(HexFrameHeader);

    memcpy(tempbuffer + datalen, (uint8_t *)&bodyhead, sizeof(ReportBodyHead)); // 填充小时报帧头
    datalen += sizeof(ReportBodyHead);
    memcpy(tempbuffer + datalen, inputdata, inputdatalen);
    datalen += inputdatalen;

    memcpy(tempbuffer + datalen, (uint8_t *)&voltage, sizeof(VoltageElement));
    datalen += sizeof(VoltageElement);

    end_marker = ETX;
    memcpy(tempbuffer + datalen, &end_marker, sizeof(uint8_t));
    datalen += sizeof(uint8_t); // 这一个字节是结束符

    // 计算并设置CRC校验
    crc16 = CRC16_Modbus(tempbuffer, datalen);
    // frame.crc16 = swap_u16(frame.crc16);
    memcpy(tempbuffer + datalen, (uint8_t *)&crc16, sizeof(uint16_t));
    datalen += sizeof(uint16_t);
    // LOG_ARRAY(LOG_DEBUG, tempbuffer, datalen, "%02x");
    //  RECORD_ARRAY(tempbuffer, datalen, "%02x");
    {
        int linkReady = WebLink_IsReady();
        int bytes_written = -1;

        if (OtaUpgrade_IsBusy()) {
            pthread_mutex_unlock(&g_lock[1]);
            return;
        }
        /* Socket 或 DTU 就绪才发；离线只影响是否落盘（定时报不落盘） */
        if (!linkReady) {
            pthread_mutex_unlock(&g_lock[1]);
            return;
        }
        bytes_written = WebLink_Send(tempbuffer, datalen);
        if (bytes_written != datalen) {
            LOG(LOG_DEBUG, "Error writing fixtime frame: %d/%d", bytes_written, datalen);
            log_dtu_frame("UP", "0x32-fail", tempbuffer, datalen);
        } else {
            log_dtu_frame("UP", WebLink_IsSocketMode() ? "0x32-socket" : "0x32",
                          tempbuffer, datalen);
        }
    }
    pthread_mutex_unlock(&g_lock[1]);
}

void sl651_send_fixtime_report(void *arg)
{
    // pthread_mutex_lock(&g_lock[g_stDevicelist.DTU]);
    assert(arg != NULL);

    unsigned char inputData[1024];
    uint8_t u8Element_identifier;                  // 要素识别码
    u8Element_identifier = SEDIMENT_CONCENTRATION; // 0x44
    DataFormat dataFmt;
    dataFmt.dataSize = 4;
    dataFmt.decimalPlaces = 3;
    unsigned char *temp = float_to_bcd((float)g_sediment_data_average, 4, 3); // g_sediment_data_average 平均泥沙量
    unsigned char sediment[4];

    memcpy(sediment, temp, 4);
    memcpy(inputData, &u8Element_identifier, sizeof(uint8_t));
    memcpy(inputData + sizeof(uint8_t), &dataFmt, sizeof(DataFormat));
    memcpy(inputData + sizeof(uint8_t) + sizeof(DataFormat), sediment, 4);
    sl651_send_report_structure(arg, inputData, sizeof(DataFormat) + sizeof(uint8_t) + 4);
    ts_printf("send fixtime report finished\n");
    // LOG(LOG_DEBUG, "send fixtime report");
    //  pthread_mutex_unlock(&g_lock[g_stDevicelist.DTU]);
}

////////////////////////////////////////////////////////////////
// 下面是均匀报

void sl651_send_hour_report_structure(void *arg, uint8_t *inputdata, int inputdatalen)
{
    pthread_mutex_lock(&g_lock[1]);
    uint8_t tempbuffer[1024];
    HexFrameHeader head;     // 651上行帧头
    ReportBodyHead bodyhead; // 正文结构通用帧头
    // VoltageElement voltage;
    uint8_t end_marker;                                  // 结束符ETX/ETB
    uint16_t crc16;                                      // CRC16校验（大端序）
    M2UPFramePara stUpframepara = *(M2UPFramePara *)arg; // 上行参数
    int datalen = 0;

    // 填充报文头
    // LOG(LOG_DEBUG,"build_hourly_report");
    head.frame_start = HEX_FRAME_START;
    head.center_addr = stUpframepara.center_addr;
    memcpy(head.telemetry_addr, stUpframepara.tele_addr, ADDR_BCD_LEN);
    stUpframepara.password = swap_u16(stUpframepara.password);
    memcpy(head.password, &stUpframepara.password, PASSWORD_HEX_LEN);

    head.func_code = FUNCTION_CODE_UNIFORM_TIME_HYDRO_INFO;                           // 功能码0x31
    head.msg_flag_len = (0x0000 << 12) | (sizeof(ReportBodyHead) + inputdatalen + 5); // 上行标识
    head.msg_flag_len = swap_u16(head.msg_flag_len);
    head.stx = STX;
    // LOG(LOG_DEBUG, "head.msg_flag_len is %04x", head.msg_flag_len);
    ts_printf("head.msg_flag_len is %04x\n", head.msg_flag_len);

    // 填充报帧头

    // 报正文
    if (g_u32CommansSeri > 65535)
        g_u32CommansSeri = 0;
    bodyhead.serial_num = ++g_u32CommansSeri;
    bodyhead.serial_num = swap_u16(bodyhead.serial_num);
    get_bcd_time(bodyhead.report_time);
    bodyhead.addr_identifier = 0xF1F1; // 地址标识符
    memcpy(&bodyhead.Stationid, &stUpframepara.tele_addr, ADDR_BCD_LEN);
    bodyhead.station_type = 0x48;
    bodyhead.obs_identifier = 0xF0F0;                   // 观测时间标识符
    memcpy(bodyhead.obs_time, bodyhead.report_time, 5); // 观测时间

    // voltage.u8Element_identifier = POWER_VOLTAGE;  //0x38
    // voltage.DataFmt.dataSize = 2;
    // voltage.DataFmt.decimalPlaces = 2;
    // unsigned char *tempvol = float_to_bcd((float)g_relay_module_volt, 2, 2);  //g_relay_module_volt 继电器的电压
    // memcpy(voltage.data, tempvol, 2);

    memcpy(tempbuffer, (uint8_t *)&head, sizeof(HexFrameHeader)); // 填充帧头
    datalen = sizeof(HexFrameHeader);

    memcpy(tempbuffer + datalen, (uint8_t *)&bodyhead, sizeof(ReportBodyHead)); // 填充均匀报帧头
    datalen += sizeof(ReportBodyHead);

    uint16_t observation_interval = swap_u16(0x0418);
    uint16_t temp = 0x0000;
    uint8_t interval = 0x05;
    memcpy(tempbuffer + datalen, (uint8_t *)&observation_interval, sizeof(uint16_t));
    datalen += sizeof(uint16_t);
    memcpy(tempbuffer + datalen, (uint8_t *)&temp, sizeof(uint16_t));
    datalen += sizeof(uint16_t);
    memcpy(tempbuffer + datalen, (uint8_t *)&interval, sizeof(uint8_t));
    datalen += sizeof(uint8_t);

    memcpy(tempbuffer + datalen, inputdata, inputdatalen);
    datalen += inputdatalen;

    // memcpy(tempbuffer + datalen, (uint8_t *)&voltage, sizeof(VoltageElement));
    // datalen += sizeof(VoltageElement);

    end_marker = ETX;
    memcpy(tempbuffer + datalen, &end_marker, sizeof(uint8_t));
    datalen += sizeof(uint8_t); // 这一个字节是结束符

    // 计算并设置CRC校验
    crc16 = CRC16_Modbus(tempbuffer, datalen);
    // frame.crc16 = swap_u16(frame.crc16);
    memcpy(tempbuffer + datalen, (uint8_t *)&crc16, sizeof(uint16_t));
    datalen += sizeof(uint16_t);
    // LOG_ARRAY(LOG_DEBUG, tempbuffer, datalen, "%02x");
    // RECORD_ARRAY(tempbuffer, datalen, "%02x");
    {
        int linkReady = WebLink_IsReady();
        int bytes_written = -1;

        if (OtaUpgrade_IsBusy()) {
            pthread_mutex_unlock(&g_lock[1]);
            return;
        }
        /* 无链路：只落盘。有链路：先发，离线或发送失败再落盘（与 Doppler 一致） */
        if (!linkReady) {
            (void)UplinkOffline_Enqueue(UPLINK_OFFLINE_KIND_SL651,
                                        FUNCTION_CODE_UNIFORM_TIME_HYDRO_INFO,
                                        tempbuffer, (uint16_t)datalen, DTU_TYPE);
            pthread_mutex_unlock(&g_lock[1]);
            return;
        }
        bytes_written = WebLink_Send(tempbuffer, datalen);
        if (bytes_written != datalen) {
            LOG(LOG_DEBUG, "Error writing hour frame: %d/%d", bytes_written, datalen);
            log_dtu_frame("UP", "0x31-fail", tempbuffer, datalen);
            (void)UplinkOffline_Enqueue(UPLINK_OFFLINE_KIND_SL651,
                                        FUNCTION_CODE_UNIFORM_TIME_HYDRO_INFO,
                                        tempbuffer, (uint16_t)datalen, DTU_TYPE);
        } else {
            log_dtu_frame("UP", WebLink_IsSocketMode() ? "0x31-socket" : "0x31",
                          tempbuffer, datalen);
            if (!UplinkOffline_IsOnline()) {
                (void)UplinkOffline_Enqueue(UPLINK_OFFLINE_KIND_SL651,
                                            FUNCTION_CODE_UNIFORM_TIME_HYDRO_INFO,
                                            tempbuffer, (uint16_t)datalen, DTU_TYPE);
            }
        }
    }
    pthread_mutex_unlock(&g_lock[1]);
}

// 均匀报0x31
void sl651_send_hour_report(void *arg)
{
    // pthread_mutex_lock(&g_lock[g_stDevicelist.DTU]);
    assert(arg != NULL);

    unsigned char inputData[1024];
    uint8_t u8Element_identifier;                  // 要素识别码
    u8Element_identifier = SEDIMENT_CONCENTRATION; // 0x44
    DataFormat dataFmt;
    dataFmt.dataSize = 5;
    dataFmt.decimalPlaces = 3;
    unsigned char *temp[12];
    unsigned char sediment[60];
    float buffer[12];
    int count = 0;
    batch_dequeue(&g_hour_float, buffer, &count);
    // if (count < 12)
    // {
    //     ts_printf("send buffer count < 12 and count = %d\n", count);
    //     return;
    // }
    ts_printf("batch_dequeue count = %d\n", count);

    for (int i = 0; i < 12; i++)
    {
        temp[i] = float_to_bcd((float)buffer[i], 5, 3); // buffer[i] 平均泥沙量
        // ts_printf("bcd= ");
        // for (int j = 0; j < 5; j++)
        // {
        //     ts_printf("%02x", temp[i][j]);
        // }
        if (temp[i] == NULL)
            return;
    }
    // ts_printf("\n");
    for (int i = 0; i < 12; i++)
    {
        memcpy(&sediment[i * 5], temp[i], 5);
    }
    memcpy(inputData, &u8Element_identifier, sizeof(uint8_t));
    memcpy(inputData + sizeof(uint8_t), &dataFmt, sizeof(DataFormat));
    memcpy(inputData + sizeof(uint8_t) + sizeof(DataFormat), sediment, 60);
    sl651_send_hour_report_structure(arg, inputData, sizeof(DataFormat) + sizeof(uint8_t) + 60);
    for (int i = 0; i < 12; i++)
        free(temp[i]);
    ts_printf("send hour report finished\n");
    // LOG(LOG_DEBUG, "send hour report");
    //  pthread_mutex_unlock(&g_lock[g_stDevicelist.DTU]);
}

/*
 *函数名称:Proc_DTU_RecData
 *函数功能:处理ZIGBEE接收数据
 *输   入:KFIFO_ID pData  存入到缓冲区中的数据
 *输   出:WORD *pUsed 已经使用的数据长度
 *返   回:SUCCESS - 成功     ERROR  - 错误
 */
void *web_control_up(uint8_t *recvBuffer)
{
    web_current_time_sec = time(NULL);
    NiShaUD_Stop_Running_Task();       // 停止正在执行的检测任务
    set_dc_motor_speed(600);           // 600转/分
    NiShaUD_Task_Mode_Manual_Up(NULL); // 上升

    return NULL;
}

void *web_control_down(uint8_t *recvBuffer)
{
    web_current_time_sec = time(NULL);
    NiShaUD_Stop_Running_Task();         // 停止正在执行的检测任务
    set_dc_motor_speed(600);             // 600转/分
    NiShaUD_Task_Mode_Manual_Down(NULL); // 下降

    return NULL;
}
extern nisha_task_info_t nisha_task_info;

void Web_control_down_Task(void *arg)
{
    if (SedimentUD_RUNNING == nisha_task_info.task_state /*|| SedimentUD_ERROR == nisha_task_info.task_state */ || SedimentUD_INITIALIZING == nisha_task_info.task_state)
    {
        ts_printf("[NISHAUD] Manual Mode Up Task is already running or in error state, cannot start again.\n");
        // 如果任务正在运行或处于错误状态，直接返回
        return;
    }

    nisha_task_info.sec_task_state = SEC_TASK_READY;
    nisha_task_info.task_state = SedimentUD_RUNNING;
    nisha_task_info.task_mode = SedimentUD_Mode_Manual;

    if (-1 == submitTask(g_threadPool, web_control_down, arg))
    {
        NiShaUD_clear_task();
    }
}
void Web_control_up_Task(void *arg)
{
    if (SedimentUD_RUNNING == nisha_task_info.task_state /*|| SedimentUD_ERROR == nisha_task_info.task_state */ || SedimentUD_INITIALIZING == nisha_task_info.task_state)
    {
        ts_printf("[NISHAUD] Manual Mode Up Task is already running or in error state, cannot start again.\n");
        // 如果任务正在运行或处于错误状态，直接返回
        return;
    }

    nisha_task_info.sec_task_state = SEC_TASK_READY;
    nisha_task_info.task_state = SedimentUD_RUNNING;
    nisha_task_info.task_mode = SedimentUD_Mode_Manual;

    if (-1 == submitTask(g_threadPool, web_control_up, arg))
    {
        NiShaUD_clear_task();
    }
}

void Web_control_stop_Task(void *arg)
{
    if (SedimentUD_RUNNING == nisha_task_info.task_state /*|| SedimentUD_ERROR == nisha_task_info.task_state */ || SedimentUD_INITIALIZING == nisha_task_info.task_state)
    {
        ts_printf("[NISHAUD] Manual Mode Up Task is already running or in error state, cannot start again.\n");
        // 如果任务正在运行或处于错误状态，直接返回
        return;
    }

    nisha_task_info.sec_task_state = SEC_TASK_READY;
    nisha_task_info.task_state = SedimentUD_RUNNING;
    nisha_task_info.task_mode = SedimentUD_Mode_Manual;

    if (-1 == submitTask(g_threadPool, NiShaUD_Task_Mode_Manual_Stop, arg))
    {
        NiShaUD_clear_task();
    }
}

void deal_with_DTU_data(uint8_t *recvBuffer, uint16_t recvLen)
{
    UplinkOffline_OnDownlink();
    if (OtaUpgrade_IsBusy()) {
        LOG(LOG_DEBUG, "ignore lift ctrl during ota");
        return;
    }
    log_dtu_frame("DOWN", "lift/ctrl", recvBuffer, (int)recvLen);
    if (recvBuffer[31] > 20) // 单次网页控制升降时间不能大于20秒
        return;

    switch (recvBuffer[27]) // 控制上下
    {
    case 0x1: // 上
        ts_printf("run time: %d\n", recvBuffer[31]);
        g_web_control_run_time = recvBuffer[31];
        Web_control_up_Task(recvBuffer);
        break;
    case 0x2: // 下
        ts_printf("run time: %d\n", recvBuffer[31]);
        g_web_control_run_time = recvBuffer[31];
        Web_control_down_Task(recvBuffer);
        break;
    case 0x3: // 停止
        g_web_control_run_time = 0;
        NiShaUD_Stop_Running_Task();
        NiShaUD_Manual_Mode_Stop_Task(NULL);
        break;
    default:
        break;
    }
}

int SL651_RecDataProc(uint8_t *recvBuffer, uint16_t recvLen, WORD *wUsed, PacketHandler handler)
{

    // 7E 7E 00 20 20 02 28 00 12 34 F1 80 15 02 01 B1 25 07 18 09 49 08 1E 04 00 00 00 02 00 00 00 02 00 00 00 03 29 4C  总共38个字节
    // 7e 7e 00 20 20 02 28 00 12 34 32 80 08 02 00 03 25 08 06 14 34 29 04 23 41 7e 7e 00 20 20 02 28 00 12 34 f1 80 15
    // 00 03 25 08 06 14 38 14 04 b0 91.7e 7e 00 20 20 02 28 00 12 34 32 80 08 02 00 03 25 08 06 14 38 14 04 b0 91
    // 7e 7e 00 20 20 02 28 00 12 34 32 80 08 02 00 03 25 08 06 14 38 14 04 b0 91

    *wUsed = 0;
    int datalen;

    for (int i = 0; i <= recvLen; i++)
    {
        // 找报头
        if (recvBuffer[i] == 0x7E && recvBuffer[i + 1] == 0x7E)
        {

            datalen = (uint16_t)((recvBuffer[i + 11] & 0x0f) << 8 | (recvBuffer[i + 12])) + 14 + 3; // 02 与 03 之间的数据长度，14是前面的长度13，再加1是起始字符0x02
            ts_printf("datalen=%d\n", datalen);
            if (datalen > 4200) // 非法长度（OTA 大包由 OtaUpgrade_RecDataProc 处理）
            {
                *wUsed = 1;
                return PACKET_NOT_FOUND;
            }

            uint16_t remaining = recvLen - i;
            if (remaining < datalen)
            {
                *wUsed = 0; /* 等更多数据，勿丢弃 */
                return PACKET_NOT_FOUND;
            }

            // 在报头后寻找 0xF1(功能码)与0x1E(升降) 0x03(报文终止)

            int j;
            // 从报头后开始找，数据 在 7E 7E 之间
            for (j = i + 2; j < recvLen - 2; j++)
            {
                // 找到7E 7E中间的数据 ，但是不是想要的
                if (recvBuffer[j] == 0x7E && recvBuffer[j + 1] == 0x7E)
                {
                    if (recvBuffer[i + 22] != 0x1E && (recvBuffer[i + 11] >> 4 != 0x8) && recvBuffer[i + 10] != 0xF1) // 说明报文前后7e 7e之间是垃圾报文
                    {
                        //ts_printf("j1=%d\n", j);
                        *wUsed = j; // 清空前面所有的报文
                        return PACKET_NOT_FOUND;
                    }
                    else
                    {
                        goto mark1; // 在报文前后7e 7e之间找到的服务器下发的控制命令
                    }
                }
                else // 找到了报头，但7e 7e不是结尾（7e 7e下一报文的开头），找到了报尾 03
                {
                mark1:
                    datalen = (uint16_t)((recvBuffer[i + 11] & 0x0f) << 8 | (recvBuffer[i + 12])) + 14; // 02 与 03 之间的数据长度，14是前面的长度13，再加1是起始字符0x02
                    if (recvBuffer[i + 10] == 0xF1 && recvBuffer[i + 22] == 0x1E && (recvBuffer[i + 11] >> 4 == 0x8) && recvBuffer[i + datalen] == 0x03)
                    {
                        int packetLength = datalen + 3; // 报文长度:datalen + 结尾3个： 0x03 + CRC
                        *wUsed = packetLength + i;

                        // 调用回调处理数据部分
                        if (handler)
                        {
                            handler(recvBuffer + i, packetLength);
                        }

                        return PACKET_OK;
                    }
                }
            }

            // 找到报头但未找到报尾，丢弃前面7E 7E所有报文
            //ts_printf("j2=%d\n", j + 2);
            *wUsed = recvLen;
            return PACKET_NOT_FOUND;
        }
    }

    // 没有找到任何有效的报头
    *wUsed = recvLen;
    return PACKET_NOT_FOUND;
}

void SetResponceFlag(uint8_t flag)
{
    pthread_mutex_lock(&g_lock[1]);
    g_RespondFlag = flag;
    pthread_mutex_unlock(&g_lock[1]);
}

void GetResponceFLag(uint8_t *flag)
{
    pthread_mutex_lock(&g_lock[1]);
    *flag = g_RespondFlag;
    pthread_mutex_unlock(&g_lock[1]);
    LOG(LOG_DEBUG, "GetResponceFLag is %d", *flag);
}

int SL651ContextProc(uint8_t *tempbuff, int dataLen)
{

    return SUCCESS;
}

// 将BCD字节转换为十进制
unsigned char bcd_to_dec(unsigned char bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/**
 * @brief 将字符串转换为BCD格式并存储在缓冲区中
 *
 * @param str 输入的字符串，长度必须是偶数
 * @param buffer 指向存储BCD码的缓冲区，大小应为 strlen(str) / 2
 * @return int 返回成功转换的字节数，若输入字符串长度不是偶数则返回 -1
 */
int str_to_bcd(const char *str, uint8_t *buffer)
{
    size_t len = strlen(str);

    // 确保字符串长度是偶数
    if (len % 2 != 0)
    {
        LOG(LOG_ERROR, "str_to_bcd: Invalid input string length");
        return ERROR;
    }

    // 遍历字符串，每两个字符一组
    size_t i;
    for (i = 0; i < len / 2; i++)
    {
        char high_nibble = str[i * 2];
        char low_nibble = str[i * 2 + 1];

        // 检查字符是否为有效的十六进制字符
        if ((high_nibble < '0' || high_nibble > '9') && (high_nibble < 'A' || high_nibble > 'F') && (high_nibble < 'a' || high_nibble > 'f'))
        {
            LOG(LOG_ERROR, "str_to_bcd: Invalid input string character");
            return ERROR;
        }
        if ((low_nibble < '0' || low_nibble > '9') && (low_nibble < 'A' || low_nibble > 'F') && (low_nibble < 'a' || low_nibble > 'f'))
        {
            LOG(LOG_ERROR, "str_to_bcd: Invalid input string character");
            return -ERROR;
        }

        // 转换字符为数值
        uint8_t high_val = (high_nibble >= '0' && high_nibble <= '9') ? (high_nibble - '0') : (10 + toupper(high_nibble) - 'A');
        uint8_t low_val = (low_nibble >= '0' && low_nibble <= '9') ? (low_nibble - '0') : (10 + toupper(low_nibble) - 'A');

        // 组合成BCD码
        buffer[i] = (high_val << 4) | low_val;
    }

    return len / 2;
}

// 设置系统时间
int set_system_time(const BCD_Time *bcd_time)
{
    // 1. 将BCD时间转换为struct tm
    struct tm timeinfo;
    memset(&timeinfo, 0, sizeof(struct tm));

    timeinfo.tm_year = bcd_to_dec(bcd_time->year) + 100; // 2000-2099年
    timeinfo.tm_mon = bcd_to_dec(bcd_time->month) - 1;   // 月份范围0-11
    timeinfo.tm_mday = bcd_to_dec(bcd_time->day);
    timeinfo.tm_hour = bcd_to_dec(bcd_time->hour);
    timeinfo.tm_min = bcd_to_dec(bcd_time->min);
    timeinfo.tm_sec = bcd_to_dec(bcd_time->sec);

    // 2. 转换为time_t
    time_t new_time = mktime(&timeinfo);
    if (new_time == -1)
    {
        perror("mktime failed");
        return ERROR;
    }

    // 3. 设置系统时间
    struct timeval tv;
    tv.tv_sec = new_time;
    tv.tv_usec = 0;

    if (settimeofday(&tv, NULL) != 0)
    {
        perror("settimeofday failed");
        return ERROR;
    }

    // 4. 同步硬件时钟（可选）
    if (stime(&new_time) != 0)
    {
        perror("stime failed");
        // 即使失败也继续
    }

    return SUCCESS;
}

int SendResultFrame(void *arg, uint8_t command)
{

    uint8_t tempbuffer[1024];

    HexFrameHeader head; // 651上行帧头
    uint16_t u16_Seir = 0;
    uint8_t sTtime[6];
    uint8_t end_marker;                                  // 结束符ETX/ETB
    uint16_t crc16;                                      // CRC16校验（大端序）
    M2UPFramePara stUpframepara = *(M2UPFramePara *)arg; // 上行参数
    int datalen = 0;
    head.frame_start = HEX_FRAME_START;
    head.center_addr = stUpframepara.center_addr;
    memcpy(head.telemetry_addr, stUpframepara.tele_addr, ADDR_BCD_LEN);
    stUpframepara.password = swap_u16(stUpframepara.password);
    memcpy(head.password, &stUpframepara.password, PASSWORD_HEX_LEN);

    head.func_code = 0XF2;                  // 功能码
    head.msg_flag_len = (0x0000 << 12) | 9; // 上行标识
    head.msg_flag_len = swap_u16(head.msg_flag_len);
    head.stx = STX;
    u16_Seir = ++g_u32CommansSeri;
    get_bcd_time(sTtime);

    memcpy(tempbuffer, (uint8_t *)&head, sizeof(HexFrameHeader)); // 填充帧头
    datalen = sizeof(HexFrameHeader);
    memcpy(tempbuffer + datalen, &u16_Seir, sizeof(int16_t));
    datalen += sizeof(int16_t);
    memcpy(tempbuffer + datalen, sTtime, sizeof(BCD_Time));
    datalen += sizeof(BCD_Time);

    memcpy(tempbuffer + datalen, &command, sizeof(uint8_t));
    datalen += sizeof(uint8_t);

    end_marker = ETX;
    memcpy(tempbuffer + datalen, &end_marker, sizeof(uint8_t));
    datalen += sizeof(uint8_t); // 这一个字节是结束符

    // 计算并设置CRC校验
    crc16 = CRC16_Modbus(tempbuffer, datalen);
    // frame.crc16 = swap_u16(frame.crc16);
    memcpy(tempbuffer + datalen, (uint8_t *)&crc16, sizeof(uint16_t));
    datalen += sizeof(uint16_t);
    LOG(LOG_DEBUG, "SL651 send result  data len is  %04x", head.msg_flag_len);
    LOG_ARRAY(LOG_DEBUG, tempbuffer, datalen, "%02x");
    RECORD_ARRAY(tempbuffer, datalen, "%02x");
    {
        int bytes_written = WebLink_Send(tempbuffer, datalen);
        if (bytes_written != datalen) {
            LOG(LOG_DEBUG, "Error writing frame: %d bytes written", bytes_written);
            return ERROR;
        }
    }
    return SUCCESS;
}
