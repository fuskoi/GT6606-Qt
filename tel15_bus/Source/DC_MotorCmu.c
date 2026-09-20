/*文件说明：与直流电机进行通信的相关内容，包括接收到上位机数据、电机数据的处理，向电机发送数据的接口*/

#include "RS485_ICD.h"
#include "CRC.h"
#include "zigbee_dataproc.h"
#include <math.h>
#include "WebSeriver.h"
#include "Relay.h"
#include "log.h"
#include "Encoder.h"
#include "AMPSTechMotor.h"
int g_ReadSpeed_flag = 0;//读取电机速度标志  1-发送过读取电机转速指令，可以读取电机转速  0-未发送过电机读取转速指令，不读取电机转速
int g_ReadComSta_flag = 0;//读取输入端子状态标志 1-发送过读取电机转速指令，可以读取电机转速  0-未发送过电机读取转速指令，不读取电机转速
DI_STATUS_FLAG g_stHoriDIStatusFlag;//水平电机输入端子状态
DI_STATUS_FLAG g_stVeroDIStatusFlag;//垂直电机输入端子状态

int CarMoveAllowed(int direction)
{
    if (direction > 0 && g_stHoriDIStatusFlag.u8FWD_LIMIT_FLAG)
    {
        LOG(LOG_WARNING, "Car FWD blocked: at FWD limit");
        return 0;
    }
    if (direction < 0 && g_stHoriDIStatusFlag.u8REV_LIMIT_FLAG)
    {
        LOG(LOG_WARNING, "Car BWD blocked: at REV limit");
        return 0;
    }
    return 1;
}

int FishMoveAllowed(int direction)
{
    if (direction > 0 && g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG)
    {
        LOG(LOG_WARNING, "Fish FWD(down) blocked: at FWD limit");
        return 0;
    }
    if (direction < 0 && g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG)
    {
        LOG(LOG_WARNING, "Fish BWD(up) blocked: at REV limit");
        return 0;
    }
    return 1;
}

void ManualMoveLimitProtect(void)
{
    if (g_motionState != MOTION_IDLE && g_motionState != MOTION_DONE)
    {
        return;
    }
    if (g_u8CatState == IS_RUNNING_STATE && g_iManualCarMoveDir != 0)
    {
        if ((g_iManualCarMoveDir > 0 && g_stHoriDIStatusFlag.u8FWD_LIMIT_FLAG)
            || (g_iManualCarMoveDir < 0 && g_stHoriDIStatusFlag.u8REV_LIMIT_FLAG))
        {
            LOG(LOG_WARNING, "Car manual jog limit hit, dir=%d", g_iManualCarMoveDir);
            AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
            Relay_MotorBreak(RELAY_COM0_ADDR);
        }
    }
    if (g_u8FishState == IS_RUNNING_STATE && g_iManualFishMoveDir != 0)
    {
        if ((g_iManualFishMoveDir > 0 && g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG)
            || (g_iManualFishMoveDir < 0 && g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG))
        {
            LOG(LOG_WARNING, "Fish manual jog limit hit, dir=%d", g_iManualFishMoveDir);
            AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
            Relay_MotorBreak(RELAY_COM1_ADDR);
        }
    }
}


