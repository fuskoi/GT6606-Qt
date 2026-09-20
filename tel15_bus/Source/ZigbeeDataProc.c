/*
 * [2026-06-22 修改说明 - ZigbeeDataProc.c]
 * 1. 触摸屏/网页小车位置: 编码器距离 - i16StartPointDes
 * 2. [2026-06-30] 触摸屏加测改走 AddMeauCtrl FSM(与Web一致)，去掉旧 AutoMeasureCtrl 及重复关门/下放
 * 3. 水位: 剥TX回显 + CRC；跳变>1.5m丢弃防假尖峰
 */
#include "zigbee_dataproc.h"
#include "Encoder.h"
#include "RS485_ICD.h"
#include "AMPSTechMotor.h"
#include "ringfifo.h"
#include "WebSeriver.h"
#include "CRC.h"
#include "Encoder.h"
#include "RS485_ICD.h"
#include "Relay.h"
#include "timer_manager.h"
#include "Sensor.h"
#include "math.h"
#include "log.h"
#include "SL651_2014.h"
#include "Timertask.h"
#include "AMPSTechMotor.h"
#include <sys/reboot.h>

TouchScreenReportBody g_stTouchScreenReportData;

/* ZigBee 水位读请求回显: 8字节 Modbus 03 读，CRC 正确则丢弃，避免当应答解析 */
static int zigbee_is_water_tx_echo(const uint8_t *frame, int len)
{
    uint16_t crc;

    if (len < (int)sizeof(ModbusSendFrame)) {
        return 0;
    }
    if (frame[0] != g_stDeviceAddr.u8WaterLevel || frame[1] != READCODE) {
        return 0;
    }
    crc = CRC16_Modbus((uint8_t *)frame, sizeof(ModbusSendFrame) - 2);
    if (crc != swap_u16(*(uint16_t *)(frame + sizeof(ModbusSendFrame) - 2))) {
        return 0;
    }
    /* 真应答字节数在 [2] 且总长=5+bytecount(雷达多为7)；8字节且CRC过的是读请求回显 */
    return 1;
}

static void ZigbeeSkipWaterTxEcho(KFIFO_ID pData)
{
    uint8_t frame[sizeof(ModbusSendFrame)];

    while (kfifo_len(pData) >= sizeof(ModbusSendFrame)) {
        kfifo_preview(pData, frame, sizeof(ModbusSendFrame));
        if (!zigbee_is_water_tx_echo(frame, sizeof(ModbusSendFrame))) {
            break;
        }
        kfifo_get(pData, frame, sizeof(ModbusSendFrame));
        LOG(LOG_DEBUG, "Zigbee drop water TX echo");
    }
}

