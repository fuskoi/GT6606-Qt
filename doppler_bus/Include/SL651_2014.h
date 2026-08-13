#ifndef SL651_2014_H
#define SL651_2014_H

#include "Common.h"
#include "WebSeriver.h"
#include <stdint.h>
#include <time.h>

#define HEX_FRAME_START         0x7E7E
#define ADDR_BCD_LEN            5
#define PASSWORD_HEX_LEN        2
#define FIVE_MINUTES_INTERVAL   (5 * 60 * 1000)

#pragma pack(push, 1)

typedef struct {
    /* SL651：数据定义字节 = 高5位字节数 | 低3位小数位
     * GCC 小端从 LSB 起分配位域，故 decimalPlaces 在前、dataSize 在后 */
    uint8_t decimalPlaces : 3;
    uint8_t dataSize : 5;
} DataFormat;

typedef struct {
    uint8_t u8Element_identifier;
    DataFormat DataFmt;
    uint8_t au8Bcd[4]; /* 要素数据：BCD，高位在前 */
} ElementPara32;

/* 电源电压等 2 字节要素（SL651 dataSize=2） */
typedef struct {
    uint8_t u8Element_identifier;
    DataFormat DataFmt;
    uint8_t au8Bcd[2]; /* 要素数据：BCD，高位在前 */
} ElementPara16;

typedef struct {
    uint16_t serial_num;
    uint8_t report_time[6];
    uint16_t addr_identifier;
    uint8_t Stationid[5];
    uint8_t station_type;
    uint16_t obs_identifier;
    uint8_t obs_time[5];
} ReportBodyHead;

typedef struct {
    uint16_t frame_start;
    uint8_t center_addr;
    uint8_t telemetry_addr[ADDR_BCD_LEN];
    uint8_t password[PASSWORD_HEX_LEN];
    uint8_t func_code;
    uint16_t msg_flag_len;
    uint8_t stx;
} HexFrameHeader;

typedef struct {
    uint8_t center_addr;
    uint8_t tele_addr[5];
    uint16_t password;
    int fd;
} M2UPFramePara;

typedef enum {
    FUNCTION_CODE_TEST = 0x30,
    FUNCTION_CODE_TIMED_REPORT = 0x32,
    FUNCTION_CODE_HOURLY_REPORT = 0x34,
    FUNCTION_CODE_SET_STATION_CLOCK = 0x4A
} FunctionCode;

#define SL651_HOURLY_WATER_COUNT  12

typedef enum {
    INSTANT_FLOW_RATE = 0x27,
    INSTANT_VELOCITY = 0x37,
    POWER_VOLTAGE = 0x38,
    INSTANT_RIVER_LEVEL = 0x39
} HydroDataIdentifier;

extern M2UPFramePara g_stM2UpFramepara;

void get_bcd_time(uint8_t* buffer);
void build_test_report(void* arg);
void build_fixtime_report(void* arg);
void build_hourly_report(void* arg);
void CheckFixtimeAndHourlyFrame(void* arg);
int set_system_time(const BCD_TIME* bcd_time);

#pragma pack(pop)

#endif