/*
*函数名称：HostComputerRecProc
*函数功能：从上位机接收到的数据处理（可同zigbee一样）
*输   入：KFIFO_ID pData 需要处理的数据缓冲区  WORD* pUserd 已经使用的字节数
*输   出：无
*返   回：SUCCESS - 接收数据处理成功  ERROR - 接收数据处理有问题
*/
// int32_t HostComputerRecProc(KFIFO_ID pData,WORD* pUserd)
// {
//     int WriteByte = 0;
//     bool bHeadflag;
//     //uint16_t u16CrcCode = 0;
//     uint8_t tempbuff[256] ={0};
//     uint32_t u32fifolen = 0;
//     struct SEND_TO_DC_MOTOR_FRAME MotorFrame;
//     bHeadflag = uartFindFrameHead(pData,HEAD1,HEAD2);
//     if(bHeadflag)
//     {
//         kfifo_preview(pData, tempbuff, 4);
//         u32fifolen = kfifo_len(pData);
//         if(u32fifolen >= ((tempbuff[2] << 8) + tempbuff[3]))
//         {
//             kfifo_get(pData,tempbuff,((tempbuff[2] << 8) + tempbuff[3]));
//             memcpy(&MotorFrame,tempbuff,sizeof(struct SEND_TO_DC_MOTOR_FRAME));
//             if(g_stDeviceAddr.u8Motor_Drive_car != MotorFrame.stFrameBody.u8MotorAddr && g_stDeviceAddr.u8Motor_Drive_fish != MotorFrame.stFrameBody.u8MotorAddr)
//             {
//                 printf("Motor Addr %d Error!\n",MotorFrame.stFrameBody.u8MotorAddr);
//                 return ERROR;
//             }
//             if(( READCODE  != MotorFrame.stFrameBody.u8FunCode) && (WRITECODE != MotorFrame.stFrameBody.u8FunCode ) )
//             {
//                 printf("RS485 recive function code  %02x Error!\n",MotorFrame.stFrameBody.u8FunCode);
//                 return ERROR;
//             }
//             /* u16CrcCode = CRC16_Modbus(tempbuff,wLength);
//             if(MotorFrame.u16CRC != u16CrcCode)
//             {
//                 printf("CRC Check  result Error,genarate CRC is %d,Recive RCR is %04x\n",u16CrcCode,MotorFrame.u16CRC);
//                 return ERROR;
//             } */
//             WriteByte = UartWrite(g_stDevicelist.UartList[g_stDevicelist.DC_Motor].i32fd,(unsigned char *)&MotorFrame.stFrameBody,sizeof(frameBody));
//             if(sizeof( frameBody) != WriteByte)
//             {
//                 return ERROR;
//             }
//             return SUCCESS;

//         }
//         else
//         {
//             printf("kfifo len is %d,rec len is %d\n",u32fifolen,((tempbuff[2] << 8) + tempbuff[3]));
//             return ERROR;
//         }
//     }
//     {
//         printf("frame head error\n");
//         return ERROR;
//     }
    
// }


/*
*函数名称：MotorRecProc
*函数功能：从直流电机接收到的数据处理
*修改说明(2026-06-22):
*  - 最短7字节再解析，避免半帧刷ERROR
*  - 使用配置中的电机地址(非硬编码1/2)
*  - 无效字节逐字节丢弃；读响应才更新DI状态
*/
int MotorRecProc(KFIFO_ID pData,WORD* pUserd)
{

    uint8_t tempbuff[256] ={0};
    uint16_t frame_len = 0;
    uint32_t fifolen = kfifo_len(pData) ;
    /* Modbus读响应最短7字节，未收齐时直接等待，避免刷屏报错 */
    if(fifolen < 7)
    {
        return ERROR;
    }
    kfifo_preview(pData,tempbuff,4);

    uint8_t DeviceType = uartFindDeviceType(pData,
        g_stDeviceAddr.u8Motor_Drive_car, (uint8_t)READCODE,
        g_stDeviceAddr.u8Motor_Drive_fish, (uint8_t)READCODE);
    if(DeviceType == 0)
    {
        kfifo_get(pData,tempbuff,1);
        return ERROR;
    }
    if(kfifo_len(pData) < 7)
    {
        return ERROR;
    }
    kfifo_preview(pData,tempbuff,7);
    if(tempbuff[1] == READCODE)
    {
        frame_len = tempbuff[2] + 5;
    }
    else if(tempbuff[1] == WRITECODE || tempbuff[1] == WRITEMULREGCODE)
    {
        frame_len = 8;
    }
    else
    {
        kfifo_get(pData,tempbuff,1);
        return ERROR;
    }
    if(frame_len > sizeof(tempbuff) || frame_len < 5)
    {
        LOG(LOG_ERROR,"frame_len is %d",frame_len);
        kfifo_get(pData,tempbuff,1);
        return ERROR;
    }
    if(kfifo_len(pData) < frame_len)
    {
        return ERROR;
    }

    if(frame_len == kfifo_get(pData,tempbuff,frame_len))
    {
        if(tempbuff[1] == READCODE && g_ReadComSta_flag)
        {
            uint16_t recData = (tempbuff[3] << 8) + tempbuff[4];
            LOG(LOG_INFO,"Read AMPS DI status is %04x", recData);
            switch (DeviceType)
            {
                case 1:
                    g_stHoriDIStatusFlag = ReadStatusRecProc(recData);
                    ManualMoveLimitProtect();
                    if(g_stHoriDIStatusFlag.u8REV_LIMIT_FLAG == 1)
                    {
                        LOG(LOG_INFO,"g_stHoriDIStatusFlag.u8REV_LIMIT_FLAG is %d\n",g_stHoriDIStatusFlag.u8REV_LIMIT_FLAG);
                        ResetCarEncoderPosition();
                    }
                    break;
                case 2:
                    g_stVeroDIStatusFlag = ReadStatusRecProc(recData);
                    ManualMoveLimitProtect();
                    if(g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG == 1)
                    {
                        LOG(LOG_INFO,"g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG is %d\n",g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG);
                        ResetEncoder(g_stDeviceAddr.u8Encoder_fish);
                    }
                    break;
                default:
                    break;
            }
            g_ReadComSta_flag = 0;
        }
        return SUCCESS;
    }
    else
    {
        LOG(LOG_ERROR,"GET motor data error");
        return ERROR;
    }
   
    

}

