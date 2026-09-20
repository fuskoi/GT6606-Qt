#ifndef _SENSOR_H
#define _SENSOR_H

/*
 * [2026-06-22 修改说明 - Sensor.h]
 * 转子组合读寄存器定义、周期性DI/计数全局变量
 */

#include "Common.h"

//#define ROTATOR_ADDR 0X01//转子流速仪地址
#define ROTATOR_DI_STATUS_REG  0X0004//输入端口寄存器地址
#define ROTATOR_DI_VALUE_REG   0X0004// DI状态 + 计数值组合读取起始地址(读取3个寄存器: DI状态, CountH, CountL)
#define RORATOR_DI1_VALUE_REG  0X0005//开关量1计数值寄存器地址
#define RESET_VALUE_REG      0X0200  //重置开关量采集小板端口值寄存器
#define COULOMB_SOC_REG    0X0000//电量百分比
#define COULOMB_CHARGE_STATE_REG  0X001E  //充放电状态
#define COULOMB_VOLTAGE_REG    0X0002//电压
#define COULOMB_CURRENT_REG    0X0003//电流
#define COULOMB_REMAIN_CAPACITY_REG  0X0010//剩余容量mAh

#define ROTATOR_COMBINED_READ_REG_NUM  3  /* [修改] 一次读DI(reg4)+CountH(reg5)+CountL(reg6) */



#define MAX_RESEND_TIMES  3


#pragma pack(push, 1)
typedef struct _SensorFlag
{
	uint8_t u8Rotator_DI_flag;//转子流速仪端子状态标志
	uint8_t u8Ratator_value_falg;//转子流速仪值标志
	uint8_t u8Rotator_get_data_Flag;
	uint8_t u8NCS_get_data_flag;
	uint8_t u8Coulomb_SOC_Flag;
	uint8_t u8Coulomb_ChargeState_flag;
	uint8_t u8Coulomb_voltage_flag;
	uint8_t u8Coulomb_current_flag;
	uint8_t u8Coulomb_Remain_capa_flag;
	uint8_t u8GetWaterlevel_flag;
}SensorFlag;

//转子流速仪接收值
typedef struct _RotatorRecValue
{
	uint8_t u8SubAddr;
	uint8_t u8FuncCode;
	uint8_t u8DataByte;
	uint32_t u32Value;
	uint16_t u16CRC;	
}RotatorRecValue,BatteryRecValue;

//转子流速仪组合读取返回值(DI状态 + CountH + CountL)
typedef struct _RotatorRecCombined
{
    uint8_t u8SubAddr;
    uint8_t u8FuncCode;
    uint8_t u8DataByte;     // 0x06 (6字节数据)
    uint16_t u16DIStatus;   // SWITCH_DI_STATUS
    uint16_t u16CountH;     // SWITCH_1_COUNT_H
    uint16_t u16CountL;     // SWITCH_1_COUNT_L
    uint16_t u16CRC;
} RotatorRecCombined;

typedef struct _SwitchStatus
{
	uint16_t U16DI0            :1;//开关0
	uint16_t u16Resive2        :1;//保留2
	uint16_t u16watersurface_1 :1;//开门到位DI: 接地=1, 有电=0, 到位时有电(0)
	uint16_t u16watersurface_2 :1;//关门到位DI: 接地=1, 有电=0, 到位时有电(0)
	uint16_t u16TouchGround    :1;//[修改] 触底，转子采集板DI bit4
	uint16_t u16Resive         :11;//保留
}SwitchStatus;
//转子流速仪接收开关量
typedef struct _RotatorRecDIStatus
{
	uint8_t u8SubAddr;
	uint8_t u8FuncCode;
	uint8_t u8DataByte;
	uint16_t u16RecData;
	uint16_t u16CRC;	
}RotatorRecDIStatus,BatteryRecStatus;

