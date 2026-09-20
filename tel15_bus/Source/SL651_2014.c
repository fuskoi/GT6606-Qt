#include "SL651_2014.h"
#include "CRC.h"
#include "zigbee_dataproc.h"
#include "timer_manager.h"
#include "Sensor.h"
#include "WebSeriver.h"
#include "log.h"
#include "UplinkOffline.h"
#include "OtaUpgrade.h"


M2UPFramePara g_stM2UpFramepara;
// ------------------------- 时间转换函数 -------------------------
void get_bcd_time(uint8_t *buffer) 
{
    time_t now = time(NULL);
    if (now == (time_t)(-1)) {
        return;
    }

    struct tm *tm = localtime(&now);   // 使用 localtime 获取本地时间（北京时间）
    if (tm == NULL) {
        return;
    }

    if (buffer == NULL) {
        return;
    }

    // 转换为 BCD 码（注意：localtime 返回的 tm_year 是从 1900 年开始的）
    buffer[0] = (uint8_t)((tm->tm_year % 100) / 10) << 4 | (tm->tm_year % 10);  // YY
    buffer[1] = (uint8_t)((tm->tm_mon + 1) / 10) << 4 | ((tm->tm_mon + 1) % 10); // MM
    buffer[2] = (uint8_t)(tm->tm_mday / 10) << 4 | (tm->tm_mday % 10);          // DD
    buffer[3] = (uint8_t)(tm->tm_hour / 10) << 4 | (tm->tm_hour % 10);          // HH
    buffer[4] = (uint8_t)(tm->tm_min / 10) << 4 | (tm->tm_min % 10);            // mm
    buffer[5] = (uint8_t)(tm->tm_sec / 10) << 4 | (tm->tm_sec % 10);            // SS
}