/*
*函数名称：SendDatatoMotor
*函数功能：通过串口向电机发送数据
*输   入：uint8_t motorAddr 电机地址 uint8_t funcCode  功能码（读或写单个寄存器）    uint16_t regAddr   寄存器地址
*输   出：无
*返   回：SUCCESS - 接收数据处理成功  ERROR - 接收数据处理有问题
*/
int SendDatatoMotor(uint8_t motorAddr, uint8_t funcCode,uint16_t regAddr,uint16_t regData)
{
    int WriteByte = 0;
    uint8_t buffer[32] = {0};
    frameBody sendFrame;
    sendFrame.u8MotorAddr = motorAddr;
    sendFrame.u8FunCode = funcCode;
    sendFrame.u16RegAddr = regAddr;
    sendFrame.u16RegData = regData;
    sendFrame.u16RegAddr = swap_u16(sendFrame.u16RegAddr);
    sendFrame.u16RegData = swap_u16(sendFrame.u16RegData);
    sendFrame.u16CRC = CRC16_Modbus((uint8_t *)&sendFrame,sizeof(frameBody) -2);
    sendFrame.u16CRC = swap_u16(sendFrame.u16CRC);
    memcpy(buffer,(unsigned char *)&sendFrame,sizeof(frameBody));
    LOG(LOG_INFO,"send data to DC_MOTOR is :");
    LOG_ARRAY(LOG_INFO, buffer, sizeof(frameBody), "%02x");       // 记录整型数组
    WriteByte = UartWrite(g_stDevicelist.UartList[g_stDevicelist.DC_Motor].i32fd,buffer,sizeof(frameBody));
    if(sizeof(frameBody)!= WriteByte)
    {
        LOG(LOG_ERROR,"Error :write %d bytes\n",WriteByte);
        return ERROR;
    }
    return SUCCESS;

}

int WriteTwoMotorRegs(uint8_t motorAddr,uint16_t regAddr,uint16_t regCount,uint16_t reg1Data,uint16_t reg2Data)
{
    int WriteByte = 0;
    uint8_t buffer[32] = {0};
    WRITETWOREGFRAME stSendData;
    mSleep(50);
    reg2Data = (reg2Data >3000)  ?  3000 : reg2Data;
    if(reg2Data >0 )
    {
        if(motorAddr == g_stDeviceAddr.u8Motor_Drive_car)
        {
            g_u8CatState = IS_RUNNING_STATE;
            Relay_LightCtrl(BLUE_LIGHT,TURN_ON);
        }
        
        else if(motorAddr == g_stDeviceAddr.u8Motor_Drive_fish)
        {
            g_u8FishState = IS_RUNNING_STATE;
            Relay_LightCtrl(YELLOW_LIGHT,TURN_ON);
        }
    }
    else if(reg2Data <= 0)
    {
        Relay_LightCtrl(GREEN_LIGHT,TURN_ON);
        if(motorAddr == g_stDeviceAddr.u8Motor_Drive_car)
        {
            g_u8CatState = IS_STOP_STATE;

        }
        
        else if(motorAddr == g_stDeviceAddr.u8Motor_Drive_fish)
        {
            g_u8FishState = IS_STOP_STATE;
        }
    }
    
    stSendData.u8MotorAddr = motorAddr;
    stSendData.u8FunctionCode = WRITEMULREGCODE;
    stSendData.u16RegAddr  = swap_u16( regAddr);
    stSendData.u16RegNum =  swap_u16(regCount);
    stSendData.u8DataNum = 0x04;
    stSendData.u16Reg1Data = swap_u16( reg1Data);
    stSendData.u16Reg2Data = swap_u16( reg2Data);
    stSendData.u16CRC = CRC16_Modbus((uint8_t * )&stSendData,sizeof(WRITETWOREGFRAME) - sizeof(stSendData.u16CRC));
    stSendData.u16CRC = swap_u16(stSendData.u16CRC);
    memcpy(buffer,(unsigned char *)&stSendData,sizeof(WRITETWOREGFRAME));
    
    ResetError(motorAddr);
    mSleep(50);
    LOG(LOG_INFO,"WriteTwoMotorRegs send data to DC_MOTOR is :");
    LOG_ARRAY(LOG_INFO,buffer,sizeof(WRITETWOREGFRAME),"%02x");
    WriteByte = UartWrite(g_stDevicelist.UartList[g_stDevicelist.DC_Motor].i32fd,buffer,sizeof(WRITETWOREGFRAME));
    if(sizeof(WRITETWOREGFRAME)!= WriteByte)
    {
        printf("Error :write %d bytes\n",WriteByte);
        return ERROR;
    }
    
    return SUCCESS;
}