typedef enum _DMA_5213_REGISTER_ID
{
    SWITCH_1_ADC_VALUE = 0,         //开关量1输入ADC值
    DMA_5213_VOLTAGE,               //电压值
    SWITCH_2_ADC_VALUE,             //开关量2输入ADC值
    SWITCH_3_ADC_VALUE,             //开关量3输入ADC值
    SWITCH_DI_STATUS,               //开关量输入状态
    SWITCH_1_COUNT_H = 5,           //开关量1计数值高字节
    SWITCH_1_COUNT_L,               //开关量1计数值低字节
    SWITCH_2_COUNT_H,               //开关量2计数值高字节
    SWITCH_2_COUNT_L,               //开关量2计数值低字节
    SWITCH_DO_STATUS,               //开关量输出状态      
    SWITCH_4_ADC_VALUE = 10,        //开关量4输入ADC值
    SWITCH_3_COUNT_H = 14,          //开关量3计数值高字节
    SWITCH_3_COUNT_L,               //开关量3计数值低字节
    SWITCH_4_COUNT_H,               //开关量4计数值高字节
    SWITCH_4_COUNT_L,               //开关量4计数值低字节
    SWITCH_RESET_VALUE_REG =0X0200  //重置开关量采集小板端口值寄存器

}DMA_5213_REGISTER_ID;          //开关量采集小板寄存器ID  

#pragma pack(pop)
extern SensorFlag g_stSensorFlag;
extern RotatorRecValue g_stRotatorRecValue;
extern RotatorRecDIStatus g_stRotatorRecDIStatus;
extern SwitchStatus g_stSwitchStatus;
extern SwitchStatus g_stDoorSwitchStatus;
extern BatteryRecStatus g_stBatteryRecStatus;
extern BatteryRecValue g_BatteryRecValue;

/* 电动门DI: 接地=1, 有电=0; 限位到位时有电, DI=0 */
#define DOOR_DI_GROUNDED   1
#define DOOR_DI_POWERED    0
#define DOOR_OPEN_REACHED()   (g_stDoorSwitchStatus.u16watersurface_1 == DOOR_DI_POWERED)
#define DOOR_CLOSE_REACHED()  (g_stDoorSwitchStatus.u16watersurface_2 == DOOR_DI_POWERED)

// ---- [修改] 转子流速仪周期性读取(150ms，main.c定时器触发) ----
extern uint32_t g_u32RotatorPeriodicCount;   /* [修改] 最新计数值(CountH<<16|CountL) */
extern uint16_t g_u16RotatorPeriodicDI;      /* [修改] 最新DI状态 */
extern volatile int g_rotatorPeriodicUpdated; /* 新数据到达标志 */


extern uint32_t g_SensorNum ;
extern uint8_t g_u8ResendTimes;
extern uint8_t g_batteryParaSeri ;
extern bool g_bDoorCtrlSendFlag;
//

int sendNCSMeasueCmd();
void ReadRotatorSpeedValue(void * arg);
void ResetRotatorValue(void * arg);
void ReadRotatorDIStatus(void * arg);
void ReadRotatorValuePeriodic(void * arg);   /* [修改] 150ms周期性组合读转子DI+计数 */
int SendADCPMeasureCmd();
int sendVE_ADCP_MeasureCmc();
int sendMUDS_MeasureCmc();
int SendTemp_MeasureCmd();
void  sendMeasureResult(uint32_t u32Element,int type);
int SensorRecDataProc(KFIFO_ID pData);
void SensorPrepareRxFifo(KFIFO_ID pData);   /* 解析前丢弃Modbus读请求TX回显 */

int DMA_5213_SendDataCmd(uint8_t SubAddr, uint8_t FuncCode, uint16_t RegAddr, uint16_t RegNum);

void DMA_5213_POWER_ON(int relay_com);

void DMA_5213_POWER_OFF(int relay_com);

int StartDoorStatusThread(void);

void Door_Open_cmd();

void Door_Close_cmd();

/* 静默后读一次门 DI，刷新 g_stDoorSwitchStatus */
void Door_ReadDiFresh(void);

/* 开到位则关，关到位则开；中间态默认关。期间静默 ZigBee（水位/状态/触摸屏让路） */
void Door_Toggle_cmd(void);

int ReadBatterySOCCmd();

int ReadBatteryVoltageCmd();

int ReadBatteryCurrentCmd();

int ReadBatteryChargeStateCmd();

int ReadBatteryRemainCapacityCmd();

void  ReadBatteryState(void *arg);

void SendMeasureResultManu(uint32_t u32Element,int type);
void SendMeasureResultAuto(int type);


#endif