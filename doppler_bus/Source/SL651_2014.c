#include "SL651_2014.h"
#include "Sensor.h"
#include "Dam5213.h"
#include "SectionParam.h"
#include "Command_proc.h"
#include "CRC.h"
#include "HydrologyRound.h"
#include "OtaUpgrade.h"
#include "SocketLink.h"
#include "UplinkOffline.h"
#include "log.h"
#include <string.h>
#include <errno.h>

M2UPFramePara g_stM2UpFramepara;

/* 过去 1 小时内每 5 分钟水位（供小时报），与 TEL-15 一致为 12 点环形缓冲 */
static uint16_t s_au16HourlyWater[SL651_HOURLY_WATER_COUNT];
static uint8_t s_u8HourlyWaterIdx = 0;
static int s_i32LastHourlySampleSlot = -1;

/* 与网页 121 一致：先放大成十进制整数，再编成 BCD（非 HEX） */
static uint32_t sl651_encode_scaled_u32(double value, uint8_t decimalPlaces)
{
    int32_t i32Scaled = 0;

    if (value < 0.0) {
        value = 0.0;
    }
    i32Scaled = hydrology_scale_round_i32(value, decimalPlaces);
    if (i32Scaled < 0) {
        i32Scaled = 0;
    }
    return (uint32_t)i32Scaled;
}

/*
 * 放大后的十进制整数 → nbytes 字节 BCD（高位在前、左补 0）。
 * 例：151402 / 4 字节 → 00 15 14 02；1262 / 2 字节 → 12 62。
 */