#if 0 /* 与 case 4 重复，暂用 uartFindMultiDevice -> case 4 解析门 DI */
/* 门模块(地址 Electroc_door)收包：读DI应答 03、写寄存器应答 06 */
static int TryProcDoorRx(KFIFO_ID pData)
{
    uint8_t head[3] = {0};
    uint8_t tempbuff[64] = {0};
    uint32_t fifoLen;
    uint8_t frameLen;

    fifoLen = kfifo_len(pData);
    if (fifoLen < 2)
    {
        return ERROR;
    }
    kfifo_preview(pData, head, 2);
    if (head[0] != g_stDeviceAddr.u8Electroc_door)
    {
        return ERROR;
    }
    if (head[1] != READCODE && head[1] != WRITECODE)
    {
        return ERROR;
    }

    if (head[1] == WRITECODE)
    {
        frameLen = 8;
        if (fifoLen < frameLen)
        {
            return ERROR;
        }
        kfifo_get(pData, tempbuff, frameLen);
        LOG(LOG_DEBUG, "Door RX write ack:");
        LOG_ARRAY(LOG_DEBUG, tempbuff, frameLen, "%02x");
        return SUCCESS;
    }

    if (fifoLen < 5)
    {
        return ERROR;
    }
    kfifo_preview(pData, head, 3);
    frameLen = (uint8_t)(head[2] + 5);
    if (frameLen < 5 || frameLen > sizeof(tempbuff))
    {
        LOG(LOG_ERROR, "Door RX read frame len invalid %u", (unsigned)frameLen);
        kfifo_get(pData, tempbuff, 1);
        return ERROR;
    }
    if (fifoLen < frameLen)
    {
        return ERROR;
    }
    kfifo_get(pData, tempbuff, frameLen);
    LOG(LOG_DEBUG, "Door RX read DI:");
    LOG_ARRAY(LOG_DEBUG, tempbuff, frameLen, "%02x");
    {
        uint16_t u16GeneCRC = CRC16_Modbus(tempbuff, frameLen - 2);
        uint16_t u16RecCRC = 0;
        memcpy(&u16RecCRC, tempbuff + frameLen - 2, sizeof(uint16_t));
        u16GeneCRC = swap_u16(u16GeneCRC);
        if (u16GeneCRC != u16RecCRC)
        {
            LOG(LOG_ERROR, "Door RX CRC fail, calc=%04x rec=%04x", u16GeneCRC, u16RecCRC);
            return ERROR;
        }
    }
    if (frameLen >= sizeof(RotatorRecDIStatus))
    {
        RotatorRecDIStatus stRotatorRecDIStatus;
        memcpy(&stRotatorRecDIStatus, tempbuff, sizeof(RotatorRecDIStatus));
        stRotatorRecDIStatus.u16RecData = swap_u16(stRotatorRecDIStatus.u16RecData);
        memcpy(&g_stDoorSwitchStatus, &stRotatorRecDIStatus.u16RecData, sizeof(SwitchStatus));
        LOG(LOG_DEBUG,
            "Door RX status=0x%04x openDI=%u closeDI=%u (接地=1有电=0, 0=到位)",
            stRotatorRecDIStatus.u16RecData,
            g_stDoorSwitchStatus.u16watersurface_1,
            g_stDoorSwitchStatus.u16watersurface_2);
    }
    return SUCCESS;
}
#endif

static void BuildTouchScreenReportBody(TouchScreenReportBody *stDataBody)
{
    memset(stDataBody, 0, sizeof(TouchScreenReportBody));
    (void)GetCurrentWaterLevel();
    stDataBody->u16StartPosData = g_stTouchScreenReportData.u16StartPosData;
    stDataBody->u16WaterLevelData = g_stTouchScreenReportData.u16WaterLevelData;
    stDataBody->u16SpeedData = g_stTouchScreenReportData.u16SpeedData;
    stDataBody->u16WaterFlow = g_stTouchScreenReportData.u16WaterFlow;
    stDataBody->u16CarPos = GetCarHoriPosForTouchScreen();
    stDataBody->u16FishPos = GetFishInwaterDeepForTouchScreen();

    stDataBody->u16StartPosData = swap_u16(stDataBody->u16StartPosData);
    stDataBody->u16WaterLevelData = swap_u16(stDataBody->u16WaterLevelData);
    stDataBody->u16SpeedData = swap_u16(stDataBody->u16SpeedData);
    stDataBody->u16WaterFlow = swap_u16(stDataBody->u16WaterFlow);
    stDataBody->u16CarPos = swap_u16(stDataBody->u16CarPos);
    stDataBody->u16FishPos = swap_u16(stDataBody->u16FishPos);
}

void TouchScreenPushReportData(void)
{
    TouchScreenReportBody stDataBody;
    BuildTouchScreenReportBody(&stDataBody);
    ReportScreenData(stDataBody);
}


