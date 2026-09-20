#include "Relay.h"
#include "CRC.h"
#include "log.h"
#include "WebSeriver.h"
/*
*函数名称:SendDataToRelay
*函数功能:向继电器发送数据
*输   入:uint16_t regAddr  寄存器地址,uint16_t regData  寄存器数据  
*输   出:无
*返   回:成功      失败
*/
ErrorState g_stErrorState;

int SendDataToRelay(uint8_t relayAddr,uint16_t regAddr,uint16_t regData)
{
    int WriteByte = 0;
    uint8_t buff[20] = {0};
    RelaySendFrame stSendData;
    stSendData.u8Addr = relayAddr;//RELAY_ADDR;
	stSendData.u8FUncCode = WRITE_SIGLE_COIL;
	stSendData.u16RegAddr = swap_u16(regAddr);
	stSendData.u16RegData = swap_u16(regData);
	stSendData.u16CRC = CRC16_Modbus((unsigned char *)&stSendData,sizeof(RelaySendFrame) - 2);
	stSendData.u16CRC = swap_u16(stSendData.u16CRC);
	WriteByte = UartWrite(g_stDevicelist.UartList[g_stDevicelist.Relay].i32fd,(unsigned char *)&stSendData,sizeof(RelaySendFrame));
    memcpy(buff,&stSendData,sizeof(RelaySendFrame));
    LOG(LOG_INFO,"Send Data to Relay is :");
    LOG_ARRAY(LOG_INFO,buff,sizeof(RelaySendFrame),"%02x");
    if(sizeof(RelaySendFrame)!= WriteByte)
    {
        LOG(LOG_ERROR,"Error :write %d bytes\n",WriteByte);
        return ERROR;
    }
    return SUCCESS;
}

/*
*函数名称:Relay_MotorBreak
*函数功能:刹车
*输   入:uint16_t regAddr  寄存器地址
*输   出:无
*返   回:成功      失败
*/

int Relay_MotorBreak(uint16_t regAddr) 
{
	return SendDataToRelay(g_stDeviceAddr.u8Relay_2_port2_break,regAddr,NC_NO);//刹车条件是继电器由常闭到常开，刹车断电失磁
}

/*
*函数名称:Relay_MotorUnbreak
*函数功能:取消刹车
*输   入:uint16_t regAddr  寄存器地址
*输   出:无
*返   回:成功      失败
*/

int Relay_MotorUnbreak(uint16_t regAddr)
{
	return SendDataToRelay(g_stDeviceAddr.u8Relay_2_port2_break,regAddr,NO_NC);//取消刹车条件是继电器常开到常闭，刹车通电励磁
}

/*
*函数名称:Relay_MotorDrivePowerON
*函数功能:电机驱动器上电
*输   入:uint8_t relayAddr    继电器地址,uint16_t regAddr   继电器端口
*输   出:无
*返   回:成功      失败
*/
int Relay_MotorDrivePowerON(uint8_t relayAddr,uint16_t regAddr)
{
    LOG(LOG_DEBUG,"MotorDrive %d Power ON",regAddr);
    return SendDataToRelay(relayAddr,regAddr,NO_NC);
}


/*
*函数名称:Relay_MotorDrivePowerOFF
*函数功能:电机驱动器断电
*输   入:uint8_t relayAddr    继电器地址,uint16_t regAddr   继电器端口
*输   出:无
*返   回:成功      失败
*/
int Relay_MotorDrivePowerOFF(uint8_t relayAddr,uint16_t regAddr)
{
    LOG(LOG_DEBUG,"MotorDrive %d PowerOFF",regAddr);
    return SendDataToRelay(relayAddr,regAddr,NC_NO);
}


int SendDataTo_8_Port_Relay(uint8_t relayAddr,uint8_t funcode,uint16_t regAddr,uint16_t regData)
{
    int WriteByte = 0;
    uint8_t buff[20] = {0};
    RelaySendFrame stSendData;
    stSendData.u8Addr = relayAddr;//RELAY_ADDR;
	stSendData.u8FUncCode = funcode;
	stSendData.u16RegAddr = swap_u16(regAddr);
	stSendData.u16RegData = swap_u16(regData);
	stSendData.u16CRC = CRC16_Modbus((unsigned char *)&stSendData,sizeof(RelaySendFrame) - 2);
	stSendData.u16CRC = swap_u16(stSendData.u16CRC);
	WriteByte = UartWrite(g_stDevicelist.UartList[g_stDevicelist.Relay].i32fd,(unsigned char *)&stSendData,sizeof(RelaySendFrame));
    memcpy(buff,&stSendData,sizeof(RelaySendFrame));
    LOG(LOG_INFO,"Send Data to  8_Port Relay is :");
    LOG_ARRAY(LOG_INFO,buff,sizeof(RelaySendFrame),"%02x ");
    if(sizeof(RelaySendFrame)!= WriteByte)
    {
        printf("Error :write %d bytes\n",WriteByte);
        return ERROR;
    }
    return SUCCESS;
}
int Relay_LightCtrl(uint16_t LightColor,uint16_t State)
{   //03 0F 00 FA 00 08 01 00 A7 58 
    uint8_t sendbuff[10]= {0x03, 0x0f, 0x00, 0xfa, 0x00, 0x08, 0x01, 0x00, 0xA7, 0x58};

    int WriteByte = UartWrite(g_stDevicelist.UartList[g_stDevicelist.Relay].i32fd,sendbuff,sizeof(sendbuff));//全关
    if(WriteByte != sizeof(sendbuff))
    {
        LOG(LOG_ERROR,"8 PORT Realy turn all port off error");
        return ERROR;
    }
    mSleep(100);
    
    return SendDataTo_8_Port_Relay(g_stDeviceAddr.u8Relay_8_port ,WRITE_SIGLE_COIL,LightColor,State);
}

void ErrorStateProc(void * arg)
{
	if(/* g_stErrorState.bMotorError == 0 && */ g_stErrorState.bEncoderError == 0 && g_stErrorState.bSensorError == 0 && g_stErrorState.bZigbeeError == 0
         && g_stReportState.u16CarVol > (g_stSetCableway.stSetCablewayBody.u16HoriLowestVol * 100) )
	{
		Relay_LightCtrl(GREEN_LIGHT,TURN_ON);
	}
    else
    {
        Relay_LightCtrl(GREEN_LIGHT,TURN_ON);
    }

}