static void sl651_u32_to_bcd(uint32_t value, uint8_t* out, uint8_t nbytes)
{
    uint8_t digits[20];
    unsigned n = 0;
    unsigned di = 0;
    unsigned i = 0;
    uint32_t v = value;

    if (out == NULL || nbytes == 0) {
        return;
    }
    memset(out, 0, nbytes);
    if (v == 0U) {
        return;
    }
    while (v > 0U && n < (unsigned)sizeof(digits)) {
        digits[n++] = (uint8_t)(v % 10U);
        v /= 10U;
    }
    for (i = nbytes; i > 0U && di < n; ) {
        uint8_t lo = 0;
        uint8_t hi = 0;

        i--;
        lo = digits[di++];
        if (di < n) {
            hi = digits[di++];
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
}

void get_bcd_time(uint8_t* buffer)
{
    time_t now = time(NULL);
    struct tm tmNow;

    if (buffer == NULL) {
        return;
    }
    localtime_r(&now, &tmNow);
    buffer[0] = (uint8_t)(((tmNow.tm_year % 100) / 10) << 4) | (uint8_t)(tmNow.tm_year % 10);
    buffer[1] = (uint8_t)(((tmNow.tm_mon + 1) / 10) << 4) | (uint8_t)((tmNow.tm_mon + 1) % 10);
    buffer[2] = (uint8_t)((tmNow.tm_mday / 10) << 4) | (uint8_t)(tmNow.tm_mday % 10);
    buffer[3] = (uint8_t)((tmNow.tm_hour / 10) << 4) | (uint8_t)(tmNow.tm_hour % 10);
    buffer[4] = (uint8_t)((tmNow.tm_min / 10) << 4) | (uint8_t)(tmNow.tm_min % 10);
    buffer[5] = (uint8_t)((tmNow.tm_sec / 10) << 4) | (uint8_t)(tmNow.tm_sec % 10);
}

static void sl651_fill_report_head(ReportBodyHead* pHead)
{
    memset(pHead, 0, sizeof(ReportBodyHead));
    pHead->serial_num = swap_u16(AllocFrameSeri());
    get_bcd_time(pHead->report_time);
    pHead->addr_identifier = swap_u16(0xF1F1);
    memcpy(pHead->Stationid, g_stM2UpFramepara.tele_addr, ADDR_BCD_LEN);
    pHead->station_type = 0x48;
    pHead->obs_identifier = swap_u16(0xF0F0);
    memcpy(pHead->obs_time, pHead->report_time, 5);
}

static int sl651_write_frame(int fd, const uint8_t* pFrame, size_t frameLen)
{
    ssize_t written = 0;

    if (pFrame == NULL || frameLen == 0) {
        return ERROR;
    }
    (void)fd;

    written = WebLink_Send(pFrame, (int)frameLen);
    if (written < 0 || (size_t)written != frameLen) {
        LOG(LOG_ERROR, "[UPLINK] SL651 write failed (%s) len=%zu",
            WebLink_IsSocketMode() ? "socket" : "dtu", frameLen);
        return ERROR;
    }
    LOG(LOG_DEBUG, "[UPLINK] SL651(%s) len=%zu",
        WebLink_IsSocketMode() ? "socket" : "dtu", frameLen);
    LOG_ARRAY(LOG_DEBUG, pFrame, (int)frameLen, "%02x");
    return SUCCESS;
}

/* 高程水位可能超过 uint16；超限时改用 0.1 精度写入小时报 2 字节槽 */
static uint16_t sl651_water_scaled_to_u16(int32_t i32Scaled)
{
    int32_t v = i32Scaled;

    if (v < 0) {
        v = 0;
    }
    if (v > 65535) {
        v /= 10;
        if (v > 65535) {
            v = 65535;
        }
    }
    return (uint16_t)v;
}

static void sl651_sample_hourly_water(int32_t i32AvgWaterScaled, const struct tm* pTm)
{
    int i32Slot;

    if (pTm == NULL) {
        return;
    }
    i32Slot = pTm->tm_hour * 12 + pTm->tm_min / 5;
    if (i32Slot == s_i32LastHourlySampleSlot) {
        return;
    }
    s_i32LastHourlySampleSlot = i32Slot;
    s_au16HourlyWater[s_u8HourlyWaterIdx] = sl651_water_scaled_to_u16(i32AvgWaterScaled);
    s_u8HourlyWaterIdx = (uint8_t)((s_u8HourlyWaterIdx + 1U) % SL651_HOURLY_WATER_COUNT);
    LOG(LOG_DEBUG, "hourly water sample[%u]=%u (scaled=%d)",
        (unsigned)((s_u8HourlyWaterIdx + SL651_HOURLY_WATER_COUNT - 1U) % SL651_HOURLY_WATER_COUNT),
        (unsigned)s_au16HourlyWater[(s_u8HourlyWaterIdx + SL651_HOURLY_WATER_COUNT - 1U) % SL651_HOURLY_WATER_COUNT],
        (int)i32AvgWaterScaled);
}

/*
 * 要素取值与网页 FLOW_JET_LAG(121) 对齐：
 * - 水位：测线平均水位（同 i32AvgWaterScaled，2 位小数）
 * - 流速/流量：断面就绪则用 Q、V=Q/A；否则用有效测线均值
 * - 小数位：流速 3、水位 2、流量 2（与 121 测线字段一致）
 */
static int sl651_fill_elements_from_snapshot(ElementPara32* pVelocity,
                                             ElementPara32* pLevel,
                                             ElementPara32* pFlow,
                                             const TrafficReportSnapshot* pSnap)
{
    float afVel[SENSOR_LINE_MAX];
    uint8_t au8Valid[SENSOR_LINE_MAX];
    uint8_t i = 0;
    uint8_t u8Count = 0;
    float fVelSum = 0.0f;
    float fSecSum = 0.0f;
    float fVel = 0.0f;
    float fFlow = 0.0f;
    double dQ = 0.0;
    SectionWettedArea stArea;

    if (pVelocity == NULL || pLevel == NULL || pFlow == NULL || pSnap == NULL || !pSnap->u8Valid) {
        return ERROR;
    }

    memset(afVel, 0, sizeof(afVel));
    memset(au8Valid, 0, sizeof(au8Valid));
    for (i = 0; i < SENSOR_LINE_MAX; i++) {
        float fLineVel = 0.0f;
        float fLineSec = 0.0f;

        if (!pSnap->astLines[i].u8Valid) {
            continue;
        }
        fLineVel = pSnap->astLines[i].stRaw.fFlowVelocity;
        fLineSec = pSnap->astLines[i].stRaw.fSecFlow;
        if (fLineVel < 0.0f) {
            fLineVel = 0.0f;
        }
        if (fLineSec < 0.0f) {
            fLineSec = 0.0f;
        }
        afVel[i] = fLineVel;
        au8Valid[i] = 1;
        fVelSum += fLineVel;
        fSecSum += fLineSec;
        u8Count++;
    }
    if (u8Count == 0) {
        return ERROR;
    }

    fVel = fVelSum / (float)u8Count;
    fFlow = fSecSum / (float)u8Count;
    if (SectionParam_CalcTotalDischarge(pSnap->i32AvgWaterScaled, afVel, au8Valid,
                                        SENSOR_LINE_MAX, &dQ, &stArea) == SUCCESS) {
        fVel = (float)stArea.fAvgVelocity;
        fFlow = (float)dQ;
    }

    memset(pVelocity, 0, sizeof(*pVelocity));
    pVelocity->u8Element_identifier = INSTANT_VELOCITY;
    pVelocity->DataFmt.dataSize = 4;
    pVelocity->DataFmt.decimalPlaces = 3;
    sl651_u32_to_bcd(sl651_encode_scaled_u32((double)fVel, 3), pVelocity->au8Bcd, 4);

    memset(pLevel, 0, sizeof(*pLevel));
    pLevel->u8Element_identifier = INSTANT_RIVER_LEVEL;
    pLevel->DataFmt.dataSize = 4;
    pLevel->DataFmt.decimalPlaces = 2;
    {
        int32_t i32Water = pSnap->i32AvgWaterScaled;
        if (i32Water < 0) {
            i32Water = 0;
        }
        sl651_u32_to_bcd((uint32_t)i32Water, pLevel->au8Bcd, 4);
    }

    memset(pFlow, 0, sizeof(*pFlow));
    pFlow->u8Element_identifier = INSTANT_FLOW_RATE;
    pFlow->DataFmt.dataSize = 4;
    pFlow->DataFmt.decimalPlaces = 2;
    sl651_u32_to_bcd(sl651_encode_scaled_u32((double)fFlow, 2), pFlow->au8Bcd, 4);

    LOG(LOG_DEBUG,
        "SL651 elements: water=%d vel=%.3f flow=%.2f lines=%u",
        (int)pSnap->i32AvgWaterScaled,
        fVel,
        fFlow,
        (unsigned)u8Count);
    return SUCCESS;
}

/* 有有效电压则填入 0x38；返回 1=已填，0=跳过 */
static int sl651_fill_voltage_element(ElementPara16* pVoltage)
{
    float fVolt = 0.0f;
    uint16_t u16Scaled = 0;

    if (pVoltage == NULL) {
        return 0;
    }
    if (g_stDevicelist.Dam5213 < 0 || g_stDeviceAddr.u8Dam5213 == 0) {
        return 0;
    }
    if (Dam5213_GetVoltage(&fVolt) != SUCCESS) {
        return 0;
    }
    if (fVolt < 0.0f) {
        fVolt = 0.0f;
    }
    /* 2 位小数：0.01V，与 TEL-15 / SL651 电源电压一致 */
    {
        int32_t i32Scaled = hydrology_scale_round_i32((double)fVolt, 2);
        if (i32Scaled < 0) {
            i32Scaled = 0;
        }
        if (i32Scaled > 65535) {
            i32Scaled = 65535;
        }
        u16Scaled = (uint16_t)i32Scaled;
    }

    memset(pVoltage, 0, sizeof(*pVoltage));
    pVoltage->u8Element_identifier = POWER_VOLTAGE;
    pVoltage->DataFmt.dataSize = 2;
    pVoltage->DataFmt.decimalPlaces = 2;
    sl651_u32_to_bcd((uint32_t)u16Scaled, pVoltage->au8Bcd, 2);
    LOG(LOG_DEBUG, "SL651 voltage=%.2fV", (double)fVolt);
    return 1;
}

void build_test_report(void* arg)
{
    uint8_t buffer[256];
    HexFrameHeader header;
    ReportBodyHead body;
    ElementPara32 velocity;
    ElementPara32 level;
    ElementPara32 flow;
    ElementPara16 voltage;
    TrafficReportSnapshot stSnapshot;
    size_t bodyLen = 0;
    size_t offset = 0;
    uint8_t endMarker = ETX;
    uint16_t crc = 0;
    int hasVoltage = 0;
    M2UPFramePara* pPara = (M2UPFramePara*)arg;

    if (OtaUpgrade_IsBusy()) {
        return;
    }
    if (pPara == NULL) {
        return;
    }
    if (WebLink_IsSocketMode()) {
        if (!SocketLink_IsConnected()) {
            return;
        }
    } else if (pPara->fd <= 0) {
        return;
    }
    if (Sensor_GetTrafficReportSnapshot(&stSnapshot) != SUCCESS) {
        LOG(LOG_DEBUG, "build_test_report skipped: no sensor snapshot");
        return;
    }
    if (sl651_fill_elements_from_snapshot(&velocity, &level, &flow, &stSnapshot) != SUCCESS) {
        return;
    }
    hasVoltage = sl651_fill_voltage_element(&voltage);

    sl651_fill_report_head(&body);
    bodyLen = sizeof(ReportBodyHead) + sizeof(ElementPara32) * 3;
    if (hasVoltage) {
        bodyLen += sizeof(ElementPara16);
    }

    memset(&header, 0, sizeof(header));
    header.frame_start = HEX_FRAME_START;
    header.center_addr = pPara->center_addr;
    memcpy(header.telemetry_addr, pPara->tele_addr, ADDR_BCD_LEN);
    {
        uint16_t password = swap_u16(pPara->password);
        memcpy(header.password, &password, PASSWORD_HEX_LEN);
    }
    header.func_code = FUNCTION_CODE_TEST;
    header.msg_flag_len = swap_u16((uint16_t)bodyLen);
    header.stx = STX;

    memcpy(buffer + offset, &header, sizeof(header));
    offset += sizeof(header);
    memcpy(buffer + offset, &body, sizeof(body));
    offset += sizeof(body);
    memcpy(buffer + offset, &velocity, sizeof(velocity));
    offset += sizeof(velocity);
    memcpy(buffer + offset, &level, sizeof(level));
    offset += sizeof(level);
    memcpy(buffer + offset, &flow, sizeof(flow));
    offset += sizeof(flow);
    if (hasVoltage) {
        memcpy(buffer + offset, &voltage, sizeof(voltage));
        offset += sizeof(voltage);
    }
    memcpy(buffer + offset, &endMarker, sizeof(endMarker));
    offset += sizeof(endMarker);
    crc = CRC16_Modbus(buffer, (uint16_t)offset);
    memcpy(buffer + offset, &crc, sizeof(crc));
    offset += sizeof(crc);

    LOG(LOG_INFO, "build_test_report");
    sl651_write_frame(pPara->fd, buffer, offset);
}

void build_fixtime_report(void* arg)
{
    time_t now = time(NULL);
    struct tm tmNow;
    uint8_t buffer[256];
    HexFrameHeader header;
    ReportBodyHead body;
    ElementPara32 velocity;
    ElementPara32 level;
    ElementPara32 flow;
    ElementPara16 voltage;
    TrafficReportSnapshot stSnapshot;
    size_t bodyLen = 0;
    size_t offset = 0;
    uint8_t endMarker = ETX;
    uint16_t crc = 0;
    int hasVoltage = 0;
    int linkReady = 1;
    M2UPFramePara* pPara = (M2UPFramePara*)arg;

    if (pPara == NULL) {
        return;
    }
    if (WebLink_IsSocketMode()) {
        if (!SocketLink_IsConnected()) {
            linkReady = 0;
        }
    } else if (pPara->fd <= 0) {
        linkReady = 0;
    }

    localtime_r(&now, &tmNow);
    if ((tmNow.tm_min % 5) != 0 || tmNow.tm_sec != 0) {
        return;
    }

    if (Sensor_GetTrafficReportSnapshot(&stSnapshot) != SUCCESS) {
        LOG(LOG_DEBUG, "build_fixtime_report skipped: no sensor snapshot");
        return;
    }
    /* 每 5 分钟采一点水位，供整点小时报使用（无论链路是否就绪） */
    sl651_sample_hourly_water(stSnapshot.i32AvgWaterScaled, &tmNow);

    if (!linkReady) {
        LOG(LOG_DEBUG, "build_fixtime_report skipped: link not ready");
        return;
    }
    if (sl651_fill_elements_from_snapshot(&velocity, &level, &flow, &stSnapshot) != SUCCESS) {
        return;
    }
    hasVoltage = sl651_fill_voltage_element(&voltage);

    sl651_fill_report_head(&body);
    bodyLen = sizeof(ReportBodyHead) + sizeof(ElementPara32) * 3;
    if (hasVoltage) {
        bodyLen += sizeof(ElementPara16);
    }

    memset(&header, 0, sizeof(header));
    header.frame_start = HEX_FRAME_START;
    header.center_addr = pPara->center_addr;
    memcpy(header.telemetry_addr, pPara->tele_addr, ADDR_BCD_LEN);
    {
        uint16_t password = swap_u16(pPara->password);
        memcpy(header.password, &password, PASSWORD_HEX_LEN);
    }
    header.func_code = FUNCTION_CODE_TIMED_REPORT;
    header.msg_flag_len = swap_u16((uint16_t)bodyLen);
    header.stx = STX;

    memcpy(buffer + offset, &header, sizeof(header));
    offset += sizeof(header);
    memcpy(buffer + offset, &body, sizeof(body));
    offset += sizeof(body);
    memcpy(buffer + offset, &velocity, sizeof(velocity));
    offset += sizeof(velocity);
    memcpy(buffer + offset, &level, sizeof(level));
    offset += sizeof(level);
    memcpy(buffer + offset, &flow, sizeof(flow));
    offset += sizeof(flow);
    if (hasVoltage) {
        memcpy(buffer + offset, &voltage, sizeof(voltage));
        offset += sizeof(voltage);
    }
    memcpy(buffer + offset, &endMarker, sizeof(endMarker));
    offset += sizeof(endMarker);
    crc = CRC16_Modbus(buffer, (uint16_t)offset);
    memcpy(buffer + offset, &crc, sizeof(crc));
    offset += sizeof(crc);

    LOG(LOG_INFO, "build_fixtime_report");
    (void)sl651_write_frame(pPara->fd, buffer, offset);
}

void build_hourly_report(void* arg)
{
    time_t now = time(NULL);
    struct tm tmNow;
    uint8_t buffer[256];
    HexFrameHeader header;
    ReportBodyHead body;
    ElementPara16 voltage;
    uint16_t u16WaterId = 0;
    uint16_t au16Water[SL651_HOURLY_WATER_COUNT];
    size_t bodyLen = 0;
    size_t offset = 0;
    uint8_t endMarker = ETX;
    uint16_t crc = 0;
    int hasVoltage = 0;
    int linkReady = 1;
    int i = 0;
    M2UPFramePara* pPara = (M2UPFramePara*)arg;

    if (pPara == NULL) {
        return;
    }
    if (WebLink_IsSocketMode()) {
        if (!SocketLink_IsConnected()) {
            linkReady = 0;
        }
    } else if (pPara->fd <= 0) {
        linkReady = 0;
    }
    /* 链路未就绪且当前判为在线：跳过；离线则继续组包落盘 */
    if (!linkReady && UplinkOffline_IsOnline()) {
        return;
    }

    localtime_r(&now, &tmNow);
    if (tmNow.tm_min != 0 || tmNow.tm_sec != 0) {
        return;
    }

    hasVoltage = sl651_fill_voltage_element(&voltage);
    sl651_fill_report_head(&body);
    /* 与 TEL-15 小时报正文一致：水位标识 + 12×5min 水位 + 可选电压 */
    bodyLen = sizeof(ReportBodyHead) + sizeof(uint16_t) +
              sizeof(uint16_t) * SL651_HOURLY_WATER_COUNT;
    if (hasVoltage) {
        bodyLen += sizeof(ElementPara16);
    }

    memset(&header, 0, sizeof(header));
    header.frame_start = HEX_FRAME_START;
    header.center_addr = pPara->center_addr;
    memcpy(header.telemetry_addr, pPara->tele_addr, ADDR_BCD_LEN);
    {
        uint16_t password = swap_u16(pPara->password);
        memcpy(header.password, &password, PASSWORD_HEX_LEN);
    }
    header.func_code = FUNCTION_CODE_HOURLY_REPORT;
    header.msg_flag_len = swap_u16((uint16_t)bodyLen);
    header.stx = STX;

    u16WaterId = (uint16_t)INSTANT_RIVER_LEVEL;
    for (i = 0; i < SL651_HOURLY_WATER_COUNT; i++) {
        /* 与 TEL-15 一致：按主机序入帧（小端设备上为低字节在前） */
        au16Water[i] = s_au16HourlyWater[i];
    }

    memcpy(buffer + offset, &header, sizeof(header));
    offset += sizeof(header);
    memcpy(buffer + offset, &body, sizeof(body));
    offset += sizeof(body);
    memcpy(buffer + offset, &u16WaterId, sizeof(u16WaterId));
    offset += sizeof(u16WaterId);
    memcpy(buffer + offset, au16Water, sizeof(au16Water));
    offset += sizeof(au16Water);
    if (hasVoltage) {
        memcpy(buffer + offset, &voltage, sizeof(voltage));
        offset += sizeof(voltage);
    }
    memcpy(buffer + offset, &endMarker, sizeof(endMarker));
    offset += sizeof(endMarker);
    crc = CRC16_Modbus(buffer, (uint16_t)offset);
    memcpy(buffer + offset, &crc, sizeof(crc));
    offset += sizeof(crc);

    if (!linkReady) {
        (void)UplinkOffline_Enqueue(UPLINK_OFFLINE_KIND_SL651,
                                    (uint32_t)FUNCTION_CODE_HOURLY_REPORT,
                                    buffer, (uint16_t)offset, DTU_TYPE);
        LOG(LOG_DEBUG, "build_hourly_report offline queued, link not ready");
        return;
    }

    LOG(LOG_INFO, "build_hourly_report");
    /* 离线或本地发送失败：小时报落盘，恢复后补发 */
    if (sl651_write_frame(pPara->fd, buffer, offset) != SUCCESS ||
        !UplinkOffline_IsOnline()) {
        (void)UplinkOffline_Enqueue(UPLINK_OFFLINE_KIND_SL651,
                                    (uint32_t)FUNCTION_CODE_HOURLY_REPORT,
                                    buffer, (uint16_t)offset, DTU_TYPE);
    }
}

void CheckFixtimeAndHourlyFrame(void* arg)
{
    if (OtaUpgrade_IsBusy()) {
        return;
    }
    if (!g_stMeasureConfig.u8Sl651Enable) {
        return;
    }
    build_hourly_report(arg);
    build_fixtime_report(arg);
}

int set_system_time(const BCD_TIME* bcd_time)
{
    struct tm timeinfo;
    time_t newTime;
    struct timeval tv;

    if (bcd_time == NULL) {
        return ERROR;
    }

    memset(&timeinfo, 0, sizeof(timeinfo));
    timeinfo.tm_year = (((bcd_time->u8Year >> 4) * 10) + (bcd_time->u8Year & 0x0F)) + 100;
    timeinfo.tm_mon = (((bcd_time->u8Month >> 4) * 10) + (bcd_time->u8Month & 0x0F)) - 1;
    timeinfo.tm_mday = ((bcd_time->u8Day >> 4) * 10) + (bcd_time->u8Day & 0x0F);
    timeinfo.tm_hour = ((bcd_time->u8Hour >> 4) * 10) + (bcd_time->u8Hour & 0x0F);
    timeinfo.tm_min = ((bcd_time->u8Minutes >> 4) * 10) + (bcd_time->u8Minutes & 0x0F);
    timeinfo.tm_sec = ((bcd_time->u8Second >> 4) * 10) + (bcd_time->u8Second & 0x0F);

    newTime = mktime(&timeinfo);
    if (newTime == (time_t)-1) {
        return ERROR;
    }
    tv.tv_sec = newTime;
    tv.tv_usec = 0;
    if (settimeofday(&tv, NULL) != 0) {
        LOG(LOG_ERROR, "settimeofday failed: %s", strerror(errno));
        return ERROR;
    }
    LOG(LOG_INFO, "system time updated by downlink");
    return SUCCESS;
}
