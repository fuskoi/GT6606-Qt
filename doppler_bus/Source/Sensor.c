#include "Sensor.h"
#include "ModbusQueue.h"
#include "CRC.h"
#include "SectionParam.h"
#include "HydrologyRound.h"
#include "OtaUpgrade.h"
#include "Command_proc.h"
#include "ReportAck.h"
#include "WebSeriver.h"
#include "log.h"
#include <string.h>
#include <sys/time.h>
#include <math.h>

FlowMeterData g_stFlowMeterData;
pthread_mutex_t g_stFlowMeterMutex = PTHREAD_MUTEX_INITIALIZER;

static uint8_t g_u8ReadPending = 0;
static uint8_t g_u8PendingSlaveAddr = 0;
static struct timeval g_stPendingSince;
static uint32_t g_u32PendingTimeoutMs = 5000;
static FlowMeterRawData g_stSensorRaw[2];
static uint8_t g_u8SensorValid[2];
static SensorTrafficLine g_astSensorLines[SENSOR_LINE_MAX];
static uint32_t g_u32SimulateTick = 0;
static uint8_t g_u8StartTestReportPending = 0;
/* 网页设置水位测流：厘米；SHUIWEI_CELIU_USE_SENSOR 表示用传感器水位。
 * 非魔法数时为一次性覆盖：下一次成功 FLOW_JET_LAG 上报后自动清回传感器水位。 */
static int32_t g_i32OverrideWaterCm = SHUIWEI_CELIU_USE_SENSOR;
/* 最近已上报且待确认的 DEVICE_TIMEOUT 设备类型；0=无 */
static uint8_t g_u8PendingTimeoutDevType = 0;

static int32_t sensor_scale_water_2dec(float fMeters)
{
    if (fMeters < 0.0f) {
        fMeters = 0.0f;
    }
    return hydrology_scale_round_i32((double)fMeters, 2);
}

static void sensor_log_total_discharge(int32_t i32AvgWaterScaled,
                                       const float* pfSensorVelocities,
                                       const uint8_t* pu8SensorValid,
                                       uint8_t u8SensorMax)
{
    double dTotalQ = 0.0;

    if (pfSensorVelocities == NULL || pu8SensorValid == NULL || u8SensorMax == 0) {
        return;
    }
    if (!SectionParam_IsCrossSectionReady() || !SectionParam_IsRunSectionReady()) {
        return;
    }
    /* 详细日志在 SectionParam_CalcTotalDischarge 内打印；仅本地算 Q */
    (void)SectionParam_CalcTotalDischarge(i32AvgWaterScaled, pfSensorVelocities, pu8SensorValid,
                                          u8SensorMax, &dTotalQ, NULL);
}

static void sensor_log_cycle_complete(void);

static float bytes_to_float_be(const uint8_t* pBytes)
{
    uint32_t raw = ((uint32_t)pBytes[0] << 24) |
                   ((uint32_t)pBytes[1] << 16) |
                   ((uint32_t)pBytes[2] << 8) |
                   (uint32_t)pBytes[3];
    float value = 0.0f;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

static int sensor_has_second_meter(void)
{
    return g_stDeviceAddr.u8FlowMeter2 > 0 &&
           g_stDeviceAddr.u8FlowMeter2 != g_stDeviceAddr.u8FlowMeter;
}

static void sensor_fill_simulated_raw(uint8_t u8SensorIndex, FlowMeterRawData* pRaw)
{
    float phase = (float)(g_u32SimulateTick % 60U) * 0.01f;
    float secFlow = 0.0f;

    if (pRaw == NULL) {
        return;
    }

    memset(pRaw, 0, sizeof(*pRaw));
    if (u8SensorIndex == 0) {
        pRaw->fFlowVelocity = 1.470f + phase * 0.02f;
        pRaw->fWaterLevel = 1514.02f;
        pRaw->fSecFlow = 0.852f + phase * 0.01f;
    } else {
        pRaw->fFlowVelocity = 1.400f + phase * 0.02f;
        pRaw->fWaterLevel = 1514.02f;
        pRaw->fSecFlow = 0.823f + phase * 0.01f;
    }
    secFlow = pRaw->fSecFlow;
    pRaw->fHourFlow = secFlow * 3600.0f;
}

static void sensor_collect_simulated(void)
{
    g_u8SensorValid[0] = 0;
    g_u8SensorValid[1] = 0;
    sensor_fill_simulated_raw(0, &g_stSensorRaw[0]);
    g_u8SensorValid[0] = 1;

    if (sensor_has_second_meter()) {
        sensor_fill_simulated_raw(1, &g_stSensorRaw[1]);
        g_u8SensorValid[1] = 1;
    }

    g_u32SimulateTick++;
    sensor_log_cycle_complete();
}

static uint32_t sensor_ms_since(const struct timeval* pTv)
{
    struct timeval now;

    if (pTv == NULL) {
        return 0xFFFFFFFFu;
    }
    gettimeofday(&now, NULL);
    if (now.tv_sec < pTv->tv_sec) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)((now.tv_sec - pTv->tv_sec) * 1000L +
                      (now.tv_usec - pTv->tv_usec) / 1000L);
}