int Proc_Zigbee_RecData(KFIFO_ID pData ,  WORD *pUsed)
{
    uint16_t u16Airdraft;
    uint8_t tempbuff[2048] ={0};
    uint32_t u32fifolen = 0;
    uint16_t dataLen = 0;
    uint8_t DeviceType  = 0;
    TouchScreenReportCmd stTouchCommand;
    TouchScreenReportBody stDataBody;
    memset(&stDataBody,0,sizeof(TouchScreenReportBody));
    u32fifolen = kfifo_len(pData);
    if (MAX_SOFTWARE_SIZE <= u32fifolen )
    {
        LOG(LOG_ERROR,"fifo len %d is out of range (max is %d)",u32fifolen,2000);
        kfifo_clear(pData);
        return ERROR;
    }
    if(u32fifolen <5)
    {
        return ERROR;
    }
    ZigbeeSkipWaterTxEcho(pData);
    u32fifolen = kfifo_len(pData);
    if (u32fifolen < 5)
    {
        return ERROR;
    }
#if 0 /* 暂关闭：改走下方 case 4 */
    /* 门模块报文优先解析，避免 03 06 写应答对不上 03 03 被丢掉 */
    {
        uint8_t peek[2] = {0};
        kfifo_preview(pData, peek, 2);
        if (peek[0] == g_stDeviceAddr.u8Electroc_door
            && (peek[1] == READCODE || peek[1] == WRITECODE))
        {
            return TryProcDoorRx(pData);
        }
    }
#endif
    g_stErrorState.bZigbeeError = 0;
    if(u32fifolen > 5)
    {
        kfifo_preview(pData,tempbuff,u32fifolen);
        LOG(LOG_INFO,"Zigbee rec data is:");
        LOG_ARRAY(LOG_INFO,tempbuff,u32fifolen,"%02x");
    }
    kfifo_head stfifohead;
    memset(&stfifohead,0,sizeof(kfifo_head));
    stfifohead.u8Head1_1 = g_stDeviceAddr.u8TouchScreenAddr;
    stfifohead.u8Head1_2 = READCODE;
    stfifohead.u8Head2_1 = g_stDeviceAddr.u8TouchScreenAddr;
    stfifohead.u8Head2_2 = WRITECODE;
    stfifohead.u8Head3_1 = g_stDeviceAddr.u8WaterLevel;
    stfifohead.u8Head3_2 = READCODE;
    stfifohead.u8Head4_1 = g_stDeviceAddr.u8Electroc_door;
    stfifohead.u8Head4_2 = READCODE;
    
    DeviceType = uartFindMultiDevice(pData,stfifohead);
    switch (DeviceType)
    {
        case 1://触摸屏读数据
            kfifo_get(pData,tempbuff,sizeof(TouchScreenReportCmd));
            BuildTouchScreenReportBody(&stDataBody);
            ReportScreenData(stDataBody);

        break;
        case 2://触摸屏发指令
            
            kfifo_get(pData,tempbuff,sizeof(TouchScreenReportCmd));
            
            ReportScreenCmd(tempbuff,sizeof(TouchScreenReportCmd));
            memset(&stTouchCommand,0,sizeof(TouchScreenReportCmd));
            memcpy(&stTouchCommand,tempbuff,sizeof(TouchScreenReportCmd));
            updateActiveTime();
            ProcTouchCommand(stTouchCommand.u16RegAddr);
        break;
        case 3://水位计
            kfifo_preview(pData, tempbuff,3);
            LOG(LOG_INFO,"Receive Rador Data");
            u32fifolen = kfifo_len(pData);
            dataLen = tempbuff[2] + 5;
            if(dataLen < 7 || dataLen > u32fifolen || dataLen > sizeof(tempbuff))
            {
                LOG(LOG_ERROR,"Waterlevel received data len %d invalid, fifolen %d",
                    dataLen, u32fifolen);
                return ERROR;
            }
            /* 仍为8字节读请求形态则当回显丢掉 */
            if (dataLen == (uint16_t)sizeof(ModbusSendFrame)) {
                kfifo_preview(pData, tempbuff, sizeof(ModbusSendFrame));
                if (zigbee_is_water_tx_echo(tempbuff, sizeof(ModbusSendFrame))) {
                    kfifo_get(pData, tempbuff, dataLen);
                    LOG(LOG_DEBUG, "Waterlevel skip TX echo in case3");
                    return SUCCESS;
                }
            }
            kfifo_get(pData,tempbuff,dataLen);
            {
                uint16_t u16GeneCRC = CRC16_Modbus(tempbuff, (uint16_t)(dataLen - 2));
                uint16_t u16RecCRC = 0;
                uint32_t newLevel = 0;

                memcpy(&u16RecCRC, tempbuff + dataLen - 2, sizeof(uint16_t));
                if (u16GeneCRC != swap_u16(u16RecCRC)) {
                    LOG(LOG_ERROR,
                        "Waterlevel CRC fail gene=%04x rec=%04x, keep last=%u",
                        u16GeneCRC, swap_u16(u16RecCRC),
                        (unsigned)g_u32waterLevel);
                    return ERROR;
                }

                if(g_stWaterLevelConfig.u16WaterType == WATER_TYPE_RADAR)//雷达
                {
                    if (tempbuff[2] != 2) {
                        LOG(LOG_WARNING,
                            "Waterlevel radar byteCount=%u expect 2, drop",
                            (unsigned)tempbuff[2]);
                        return ERROR;
                    }
                    u16Airdraft = tempbuff[3];
                    u16Airdraft = u16Airdraft << 8;
                    u16Airdraft = u16Airdraft + tempbuff[4];
                    newLevel = g_stWaterLevelConfig.u16WaterBase - (u16Airdraft / 10);
                    LOG(LOG_INFO,"Receive Rador Data,u16Airdraft level is %d,water level is %d ",
                        u16Airdraft, newLevel);
                }
                else if(g_stWaterLevelConfig.u16WaterType == WATER_TYPE_PASSIVE) // 被动式
                {
                    newLevel = (tempbuff[3] << 24) + (tempbuff[4] << 16)
                             + (tempbuff[5] << 8) + tempbuff[6];
                }
                else
                {
                    newLevel = 0;
                }

                /* 相对上次有效值跳变过大(>1.5m)丢弃，防偶发尖峰(如50.x) */
                if (g_u32waterLevel > 0) {
                    uint32_t diff = (newLevel > g_u32waterLevel)
                                  ? (newLevel - g_u32waterLevel)
                                  : (g_u32waterLevel - newLevel);
                    if (diff > 150U) {
                        LOG(LOG_WARNING,
                            "Waterlevel jump %u->%u cm >150, keep last",
                            (unsigned)g_u32waterLevel, (unsigned)newLevel);
                        g_stSensorFlag.u8GetWaterlevel_flag = 1;
                        break;
                    }
                }

                g_u32waterLevel = newLevel;
                g_stSensorFlag.u8GetWaterlevel_flag = 1;
                g_stReportState.u32CurWaterLevel = g_u32waterLevel;
                g_stTouchScreenReportData.u16WaterLevelData = (uint16_t)g_u32waterLevel;
                LOG(LOG_INFO,"g_stTouchScreenReportData.u16WaterLevelData is %04x",
                    g_stTouchScreenReportData.u16WaterLevelData);
                g_u16WaterLevelArray[g_waterLevelCount] = (uint16_t)g_u32waterLevel;
            }
        break;

        case 4:
            u32fifolen = kfifo_len(pData);
            if(u32fifolen < 5)
            {
                return ERROR;
            }
            kfifo_preview(pData,tempbuff,5);
            uint8_t recframelen = tempbuff[2] + 5;
            if(u32fifolen < recframelen)
            {
                return ERROR;
            }
            kfifo_get(pData,tempbuff,recframelen);
            LOG(LOG_DEBUG,"DOOR REC DATA IS ");
            LOG_ARRAY(LOG_DEBUG,tempbuff,recframelen,"%02x");
            {
                uint16_t u16GeneCRC = 0;
                uint16_t u16RecCRC = 0;
                memcpy(&u16RecCRC, tempbuff + recframelen - 2, sizeof(uint16_t));
                u16GeneCRC = CRC16_Modbus(tempbuff, recframelen - 2);
                u16GeneCRC = swap_u16(u16GeneCRC);
                if (u16GeneCRC != u16RecCRC) 
                {
                    LOG(LOG_ERROR, "CRC check failed, expected %04x, got %04x", u16GeneCRC, u16RecCRC);
                    return ERROR;
                }
            }
            {
                RotatorRecDIStatus stRotatorRecDIStatus;
                memcpy(&stRotatorRecDIStatus, tempbuff, sizeof(RotatorRecDIStatus));
                stRotatorRecDIStatus.u16RecData = swap_u16(stRotatorRecDIStatus.u16RecData);
                memcpy(&g_stDoorSwitchStatus,&stRotatorRecDIStatus.u16RecData,sizeof(SwitchStatus));
                LOG(LOG_DEBUG, "g_stDoorSwitchStatus : %04x,UP LIMIT IS : %d,down limit is %d, touch limit is %d", g_stDoorSwitchStatus,g_stDoorSwitchStatus.u16watersurface_1,g_stDoorSwitchStatus.u16watersurface_2,g_stDoorSwitchStatus.u16TouchGround);
            }

        break;
        default:
        break;
        
    }
    return SUCCESS;
}