// 示例函数：将浮点数转换为HEX编码的16位整数
uint16_t float_to_hex(float value, float scale) {
    return (uint16_t)(value * scale);
}
// ------------------------- 构造测试报 -------------------------
void build_test_report(void *arg) 
{
    pthread_mutex_lock(&g_lock[g_stDevicelist.ZigBee]);
    LOG(LOG_DEBUG,"build_test_report");
    assert(arg != NULL);
    M2UpTestFrame frame;//测试报报文
    M2UPFramePara stUpframepara =*(M2UPFramePara *)arg;//上行参数
    size_t data_len = sizeof(HexFrameHeader) + sizeof(ReportBodyHead) + (sizeof(uint8_t)   + sizeof(DataFormat)  + sizeof(uint16_t)) * 2;
    

     // 填充报文头
    frame.header.frame_start = HEX_FRAME_START;
    frame.header.center_addr = stUpframepara.center_addr;
    memcpy(frame.header.telemetry_addr, stUpframepara.tele_addr, ADDR_BCD_LEN);
    memcpy(frame.header.password, &stUpframepara.password, PASSWORD_HEX_LEN);
    frame.header.func_code = FUNCTION_CODE_TEST;  // 测试报功能码
    frame.header.msg_flag_len = (0x0000 << 12) | (data_len - sizeof(HexFrameHeader)) ; // 上行标识
    frame.header.stx = STX;//报文起始

    // 填充正文
    frame.test_report.report_head. serial_num = g_u32CommansSeri++;//报文序号
    get_bcd_time(frame.test_report.report_head.report_time);
    frame.test_report.report_head.addr_identifier = 0xF1F1;        // 地址标识符
    memcpy(frame.test_report.report_head.Stationid, stUpframepara.tele_addr, ADDR_BCD_LEN);  
    frame.test_report.report_head.station_type = 0x48;           // 河道站分类码
    frame.test_report.report_head.obs_identifier = 0xF0F0;//观测时间标识符
    memcpy(frame.test_report.report_head.obs_time,frame.test_report.report_head.report_time,5);        // 观测时间
    
 	//要素信息
    frame.test_report.voltage.u8Element_identifier = POWER_VOLTAGE;//电压标识符
    frame.test_report.voltage.DataFmt.dataSize = 2;
    frame.test_report.voltage.DataFmt.decimalPlaces = 2;
    frame.test_report.voltage .data.u16Voltage=g_stReportState.u16CarVol / 10; 

    frame.test_report.water_level.u8Element_identifier = INSTANT_RIVER_LEVEL;//水位标识符
    frame.test_report.water_level.DataFmt.dataSize = 4;
    frame.test_report.water_level.DataFmt.decimalPlaces = 2;

    frame.test_report.water_level.data.u16waterlevel = g_u32waterLevel;   
    

    // 计算CRC（注意：需计算从帧头到正文结束的所有字节）
    
    
    frame.end_marker = ETX;  // 无后续报文
    frame.crc16 = CRC16_Modbus((uint8_t*)&frame, data_len + 1);
    //frame.crc16 = swap_u16(frame.crc16);
    // 写入数据并检查结果
    ssize_t bytes_written = write(stUpframepara.fd, (uint8_t *)&frame, data_len + 3);
    if (bytes_written != data_len + 3)
    {
        LOG(LOG_DEBUG, "Error writing frame: %zd bytes written", bytes_written);
    }
    pthread_mutex_unlock(&g_lock[g_stDevicelist.ZigBee]);
}
//构建小时报
void build_hourly_report(void * arg) 
{
    pthread_mutex_lock(&g_lock[g_stDevicelist.ZigBee]);
    uint8_t tempbuffer[1024];
    assert(arg != NULL);
    // 小时报帧和上行参数结构体
    M2UpHourlyFrame frame;//小时报报文
    M2UPFramePara stUpframepara =*(M2UPFramePara *)arg;//上行参数
    
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    // 如果当前时间的分钟或秒不为0，解锁并返回，因为小时报应在整点发送
    if (tm->tm_min != 0x00 || tm->tm_sec != 0x00)
    {
        pthread_mutex_unlock(&g_lock[g_stDevicelist.ZigBee]);
        return ;
    }
    // 填充报文头
    LOG(LOG_DEBUG,"build_hourly_report");
    size_t data_len  = sizeof(HourlyReportBody )+ sizeof(HexFrameHeader);
    frame.header.frame_start = HEX_FRAME_START;
    frame.header.center_addr = stUpframepara.center_addr;
    memcpy(frame.header.telemetry_addr, stUpframepara.tele_addr, ADDR_BCD_LEN);
    memcpy(frame.header.password, &stUpframepara.password, PASSWORD_HEX_LEN);
    frame.header.func_code = FUNCTION_CODE_HOURLY_REPORT;  // 功能码
    frame.header.msg_flag_len = (0x0000 << 12) | ((data_len) - sizeof(HexFrameHeader)) ; // 上行标识
    frame.header.stx = STX;

    //小时报正文
    frame.hourly_report.report_head.serial_num = g_u32CommansSeri++;
    
    get_bcd_time(frame.hourly_report.report_head.report_time);
    frame.hourly_report.report_head.addr_identifier = 0xF1F1;// 地址标识符
    frame.hourly_report.report_head.station_type = 0x48;
	frame.hourly_report.report_head.obs_identifier = 0xF0F0;//观测时间标识符
    memcpy(frame.hourly_report.report_head.obs_time,frame.hourly_report.report_head.report_time,5);        // 观测时间
    frame.hourly_report.waterlevel_identifier = INSTANT_RIVER_LEVEL;
    // 填充过去一小时内的水位信息
    int i = 0;
    for (i = 0; i < 12; i++) 
    {
        frame.hourly_report.water_level_5min[i] = g_u16WaterLevelArray[i]; // 12个水位信息值
    }
    // 电源电压信息
    frame.hourly_report.voltage.u8Element_identifier = POWER_VOLTAGE;
    frame.hourly_report.voltage.DataFmt.dataSize = 2;
    frame.hourly_report.voltage.DataFmt.decimalPlaces = 2;
    frame.hourly_report.voltage .data.u16Voltage= swap_u16( g_stReportState.u16CarVol / 10);

    // 设置结束符
    frame.end_marker = ETX;
    // 计算并设置CRC校验
    frame.crc16 = CRC16_Modbus((uint8_t*)&frame, data_len + 1);
    //frame.crc16 = swap_u16(frame.crc16);
    memcpy(tempbuffer,(uint8_t *)&frame,data_len + 3);
    LOG_ARRAY(LOG_DEBUG,tempbuffer,data_len + 3,"%02x");
    ssize_t bytes_written = write(stUpframepara.fd, tempbuffer, data_len + 3);
    if (bytes_written != (ssize_t)(data_len + 3)) {
        LOG(LOG_DEBUG, "Error writing frame: %zd bytes written", bytes_written);
    }
    /* 离线或本地发送失败：小时报落盘，恢复后补发 */
    if (bytes_written != (ssize_t)(data_len + 3) || !UplinkOffline_IsOnline()) {
        (void)UplinkOffline_Enqueue(UPLINK_OFFLINE_KIND_SL651,
                                    (uint32_t)FUNCTION_CODE_HOURLY_REPORT,
                                    tempbuffer, (uint16_t)(data_len + 3), ZIGBEE_TYPE);
    }
    pthread_mutex_unlock(&g_lock[g_stDevicelist.ZigBee]);
}