static void sensor_try_start_test_report(uint8_t u8LineCount)
{
    static int s_forceFlag = 1;

    if (!g_u8StartTestReportPending) {
        return;
    }
    if (u8LineCount == 0) {
        g_u8StartTestReportPending = 0;
        Sensor_SetOverrideWaterCm(SHUIWEI_CELIU_USE_SENSOR);
        LOG(LOG_WARNING, "START_TEST: collect done but no valid lines");
        return;
    }
    if (OtaUpgrade_IsBusy()) {
        /* 保持 pending，等非升级周期再报 */
        return;
    }
    g_u8StartTestReportPending = 0;
    LOG(LOG_DEBUG, "START_TEST: collect ready, uplink FLOW_JET_LAG(121)");
    SendDopplerRealData(&s_forceFlag);
}

static int sensor_water_level_missing(float fWater)
{
    if (isnan(fWater) || isinf(fWater)) {
        return 1;
    }
    /* 无水位：仪器常回 0 或负值 */
    if (fWater <= 0.0f) {
        return 1;
    }
    return 0;
}

/*
 * DEVICE_TIMEOUT(1)：
 *   type=20 传感器无应答/取不到数据
 *   type=2  有应答但无有效水位
 */
static void sensor_report_device_timeout(uint8_t u8DeviceType, uint8_t u8SlaveAddr)
{
    if (g_u8StartTestReportPending) {
        g_u8StartTestReportPending = 0;
        Sensor_SetOverrideWaterCm(SHUIWEI_CELIU_USE_SENSOR);
        LOG(LOG_WARNING, "START_TEST: collect timeout, no FLOW_JET_LAG");
    }
    /* 同类型已在待确认：不狂发，等 5 分钟重试；不同类型可立即换报 */
    if (ReportAck_HasPending((uint8_t)REPORT_DEVICE_TIMEOUT_CMD) &&
        g_u8PendingTimeoutDevType == u8DeviceType) {
        LOG(LOG_DEBUG,
            "DEVICE_TIMEOUT skip: type=%u still pending, slave=%u",
            (unsigned)u8DeviceType, u8SlaveAddr);
        return;
    }
    LOG(LOG_WARNING,
        "DEVICE_TIMEOUT uplink: slave=%u deviceType=%u (%s)",
        u8SlaveAddr,
        (unsigned)u8DeviceType,
        (u8DeviceType == DEVICE_TYPE_SHUIWEI) ? "no water" : "no sensor data");
    g_u8PendingTimeoutDevType = u8DeviceType;
    ReportAck_Send((uint8_t)REPORT_DEVICE_TIMEOUT_CMD,
                   &u8DeviceType, (uint16_t)sizeof(u8DeviceType));
}

static void sensor_report_timeout(uint8_t u8SlaveAddr)
{
    /* 传感器通信超时：取不到数据 → 类型 20 */
    sensor_report_device_timeout((uint8_t)DEVICE_TYPE_FLOW_JET_LAG, u8SlaveAddr);
}

static void sensor_report_no_water(void)
{
    /* 采到数据但无水位 → 类型 2 */
    sensor_report_device_timeout((uint8_t)DEVICE_TYPE_SHUIWEI, 0);
}

static void sensor_clear_pending(void)
{
    g_u8ReadPending = 0;
    g_u8PendingSlaveAddr = 0;
    memset(&g_stPendingSince, 0, sizeof(g_stPendingSince));
}

static void sensor_mark_pending(uint8_t u8SlaveAddr)
{
    g_u8ReadPending = 1;
    g_u8PendingSlaveAddr = u8SlaveAddr;
    gettimeofday(&g_stPendingSince, NULL);
}