/*
*函数名称：readSpeed
*函数功能：向电机发送读取电机转速指令
*输   入：uint8_t motorAddr 电机地址
*输   出：无
*返   回：SUCCESS - 发送数据成功  ERROR - 发送数据有问题
*/
uint16_t readSpeedCmd(uint8_t motorAddr)
{
    
    if(SUCCESS !=   SendDatatoMotor(motorAddr,READCODE,READ_SPEED_REG_ADDR,READ_CMD_REGDATA))
    {
        
        return ERROR;
    }
    LOG(LOG_INFO,"Send readSpeedCmd");
    
    g_ReadSpeed_flag = 1;
    return SUCCESS;
}


/*
*函数名称：readSpeed
*函数功能：向电机发送读取电机电压指令
*输   入：uint8_t motorAddr 电机地址
*输   出：无
*返   回：SUCCESS - 发送数据成功  ERROR - 发送数据有问题
*/
uint16_t readVoltageCmd(uint8_t motorAddr)
{
    
    if(SUCCESS !=   SendDatatoMotor(motorAddr,READCODE,READ_VOLTAGE_REG_ADDR,READ_CMD_REGDATA))
    {
        
        return ERROR;
    }
    //g_ReadSpeed_flag = 1;
    return SUCCESS;
}

/*
*函数名称：readCurrentCmd
*函数功能：向电机发送读取电机电流指令
*输   入：uint8_t motorAddr 电机地址
*输   出：无
*返   回：SUCCESS - 发送数据成功  ERROR - 发送数据有问题
*/
uint16_t readCurrentCmd(uint8_t motorAddr)
{
    
    if(SUCCESS !=   SendDatatoMotor(motorAddr,READCODE,READ_CURRENT_REG_ADDR,READ_CMD_REGDATA))
    {
        
        return ERROR;
    }
    //g_ReadSpeed_flag = 1;
    return SUCCESS;
}


uint16_t readComStatus(uint8_t motorAddr)
{
    if(SUCCESS !=   SendDatatoMotor(motorAddr,READCODE,READ_COMSTATUS_REG_ADDR,READ_CMD_REGDATA))
    {
        
        return ERROR;
    }
    g_ReadComSta_flag = 1;
    return SUCCESS;
}
/*
*SetMotorRPM
*函数功能：设置电机转速
*输   入：uint8_t motorAddr 电机地址 uint16_t wheelDm    轮直径       uint16_t HoriSpeed 运动速度
*输   出：无
*返   回：SUCCESS - 发送数据成功  ERROR - 发送数据有问题
*/
/*
 * 网页缆道速度下发值为 cm/s（0.5 m/s → 50），行轮直径为毫米。
 * 水平线速度上限 1.5 m/s、垂直 0.5 m/s；电机 RPM 再封顶 MAX_MOTOR_RPM。
 * n = (v_cm/s) / (D_mm * π) * REDUCTION_RATIO
 */
uint16_t CablewayLinearSpeedToRpm(uint16_t speed_cm_s, uint16_t wheel_dm_mm)
{
    float rpm;

    if (speed_cm_s == 0 || wheel_dm_mm == 0) {
        return 0;
    }
    rpm = (float)speed_cm_s / ((float)wheel_dm_mm * (float)M_PI) * (float)REDUCTION_RATIO;
    if (rpm < 1.0f) {
        return 1;
    }
    if (rpm > (float)MAX_MOTOR_RPM) {
        return (uint16_t)MAX_MOTOR_RPM;
    }
    return (uint16_t)(rpm + 0.5f);
}

