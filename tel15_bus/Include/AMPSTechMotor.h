#ifndef _AMPSTechMotor_H_
#define _AMPSTechMotor_H_

#include "Common.h"

#define SET_SPEED_REG    0X8C00//设置转速寄存器地址(4byte)
#define SET_SPEED_UP     0X7930//设置加速寄存器地址(4byte)
#define SET_SPEED_DOWN   0X7940//设置减速寄存器地址(4byte)
#define SET_CTRL_STATUS  0X7400//设置控制状态寄存器地址(2byte)
#define READ_ACTUAL_SPEED  0X76C0 // 读取实际转速寄存器地址(4byte)
#define READ_DIO_STATUS    0x420B//读取DI状态寄存器地址(2byte)
#define READ_CTRL_STATUS   0X7410//读取控制状态寄存器地址(2byte)
#define ENABLE             0X000F    //启动
#define DISABLE            0X0006    //停止
#define AMPS_FWD           (1)
#define AMPS_BWD           (-1)




#pragma pack(push, 1)
typedef struct _Modbus_SignleReg
{
    uint8_t      u8DeviceAddr;         //电机地址
    uint8_t      u8FunCode;      //功能码
    uint16_t     u16RegAddr;        //寄存器地址
    uint16_t     u16RegData;    //寄存器数据
    uint16_t     u16CRC;         //CRC校验
}Modbus_SignleRegSend,Modbus_SignleRegRecv;



typedef struct Modbus_MultiReg
{
    uint8_t      u8DeviceAddr;         //电机地址
    uint8_t      u8FunCode;      //功能码
    uint16_t     u16RegAddr;        //寄存器地址
    uint16_t     u16RegNum;//寄存器数量
    uint8_t      u8DataByte;//数据字节
    int16_t      i16RegDataLow;//寄存器数据
    int16_t      i16RegDataHigh; //  
    uint16_t     u16CRC;         //CRC校验
    /* data */
}Modbus_MultiRegSend;
#pragma pack(pop)

int AMPS_WriteSignle_Reg(uint8_t u8DeviceAddr,uint8_t FuncCode,uint16_t RegAddr,uint16_t RegData);
int AMPS_WriteMulti_reg(uint8_t u8DeviceAddr,uint8_t FuncCode,uint16_t RegAddr, uint16_t RegCount,int32_t RegData);
int SetSpeed(uint8_t MotorAddr,int32_t i32Speed);
int SetSpeedUP(uint8_t MotorAddr,uint32_t Speed);
int SetSpeedDown(uint8_t MotorAddr,uint32_t Speed);
int SetCtrlState(uint8_t MotorAddr,uint8_t State);
int Read_DI_Status(uint8_t MotorAddr);
int AMPS_MotorMove(uint8_t MotorAddr,int32_t Speed);
int AMPS_MotorStop(uint8_t MotorAddr);
int ReadActualSpeed(uint8_t MotorAddr);
DI_STATUS_FLAG ReadStatusRecProc(uint16_t recData);
void ReadDIStatusTimerCallback(void *arg);
extern int g_iManualCarMoveDir;
extern int g_iManualFishMoveDir;
#endif
