#ifndef SL651_2014_H
#define SL651_2014_H
#include "Common.h"
#include <stdint.h>
#include <time.h>
#include <assert.h>
#include "WebSeriver.h"
// ------------------------- 通用宏定义 -------------------------
#define ASCII_FRAME_START       0x01    // ASCII帧起始符SOH
#define HEX_FRAME_START         0x7E7E  // HEX帧起始符

#define MAX_CENTER_ADDR         255      // 中心站地址最大值
#define ADDR_BCD_LEN            5        // 遥测站地址BCD编码长度（5字节）
#define PASSWORD_HEX_LEN        2        // 密码HEX编码长度
#define FUNC_CODE_LEN           1        // 功能码长度（HEX编码）
#define FIVE_MINUTES_INTERVAL       (5 * 60 * 1000)  //5分钟
#define HOUR_INTERVAL               (60 * 60 * 1000) //  1小时




// ------------------------- 结构体定义 -------------------------
#pragma pack(push, 1)  // 关闭内存对齐，确保结构体紧凑


// 通用报文头（HEX/BCD编码）
typedef struct {
    uint16_t frame_start;      // 帧起始符7E7E
    uint8_t center_addr;       // 中心站地址1~255
    uint8_t telemetry_addr[ADDR_BCD_LEN]; // 遥测站地址（5字节BCD）
    uint8_t password[PASSWORD_HEX_LEN];  // 密码（2字节HEX）
    uint8_t func_code;         // 功能码
    uint16_t msg_flag_len;     // 报文标识及长度（高4位标识，低12位长度）
    uint8_t stx;               // 正文起始符STX
} HexFrameHeader;//sizeof(HexFrameHeader) = 14;


//数据格式
typedef struct {
    uint8_t dataSize : 5;       // 自定义数据大小，5位，包括小数点后的有效位
    uint8_t decimalPlaces : 3; // 自定义数据保留小数，3位
} DataFormat;


//要素信息
typedef struct 
{
    /* data */
    uint8_t u8Element_identifier;//要素识别码
    DataFormat DataFmt;//数据格式
    union 
    {
        uint16_t u16waterlevel;/* data */
        uint8_t u8RainFall;
        uint16_t u16Voltage;
    }data;//数据
    
}ElementPara;

//报文正文通用内容
typedef struct 
{
    uint16_t serial_num;       // 流水号（2字节HEX）
    uint8_t report_time[6];    // 发报时间BCD码YYMMDDHHmmSS
    uint16_t addr_identifier;   // 地址标识符（固定值）
    uint8_t Stationid[5];
    uint8_t station_type;      // 遥测站分类码HEX
    uint16_t obs_identifier;	//观测时间标识符
    uint8_t obs_time[5];       // 观测时间BCD码YYMMDDHHmm
    /* data */
}ReportBodyHead;//sizeof(ReportBodyHead) = 23


// 测试报正文（功能码30H）
typedef struct {
    ReportBodyHead report_head;
    ElementPara water_level;      // 瞬时水位（0.01m，HEX编码）
    ElementPara voltage;          // 电压（0.01V，HEX编码）
} TestReportBody;


// 小时报正文（功能码34H）
typedef struct {
    ReportBodyHead report_head;
    uint16_t waterlevel_identifier;
    uint16_t water_level_5min[12]; // 12组5分钟相对水位
    ElementPara voltage;
} HourlyReportBody;//sizeof(HourlyReportBody) = 51;


//遥测站定时报 功能码为32H
typedef struct 
{
    ReportBodyHead report_head;
    ElementPara water_level;      // 瞬时水位（0.01m，HEX编码）
    ElementPara voltage;          // 电压（0.01V，HEX编码）
}FixTimeReportBody;

//上行测试报报文结构
typedef struct {
    HexFrameHeader header;
    TestReportBody test_report;     
    uint8_t end_marker;        // 结束符ETX/ETB
    uint16_t crc16;            // CRC16校验（大端序）
} M2UpTestFrame;


//上行小时报报文结构
typedef struct {
    HexFrameHeader header;
    HourlyReportBody hourly_report;       
    uint8_t end_marker;        // 结束符ETX/ETB
    uint16_t crc16;            // CRC16校验（大端序）
} M2UpHourlyFrame;