static void sensor_log_one(uint8_t u8SensorIndex, const FlowMeterRawData* pRaw)
{
    if (pRaw == NULL) {
        return;
    }
    LOG(LOG_DEBUG,
        "sensor%u: vel=%.4f level=%.4f secQ=%.4f hourQ=%.4f",
        (unsigned)(u8SensorIndex + 1),
        pRaw->fFlowVelocity,
        pRaw->fWaterLevel,
        pRaw->fSecFlow,
        pRaw->fHourFlow);
}

void SensorProcessRawData(const FlowMeterRawData* pRaw, FlowMeterData* pOut)
{
    if (pRaw == NULL || pOut == NULL) {
        return;
    }

    pOut->stRaw = *pRaw;
    pOut->dPosAccumM3 = 0.0;
    pOut->dNegAccumM3 = 0.0;
    time_t now = time(NULL);
    localtime_r(&now, &pOut->stCaptureTime);
    pOut->u8Valid = 1;
    pOut->u32CollectCount++;
}

static int sensor_frame_crc_ok(const uint8_t* pFrame, uint16_t frameLen)
{
    uint16_t crcCalc = 0;
    uint16_t crcRaw = 0;

    if (pFrame == NULL || frameLen < 2) {
        return 0;
    }
    crcCalc = CRC16_Modbus(pFrame, (uint16_t)(frameLen - 2));
    memcpy(&crcRaw, pFrame + frameLen - 2, sizeof(crcRaw));
    return (crcCalc == crcRaw || crcCalc == swap_u16(crcRaw)) ? 1 : 0;
}

static int sensor_frame_is_tx_echo(const uint8_t* frame, int len, uint8_t u8SlaveAddr)
{
    if (len < (int)sizeof(ModbusSendFrame)) {
        return 0;
    }
    if (!sensor_frame_crc_ok(frame, (uint16_t)sizeof(ModbusSendFrame))) {
        return 0;
    }
    return frame[0] == u8SlaveAddr &&
           frame[1] == MODBUS_READ_HOLDING &&
           frame[2] == (uint8_t)(DOPPLER_REG_START >> 8) &&
           frame[3] == (uint8_t)(DOPPLER_REG_START & 0xFF);
}

static int sensor_is_known_slave(uint8_t u8SlaveAddr)
{
    if (u8SlaveAddr == g_stDeviceAddr.u8FlowMeter) {
        return 1;
    }
    if (sensor_has_second_meter() && u8SlaveAddr == g_stDeviceAddr.u8FlowMeter2) {
        return 1;
    }
    return 0;
}

static void sensor_skip_tx_echo(KFIFO_ID pData, uint8_t u8SlaveAddr)
{
    uint8_t frame[sizeof(ModbusSendFrame)];

    while (kfifo_len(pData) >= sizeof(ModbusSendFrame)) {
        kfifo_preview(pData, frame, sizeof(ModbusSendFrame));
        if (!sensor_frame_is_tx_echo(frame, (int)sizeof(ModbusSendFrame), u8SlaveAddr)) {
            break;
        }
        kfifo_get(pData, frame, sizeof(ModbusSendFrame));
    }
}

static int sensor_parse_batch_response(const uint8_t* pRsp, int len, uint8_t u8SlaveAddr, FlowMeterRawData* pRaw)
{
    const uint8_t* pPayload = NULL;

    if (len < MODBUS_BATCH_RSP_LEN) {
        return ERROR;
    }
    if (pRsp[0] != u8SlaveAddr || pRsp[1] != MODBUS_READ_HOLDING) {
        return ERROR;
    }
    if (pRsp[2] != (DOPPLER_REG_COUNT * 2)) {
        return ERROR;
    }

    pPayload = pRsp + 3;
    pRaw->fFlowVelocity = bytes_to_float_be(pPayload + 0);
    pRaw->fWaterLevel = bytes_to_float_be(pPayload + 4);
    pRaw->fSecFlow = bytes_to_float_be(pPayload + 8);
    pRaw->fHourFlow = bytes_to_float_be(pPayload + 12);
    pRaw->u32PosAccumHi = 0;
    pRaw->u32PosAccumLo = 0;
    pRaw->u32NegAccumHi = 0;
    pRaw->u32NegAccumLo = 0;
    return SUCCESS;
}

