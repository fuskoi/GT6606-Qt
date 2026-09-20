#ifndef _RS485_ICD_H
#define _RS485_ICD_H

#include "Common.h"



#define READCODE  0x03                //读单个寄存器操作
#define WRITECODE 0x06               //写单个寄存器操作
#define READ_RELAY_CODE   0X04
#define WRITEMULREGCODE  0X10        
#define FWD  0X02                     //反转
#define BWD  0X01                     //正转
#define UP   0X01                     //正转
#define DOWN 0X02                    //反转
#define STOP  0X05                   //停止
#define ERR_RESET  0X07               //故障重置
#define SET_SPEED  0x0BB8             //设置转速
#define W_FUNCODE_ENABLE  0X01        //使能写功能码
//#define g_stDeviceAddr.u8Motor_Drive_car   0X01             //水平电机地址
//#define g_stDeviceAddr.u8Motor_Drive_fish   0X02             //垂直电机地址
#define READ_CMD_REGDATA  0x0001      //读取数据指令寄存器数据   

#define MOTOR_MIN_RPM   200   //电机最低转速


#define CTRL_ADDR               0X2000      //通讯控制命令寄存器地址
#define SET_SPEED_ADDR          0X2001      //设定转速地址
#define SET_SPEED_UP_TIME       0X2003      //设置加速时间地址
#define SET_SPEED_DOWN_TIME     0X2004      //设置减速时间复制
#define READ_SPEED_REG_ADDR     0X3006      //读取转速寄存器地址
#define READ_VOLTAGE_REG_ADDR   0X3003      //读取电压寄存器地址
#define READ_CURRENT_REG_ADDR   0X3004      //读取电流寄存器地址
#define READ_COMSTATUS_REG_ADDR 0X300D      //读取输入端子状态地址

//输入端子功能
typedef enum DI_COM
{
    DI3_FWD_LIMIT = 1,	 //DI3正转限位
    DI4_REV_LIMIT = 2,   //DI4反转限位  
    //DI5_BREAK = 16       //DI5刹车

}DI_COM;

#pragma pack(push, 1)

//输入状态标志
typedef struct DI_STATUS_FLAG
{
    uint8_t u8FWD_DI_FLAG;        //正转
    uint8_t u8Rev_DI_FLAG;        //反转
    uint8_t u8FWD_LIMIT_FLAG;     //正转限位
    uint8_t u8REV_LIMIT_FLAG;     //反转限位
    uint8_t u8BREAK_DI_FLAG;      //刹车
    int32_t i32OutRPM;           // 输出转速
    int32_t i32SetRPM ;          //设定转速
    uint8_t g_GetMotorSpeedFlag ;
}DI_STATUS_FLAG,MOTOR_STATE;



typedef struct _frameHead
{
    uint16_t u16Head;
    uint16_t u16Length;
    uint16_t u16FrameSeri;
}frameHead;

typedef struct _frameBody
{
    uint8_t      u8MotorAddr;         //电机地址
    uint8_t      u8FunCode;      //功能码
    uint16_t     u16RegAddr;        //寄存器地址
    uint16_t     u16RegData;    //寄存器数据
    uint16_t     u16CRC;         //CRC校验
}frameBody;

typedef struct SEND_TO_DC_MOTOR_FRAME   //发送给电机的帧结构定义
{
    frameHead stFrameHead;
    frameBody stFrameBody;
}stSEND_TO_DC_MOTOR_FRAME;

typedef struct _WRITETWOREGFRAME
{
    uint8_t u8MotorAddr;
    uint8_t u8FunctionCode;
    uint16_t u16RegAddr;
    uint16_t u16RegNum;
    uint8_t u8DataNum;
    uint16_t u16Reg1Data;
    uint16_t u16Reg2Data;
    uint16_t u16CRC;
    
}WRITETWOREGFRAME;