//上行定时报报文结构
typedef struct {
    HexFrameHeader header;
    FixTimeReportBody fixtime_report;     
    uint8_t end_marker;        // 结束符ETX/ETB
    uint16_t crc16;            // CRC16校验（大端序）
} M2UpFixtimeFrame;

//上行报文参数
typedef struct {
    
    uint8_t center_addr;//中心站站码
    uint8_t tele_addr[5];//遥测站站码
    uint16_t password;//密码
    int fd;//发送发端口句柄
} M2UPFramePara;

extern M2UPFramePara g_stM2UpFramepara;

typedef enum {
    FUNCTION_CODE_RESERVED_00H_2EH = 0x00,   // 00H - 2EH：保留，扩展功能码
    FUNCTION_CODE_LINK_MAINTENANCE = 0x2F,   // 2FH：链路维持报
    FUNCTION_CODE_TEST = 0x30,   // 30H：测试报
    FUNCTION_CODE_UNIFORM_TIME_HYDRO_INFO = 0x31,   // 31H：均匀时段水文信息报
    FUNCTION_CODE_TIMED_REPORT = 0x32,   // 32H：遥测站定时报
    FUNCTION_CODE_ADDITIONAL_REPORT = 0x33,   // 33H：遥测站加报报
    FUNCTION_CODE_HOURLY_REPORT = 0x34,   // 34H：遥测站小时报
    FUNCTION_CODE_MANUAL_SETTING = 0x35,   // 35H：遥测站人工置数报
    FUNCTION_CODE_PICTURE_INFO = 0x36,   // 36H：遥测站图片报或中心站查询遥测站图片采集信息
    FUNCTION_CODE_QUERY_REAL_TIME_DATA = 0x37,   // 37H：中心站查询遥测站实时数据
    FUNCTION_CODE_QUERY_TIME_PERIOD_DATA = 0x38,   // 38H：中心站查询遥测站时段数据
    FUNCTION_CODE_QUERY_MANUAL_SETTING = 0x39,   // 39H：中心站查询遥测站人工置数
    FUNCTION_CODE_QUERY_SPECIFIED_ELEMENT_DATA = 0x3A,   // 3AH：中心站查询遥测站指定要素数据
    FUNCTION_CODE_RESERVED_3B_3FH = 0x3B,   // 3B - 3FH：保留，扩展功能码
    FUNCTION_CODE_MODIFY_STATION_CONFIG = 0x40,   // 40H：中心站修改遥测站基本配置表
    FUNCTION_CODE_READ_STATION_CONFIG = 0x41,   // 41H：中心站读取遥测站基本配置表/遥测站自报基本配置表
    FUNCTION_CODE_MODIFY_RUNNING_PARAM_CONFIG = 0x42,   // 42H：中心站修改遥测站运行参数配置表
    FUNCTION_CODE_READ_RUNNING_PARAM_CONFIG = 0x43,   // 43H：中心站读取遥测站运行参数配置表/遥测站自报运行参数配置表
    FUNCTION_CODE_QUERY_PUMP_MOTOR_DATA = 0x44,   // 44H：中心站查询水泵电机实时工作数据
    FUNCTION_CODE_QUERY_TERMINAL_VERSION = 0x45,   // 45H：查询遥测终端软件版本
    FUNCTION_CODE_QUERY_STATION_STATUS = 0x46,   // 46H：查询遥测站状态和报警信息
    FUNCTION_CODE_INITIALIZE_STORAGE = 0x47,   // 47H：初始化固态存储数据
    FUNCTION_CODE_RESTORE_FACTORY_SETTINGS = 0x48,   // 48H：恢复终端出厂设置
    FUNCTION_CODE_MODIFY_PASSWORD = 0x49,   // 49H：修改密码
    FUNCTION_CODE_SET_STATION_CLOCK = 0x4A,   // 4A：设置遥测站时钟
    FUNCTION_CODE_SET_TERMINAL_IC_CARD_STATUS = 0x4B,   // 4B：设置遥测终端IC卡状态
    FUNCTION_CODE_CONTROL_PUMP_SWITCH = 0x4C,   // 4C：控制水泵开关命令/水泵状态信息自报
    FUNCTION_CODE_CONTROL_VALVE_SWITCH = 0x4D,   // 4D：控制阀门开关命令/阀门状态信息自报
    FUNCTION_CODE_CONTROL_GATE_SWITCH = 0x4E,   // 4E：控制闸门开关命令/闸门状态信息自报
    FUNCTION_CODE_WATER_QUANTITY_CONTROL = 0x4F,   // 4F：水量定值控制命令
    FUNCTION_CODE_QUERY_STATION_EVENT_RECORD = 0x50,   // 50H：中心站查询遥测站事件记录
    FUNCTION_CODE_QUERY_STATION_CLOCK = 0x51,   // 51H：中心站查询遥测站时钟
    FUNCTION_CODE_RESERVED_52H_DFH = 0x52,   // 52H - DFH：保留，扩展功能码
    FUNCTION_CODE_USER_DEFINED_EXTENSION_START = 0xE0   // E0H - FFH：用户自定义扩展区
} FunctionCode;