int SendDataToScreen(uint8_t * data,uint16_t datalen)
{
    int writeByte = 0;
#ifdef BOARD_GT6606
    /* Qt 液晶替代外部触摸屏，不再往 ZigBee 口推屏数据。 */
    (void)data;
    (void)datalen;
    return SUCCESS;
#endif
    if (g_bDoorCtrlSendFlag) {
        LOG(LOG_DEBUG, "SendDataToScreen skip: door bus busy");
        return SUCCESS;
    }
    writeByte = write(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd, data, datalen );
    if(writeByte != datalen)
    {
        LOG(LOG_ERROR,"write data to screen error");
        return ERROR;
    }
    return SUCCESS;
}
int ReportScreenData(TouchScreenReportBody stDataBody)
{
    TouchScreenReportData stDataFrame;
    memset(&stDataFrame,0,sizeof(TouchScreenReportData));
    stDataFrame.u8Addr = g_stDeviceAddr.u8TouchScreenAddr;
    stDataFrame.u8FunctionCode = READCODE;
    stDataFrame.u8DataLen = sizeof(TouchScreenReportBody);
    stDataFrame.stDataBody = stDataBody;
    stDataFrame.u16CRC = CRC16_Modbus((uint8_t *)&stDataFrame,sizeof(TouchScreenReportData)-2);
    stDataFrame.u16CRC = swap_u16(stDataFrame.u16CRC);
    LOG(LOG_INFO,"ReportScreenData is ");
    LOG_ARRAY(LOG_INFO,(uint8_t *)&stDataFrame,sizeof(TouchScreenReportData),"%02x");
    return SendDataToScreen((uint8_t *)&stDataFrame,sizeof(TouchScreenReportData));
}