/**
 * 构建定时上报数据包
 * 
 * 本函数用于构建并发送定时上报的数据帧。它在特定的时间点（每5分钟的整点）生成数据报告，
 * 并通过TCP连接发送到中心地址。函数首先检查当前时间是否满足定时上报的条件，如果满足则构建数据帧，
 * 计算CRC校验码，并发送数据。
 * 
 * @param arg 一个指向M2UPFramePara结构体的指针，包含构建数据帧所需的参数。
 * 
 * 注意：函数假设g_lock数组和g_stDevicelist结构体已在外部定义并初始化。
 */

void build_fixtime_report(void * arg)
{
    pthread_mutex_lock(&g_lock[g_stDevicelist.ZigBee]);
    assert(arg != NULL);
    M2UpFixtimeFrame frame;
    M2UPFramePara stUpframepara =*(M2UPFramePara *)arg;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    uint8_t buffer[1024] = {0};
    memset(&frame,0,sizeof(M2UpFixtimeFrame));
    // 检查是否到达定时上报的时间点（每5分钟的整点，本机时区）
    if( (tm->tm_min % 5 == 0)  && (tm->tm_sec == 0) )
    {
        
        size_t data_len = sizeof(HexFrameHeader) + sizeof(ReportBodyHead) + (sizeof(uint8_t)   + sizeof(DataFormat)  + sizeof(uint16_t)) * 2;
        LOG(LOG_DEBUG,"build_fixtime_report,data_len is %d",data_len);
        frame.header.frame_start = HEX_FRAME_START;
        frame.header.center_addr = stUpframepara.center_addr;
        memcpy(frame.header.telemetry_addr, stUpframepara.tele_addr, ADDR_BCD_LEN);
        stUpframepara.password = swap_u16(stUpframepara.password);
        memcpy(frame.header.password, &stUpframepara.password, PASSWORD_HEX_LEN);
        frame.header.func_code = FUNCTION_CODE_TIMED_REPORT;  // 功能码
        frame.header.msg_flag_len = (0x0000 << 12) | ((data_len ) - sizeof(HexFrameHeader)) ; // 上行标识
        frame.header.msg_flag_len = swap_u16(frame.header.msg_flag_len);
        frame.header.stx = STX;

        // 初始化定时报告正文
        frame.fixtime_report.report_head.serial_num = swap_u16((uint16_t)g_u32CommansSeri++);  
        get_bcd_time(frame.fixtime_report.report_head.report_time);
        LOG_ARRAY(LOG_INFO,frame.fixtime_report.report_head.report_time,6,"%02x");
        frame.fixtime_report.report_head.addr_identifier = 0xF1F1;// 地址标识符
        frame.fixtime_report.report_head.station_type = 0x48;
		frame.fixtime_report.report_head.obs_identifier =  0xF0F0;//观测时间标识符
        memcpy(frame.fixtime_report.report_head.obs_time,frame.fixtime_report.report_head.report_time,5);        // 观测时间

        frame.fixtime_report.voltage.u8Element_identifier = POWER_VOLTAGE;
        frame.fixtime_report.voltage.DataFmt.dataSize = 2;
        frame.fixtime_report.voltage.DataFmt.decimalPlaces = 2;
        frame.fixtime_report.voltage .data.u16Voltage= swap_u16( g_stReportState.u16CarVol / 10);

        frame.fixtime_report.water_level.u8Element_identifier = INSTANT_RIVER_LEVEL;
        frame.fixtime_report.water_level.DataFmt.dataSize = 4;
        frame.fixtime_report.water_level.DataFmt.decimalPlaces = 2;

        frame.fixtime_report.water_level.data.u16waterlevel = swap_u16((uint16_t)g_u32waterLevel);   // 12.34m
        // 计算CRC校验码
        frame.end_marker = ETX;  // 无后续报文
        memcpy(buffer,(uint8_t *)&frame,data_len + 1);
        frame.crc16 = CRC16_Modbus(buffer, data_len + 1);
        //frame.crc16 = swap_u16(frame.crc16);
        // 写入数据并检查结果
        memcpy(buffer,(uint8_t *)&frame,data_len +3);
        LOG_ARRAY(LOG_INFO,buffer,data_len +3,"%02x");
        ssize_t bytes_written = write(stUpframepara.fd, buffer, data_len + 3);
        if (bytes_written != (ssize_t)(data_len + 3))
        {
            LOG(LOG_INFO, "Error writing frame: %d bytes written", bytes_written);
        }
        pthread_mutex_unlock(&g_lock[g_stDevicelist.ZigBee]);
        return;
    }
    else
    {
        // 如果未到达上报时间点，则解锁互斥锁并返回
        pthread_mutex_unlock(&g_lock[g_stDevicelist.ZigBee]);
        return ;
    }

}
void CheckFixtimeAndHourlyFrame(void *arg)
{
    if (OtaUpgrade_IsBusy()) {
        return;
    }
    build_hourly_report(arg);
    build_fixtime_report(arg);
}