static int SensorSendReadSlave(uint8_t u8SlaveAddr)
{
    ModbusSendFrame stSendFrame;
    MODBUS_CMD_PARAM stCmd;
    int sensorPort = g_stDevicelist.Sensor;

    if (sensorPort < 0 || sensorPort >= MAX_UART_NUM || u8SlaveAddr == 0) {
        return ERROR;
    }
    if (g_u8ReadPending) {
        return SUCCESS;
    }

    memset(&stSendFrame, 0, sizeof(stSendFrame));
    stSendFrame.u8SubAddr = u8SlaveAddr;
    stSendFrame.u8Funcode = MODBUS_READ_HOLDING;
    stSendFrame.u16RegAddr = swap_u16(DOPPLER_REG_START);
    stSendFrame.u16RegData = swap_u16(DOPPLER_REG_COUNT);
    stSendFrame.u16CRC = CRC16_Modbus((uint8_t*)&stSendFrame, sizeof(ModbusSendFrame) - 2);
    stSendFrame.u16CRC = swap_u16(stSendFrame.u16CRC);

    memset(&stCmd, 0, sizeof(stCmd));
    stCmd.i32Fd = g_stDevicelist.UartList[sensorPort].i32fd;
    stCmd.u8SlaveAddr = u8SlaveAddr;
    stCmd.u8FunctionCode = MODBUS_READ_HOLDING;
    stCmd.u16RawFrameLen = sizeof(ModbusSendFrame);
    memcpy(stCmd.pu8RawFrame, &stSendFrame, sizeof(ModbusSendFrame));

    if (ModbusQueue_AddCommand(&stCmd) != SUCCESS) {
        LOG(LOG_WARNING, "ModbusQueue_AddCommand failed, slave=%u", u8SlaveAddr);
        return ERROR;
    }

    sensor_mark_pending(u8SlaveAddr);
    LOG(LOG_DEBUG, "SensorSendRead slave=%u", u8SlaveAddr);
    return SUCCESS;
}

static void sensor_log_cycle_complete(void)
{
    FlowMeterData stProcessed;
    struct tm tmNow;
    time_t now = time(NULL);
    uint8_t u8LineCount = 0;
    float fWaterSum = 0.0f;
    float afSensorVel[SENSOR_LINE_MAX];
    uint8_t au8SensorValid[SENSOR_LINE_MAX];
    uint8_t i = 0;

    localtime_r(&now, &tmNow);

    /* 升级占用 HOST 串口与 Flash 时，跳过重日志/过水断面计算，避免拖垮收包 */
    if (OtaUpgrade_IsBusy()) {
        pthread_mutex_lock(&g_stFlowMeterMutex);
        memset(g_astSensorLines, 0, sizeof(g_astSensorLines));
        for (i = 0; i < SENSOR_LINE_MAX; i++) {
            if (i == 1 && !sensor_has_second_meter()) {
                break;
            }
            if (!g_u8SensorValid[i]) {
                continue;
            }
            g_astSensorLines[i].stRaw = g_stSensorRaw[i];
            g_astSensorLines[i].stCaptureTime = tmNow;
            g_astSensorLines[i].u8Valid = 1;
            u8LineCount++;
        }
        pthread_mutex_unlock(&g_stFlowMeterMutex);
        sensor_try_start_test_report(u8LineCount);
        return;
    }

    if (g_u8SensorValid[0]) {
        sensor_log_one(0, &g_stSensorRaw[0]);
    }
    if (sensor_has_second_meter() && g_u8SensorValid[1]) {
        sensor_log_one(1, &g_stSensorRaw[1]);
    }

    pthread_mutex_lock(&g_stFlowMeterMutex);
    memset(g_astSensorLines, 0, sizeof(g_astSensorLines));
    memset(afSensorVel, 0, sizeof(afSensorVel));
    memset(au8SensorValid, 0, sizeof(au8SensorValid));
    for (i = 0; i < SENSOR_LINE_MAX; i++) {
        if (i == 1 && !sensor_has_second_meter()) {
            break;
        }
        if (!g_u8SensorValid[i]) {
            continue;
        }
        g_astSensorLines[i].stRaw = g_stSensorRaw[i];
        g_astSensorLines[i].stCaptureTime = tmNow;
        g_astSensorLines[i].u8Valid = 1;
        /* 下标固定：0=sensor1(上), 1=sensor2(下)，供本地算 Q 绑定测线 */
        afSensorVel[i] = g_stSensorRaw[i].fFlowVelocity;
        au8SensorValid[i] = 1;
        fWaterSum += g_stSensorRaw[i].fWaterLevel;
        u8LineCount++;
    }
    pthread_mutex_unlock(&g_stFlowMeterMutex);

    if (u8LineCount > 0) {
        int32_t i32AvgScaled = sensor_scale_water_2dec(fWaterSum / (float)u8LineCount);
        sensor_log_total_discharge(i32AvgScaled, afSensorVel, au8SensorValid, SENSOR_LINE_MAX);
    }

    if (g_u8SensorValid[0]) {
        memset(&stProcessed, 0, sizeof(stProcessed));
        SensorProcessRawData(&g_stSensorRaw[0], &stProcessed);
        stProcessed.stCaptureTime = tmNow;
        pthread_mutex_lock(&g_stFlowMeterMutex);
        g_stFlowMeterData = stProcessed;
        pthread_mutex_unlock(&g_stFlowMeterMutex);
    }

    if (u8LineCount > 0) {
        int32_t i32AvgScaled = sensor_scale_water_2dec(fWaterSum / (float)u8LineCount);
        LOG(LOG_DEBUG,
            "sensor cycle complete: lines=%u avgWater=%d mode=%u",
            (unsigned)u8LineCount,
            (int)i32AvgScaled,
            (unsigned)SectionParam_GetWaterMode(i32AvgScaled));
        /* 有应答但全部无有效水位 → DEVICE_TIMEOUT 类型 2 */
        {
            uint8_t u8AnyWater = 0;
            for (i = 0; i < SENSOR_LINE_MAX; i++) {
                if (!g_u8SensorValid[i]) {
                    continue;
                }
                if (i == 1 && !sensor_has_second_meter()) {
                    break;
                }
                if (!sensor_water_level_missing(g_stSensorRaw[i].fWaterLevel)) {
                    u8AnyWater = 1;
                    break;
                }
            }
            if (!u8AnyWater) {
                sensor_report_no_water();
            }
        }
    }
    sensor_try_start_test_report(u8LineCount);
}