int ReportScreenCmd(uint8_t *data,uint8_t datalen)
{
    return SendDataToScreen(data,datalen);
}
int ProcTouchCommand(uint16_t ButtonAddr)
{
    LOG(LOG_INFO,"ButtonAddr is %04x",swap_u16(ButtonAddr));
    int ret = 0;
    switch ( swap_u16(ButtonAddr))
    {
        case TOUCH_FORWARD:
            LOG(LOG_DEBUG,"TOUCH_FORWARD");
            if (!CarMoveAllowed(AMPS_FWD))
            {
                break;
            }
            Relay_MotorUnbreak(RELAY_COM0_ADDR);
            ret = AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car ,(AMPS_FWD) * GetCarRunRpm() * 10) ;
            /* code */
            break;
        case TOUCH_BACKWARD:
            LOG(LOG_DEBUG,"TOUCH_BACKWARD");
            if (!CarMoveAllowed(AMPS_BWD))
            {
                break;
            }
            Relay_MotorUnbreak(RELAY_COM0_ADDR);
            ret = AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car ,(AMPS_BWD) * GetCarRunRpm() * 10) ;
            
            break;
        case TOUCH_CAR_STOP:
            LOG(LOG_DEBUG,"TOUCH_CAR_STOP");
            ret = AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
            Relay_MotorBreak(RELAY_COM0_ADDR);
            break;
        case TOUCH_UP:
            LOG(LOG_DEBUG,"TOUCH_UP");
            if (!FishMoveAllowed(AMPS_BWD))
            {
                break;
            }
            Relay_MotorUnbreak(RELAY_COM1_ADDR);
            ret = AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish ,(AMPS_BWD) * GetFishRunRpm() * 10) ;
            
            break;
        case TOUCH_DOWN:
            LOG(LOG_DEBUG,"TOUCH_DOWN");
            if (!FishMoveAllowed(AMPS_FWD))
            {
                break;
            }
            Relay_MotorUnbreak(RELAY_COM1_ADDR);
            ret = AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish ,(AMPS_FWD) * GetFishRunRpm() * 10) ;
            
            break;
        case TOUCH_FISH_STOP:
            LOG(LOG_DEBUG,"TOUCH_FISH_STOP");
            ret = AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
            Relay_MotorBreak(RELAY_COM1_ADDR);
            break;
        case TOUCH_ADDMEARESE:
            LOG(LOG_DEBUG,"TOUCH_ADDMEARESE");
            if (g_addMeasureState != FSM_IDLE && g_addMeasureState != FSM_COMPLETE) {
                LOG(LOG_WARNING, "Touch add measure ignored: FSM busy (state=%d)", g_addMeasureState);
                break;
            }
            pthread_t cmdprocid;
            if(SUCCESS == pthread_create(&cmdprocid,NULL,TouchCmdProcThread,NULL))
            {
                pthread_detach(cmdprocid);
                LOG(LOG_INFO,"pthread id %d start success!",getpid());
                
        
            }
            else
            {
                LOG(LOG_ERROR," touch cmdproc thread creat error!");
                
            }
            
            

            break;
        case TOUCH_RESTART:
            LOG(LOG_DEBUG,"TOUCH_RESTART");
            emergency_stop_handler(NULL);
            
            
            break;
        default:
        break;
    }
    return ret;
}


