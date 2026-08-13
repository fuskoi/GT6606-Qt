#ifndef _SENSOR_H
#define _SENSOR_H

#include "Common.h"
#include "DopplerProtocol.h"

#define SENSOR_LINE_MAX 2

typedef struct FlowMeterRawData
{
    float fFlowVelocity;
    float fWaterLevel;
    float fSecFlow;
    float fHourFlow;
    uint32_t u32PosAccumHi;
    uint32_t u32PosAccumLo;
    uint32_t u32NegAccumHi;
    uint32_t u32NegAccumLo;
} FlowMeterRawData;

typedef struct FlowMeterData
{
    FlowMeterRawData stRaw;
    double dPosAccumM3;
    double dNegAccumM3;
    struct tm stCaptureTime;
    uint8_t u8Valid;
    uint32_t u32CollectCount;
} FlowMeterData;

extern FlowMeterData g_stFlowMeterData;
extern pthread_mutex_t g_stFlowMeterMutex;

typedef struct SensorTrafficLine
{
    FlowMeterRawData stRaw;
    struct tm stCaptureTime;
    uint8_t u8Valid;
} SensorTrafficLine;

typedef struct TrafficReportSnapshot
{
    int32_t i32AvgWaterScaled;
    uint8_t u8WaterMode;
    uint8_t u8LineCount;
    SensorTrafficLine astLines[SENSOR_LINE_MAX];
    uint8_t u8Valid;
} TrafficReportSnapshot;

int Sensor_Init(void);
void Sensor_CheckTimeout(void* arg);
void ReadFlowMeterData(void* arg);
int SensorRecDataProc(KFIFO_ID pData, WORD* pUsed);
int SensorSendReadAll(void);
void SensorProcessRawData(const FlowMeterRawData* pRaw, FlowMeterData* pOut);
int Sensor_GetTrafficReportSnapshot(TrafficReportSnapshot* pOut);
/* 用上报快照再算一遍本地断面流量并打与采集周期相同风格的日志 */
void Sensor_LogLocalDischargeFromSnapshot(const TrafficReportSnapshot* pSnapshot);
/* 加测：触发一轮采集，成功后由周期完成回调上报 FLOW_JET_LAG(121) */
void Sensor_RequestStartTestReport(void);
/*
 * 网页 SHUIWEI_CELIU(29)：设置测流用水位（厘米）。
 * 设置后仅用于随后一次 FLOW_JET_LAG(121) 上报，上报成功（或加测失败收尾）后自动恢复传感器水位。
 * i32WaterCm == -10000 / -100000 时立即恢复传感器水位。
 */
void Sensor_SetOverrideWaterCm(int32_t i32WaterCm);
/* 121 上报成功后调用：清掉一次性水位覆盖 */
void Sensor_ClearOverrideWaterAfterReport(void);

#endif