int Sensor_GetTrafficReportSnapshot(TrafficReportSnapshot* pOut)
{
    uint8_t i = 0;
    uint8_t u8LineCount = 0;
    float fWaterSum = 0.0f;
    int32_t i32AvgScaled = 0;

    if (pOut == NULL) {
        return ERROR;
    }

    memset(pOut, 0, sizeof(*pOut));
    pthread_mutex_lock(&g_stFlowMeterMutex);
    for (i = 0; i < SENSOR_LINE_MAX; i++) {
        if (g_astSensorLines[i].u8Valid) {
            pOut->astLines[i] = g_astSensorLines[i];
            fWaterSum += g_astSensorLines[i].stRaw.fWaterLevel;
            u8LineCount++;
        }
    }
    pthread_mutex_unlock(&g_stFlowMeterMutex);

    if (u8LineCount == 0) {
        return ERROR;
    }

    i32AvgScaled = sensor_scale_water_2dec(fWaterSum / (float)u8LineCount);
    pOut->i32AvgWaterScaled = i32AvgScaled;
    pOut->u8LineCount = u8LineCount;
    pOut->u8WaterMode = SectionParam_GetWaterMode(i32AvgScaled);
    pOut->u8Valid = 1;

    /* 网页水位测流覆盖：厘米与「米×100」水位刻度一致 */
    if (g_i32OverrideWaterCm != SHUIWEI_CELIU_USE_SENSOR) {
        float fOverrideM = (float)g_i32OverrideWaterCm / 100.0f;
        pOut->i32AvgWaterScaled = g_i32OverrideWaterCm;
        pOut->u8WaterMode = SectionParam_GetWaterMode(g_i32OverrideWaterCm);
        for (i = 0; i < SENSOR_LINE_MAX; i++) {
            if (pOut->astLines[i].u8Valid) {
                pOut->astLines[i].stRaw.fWaterLevel = fOverrideM;
            }
        }
    }
    return SUCCESS;
}

void Sensor_SetOverrideWaterCm(int32_t i32WaterCm)
{
    if (i32WaterCm == SHUIWEI_CELIU_USE_SENSOR ||
        i32WaterCm == SHUIWEI_CELIU_USE_SENSOR_ALT) {
        g_i32OverrideWaterCm = SHUIWEI_CELIU_USE_SENSOR;
        LOG(LOG_DEBUG, "SHUIWEI_CELIU: use sensor water (magic=%d)", (int)i32WaterCm);
        return;
    }
    g_i32OverrideWaterCm = i32WaterCm;
    LOG(LOG_DEBUG, "SHUIWEI_CELIU: override water=%d cm (%.2f m), one-shot until next 121",
        (int)i32WaterCm, (double)i32WaterCm / 100.0);
}

