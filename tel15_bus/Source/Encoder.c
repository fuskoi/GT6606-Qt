#include "Encoder.h"
#include "CRC.h"
#include "RS485_ICD.h"
#include "WebSeriver.h"
#include "log.h"
#include <math.h>
#include "Relay.h"
#include "zigbee_dataproc.h"
int g_ReadEncoderDataFlag = 0;//可以读取编码器值标志
float g_HoriTotalSpace = 0;//编码器走过的水平圈数
float g_VeroTotalSpace = 0;//编码器走过的垂直圈数
uint8_t g_EncoderResetFlag = 0;//
uint8_t g_EncoderSeri = 0;

/* 编码器原始水平距离(cm)，不含起点距偏移 */
int32_t GetCarHoriEncoderCm(void)
{
    return (int32_t)(g_HoriTotalSpace * g_CarEncoderDistence);
}

/* 水尺起点距(cm) = 编码器距离 - i16StartPointDes */
int32_t GetCarHoriSurveyPosCm(void)
{
    return GetCarHoriEncoderCm()
         - (int32_t)g_stSetCableway.stSetCablewayBody.i16StartPointDes;
}

/* DTU/网页上报起点距(cm)，与 GetCarHoriSurveyPosCm 一致 */
int32_t GetCarHoriPosCm(void)
{
    return GetCarHoriSurveyPosCm();
}

uint16_t GetCarHoriPosForTouchScreen(void)
{
    int32_t posCm = GetCarHoriSurveyPosCm();

    if (posCm < -32768)
    {
        posCm = -32768;
    }
    else if (posCm > 32767)
    {
        posCm = 32767;
    }
    return (uint16_t)(int16_t)posCm;
}

uint16_t GetFishInwaterDeepForTouchScreen(void)
{
    int32_t deep = (int32_t)(g_VeroTotalSpace * g_FishEncoderDistence)
                 - (int32_t)g_stSetCableway.stSetCablewayBody.u32CableHight
                 + (int32_t)GetCurrentWaterLevel()
                 - (int32_t)FISH_BODY_HIGHT;

    if (deep < 0)
    {
        deep = 0;
    }
    else if (deep > 65535)
    {
        deep = 65535;
    }
    return (uint16_t)deep;
}

void InitCarHoriPositionAfterReset(void)
{
    g_HoriTotalSpace = 0;
    g_stTouchScreenReportData.u16CarPos = GetCarHoriPosForTouchScreen();
}

void ResetCarEncoderPosition(void)
{
    ResetEncoder(g_stDeviceAddr.u8Encoder_car);
    g_HoriTotalSpace = 0;
    g_EncoderResetFlag = 3;
    g_stTouchScreenReportData.u16CarPos = GetCarHoriPosForTouchScreen();
}

static bool CarEncoderRawCmIsPlausible(float rawCm)
{
    uint32_t maxCm = g_stSetCableway.stSetCablewayBody.u32MaxDistance;

    if (maxCm == 0)
    {
        maxCm = 50000;
    }
    return fabsf(rawCm) <= ((float)maxCm + 200.0f);
}
/*
*函数名称:SendDataToEncoder
*函数功能:向编码器发送数据
*输   入:uint8_t encoderAddr,  编码器地址 uint8_t funcCode,       功能码uint16_t regAddr,     寄存器地址      int16_t regNum   寄存器数量
*输   出:无
*返   回:成功      失败
*/
int SendDataToEncoder(uint8_t encoderAddr, uint8_t funcCode,uint16_t regAddr,int16_t regNum)
{
    int WriteByte = 0;
    uint8_t buff[20] = {0};
    EncoderSendFrame stSendFrame;
    stSendFrame.u8SubAddr = encoderAddr;
    stSendFrame.u8FunctionCode = funcCode;
    stSendFrame.u16RegAddr = regAddr;
    stSendFrame.u16RegNum = regNum;

    stSendFrame.u16RegAddr = swap_u16(stSendFrame.u16RegAddr);
    stSendFrame.u16RegNum = swap_u16(stSendFrame.u16RegNum);
    stSendFrame.u16CRC = CRC16_Modbus((uint8_t * )&stSendFrame,sizeof(EncoderSendFrame) - 2);
    stSendFrame.u16CRC = swap_u16(stSendFrame.u16CRC);
    WriteByte = UartWrite(g_stDevicelist.UartList[g_stDevicelist.Encoder].i32fd,(unsigned char *)&stSendFrame,sizeof(EncoderSendFrame));
    memcpy(buff,&stSendFrame,sizeof(EncoderSendFrame));
    //int i = 0;
    // printf("Send Data to Encoder is :");
    // for(i = 0;i< sizeof(EncoderSendFrame);i++)
    // {
    //     printf("%02x ",buff[i]);
    // }
    // printf("\n");
    LOG(LOG_INFO,"Encoder Send Data is");
    LOG_ARRAY(LOG_INFO,buff,sizeof(EncoderSendFrame),"%02x");
    if(sizeof(EncoderSendFrame)!= WriteByte)
    {
        LOG(LOG_ERROR,"Encoder send %d bytes,need send byte is %d",WriteByte,sizeof(EncoderSendFrame));
        return ERROR;
    }
    return SUCCESS;
}