/*
*函数名称:uartRecvFun
*函数功能:串口接收线程工作函数，用于接收所有串口的信息，放到每个串口对应的环形缓冲区中
*输   入:void * pParam  线程传入参数，devicelist，设备列表
*输   出:无
*返   回:无
*/
void  * TouchCmdProcThread(void * pParam)
{
    AddMeasureCtrl stAddmeau;
    CommandProcParam stCommandPara;
    uint8_t payload[sizeof(AddMeasureCtrl)];

    (void)pParam;

    ReadSectionPara_new(&g_stSection_new);
    ReadWaterLevel(NULL);

    memset(&stAddmeau, 0, sizeof(stAddmeau));
    stAddmeau.u32WaterLevel = 1000000; /* 与Web默认一致: 使用实时水位 */
    stAddmeau.u8SpeedCtrl = 2;         /* 转子测流，与原触摸屏加测强制ROTATOR一致 */

    memset(&stCommandPara, 0, sizeof(stCommandPara));
    memcpy(payload, &stAddmeau, sizeof(stAddmeau));
    stCommandPara.pData = payload;
    stCommandPara.u16datalen = sizeof(AddMeasureCtrl);
    stCommandPara.Devicetype = DTU_TYPE;

    LOG(LOG_INFO, "Touch add measure -> AddMeauCtrl FSM (ROTATOR, live water level)");
    return AddMeauCtrl(&stCommandPara);
}