/* 水位测流一次性覆盖：121 上报成功后清回传感器水位 */
void Sensor_ClearOverrideWaterAfterReport(void)
{
    if (g_i32OverrideWaterCm == SHUIWEI_CELIU_USE_SENSOR) {
        return;
    }
    LOG(LOG_DEBUG, "SHUIWEI_CELIU: one-shot report done, restore sensor water (was %d cm)",
        (int)g_i32OverrideWaterCm);
    g_i32OverrideWaterCm = SHUIWEI_CELIU_USE_SENSOR;
}

void Sensor_LogLocalDischargeFromSnapshot(const TrafficReportSnapshot* pSnapshot)
{
    float afSensorVel[SENSOR_LINE_MAX];
    uint8_t au8SensorValid[SENSOR_LINE_MAX];
    uint8_t i = 0;
    double dTotalQ = 0.0;

    if (pSnapshot == NULL || !pSnapshot->u8Valid) {
        return;
    }
    if (!SectionParam_IsCrossSectionReady() || !SectionParam_IsRunSectionReady()) {
        LOG(LOG_DEBUG, "web report section discharge skipped: section not ready");
        return;
    }

    memset(afSensorVel, 0, sizeof(afSensorVel));
    memset(au8SensorValid, 0, sizeof(au8SensorValid));
    for (i = 0; i < SENSOR_LINE_MAX; i++) {
        if (!pSnapshot->astLines[i].u8Valid) {
            continue;
        }
        afSensorVel[i] = pSnapshot->astLines[i].stRaw.fFlowVelocity;
        au8SensorValid[i] = 1;
    }

    LOG(LOG_DEBUG,
        "web report section discharge: avgWater=%d mode=%u lines=%u",
        (int)pSnapshot->i32AvgWaterScaled,
        (unsigned)pSnapshot->u8WaterMode,
        (unsigned)pSnapshot->u8LineCount);
    (void)SectionParam_CalcTotalDischarge(pSnapshot->i32AvgWaterScaled, afSensorVel, au8SensorValid,
                                          SENSOR_LINE_MAX, &dTotalQ, NULL);
}

static int sensor_store_response(uint8_t u8SlaveAddr, const FlowMeterRawData* pRaw)
{
    if (u8SlaveAddr == g_stDeviceAddr.u8FlowMeter) {
        g_stSensorRaw[0] = *pRaw;
        g_u8SensorValid[0] = 1;
        if (sensor_has_second_meter()) {
            return SensorSendReadSlave(g_stDeviceAddr.u8FlowMeter2);
        }
        sensor_log_cycle_complete();
        sensor_clear_pending();
        return SUCCESS;
    }
    if (sensor_has_second_meter() && u8SlaveAddr == g_stDeviceAddr.u8FlowMeter2) {
        g_stSensorRaw[1] = *pRaw;
        g_u8SensorValid[1] = 1;
        if (g_u8SensorValid[0]) {
            sensor_log_cycle_complete();
        }
        sensor_clear_pending();
        return SUCCESS;
    }
    return ERROR;
}

int SensorSendReadAll(void)
{
    g_u8SensorValid[0] = 0;
    g_u8SensorValid[1] = 0;
    return SensorSendReadSlave(g_stDeviceAddr.u8FlowMeter);
}