/*
*函数名称:ReadEncoderTotalData
*函数功能:读取编码器总值
*输   入:uint8_t encoderAddr,  编码器地址 
*输   出:无
*返   回:成功      失败
*/
int ReadEncoderTotalData(uint8_t encoderAddr)
{
    //printf("Send data to Encoder***\n");
    if(ERROR == SendDataToEncoder(encoderAddr,READCODE,TOTAL_SPACE_REGADDR,ENCODER_REG_NUM))
    {
        return ERROR;
    }
    g_ReadEncoderDataFlag = 1;      
    return SUCCESS;
}

void ReadEncoderDistencs(void * arg)
{
    switch (g_EncoderSeri++)
    {
        case 0:
            ReadEncoderTotalData(g_stDeviceAddr.u8Encoder_car);
            break;
        case 1:
            ReadEncoderTotalData(g_stDeviceAddr.u8Encoder_fish);
            g_EncoderSeri = 0;
        break;
    
        default:
        g_EncoderSeri = 0;
        break;
    }

}


int ResetEncoder(uint8_t encoderAddr)
{
    if(ERROR == SendDataToEncoder(encoderAddr,WRITECODE,RESET_ZERO_REGADDR,WRITE_SUBDATA_2))
    {
        return ERROR;
    }
    g_ReadEncoderDataFlag = 1;      
    return SUCCESS;
}

int EncoderRecDataProc(KFIFO_ID pData)
{
    EncoderRecFrame_RTU stEncoderRec;
    uint16_t u16RecCRC = 0;
    uint8_t tempbuff[128] ={0};
    uint32_t u32fifolen = 0;
    u32fifolen = kfifo_len (pData);
    
    if(u32fifolen < 4)
    {
        return ERROR;
    }
    g_stErrorState.bEncoderError = 0;
    /* if(u32fifolen > 54)//6tiaozhiling
    {
        kfifo_clear(pData);
        LOG(LOG_ERROR,"Encoder receive data len Error,data cleared");
        return ERROR;
    } */
    if(u32fifolen >=  sizeof(EncoderRecFrame_RTU))
    {
        if(g_EncoderResetFlag > 0)//清编码器复位后的旧应答
        {
            g_EncoderResetFlag--;
            kfifo_clear(pData);
        }
        
        else
        {
            uint8_t u8DeviceType = uartFindDeviceType(pData,Encoder_car_addr,READCODE,Encoder_fish_addr,READCODE);
            if(u8DeviceType  == 0)
            {
                return ERROR;
            }
            kfifo_get(pData,tempbuff,sizeof(EncoderRecFrame_RTU));//读取9个字节
            LOG(LOG_INFO,"Encoder Rec Data is :");
            LOG_ARRAY(LOG_INFO,tempbuff,sizeof(EncoderRecFrame_RTU),"%02x");
            memcpy(&stEncoderRec,tempbuff,sizeof(EncoderRecFrame_RTU));
            u16RecCRC = CRC16_Modbus(tempbuff,sizeof(EncoderRecFrame_RTU) -2);
            if(u16RecCRC != swap_u16( stEncoderRec.u16CRC))
            {
                LOG(LOG_ERROR,"Encoder recData CRC is not Equal,genaratr CRC is %04x,rec CRC is %04x",u16RecCRC,stEncoderRec.u16CRC);
                return ERROR;
            }
            stEncoderRec.u32RegData = swap_u32( stEncoderRec.u32RegData);
            LOG(LOG_INFO,"stEncoderRec.u32RegData is %d", stEncoderRec.u32RegData);
            
            switch (stEncoderRec.u8SubAddr)
            {
                case Encoder_car_addr:
                {
                    float newSpace = ((float)stEncoderRec.u32RegData - ENCODE_HALF_BASE) / 1024;
                    float rawCm = newSpace * g_CarEncoderDistence;

                    if (!CarEncoderRawCmIsPlausible(rawCm))
                    {
                        LOG(LOG_WARNING, "ignore car encoder raw %.0f cm, reg=%u", rawCm, stEncoderRec.u32RegData);
                        g_stTouchScreenReportData.u16CarPos = GetCarHoriPosForTouchScreen();
                        break;
                    }

                    g_HoriTotalSpace = newSpace;

                    LOG(LOG_INFO,"Car stEncoderRec.u32RegData is %d,circel is %.4f,position is %.2f cm",stEncoderRec.u32RegData,g_HoriTotalSpace,g_HoriTotalSpace * g_CarEncoderDistence);
                    g_stTouchScreenReportData.u16CarPos = GetCarHoriPosForTouchScreen();
                    break;
                }
                case Encoder_fish_addr:
                    g_VeroTotalSpace =((float)stEncoderRec.u32RegData - ENCODE_HALF_BASE) / 1024;

                    LOG(LOG_INFO,"Fish stEncoderRec.u32RegData is %d,circel is %.4f,Position  is %.2f cm",stEncoderRec.u32RegData,g_VeroTotalSpace,g_VeroTotalSpace * g_FishEncoderDistence);
                    break;
                default:
                    break;
            }
            

        }
        
        return SUCCESS;
    }
    else
    {
        return ERROR;
    }
    
    
}