typedef enum MODBUS_ERROR_CODE  //MODBUS异常码
{
    ILL_CODE = 0X01,        //非法命令
    ILL_ADDR,               //非法数据地址
    ILL_DATA,               //非法数据值
    OPERA_FAIL,             //操作失败
    ERR_PWD,                //密码错误
    DATA_FRAME_ERR,         //数据帧错误
    ONLY_READ_PARA,         //参数为只读
    INOPERATING             //参数运行不可改（上位机写操作时更改的参数为运行中不可更改的参数；）

}stMODBUS_ERROR_CODE;

typedef struct _ModbusSendFrame 
{
	uint8_t u8SubAddr;
	uint8_t u8Funcode;
	uint16_t u16RegAddr;
	uint16_t u16RegData;
	uint16_t u16CRC;
}ModbusSendFrame ;


typedef struct _ModbusRecFrame 
{
	uint8_t u8SubAddr;
	uint8_t u8Funcode;
	uint8_t u8DataByte;
	uint16_t u16RegData;
	uint16_t u16CRC;
}ModbusRecFrame ;

typedef struct {
    uint8_t u8SlaveAddr;
    uint8_t u8FunctionCode;
    uint16_t  u16StartAddr;
    uint16_t u16data;
    uint16_t u16CRC; 
} MODBUS_FRAME;

typedef struct _DMA_5213_DOSTATUS
{
    uint16_t  u16DO0_STATUS  :1;    //开关量0输出状态
    uint16_t  u16DO1_STATUS  :1;    //开关量1输出状态
    uint16_t  u16DO2_STATUS  :1;    //开关量2输出状态
    uint16_t  u16DO3_STATUS  :1;    //开关量3输出状态
    uint16_t  u16Reserve2    :12;   //保留
}DMA_5213_DOSTATUS;             //开关量输出状态

typedef enum _DMA_5213_RELAY_CMD {

    RELAY_OFF = 0,
    RELAY_ON = 1
    
}DMA_5213_RELAY_CMD;

typedef enum _DMA_5213_SWITCH_COM 
{
    DOOR_OPEN = 1,
    DOOR_CLOSE = 2,
    
}DMA_5213_SWITCH_COM;

#pragma pack(pop)
extern int g_ReadComSta_flag;//读取输入端子状态标志
extern int g_ReadSpeed_flag;//读取速度标志
extern DI_STATUS_FLAG g_stHoriDIStatusFlag; //水平电机输入端子状态
extern DI_STATUS_FLAG g_stVeroDIStatusFlag;//垂直电机输入端子状态

int UartReceiveDataProc(enum UartType StaticType,KFIFO_ID pData,WORD* pUserd);

int32_t HostComputerRecProc(KFIFO_ID pData,WORD* pUserd);
int MotorRecProc(KFIFO_ID pData,WORD* pUserd);
int SendDatatoMotor(uint8_t motorAddr, uint8_t funcCode,uint16_t regAddr,uint16_t regData);
uint16_t readSpeedCmd(uint8_t motorAddr);
uint16_t readVoltageCmd(uint8_t motorAddr);
uint16_t readCurrentCmd(uint8_t motorAddr);
uint32_t GetCurrentWaterLevel();
uint16_t DC_MotorMoveCtrl(uint8_t motorAddr,uint16_t MoveCmd);
uint16_t SetMotorRPM(uint8_t motorAddr,uint16_t wheelDm,uint16_t HoriSpeed);
uint16_t GenarateRpm(uint16_t Speed);
/* 缆道线速度(cm/s，网页0.5m/s→50)+行轮直径(mm)→电机RPM，封顶3000；无效时回退默认 */
uint16_t CablewayLinearSpeedToRpm(uint16_t speed_cm_s, uint16_t wheel_dm_mm);
uint16_t GetCarRunRpm(void);
uint16_t GetFishRunRpm(void);
uint16_t readComStatus(uint8_t motorAddr);
uint16_t ResetError(uint8_t motorAddr);
int WriteTwoMotorRegs(uint8_t motorAddr,uint16_t regAddr,uint16_t regCount,uint16_t reg1Data,uint16_t reg2Data);
void  MotorDrivePowerOFF();

void updateActiveTime();
time_t GetLastActiveTime(void);
int CarMoveAllowed(int direction);
int FishMoveAllowed(int direction);
void ManualMoveLimitProtect(void);
#endif