int SensorRecDataProc(KFIFO_ID pData, WORD* pUsed)
{
    uint8_t frame[MODBUS_BATCH_RSP_LEN];
    FlowMeterRawData stRaw;
    uint8_t u8SlaveAddr = g_u8PendingSlaveAddr;

    (void)pUsed;
    if (u8SlaveAddr == 0) {
        u8SlaveAddr = g_stDeviceAddr.u8FlowMeter;
    }

    /* 先丢掉本机发出的查询回显，再找 37 字节应答 */
    sensor_skip_tx_echo(pData, u8SlaveAddr);
    if (sensor_has_second_meter()) {
        sensor_skip_tx_echo(pData, g_stDeviceAddr.u8FlowMeter2);
    }

    while (kfifo_len(pData) >= MODBUS_BATCH_RSP_LEN) {
        kfifo_preview(pData, frame, MODBUS_BATCH_RSP_LEN);
        if (!sensor_frame_crc_ok(frame, MODBUS_BATCH_RSP_LEN)) {
            uint8_t drop = 0;
            kfifo_get(pData, &drop, 1);
            continue;
        }
        if (sensor_parse_batch_response(frame, MODBUS_BATCH_RSP_LEN, frame[0], &stRaw) != SUCCESS) {
            kfifo_get(pData, frame, MODBUS_BATCH_RSP_LEN);
            return ERROR;
        }
        if (!sensor_is_known_slave(frame[0])) {
            LOG(LOG_DEBUG, "sensor response ignored: unknown slave=%u", frame[0]);
            kfifo_get(pData, frame, MODBUS_BATCH_RSP_LEN);
            return SUCCESS;
        }
        /*
         * 有 pending 时必须地址匹配；无 pending（超时后补回、重复帧）仍接收已知从站数据，
         * 方便串口助手联调，也避免“回了却被丢掉”。
         */
        if (g_u8ReadPending && frame[0] != g_u8PendingSlaveAddr) {
            LOG(LOG_WARNING,
                "sensor response addr mismatch pending=%u recv=%u",
                g_u8PendingSlaveAddr,
                frame[0]);
            kfifo_get(pData, frame, MODBUS_BATCH_RSP_LEN);
            return ERROR;
        }
        if (!g_u8ReadPending) {
            LOG(LOG_DEBUG, "sensor response accepted without pending slave=%u", frame[0]);
        }

        kfifo_get(pData, frame, MODBUS_BATCH_RSP_LEN);
        if (sensor_store_response(frame[0], &stRaw) != SUCCESS) {
            LOG(LOG_WARNING, "unexpected sensor response slave=%u", frame[0]);
            return ERROR;
        }
        return SUCCESS;
    }
    return SUCCESS;
}

void Sensor_CheckTimeout(void* arg)
{
    (void)arg;

    if (OtaUpgrade_IsBusy() || g_stMeasureConfig.u8SensorSimulate) {
        return;
    }
    if (!g_u8ReadPending) {
        return;
    }
    if (sensor_ms_since(&g_stPendingSince) < g_u32PendingTimeoutMs) {
        return;
    }

    sensor_report_timeout(g_u8PendingSlaveAddr);
    sensor_clear_pending();
}

void ReadFlowMeterData(void* arg)
{
    (void)arg;

    if (OtaUpgrade_IsBusy()) {
        return;
    }
    if (g_stMeasureConfig.u8SensorSimulate) {
        sensor_collect_simulated();
        return;
    }
    if (g_u8ReadPending) {
        return;
    }
    SensorSendReadAll();
}

void Sensor_RequestStartTestReport(void)
{
    if (OtaUpgrade_IsBusy()) {
        LOG(LOG_WARNING, "START_TEST skipped: ota busy");
        return;
    }
    g_u8StartTestReportPending = 1;
    LOG(LOG_DEBUG, "START_TEST: trigger collect then FLOW_JET_LAG(121)");
    ReadFlowMeterData(NULL);
    /* 若当前已有采集在途：等本轮完成后由 sensor_try_start_test_report 上报 */
}

int Sensor_Init(void)
{
    memset(&g_stFlowMeterData, 0, sizeof(g_stFlowMeterData));
    memset(g_stSensorRaw, 0, sizeof(g_stSensorRaw));
    memset(g_u8SensorValid, 0, sizeof(g_u8SensorValid));
    memset(g_astSensorLines, 0, sizeof(g_astSensorLines));
    sensor_clear_pending();
    g_u8StartTestReportPending = 0;
    g_i32OverrideWaterCm = SHUIWEI_CELIU_USE_SENSOR;
    g_u8PendingTimeoutDevType = 0;
    g_u32SimulateTick = 0;
    g_u32PendingTimeoutMs = g_stMeasureConfig.u32SensorTimeoutSec * 1000U;
    if (g_u32PendingTimeoutMs < 1000U) {
        g_u32PendingTimeoutMs = 1000U;
    }
    if (g_stMeasureConfig.u8SensorSimulate) {
        LOG(LOG_DEBUG, "sensor simulate mode enabled");
    } else {
        LOG(LOG_DEBUG, "sensor init: timeout=%ums", (unsigned)g_u32PendingTimeoutMs);
    }
    return SUCCESS;
}