/**
 * 水文监测数据标识符枚举
 * 符合《编码要素及标识符汇总表》规范
 */
typedef enum {
    /* 表C.1 第一部分 */
    OBSERVATION_TIME_LEADER    = 0xF0, // 观测时间引导符^a^
    STATION_CODE_LEADER        = 0xF1, // 测站编码引导符^b^
    MANUAL_INPUT               = 0xF2, // 人工置数^c^
    IMAGE_INFO                 = 0xF3, // 图片信息^d^
    HOURLY_5MIN_RAINFALL       = 0xF4, // 1小时内每5分钟时段雨量^e^
    RELATIVE_WATER_LEVEL_1     = 0xF5, // 1小时内5分钟间隔相对水位1
    RELATIVE_WATER_LEVEL_2     = 0xF6, // 1小时内5分钟间隔相对水位2
    RELATIVE_WATER_LEVEL_3     = 0xF7, // 1小时内5分钟间隔相对水位3
    RELATIVE_WATER_LEVEL_4     = 0xF8, // 1小时内5分钟间隔相对水位4
    RELATIVE_WATER_LEVEL_5     = 0xF9, // 1小时内5分钟间隔相对水位5
    RELATIVE_WATER_LEVEL_6     = 0xFA, // 1小时内5分钟间隔相对水位6
    RELATIVE_WATER_LEVEL_7     = 0xFB, // 1小时内5分钟间隔相对水位7
    RELATIVE_WATER_LEVEL_8     = 0xFC, // 1小时内5分钟间隔相对水位8
    FLOW_VELOCITY_BATCH        = 0xFD, // 流速批量数据传输^f^

    /* 表C.1 第二部分 */
    CROSS_SECTIONAL_AREA       = 0x01, // 断面面积
    INSTANT_AIR_TEMP           = 0x02, // 瞬时气温
    INSTANT_WATER_TEMP         = 0x03, // 瞬时水温
    TIME_STEP_CODE             = 0x04, // 时间步长码^g^
    DURATION                   = 0x05, // 时段长,降水、引排水、抽水历时
    DAILY_EVAPORATION          = 0x06, // 日蒸发量
    CURRENT_EVAPORATION        = 0x07, // 当前蒸发
    ATMOSPHERIC_PRESSURE       = 0x08, // 气压
    GATE_OPEN_HEIGHT           = 0x09, // 闸坝、水库闸门开启高度
    EQUIPMENT_GROUP_NUM        = 0x0A, // 输水设备、闸门(组)编号
    EQUIPMENT_TYPE             = 0x0B, // 输水设备类别
    GATE_OPEN_COUNT            = 0x0C, // 水库、闸坝闸门开启孔数
    GROUND_TEMPERATURE         = 0x0D, // 地温
    GROUNDWATER_DEPTH          = 0x0E, // 地下水瞬时埋深
    WAVE_HEIGHT                = 0x0F, // 波浪高度
    SOIL_MOISTURE_10CM         = 0x10, // 10厘米处土壤含水量
    SOIL_MOISTURE_20CM         = 0x11, // 20厘米处土壤含水量
    SOIL_MOISTURE_30CM         = 0x12, // 30厘米处土壤含水量
    SOIL_MOISTURE_40CM         = 0x13, // 40厘米处土壤含水量
    SOIL_MOISTURE_50CM         = 0x14, // 50厘米处土壤含水量
    SOIL_MOISTURE_60CM         = 0x15, // 60厘米处土壤含水量
    SOIL_MOISTURE_80CM         = 0x16, // 80厘米处土壤含水量
    SOIL_MOISTURE_100CM        = 0x17, // 100厘米处土壤含水量
    HUMIDITY                   = 0x18, // 湿度
    OPERATING_UNITS            = 0x19, // 开机台数
    PRECIPITATION_1H           = 0x1A, // 1小时时段降水量
    PRECIPITATION_2H           = 0x1B, // 2小时时段降水量
    PRECIPITATION_3H           = 0x1C, // 3小时时段降水量
    PRECIPITATION_6H           = 0x1D, // 6小时时段降水量
    PRECIPITATION_12H          = 0x1E, // 12小时时段降水量
    DAILY_PRECIPITATION        = 0x1F, // 日降水量
    CURRENT_PRECIPITATION      = 0x20, // 当前降水量
    PRECIPITATION_1MIN         = 0x21, // 1分钟时段降水量
    PRECIPITATION_5MIN         = 0x22, // 5分钟时段降水量
    PRECIPITATION_10MIN        = 0x23, // 10分钟时段降水量
    PRECIPITATION_30MIN        = 0x24, // 30分钟时段降水量

    /* 表C.1 第三部分 */
    STORM_RAINFALL             = 0x25, // 暴雨量
    PRECIPITATION_ACCUMULATION = 0x26, // 降水量累计值
    INSTANT_FLOW_RATE          = 0x27, // 瞬时流量、抽水流量
    INTAKE_FLOW_1              = 0x28, // 取(排）水口流量1
    INTAKE_FLOW_2              = 0x29, // 取(排）水口流量2
    INTAKE_FLOW_3              = 0x2A, // 取(排）水口流量3
    INTAKE_FLOW_4              = 0x2B, // 取(排）水口流量4
    INTAKE_FLOW_5              = 0x2C, // 取(排）水口流量5
    INTAKE_FLOW_6              = 0x2D, // 取(排）水口流量6
    INTAKE_FLOW_7              = 0x2E, // 取(排）水口流量7
    INTAKE_FLOW_8              = 0x2F, // 取(排）水口流量8
    TOTAL_OUTFLOW              = 0x30, // 总出库流量、过闸总流量
    EQUIPMENT_FLOW_RATE        = 0x31, // 输水设备流量、过闸(组)流量
    SEDIMENT_LOAD              = 0x32, // 输沙量
    WIND_DIRECTION             = 0x33, // 风向
    WIND_FORCE                 = 0x34, // 风力(级)
    WIND_SPEED                 = 0x35, // 风速
    AVG_SECTION_VELOCITY       = 0x36, // 断面平均流速
    INSTANT_VELOCITY           = 0x37, // 当前瞬时流速
    POWER_VOLTAGE              = 0x38, // 电源电压
    INSTANT_RIVER_LEVEL        = 0x39, // 瞬时河道水位、潮位
    DOWNSTREAM_WATER_LEVEL     = 0x3A, // 库(闸、站)下水位
    UPSTREAM_WATER_LEVEL       = 0x3B, // 库(闸、站)上水位
    INTAKE_LEVEL_1             = 0x3C, // 取(排）水口水位1
    INTAKE_LEVEL_2             = 0x3D, // 取(排）水口水位2
    INTAKE_LEVEL_3             = 0x3E, // 取(排）水口水位3
    INTAKE_LEVEL_4             = 0x3F, // 取(排）水口水位4
    INTAKE_LEVEL_5             = 0x40, // 取(排）水口水位5
    INTAKE_LEVEL_6             = 0x41, // 取(排）水口水位6
    INTAKE_LEVEL_7             = 0x42, // 取(排）水口水位7
    INTAKE_LEVEL_8             = 0x43, // 取(排）水口水位8
    SEDIMENT_CONCENTRATION     = 0x44, // 含沙量
    STATUS_ALARM_INFO          = 0x45, // 遥测站状态及报警信息
    PH_VALUE                   = 0x46, // pH值
    DISSOLVED_OXYGEN           = 0x47, // 溶解氧
    CONDUCTIVITY               = 0x48, // 电导率
    TURBIDITY                  = 0x49, // 浊度

    /* 表C.1 第四部分 */
    PERMANGANATE_INDEX         = 0x4A, // 高锰酸盐指数
    REDOX_POTENTIAL            = 0x4B, // 氧化还原电位
    AMMONIA_NITROGEN          = 0x4C, // 氨氮
    TOTAL_PHOSPHORUS           = 0x4D, // 总磷
    TOTAL_NITROGEN             = 0x4E, // 总氮
    TOTAL_ORGANIC_CARBON       = 0x4F, // 总有机碳
    COPPER                     = 0x50, // 铜
    ZINC                       = 0x51, // 锌
    SELENIUM                   = 0x52, // 硒
    ARSENIC                    = 0x53, // 砷
    TOTAL_MERCURY              = 0x54, // 总汞
    CADMIUM                    = 0x55, // 镉
    LEAD                       = 0x56, // 铅
    CHLOROPHYLL_A              = 0x57, // 叶绿素a
    WATER_PRESSURE_1           = 0x58, // 水压1
    WATER_PRESSURE_2           = 0x59, // 水压2
    WATER_PRESSURE_3           = 0x5A, // 水压3
    WATER_PRESSURE_4           = 0x5B, // 水压4
    WATER_PRESSURE_5           = 0x5C, // 水压5
    WATER_PRESSURE_6           = 0x5D, // 水压6
    WATER_PRESSURE_7           = 0x5E, // 水压7
    WATER_PRESSURE_8           = 0x5F, // 水压8
    WATER_METER_REMAIN_1       = 0x60, // 水表1剩余水量
    WATER_METER_REMAIN_2       = 0x61, // 水表2剩余水量
    WATER_METER_REMAIN_3       = 0x62, // 水表3剩余水量
    WATER_METER_REMAIN_4       = 0x63, // 水表4剩余水量
    WATER_METER_REMAIN_5       = 0x64, // 水表5剩余水量
    WATER_METER_REMAIN_6       = 0x65, // 水表6剩余水量
    WATER_METER_REMAIN_7       = 0x66, // 水表7剩余水量
    WATER_METER_REMAIN_8       = 0x67, // 水表8剩余水量
    WATER_METER_HOURLY_1       = 0x68, // 水表1每小时水量
    WATER_METER_HOURLY_2       = 0x69, // 水表2每小时水量
    WATER_METER_HOURLY_3       = 0x6A, // 水表3每小时水量
    WATER_METER_HOURLY_4       = 0x6B, // 水表4每小时水量
    WATER_METER_HOURLY_5       = 0x6C, // 水表5每小时水量
    WATER_METER_HOURLY_6       = 0x6D, // 水表6每小时水量
    WATER_METER_HOURLY_7       = 0x6E, // 水表7每小时水量
    WATER_METER_HOURLY_8       = 0x6F, // 水表8每小时水量

    /* 表C.1 第五部分 */
    AC_VOLTAGE_PHASE_A         = 0x70, // 交流A相电压
    AC_VOLTAGE_PHASE_B         = 0x71, // 交流B相电压
    AC_VOLTAGE_PHASE_C         = 0x72, // 交流C相电压
    AC_CURRENT_PHASE_A         = 0x73, // 交流A相电流
    AC_CURRENT_PHASE_B         = 0x74, // 交流B相电流
    AC_CURRENT_PHASE_C         = 0x75  // 交流C相电流
} HydroDataIdentifier;







void get_bcd_time(uint8_t *buffer) ;
int bcd_time_is_valid(const BCD_TIME *bcd_time);
void build_test_report(void *arg) ;
void build_hourly_report(void * arg);
void build_fixtime_report(void * arg);
void CheckFixtimeAndHourlyFrame(void *arg);

unsigned char bcd_to_dec(unsigned char bcd);

int set_system_time(const BCD_TIME *bcd_time);

#pragma pack(pop)  // 恢复默认内存对齐


#endif