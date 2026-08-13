#ifndef _WEBSERIVER_H
#define _WEBSERIVER_H

/*
 * 扩展 651 规约（与《设备和服务器通信协议》一致）
 * - 数据小端、结构体 1 字节对齐
 * - 上行功能码 0xF2，下行 0xF1
 * - BCD 发送时间之后第 1 字节为指令字(u8Signature)，其后为业务数据
 */
#include "Common.h"

#define HEAD1 0x7E
#define HEAD2 0x7E
#define STX 0x02
#define ETX 0x03
#define UP_EXPAND_CODE   0xF2
#define DOWN_EXPAND_CODE 0xF1
#define MAX_SOFTWARE_SIZE (2 * 1024 * 1024)

#define REPORT_DOPPLER_REALDATA 0xFFFFFFFA
#define REPORT_DOPPLER_REALDATA_CMD 0xFA
#define REPORT_DEVICE_TIMEOUT_CMD 1
#define DEVICE_TYPE_SHUIWEI       2   /* 水位设备：有通信但无有效水位时 DEVICE_TIMEOUT */
#define DEVICE_TYPE_FLOW_JET_LAG  20  /* 时差法：传感器无应答/取不到数据时 DEVICE_TIMEOUT */
#define REPORT_RTU_RESTART_CMD    88  /* 上行：RTU重启确认，净荷1字节错误码 */
#define SERVER_RESTART_CODE       3   /* 下行：网页重启设备 */
#define SERVER_START_TEST_CODE    1   /* 下行：网页加测 */
#define SERVER_GET_CONFIG_CODE    14  /* 下行：获取配置文件 */
#define SERVER_SETCONFIG_FILE_CODE 18 /* 下行：设置配置文件 WORD长度+内容 */
#define SERVER_SHUIWEI_CELIU_CODE 29  /* 下行：设置水位测流，净荷 int32 厘米；用一次后恢复；-10000=立即用传感器 */
#define SHUIWEI_CELIU_USE_SENSOR  (-10000)
/* 网页偶发下发 -100000，与 -10000 同义：恢复传感器水位 */
#define SHUIWEI_CELIU_USE_SENSOR_ALT (-100000)
#define SERVER_RETRIEVE_LOGS_CODE 103 /* 下行：获取设备日志 RETRIEVE_LOGS */
#define SERVER_DATA_RESPONSE_CODE 150 /* 下行：每包数据回复 DATA_RESPONSE（对称 OTA 每包 ACK） */
#define REPORT_FLOW_JET_LAG_CMD 121
#define REPORT_GET_CONFIG_RESPONSE 83 /* 上行：配置文件内容 */
#define REPORT_RETRIEVE_LOGS_RESULT 122 /* 上行：日志结果 RETRIEVE_LOGS_RESULT */
#define RETRIEVE_LOGS_DEVICE_RTU  1   /* 设备类型：1=rtu */
/* 日志级别：1全部 2警告 3错误 4debug */
#define RETRIEVE_LOGS_LEVEL_ALL      1
#define RETRIEVE_LOGS_LEVEL_WARNING  2
#define RETRIEVE_LOGS_LEVEL_ERROR    3
#define RETRIEVE_LOGS_LEVEL_DEBUG    4
#define TRAFFIC_SENSOR_LINE_MAX 2
#define TRAFFIC_LINE_DETAIL_WIRE_SIZE 20

#pragma pack(push, 1)

/* RETRIEVE_LOGS_RESULT(122) 业务头；其后为不定长包内容。int/short 小端。 */
typedef struct RetrieveLogsResultWire {
    uint8_t  u8DeviceType;  /* 1=rtu */
    uint8_t  u8LogLevel;    /* 1全部 2警告 3错误 4debug */
    int32_t  i32FileSize;   /* 本次导出日志总字节数 */
    uint16_t u16PktTotal;
    uint16_t u16PktIndex; /* 从 1 起 */
} RetrieveLogsResultWire;

/* DATA_RESPONSE(150) 对 122 的回复净荷 */
typedef struct RetrieveLogsAckWire {
    uint8_t  u8Cmd;         /* 122 */
    uint8_t  u8DeviceType;
    uint8_t  u8LogLevel;
    uint16_t u16PktIndex;   /* 与上行包序号对应 */
} RetrieveLogsAckWire;

typedef struct _BCD_TIME
{
    uint8_t u8Year;
    uint8_t u8Month;
    uint8_t u8Day;
    uint8_t u8Hour;
    uint8_t u8Minutes;
    uint8_t u8Second;
} BCD_TIME;

typedef struct _UPFrameHead
{
    uint16_t u16SOH;
    uint8_t u8CentreStaID;
    uint8_t aRTUID[5];
    uint16_t u16Password;
    uint8_t u8FunctionCode;
    uint16_t u16FlagAndLen;
    uint8_t u8DataStart;
    uint16_t u16FrameSeri;
    BCD_TIME BCD_time;
    uint8_t u8Signature;
} UPFrameHead;

typedef struct _DOWNFrameHead
{
    uint16_t u16SOH;
    uint8_t aRTUID[5];
    uint8_t u8CentreStaID;
    uint16_t u16Password;
    uint8_t u8FunctionCode;
    uint16_t u16FlagAndLen;
    uint8_t u8DataStart;
    uint16_t u16FrameSeri;
    BCD_TIME BCD_time;
    uint8_t u8Signature;
} DOWNFrameHead;

typedef struct {
    uint8_t u8EOF;
    uint16_t u16CRC;
} DOWNFrametail;

typedef struct _SeriverCommandHead
{
    uint32_t u32CommandSeri;
    uint32_t u32CommandLen;
    uint32_t u32Command;
    uint8_t au8StationID[12];
} SeriverCommandHead;

typedef struct _ReportDopplerData
{
    SeriverCommandHead stCommandHead;
    float fFlowVelocity;
    float fWaterLevel;
    float fSecFlow;
    float fHourFlow;
    double dPosAccumM3;
    double dNegAccumM3;
    uint32_t u32CollectCount;
} ReportDopplerData;

/* FLOW_JET_LAG(121) 时差法：头 6 字节 + lineCount * 20 字节测线详情 */
typedef struct TrafficTimeDifferenceDetailsWire
{
    int32_t i32LineWater;
    int16_t i16LineFlowRate;
    uint8_t au8LineTime[6];
    int32_t i32LineSecondFlow;
    int32_t i32LineHourFlow;
} TrafficTimeDifferenceDetailsWire;

typedef struct TrafficTimeDifferenceWire
{
    int32_t i32WWater;
    uint8_t u8WWaterMode;
    uint8_t u8LineCount;
} TrafficTimeDifferenceWire;

#pragma pack(pop)

#endif
