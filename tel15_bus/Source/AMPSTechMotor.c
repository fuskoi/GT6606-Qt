#include "RS485_ICD.h"
#include "CRC.h"
#include "zigbee_dataproc.h"
#include <math.h>
#include "WebSeriver.h"
#include "Relay.h"
#include "log.h"
#include "Encoder.h"
#include "AMPSTechMotor.h"
int g_iManualCarMoveDir = 0;
int g_iManualFishMoveDir = 0;
#include "ModbusQueue.h"

/* 通过Modbus发送队列发送指令（内联帧，无malloc） */
static int AMPS_SendViaQueue(uint8_t *pFrame, uint16_t frameLen, uint8_t devAddr, uint8_t funcCode)
{
    MODBUS_CMD_PARAM stParam;
    memset(&stParam, 0, sizeof(MODBUS_CMD_PARAM));
    stParam.i32Fd = g_stDevicelist.UartList[g_stDevicelist.DC_Motor].i32fd;
    stParam.u8SlaveAddr = devAddr;
    stParam.u8FunctionCode = funcCode;
    if (frameLen > MODBUS_MAX_FRAME_LEN) {
        LOG(LOG_ERROR, "AMPS_SendViaQueue: frameLen %d exceeds max %d", frameLen, MODBUS_MAX_FRAME_LEN);
        return ERROR;
    }
    memcpy(stParam.pu8RawFrame, pFrame, frameLen);
    stParam.u16RawFrameLen = frameLen;

    return ModbusQueue_AddCommand(&stParam);
}

int AMPS_WriteSignle_Reg(uint8_t u8DeviceAddr,uint8_t FuncCode,uint16_t RegAddr,uint16_t RegData)
{
    uint8_t buffer[32] = {0};
    Modbus_SignleRegSend stSend;
    stSend.u8DeviceAddr = u8DeviceAddr;
    stSend.u8FunCode = FuncCode;
    stSend.u16RegAddr = RegAddr;
    stSend.u16RegData = RegData;
    stSend.u16RegAddr = swap_u16(stSend.u16RegAddr);
    stSend.u16RegData = swap_u16(stSend.u16RegData);

    stSend.u16CRC = CRC16_Modbus((uint8_t *)&stSend,sizeof(Modbus_SignleRegSend) -2);
    stSend.u16CRC = swap_u16(stSend.u16CRC);
    memcpy(buffer,(unsigned char *)&stSend,sizeof(Modbus_SignleRegSend));
    //LOG(LOG_DEBUG,"AMPS_WriteSignle_Reg, addr=0x%02X, func=0x%02X, reg=0x%04X, data=0x%04X",
    //    u8DeviceAddr, FuncCode, RegAddr, RegData);

    return AMPS_SendViaQueue(buffer, sizeof(Modbus_SignleRegSend), u8DeviceAddr, FuncCode);
}

int AMPS_WriteMulti_reg(uint8_t u8DeviceAddr,uint8_t FuncCode,uint16_t RegAddr, uint16_t RegCount,int32_t RegData)
{
    uint8_t buffer[32] = {0};
    Modbus_MultiRegSend stSend;
    stSend.u8DeviceAddr = u8DeviceAddr;
    stSend.u8FunCode = FuncCode;
    stSend.u16RegAddr = RegAddr;
    stSend.u16RegNum = RegCount;
    stSend.i16RegDataLow = RegData & 0xffff;
    stSend.i16RegDataHigh = RegData >> 16;
    stSend.u8DataByte = 4;
    stSend.u16RegAddr = swap_u16(stSend.u16RegAddr);
    stSend.u16RegNum = swap_u16(stSend.u16RegNum);
    stSend.i16RegDataLow = swap_i16(stSend.i16RegDataLow);
    stSend.i16RegDataHigh = swap_i16(stSend.i16RegDataHigh);

    stSend.u16CRC = CRC16_Modbus((uint8_t *)&stSend,sizeof(Modbus_MultiRegSend) -2);
    stSend.u16CRC = swap_u16(stSend.u16CRC);

    memcpy(buffer,(unsigned char *)&stSend,sizeof(Modbus_MultiRegSend));
    // if(FuncCode == WRITEMULREGCODE)
    // {
    //     LOG(LOG_DEBUG,"send data to AMPS MOTOR is :");
    //     LOG_ARRAY(LOG_DEBUG, buffer, sizeof(Modbus_MultiRegSend), "%02x");
    // }
    

    return AMPS_SendViaQueue(buffer, sizeof(Modbus_MultiRegSend), u8DeviceAddr, FuncCode);
}