unsigned char bcd_to_dec(unsigned char bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static int bcd_byte_ok(uint8_t b)
{
    return ((b >> 4) <= 9) && ((b & 0x0F) <= 9);
}

/* 校验帧头 BCD：防粘包/错位把脏字节当时间写入 RTC */
int bcd_time_is_valid(const BCD_TIME *bcd_time)
{
    int y, mon, day, hour, min, sec;
    int dim;
    static const uint8_t days_in_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (bcd_time == NULL) {
        return 0;
    }
    if (!bcd_byte_ok(bcd_time->u8Year) || !bcd_byte_ok(bcd_time->u8Month)
        || !bcd_byte_ok(bcd_time->u8Day) || !bcd_byte_ok(bcd_time->u8Hour)
        || !bcd_byte_ok(bcd_time->u8Minutes) || !bcd_byte_ok(bcd_time->u8Second)) {
        return 0;
    }

    y = bcd_to_dec(bcd_time->u8Year);
    mon = bcd_to_dec(bcd_time->u8Month);
    day = bcd_to_dec(bcd_time->u8Day);
    hour = bcd_to_dec(bcd_time->u8Hour);
    min = bcd_to_dec(bcd_time->u8Minutes);
    sec = bcd_to_dec(bcd_time->u8Second);

    /* 协议年为 20xx 的后两位；过宽易放过脏数据，过窄影响长期使用 */
    if (y < 20 || y > 99) {
        return 0;
    }
    if (mon < 1 || mon > 12) {
        return 0;
    }
    if (hour > 23 || min > 59 || sec > 59) {
        return 0;
    }

    dim = days_in_month[mon - 1];
    if (mon == 2) {
        int full_year = 2000 + y;
        if ((full_year % 4 == 0 && full_year % 100 != 0) || (full_year % 400 == 0)) {
            dim = 29;
        }
    }
    if (day < 1 || day > dim) {
        return 0;
    }
    return 1;
}

// 设置系统时间
int set_system_time(const BCD_TIME *bcd_time) 
{
    struct tm timeinfo = {0};

    if (!bcd_time_is_valid(bcd_time)) {
        LOG(LOG_ERROR, "set_system_time: invalid BCD time");
        return ERROR;
    }

    timeinfo.tm_year = bcd_to_dec(bcd_time->u8Year) + 100;
    timeinfo.tm_mon  = bcd_to_dec(bcd_time->u8Month) - 1;
    timeinfo.tm_mday = bcd_to_dec(bcd_time->u8Day);
    timeinfo.tm_hour = bcd_to_dec(bcd_time->u8Hour);
    timeinfo.tm_min  = bcd_to_dec(bcd_time->u8Minutes);
    timeinfo.tm_sec  = bcd_to_dec(bcd_time->u8Second);

    // 使用 mktime 将北京时间转换为 UTC 时间戳（假设系统时区为 UTC+8）
    time_t new_time = mktime(&timeinfo);
    if (new_time == -1) {
        LOG(LOG_ERROR, "mktime failed");
        return ERROR;
    }

    struct timeval tv = { .tv_sec = new_time, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        LOG(LOG_ERROR, "settimeofday failed");
        return ERROR;
    }

    // 同步硬件时钟（假设使用 UTC 存储）
    if (system("hwclock -u -w") != 0) {
        LOG(LOG_WARNING, "hwclock command failed");
    }
    return SUCCESS;
}