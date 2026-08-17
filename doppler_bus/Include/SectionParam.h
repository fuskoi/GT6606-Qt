#ifndef _SECTION_PARAM_H
#define _SECTION_PARAM_H

#include "Common.h"

#define SECTION_FILE_MAGIC  0x41464353U /* SCFA */
#define RUN_FILE_MAGIC      0x41505859U /* YXPA */
#define SECTION_FILE_VER    1U
#define RUN_FILE_VER        1U
#define MAX_AUTO_FLOW_TIME_COUNT 12
#define MAX_SPEED_LINE_COUNT     65

#define SHICHAFA_DATA_FILE  "fracture.data"
#define YUNXING_DATA_FILE   "operation.data"
#define SHICHAFA_JSON_FILE  "fracture.json"
#define YUNXING_JSON_FILE   "operation.json"

#define REPORT_DUANMIAN_ID          11
#define SERVER_CROSS_SECTION_NEW    100
#define SERVER_RUN_SECTION_NEW      101
/* DOWN_EXPAND_CODE 见 WebSeriver.h */

#pragma pack(push, 1)

typedef struct BankCoef3
{
    uint8_t u8High;
    uint8_t u8Mid;
    uint8_t u8Low;
} BankCoef3;

typedef struct AutoFlowTimeMode
{
    int32_t i32Amplitude;
    int32_t i32WaterLevel;
    int16_t i16DepthCount;
    int16_t i16TimeCount;
    int16_t ai16FlowTimes[MAX_AUTO_FLOW_TIME_COUNT];
} AutoFlowTimeMode;

typedef struct CrossSectionData
{
    int32_t i32SectionId;
    uint8_t u8BankFlag;
    uint8_t au8Reserved[3];
    int32_t i32BottomElevation;
    int32_t i32SectionBottomElevation;
    int32_t i32WaterLevelBase;
    int32_t i32MeterInstallHeight;
    int32_t i32MeterHorizontalPos;
    int32_t i32OriginDistance;
    BankCoef3 stStartBank;
    BankCoef3 stEndBank;
    AutoFlowTimeMode stAutoHigh;
    AutoFlowTimeMode stAutoMid;
    AutoFlowTimeMode stAutoLow;
} CrossSectionData;

typedef struct SpeedLineItem
{
    int32_t i32StartDistance;
    int32_t i32RiverbedElevation;
    uint8_t u8HighCoef;
    uint8_t u8MidCoef;
    uint8_t u8LowCoef;
    uint8_t u8Reserved;
} SpeedLineItem;

typedef struct RunSectionData
{
    int16_t i16SpeedLineCount;
    int16_t i16Reserved;
    SpeedLineItem astLines[MAX_SPEED_LINE_COUNT];
} RunSectionData;

#pragma pack(pop)

typedef struct SectionWettedArea
{
    double fWaterElevation;
    double fWaterLeft;
    double fWaterRight;
    double fTotalArea;
    double fAreaUpper;   /* 分割高程以上至水面 */
    double fAreaLower;   /* 河底至分割高程（不超过水面） */
    double fSplitElev;   /* 两有效测线河底中点；无分割时为水位 */
    double fWidthAtWater;
    double fAvgVelocity; /* 断面平均流速 = 总流量 / 总面积（非两传感器算术平均） */
} SectionWettedArea;

extern CrossSectionData g_stCrossSection;
extern RunSectionData g_stRunSection;
extern uint8_t g_u8CrossSectionReady;
extern uint8_t g_u8RunSectionReady;

int SectionParam_Init(void);
int SectionParam_IsCrossSectionFileEmpty(void);
int SectionParam_IsRunSectionFileEmpty(void);
void SectionParam_ReportSectionId(void);
int SectionParam_HandleDownlink(uint8_t u8Command, const uint8_t* pBody, uint16_t u16BodyLen);
uint8_t SectionParam_GetWaterMode(int32_t i32WaterScaled);

const CrossSectionData* SectionParam_GetCrossSection(void);
const RunSectionData* SectionParam_GetRunSection(void);
int SectionParam_IsCrossSectionReady(void);
int SectionParam_IsRunSectionReady(void);
int SectionParam_CalcWettedArea(int32_t i32WaterScaled, SectionWettedArea* pOut);
/* pfSensorVelocities / pu8SensorValid 按传感器下标：
 * 0=sensor1（下测线，现场从下往上编号的 1 号）
 * 1=sensor2（上测线，编号更高/更靠上）
 */
int SectionParam_CalcTotalDischarge(int32_t i32WaterScaled,
                                    const float* pfSensorVelocities,
                                    const uint8_t* pu8SensorValid,
                                    uint8_t u8SensorMax,
                                    double* pDischargeM3s,
                                    SectionWettedArea* pAreaOut);

#endif