/*
*函数名称:FreeDataProc
*函数功能:编码器自由协议数据处理
*输   入:KFIFO_ID pData   数据缓区  ,  WORD *pUsed  已经使用的数据总长
*输   出:无
*返   回:成功      失败
*/
uint32_t FreeDataProc(KFIFO_ID pData ,  WORD *pUsed)
{
    bool bHeadflag;
    uint8_t u8SumCheck = 0;
    uint8_t u8NorCheck = 0;
    //uint16_t u16CrcCode = 0;
    EncoderRecFrame_FREE stEncodeFree;;
    uint8_t tempbuff[1024] ={0};
    uint32_t u32fifolen = 0;
    bHeadflag = uartFindFrameHead(pData,ENCODE_HEAD1,ENCODE_HEAD2);//检测帧头是否正确
    if(!bHeadflag)
    {
        return ERROR;
    }
    kfifo_preview(pData, tempbuff, 3);//查找帧长度，编码器固定返回0x05,
    u32fifolen = kfifo_len(pData);
    if((tempbuff[2] + 5) >= u32fifolen)//自由协议帧长度10个字节，编码器固定返回0x05，如果缓冲区中帧长小于10，返回错误，读取下一帧
    {
        return ERROR;
    }
    kfifo_get(pData,tempbuff,(tempbuff[2] + 5));//读取10个字节
    memcpy(&stEncodeFree,tempbuff,sizeof(EncoderRecFrame_FREE));
    u8SumCheck = stEncodeFree.u8DataLen + stEncodeFree.u8TurnsNum_H + stEncodeFree.u8TurnsNum_L + stEncodeFree.u8SigleData_H + stEncodeFree.u8SigleData_L;//计算和校验
    u8NorCheck = stEncodeFree.u8DataLen ^ stEncodeFree.u8TurnsNum_H ^ stEncodeFree.u8TurnsNum_L ^ stEncodeFree.u8SigleData_H ^ stEncodeFree.u8SigleData_L;//计算异或校验
    if((u8SumCheck != stEncodeFree.u8SumCheck)  || (u8NorCheck != stEncodeFree.u8NorCheck))
    {
        return ERROR;
    }
	//编码器总值 = 圈数值 + 单圈值
    g_HoriTotalSpace = (((stEncodeFree.u8TurnsNum_H << 8) + stEncodeFree.u8TurnsNum_L)  +  ((stEncodeFree.u8SigleData_H << 8 ) + stEncodeFree.u8SigleData_L)) ;

    return SUCCESS;          


}