int SetSpeed(uint8_t MotorAddr,int32_t i32Speed)
{
    LOG(LOG_DEBUG,"set AMPS motor speed is %04x",i32Speed);
    return AMPS_WriteMulti_reg(MotorAddr,WRITEMULREGCODE,SET_SPEED_REG,2,i32Speed);
}
int SetSpeedUP(uint8_t MotorAddr,uint32_t Speed)
{
    return AMPS_WriteMulti_reg(MotorAddr,WRITEMULREGCODE,SET_SPEED_UP,2,Speed);
}
int SetSpeedDown(uint8_t MotorAddr,uint32_t Speed)
{
    return AMPS_WriteMulti_reg(MotorAddr,WRITEMULREGCODE,SET_SPEED_DOWN,2,Speed);    
}
int SetCtrlState(uint8_t MotorAddr,uint8_t State)
{
    LOG(LOG_DEBUG,"set AMPS motor SetCtrlState,state is %s",State == DISABLE ? "DISABLE" : "ENABLE" );
    return AMPS_WriteSignle_Reg(MotorAddr,WRITECODE,SET_CTRL_STATUS,State);
}

int Read_DI_Status(uint8_t MotorAddr)
{
    LOG(LOG_INFO,"Send Read_DI_Status ");
    g_ReadComSta_flag = 1;
    return AMPS_WriteSignle_Reg(MotorAddr,READCODE,READ_DIO_STATUS,0x0001);
}

int ReadActualSpeed(uint8_t MotorAddr)
{
    LOG(LOG_INFO,"Send ReadActualSpeed ");
    g_ReadSpeed_flag = 1;
    
    return AMPS_WriteSignle_Reg(MotorAddr,READCODE,READ_ACTUAL_SPEED,0x0002);
}


int AMPS_MotorMove(uint8_t MotorAddr,int32_t Speed)
{
    int ret = SUCCESS;
    ret = SetSpeed(MotorAddr,Speed);
    ret &= SetCtrlState(MotorAddr,ENABLE);
    Relay_LightCtrl(BLUE_LIGHT,TURN_ON);
    mSleep(50);
    
    
    if(MotorAddr == g_stDeviceAddr.u8Motor_Drive_car)
    {
        g_u8CatState = IS_RUNNING_STATE;
        g_iManualCarMoveDir = (Speed > 0) ? AMPS_FWD : AMPS_BWD;
    }
    
    else if(MotorAddr == g_stDeviceAddr.u8Motor_Drive_fish)
    {
        g_u8FishState = IS_RUNNING_STATE;
        g_iManualFishMoveDir = (Speed > 0) ? AMPS_FWD : AMPS_BWD;
    }
    
    return ret;
}
int AMPS_MotorStop(uint8_t MotorAddr)
{
    extern uint8_t g_sendcount;
    /* 先改本地状态再上报，避免状态帧仍带“运动中” */
    if(MotorAddr == g_stDeviceAddr.u8Motor_Drive_car)
    {
        g_u8CatState = IS_STOP_STATE;
        g_iManualCarMoveDir = 0;
    }
    else if(MotorAddr == g_stDeviceAddr.u8Motor_Drive_fish)
    {
        g_u8FishState = IS_STOP_STATE;
        g_iManualFishMoveDir = 0;
    }
    g_sendcount = 9; /* 静止时也强制发一帧状态 */
    SendStatusFrame((void *)DTU_TYPE);
    return SetCtrlState(MotorAddr,DISABLE);
}

DI_STATUS_FLAG ReadStatusRecProc(uint16_t recData)
{
    DI_STATUS_FLAG stDIStatusFlag;
    memset(&stDIStatusFlag,0,sizeof(DI_STATUS_FLAG));
    LOG(LOG_INFO,"ReadStatusRecProc recData = %d",recData);
    if(recData & 0x0001)
    {   
        stDIStatusFlag.u8FWD_LIMIT_FLAG = 1;
    }
    if(recData & 0x0002)
    {
        stDIStatusFlag.u8REV_LIMIT_FLAG = 1;
    }
    return stDIStatusFlag;

}

/* 周期读取水平/垂直电机DI状态（20ms调用一次，轮流读取） */
static uint8_t g_u8DICmdSeri = 0;  /* 0=读水平, 1=读垂直 */

void ReadDIStatusTimerCallback(void *arg)
{
    (void)arg;
    
    if (g_u8DICmdSeri == 0) {
        Read_DI_Status(g_stDeviceAddr.u8Motor_Drive_car);
        g_u8DICmdSeri = 1;
    } else {
        Read_DI_Status(g_stDeviceAddr.u8Motor_Drive_fish);
        g_u8DICmdSeri = 0;
    }
}