uint16_t GetCarRunRpm(void)
{
    uint16_t speed = g_stSetCableway.stSetCablewayBody.u16HoriSpeed;
    uint16_t rpm;

    if (speed > MAX_HORI_SPEED) {
        speed = MAX_HORI_SPEED;
    }
    rpm = CablewayLinearSpeedToRpm(speed, g_stSetCableway.stSetCablewayBody.u16HoriWheelDm);
    return (rpm != 0) ? rpm : (uint16_t)CAR_RPM;
}

uint16_t GetFishRunRpm(void)
{
    uint16_t speed = g_stSetCableway.stSetCablewayBody.u16VeriSpeed;
    uint16_t rpm;

    if (speed > MAX_VERO_SPEED) {
        speed = MAX_VERO_SPEED;
    }
    rpm = CablewayLinearSpeedToRpm(speed, g_stSetCableway.stSetCablewayBody.u16VeriWheelDm);
    return (rpm != 0) ? rpm : (uint16_t)FISH_RPM;
}

uint16_t SetMotorRPM(uint8_t motorAddr,uint16_t wheelDm,uint16_t speed_cm_s)
{
	uint16_t rpm = CablewayLinearSpeedToRpm(speed_cm_s, wheelDm);
    if (rpm == 0) {
        rpm = (motorAddr == g_stDeviceAddr.u8Motor_Drive_fish)
                  ? (uint16_t)FISH_RPM
                  : (uint16_t)CAR_RPM;
    }
    LOG(LOG_DEBUG, "SetMotorRPM addr=%u wheelDm=%u speed_cm_s=%u -> rpm=%u",
        motorAddr, wheelDm, speed_cm_s, rpm);
	if(SUCCESS !=   SendDatatoMotor(motorAddr,WRITECODE,SET_SPEED_ADDR,rpm))
    {
        
        return ERROR;
    }
	return SUCCESS;
}
/**
 * 根据速度生成转速表索引(兼容旧逻辑)
 * @param Speed 速度值
 * @return 转速表索引，范围为0-6
 */
uint16_t GenarateRpm(uint16_t Speed)
{
    uint16_t rpm = 0;
    rpm =(uint16_t)Speed / 100;

    if(rpm >= 6)
    {
        rpm =6; 
    }
    LOG(LOG_DEBUG,"speed array index is %d",rpm);
    return rpm;

}
/*
*函数名称:DC_MotorFwd
*函数功能:电机运动控制
*输   入：uint8_t motorAddr 电机地址  uint16_t MoveCmd   控制指令（前进、后退、停机）
*输   出：无
*返   回：SUCCESS - 发送数据成功  ERROR - 发送数据有问题
*/

uint16_t DC_MotorMoveCtrl(uint8_t motorAddr,uint16_t MoveCmd)
{
    if(MoveCmd == STOP)
    {
        LOG(LOG_INFO,"MotorStop!");
        Relay_LightCtrl(GREEN_LIGHT,TURN_ON);
        mSleep(50);
        if(motorAddr == g_stDeviceAddr.u8Motor_Drive_car)
        {
            g_u8CatState = IS_STOP_STATE;
        }
        
        else if(motorAddr == g_stDeviceAddr.u8Motor_Drive_fish)
        {
            g_u8FishState = IS_STOP_STATE;
        }
    }
	if(SUCCESS !=   SendDatatoMotor(motorAddr,WRITECODE,CTRL_ADDR,MoveCmd))
    {
        
        return ERROR;
    }
    
    
    return SUCCESS;
}

//01 06 20 00 00 07 c3 c8

uint16_t ResetError(uint8_t motorAddr)
{
	if(SUCCESS !=   SendDatatoMotor(motorAddr,WRITECODE,CTRL_ADDR,ERR_RESET))
    {
        
        return ERROR;
    }
    return SUCCESS;
}


void  MotorDrivePowerOFF()
{
    sleep(2);
	Relay_MotorDrivePowerOFF(g_stDeviceAddr.u8Relay_2_port_power,RELAY_COM0_ADDR);//驱动器1下电
    sleep(2);
    Relay_MotorDrivePowerOFF(g_stDeviceAddr.u8Relay_2_port_power,RELAY_COM1_ADDR);//驱动器2下电
    
    LOG(LOG_DEBUG,"Motor PowerOff,into standby Mode!");
}
