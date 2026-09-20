#ifndef _RELAY_H
#define _RELAY_H

#include "Common.h"

#ifndef uint32_t
#define uint32_t unsigned int
#endif

#ifndef uint8_t
#define uint8_t unsigned char
#endif

#ifndef uint16_t
#define uint16_t unsigned short
#endif

#define RELAY_ADDR   0X01  //继电器地址
#define RELAY_COM0_ADDR 0X00  //端口0地址
#define RELAY_COM1_ADDR 0X01  //端口1地址
#define WRITE_SIGLE_COIL  0X05 //写单个线圈功能码
#define NO_NC   0xff00 //常开 ->常闭
#define NC_NO  0x0000 //常闭 ->常开
#define RED_LIGHT  0X00fd
#define GREEN_LIGHT  0X00fa
#define BLUE_LIGHT  0X00fb
#define YELLOW_LIGHT  0X00fc
#define TURN_ON   0XFF00
#define TURN_OFF  0X0000

#pragma pack(push, 1)
/*继电器发送数据帧*/
typedef struct _RelaySendFrame
{
    uint8_t u8Addr;//地址
    uint8_t u8FUncCode;//功能码
    uint16_t u16RegAddr;//寄存器地址
    uint16_t u16RegData;//寄存器数据
    uint16_t u16CRC;//CRC校验
    
}RelaySendFrame;

typedef struct _ErrorState
{
    bool bMotorError;
    bool bEncoderError;
    bool bRealyError;
    bool bSensorError;
    bool bZigbeeError;
}ErrorState;

extern ErrorState g_stErrorState;


#pragma pack(pop)
int SendDataToRelay(uint8_t relayAddr,uint16_t regAddr,uint16_t regData);
int Relay_MotorBreak(uint16_t regAddr) ;
int Relay_MotorUnbreak(uint16_t regAddr);
int Relay_MotorDrivePowerON(uint8_t relayAddr,uint16_t regAddr);
int Relay_MotorDrivePowerOFF(uint8_t relayAddr,uint16_t regAddr);
int Relay_LightCtrl(uint16_t LightColor,uint16_t State);
int SendDataTo_8_Port_Relay(uint8_t relayAddr,uint8_t funcode,uint16_t regAddr,uint16_t regData);
void ErrorStateProc(void * arg);
#endif