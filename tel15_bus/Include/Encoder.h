#ifndef _ENCODER_H
#define _ENCODER_H

#include "Common.h"


#define Encoder_car_addr 0X01
#define Encoder_fish_addr 0X02
#define TOTAL_SPACE_REGADDR  0X0009
#define RESET_ZERO_REGADDR   0X0005
#define WRITE_SUBDATA_2    0X00FF
#define ENCODER_REG_NUM  2
#define ENCODE_HEAD1 0XAB
#define ENCODE_HEAD2 0XCD
#define ENCODE_DATALEN 0X05
#define ENCODE_TAIL 0X3D
#define ENCODE_HALF_BASE   0x200200    //编码器中间值
#pragma pack(push, 1)
typedef struct _EncoderSendFrame
{
    uint8_t u8SubAddr;
    uint8_t u8FunctionCode;
    uint16_t u16RegAddr;
    uint16_t u16RegNum;
    uint16_t u16CRC;
}EncoderSendFrame;


typedef struct _EncoderRecFrame_RTU
{
    uint8_t u8SubAddr;
    uint8_t u8FunctionCode;
    uint8_t u8ByteNUm;
    uint32_t u32RegData;
    uint16_t u16CRC;
}EncoderRecFrame_RTU;


typedef struct _EncoderRecFrame_FREE
{
    uint8_t u8Head1;
    uint8_t u8Head2;
    uint8_t u8DataLen;
    uint8_t u8TurnsNum_H;
    uint8_t u8TurnsNum_L;
    uint8_t u8SigleData_H;
    uint8_t u8SigleData_L;
    uint8_t u8SumCheck;
    uint8_t u8NorCheck;
    uint8_t u8Tail;
}EncoderRecFrame_FREE;

#pragma pack(pop)

extern int g_ReadEncoderDataFlag;
extern float g_HoriTotalSpace ;//编码器走过的水平圈数
extern float g_VeroTotalSpace ;//编码器走过的垂直圈数
extern uint8_t g_EncoderResetFlag ;


int ReadEncoderTotalData(uint8_t encoderAddr);
int ResetEncoder(uint8_t encoderAddr);
int EncoderRecDataProc(KFIFO_ID pData);
int SendDataToEncoder(uint8_t encoderAddr, uint8_t funcCode,uint16_t regAddr,int16_t regNum);
uint32_t FreeDataProc(KFIFO_ID pData ,  WORD *pUsed);
void  ReadEncoderDistencs(void * arg);
void InitCarHoriPositionAfterReset(void);
void ResetCarEncoderPosition(void);
int32_t GetCarHoriEncoderCm(void);
int32_t GetCarHoriSurveyPosCm(void);
int32_t GetCarHoriPosCm(void);
uint16_t GetCarHoriPosForTouchScreen(void);
uint16_t GetFishInwaterDeepForTouchScreen(void);
#endif