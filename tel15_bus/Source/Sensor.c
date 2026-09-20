#include "CRC.h"
#include "RS485_ICD.h"
#include "WebSeriver.h"
#include "log.h"
#include "Sensor.h"
#include "zigbee_dataproc.h"
#include "LinkList.h"
#include "Relay.h"
#include "OtaUpgrade.h"
#include <sys/time.h>
SensorFlag g_stSensorFlag;
RotatorRecValue g_stRotatorRecValue;
RotatorRecDIStatus g_stRotatorRecDIStatus;
BatteryRecStatus g_stBatteryRecStatus;
BatteryRecValue g_BatteryRecValue;
uint8_t g_batteryParaSeri = 0;

/*
 * [2026-06-22 修改说明 - Sensor.c]
 * 1. 转子RS485: 过滤本机读指令TX回显(转子01 03 + 库仑计02 04)，避免FIFO混帧
 * 2. 转子轮询: 应答后再发下一条；与库仑计分时复用ttyS5
 * 3. 库仑计(H56): 04H读输入寄存器；电压/电流系数0.01，SOC系数0.1；u16CarVol存0.01V
 * 4. [2026-06-26] TX回显: v0.6原仅滤转子01 03，补充库仑计02 04，修复CRC误报与电压读不出
 */
// ---- [修改] 转子流速仪周期性读取(150ms) ----
uint32_t g_u32RotatorPeriodicCount = 0;
uint16_t g_u16RotatorPeriodicDI = 0;
volatile int g_rotatorPeriodicUpdated = 1;

#define ROTATOR_READ_REQ_LEN       sizeof(ModbusSendFrame)
#define ROTATOR_PENDING_TIMEOUT    20   /* [修改] 20×150ms≈3s，转子读超时后重发 */

static uint8_t g_u8RotatorReadPending = 0;   /* [修改] 转子组合读等待应答标志 */
static uint32_t g_u32RotatorPendingTicks = 0;
static uint8_t g_u8RotatorFaultReported = 0; /* 本故障周期是否已上报传感器出错 */
static uint32_t g_u32CoulombPendingTicks = 0; /* [修改] 库仑计应答超时计数(×10s) */

#define COULOMB_RSP_TIMEOUT_TICKS    3   /* [修改] 库仑计应答超时(×10s定时周期)后重发 */

/* [修改] 发新库仑计读指令前清除各应答标志，避免多标志同时置位 */
static void ClearCoulombPendingFlags(void)
{
    g_stSensorFlag.u8Coulomb_SOC_Flag = 0;
    g_stSensorFlag.u8Coulomb_voltage_flag = 0;
    g_stSensorFlag.u8Coulomb_current_flag = 0;
    g_stSensorFlag.u8Coulomb_ChargeState_flag = 0;
    g_u32CoulombPendingTicks = 0;
}

/* [修改] 库仑计有未应答读指令时，转子轮询须让出ttyS5 */
static bool CoulombHasPendingFlag(void)
{
    return g_stSensorFlag.u8Coulomb_SOC_Flag
        || g_stSensorFlag.u8Coulomb_voltage_flag
        || g_stSensorFlag.u8Coulomb_current_flag
        || g_stSensorFlag.u8Coulomb_ChargeState_flag;
}

/*
 * [修改] 判断8字节Modbus读请求是否为本机TX回显(CRC校验通过)
 * 2026-06-26: 在v0.6仅识别转子01 03基础上，增加库仑计02 04
 */
static int sensor_frame_is_tx_echo(const uint8_t *frame, int len)
{
    uint16_t crc;

    if (len < (int)sizeof(ModbusSendFrame)) {
        return 0;
    }
    crc = CRC16_Modbus((uint8_t *)frame, sizeof(ModbusSendFrame) - 2);
    if (crc != swap_u16(*(uint16_t *)(frame + sizeof(ModbusSendFrame) - 2))) {
        return 0;
    }
    if (frame[0] == g_stDeviceAddr.u8Rotator_addr
        && frame[1] == READCODE && frame[2] == 0x00) {
        return 1;
    }
    if (frame[0] == g_stDeviceAddr.u8CoulombAddr
        && frame[1] == READ_RELAY_CODE && frame[2] == 0x00) {
        return 1;
    }
    return 0;
}

/* [修改] 解析前循环剥离ttyS5本机读请求回显(原RotatorSkipTxEchoFrames，现覆盖转子+库仑计) */
static void SensorSkipTxEchoFrames(KFIFO_ID pData)
{
    uint8_t frame[ROTATOR_READ_REQ_LEN];

    while (kfifo_len(pData) >= ROTATOR_READ_REQ_LEN)
    {
        kfifo_preview(pData, frame, ROTATOR_READ_REQ_LEN);
        if (!sensor_frame_is_tx_echo(frame, ROTATOR_READ_REQ_LEN)) {
            break;
        }
        kfifo_get(pData, frame, ROTATOR_READ_REQ_LEN);
    }
}

SwitchStatus g_stSwitchStatus;
SwitchStatus g_stDoorSwitchStatus = { .u16watersurface_1 = 1, .u16watersurface_2 = 1 };
DMA_5213_DOSTATUS g_stDOStatus;
bool g_bDoorCtrlSendFlag = false;
uint32_t g_SensorNum = 0;
uint8_t g_u8ResendTimes = MAX_RESEND_TIMES;
ReportState g_stReportState;//上报基本状态

void printData(char *note,uint8_t * data,int datalen)
{
	int i = 0;
	printf("%s: ",note);
	for(i = 0;i< datalen;i++)
	{
		printf("%02x ",data[i]);
	}
	printf("\n");
}



/*
*函数名称:SensorSendDataCmd
*函数功能:通过串口发送MODBUS协议数据
*输   入:uint8_t SubAddr  从设备地址,uint8_t FuncCode  功能码, uint16_t RegAddr  寄存器地址,uint16_t RegNum    寄存器数量
*输   出: 
*返   回:成功       失败
*/

void SensorPrepareRxFifo(KFIFO_ID pData)
{
	/* [修改] 传感器串口收包预处理：先剥TX回显再解析 */
	SensorSkipTxEchoFrames(pData);
}

int SensorSendDataCmd(uint8_t SubAddr,uint8_t FuncCode, uint16_t RegAddr,uint16_t RegNum)
{
	uint8_t buffer[24] = {0};
	ModbusSendFrame stSendFrame;
	//printf("sizeof(ModbusSendFrame) is %d\n",sizeof(ModbusSendFrame));
	stSendFrame.u8SubAddr = SubAddr;
	stSendFrame.u8Funcode = FuncCode;
	stSendFrame.u16RegAddr = RegAddr;
	stSendFrame.u16RegData = RegNum;
	stSendFrame.u16RegAddr = swap_u16(stSendFrame.u16RegAddr);
	stSendFrame.u16RegData = swap_u16(stSendFrame.u16RegData);
	stSendFrame.u16CRC = CRC16_Modbus((uint8_t *)&stSendFrame, sizeof(ModbusSendFrame) - 2);
	stSendFrame.u16CRC = swap_u16(stSendFrame.u16CRC);
	memcpy(buffer,(uint8_t * )&stSendFrame,sizeof(ModbusSendFrame));
	LOG(LOG_INFO,"Sensor send data is");
	LOG_ARRAY(LOG_INFO,buffer,sizeof(ModbusSendFrame),"%02x");
	pthread_mutex_lock(&g_lock[g_stDevicelist.Sensor]);
	write(g_stDevicelist.UartList[g_stDevicelist.Sensor].i32fd, buffer, sizeof(ModbusSendFrame));
	pthread_mutex_unlock(&g_lock[g_stDevicelist.Sensor]);
	return SUCCESS;
}


int sendNCSMeasueCmd()
{
	return SUCCESS;
}



/*
*函数名称:ReadRotatorSpeedValue
*函数功能:读取转子流速仪流速指令
*输   入:无
*输   出: 无
*返   回:成功       失败
*/

void  ReadRotatorSpeedValue(void * arg)
{
	pthread_mutex_lock(&g_lock[g_stDevicelist.Sensor]);
	g_stSensorFlag.u8Ratator_value_falg = 1;
	pthread_mutex_unlock(&g_lock[g_stDevicelist.Sensor]);
	LOG(LOG_DEBUG,"g_stSensorFlag.u8Ratator_value_falg is %d",g_stSensorFlag.u8Ratator_value_falg);
	SensorSendDataCmd(g_stDeviceAddr.u8Rotator_addr,READCODE,RORATOR_DI1_VALUE_REG,2);
}

/**
 * 重置转子值函数
 * 
 * 本函数旨在向转子传感器发送重置信号，将其内部的值重置为预设状态
 * 它通过调用SensorSendDataCmd函数，以WRITECODE写入方式，向转子传感器
 * 发送重置值命令和数据长度，以实现重置操作
 * 
 * @param arg 通用参数，允许函数接受额外的参数以提高灵活性和可扩展性
 */
void ResetRotatorValue(void * arg)
{
    // 记录函数执行日志，便于调试和追踪
    LOG(LOG_DEBUG,"ResetRotatorValue");
    
    // 向转子传感器发送重置值命令，重置内部值
    SensorSendDataCmd(g_stDeviceAddr.u8Rotator_addr,WRITECODE,RESET_VALUE_REG,0x000f);
}


/*
*函数名称:ReadRotatorSpeedValue
*函数功能:读取转子流速仪输入端口状态
*输   入:无
*输   出: 无
*返   回:成功       失败
*/

void ReadRotatorDIStatus(void * arg)
{
	g_stSensorFlag.u8Rotator_DI_flag = 1;
	
	SensorSendDataCmd(g_stDeviceAddr.u8Rotator_addr,READCODE,ROTATOR_DI_STATUS_REG,1);
}

int SendADCPMeasureCmd()
{
	return SUCCESS;
}

int sendVE_ADCP_MeasureCmc()
{
	return SUCCESS;
}


int sendMUDS_MeasureCmc()
{
	return SUCCESS;
}

int SendTemp_MeasureCmd()
{
	return SUCCESS;
}

/*
*函数名称:ReadBatterySOCCmd
*函数功能:读取电池剩余容量百分比
*输   入:无
*输   出: 无
*返   回:成功       失败
*/

int ReadBatterySOCCmd()
{
	//pthread_mutex_lock(&g_lock[g_stDevicelist.Relay]);
	g_stSensorFlag.u8Coulomb_SOC_Flag = 1;
	//pthread_mutex_unlock(&g_lock[g_stDevicelist.Relay]);
	LOG(LOG_INFO,"Send Read Battery SOC Cmd");
	return SensorSendDataCmd(g_stDeviceAddr.u8CoulombAddr,READ_RELAY_CODE,COULOMB_SOC_REG,1);
}

/*
*函数名称:ReadBatteryVoltageCmd
*函数功能:读取电池电压
*输   入:无
*输   出: 无
*返   回:成功       失败
*/

int ReadBatteryVoltageCmd()
{
	//pthread_mutex_lock(&g_lock[g_stDevicelist.Relay]);
	g_stSensorFlag.u8Coulomb_voltage_flag = 1;
	//pthread_mutex_unlock(&g_lock[g_stDevicelist.Relay]);
	LOG(LOG_INFO,"Send Read Battery Voltage Cmd");
	return SensorSendDataCmd(g_stDeviceAddr.u8CoulombAddr,READ_RELAY_CODE,COULOMB_VOLTAGE_REG,1);
}

/*
*函数名称:ReadBatteryCurrentCmd
*函数功能:读取电池电流
*输   入:无
*输   出: 无
*返   回:成功       失败
*/

int ReadBatteryCurrentCmd()
{
	//pthread_mutex_lock(&g_lock[g_stDevicelist.Relay]);
	g_stSensorFlag.u8Coulomb_current_flag = 1;
	//pthread_mutex_unlock(&g_lock[g_stDevicelist.Relay]);
	LOG(LOG_INFO,"Send Read Battery Current Cmd");
	return SensorSendDataCmd(g_stDeviceAddr.u8CoulombAddr,READ_RELAY_CODE,COULOMB_CURRENT_REG,1);
}

/*
*函数名称:ReadBatteryChargeStateCmd
*函数功能:读取电池充电状态
*输   入:无
*输   出: 无
*返   回:成功       失败
*/

int ReadBatteryChargeStateCmd()
{
	//pthread_mutex_lock(&g_lock[g_stDevicelist.Relay]);
	g_stSensorFlag.u8Coulomb_ChargeState_flag = 1;
	//pthread_mutex_unlock(&g_lock[g_stDevicelist.Relay]);
	LOG(LOG_INFO,"Send Read Battery Charge State Cmd");
	return SensorSendDataCmd(g_stDeviceAddr.u8CoulombAddr,READ_RELAY_CODE,COULOMB_CHARGE_STATE_REG,1);

}

/*
*函数名称:ReadBatteryRemainCapacityCmd
*函数功能:读取电池剩余容量mAh
*输   入:无
*输   出: 无
*返   回:成功       失败
*/

int ReadBatteryRemainCapacityCmd()
{
	//pthread_mutex_lock(&g_lock[g_stDevicelist.Relay]);
	g_stSensorFlag.u8Coulomb_Remain_capa_flag = 1;
	//pthread_mutex_unlock(&g_lock[g_stDevicelist.Relay]);
	
	return SensorSendDataCmd(g_stDeviceAddr.u8CoulombAddr,READ_RELAY_CODE,COULOMB_REMAIN_CAPACITY_REG,2);
}


/*
*函数名称:ReadBatteryState
*函数功能:轮询读取库仑计SOC/电压/电流/充放电状态
*修改说明(2026-06-22): 保持原轮询顺序；上一条未应答时不发新指令；与转子分时复用ttyS5
*/
void  ReadBatteryState(void *arg)
{
	(void)arg;
	if (OtaUpgrade_IsBusy()) {
		return;
	}
	/* [修改] 转子读指令等待应答时，不在同一条RS485上插入库仑计请求 */
	if (g_u8RotatorReadPending)
	{
		return;
	}
	if (CoulombHasPendingFlag())
	{
		g_u32CoulombPendingTicks++;
		if (g_u32CoulombPendingTicks < COULOMB_RSP_TIMEOUT_TICKS)
		{
			return;
		}
		/* [修改] 库仑计长时间无应答则清标志重发，避免电压/SOC永久不更新 */
		LOG(LOG_WARNING, "Coulomb read pending timeout, resend");
	}

	ClearCoulombPendingFlags();
	switch (g_batteryParaSeri ++)
	{
		case 0:
			ReadBatterySOCCmd();
			break;
		case 1:
			ReadBatteryVoltageCmd();
			break;
		case 2:
			ReadBatteryCurrentCmd();
			break;
		case 3:
			ReadBatteryChargeStateCmd();
			break;
		default:
			g_batteryParaSeri = 0;
			break;
	}
}

void SendMeasureResultManu(uint32_t u32Element,int type)
{
	LOG(LOG_INFO,"type is %d",type);
	printf("Current Measure element is %08x\n",u32Element);
	if(u32Element &0x01)//非接触式测流类型
	{
		
		//sendNCSMeasueCmd();
		
	}
	mSleep(50);
	if( (u32Element >>1)& 0x01 ) //转子流速仪类型
	{ 
		SendRotatorResultManual((void *) type);
		//SendRotatorResultAuto((void *)type);
		
			
	}
	mSleep(50);
	if( (u32Element >>2)& 0x01) //ADCP类型
	{
		//SendADCPMeasureCmd();	
		
	}
	mSleep(50);
	if( (u32Element >>3)& 0x01) //走航式ADCP类型
	{
		//sendVE_ADCP_MeasureCmc();
		
		
	}
	mSleep(50);
	if( (u32Element >>4)& 0x01)//泥沙类型
	{
		//sendMUDS_MeasureCmc();
		
	}
	mSleep(50);
	if( (u32Element >>5)& 0x01)//温度类型
	{
		//SendTemp_MeasureCmd();  
		
	}
}

void SendMeasureResultAuto(int type)
{
	if(g_pstAutoRotatorList != NULL)
	{
		SendRotatorResultAuto((void *)type);
	}

}


void  sendMeasureResult(uint32_t u32Element,int type)
{
	LOG(LOG_INFO,"type is %d",type);
	printf("Current Measure element is %08x\n",u32Element);
	if(u32Element &0x01)//非接触式测流类型
	{
		
		sendNCSMeasueCmd();
		
	}
	mSleep(50);
	if( (u32Element >>1)& 0x01 ) //转子流速仪类型
	{ 
		
		SendRotatorResultAuto((void *)type);
		
			
	}
	mSleep(50);
	if( (u32Element >>2)& 0x01) //ADCP类型
	{
		SendADCPMeasureCmd();	
		
	}
	mSleep(50);
	if( (u32Element >>3)& 0x01) //走航式ADCP类型
	{
		sendVE_ADCP_MeasureCmc();
		
		
	}
	mSleep(50);
	if( (u32Element >>4)& 0x01)//泥沙类型
	{
		sendMUDS_MeasureCmc();
		
	}
	mSleep(50);
	if( (u32Element >>5)& 0x01)//温度类型
	{
		SendTemp_MeasureCmd();  
		
	}

}


/*
*函数名称:ReadRotatorValuePeriodic
*函数功能:周期性读取转子流速仪组合数据(DI状态+计数值)
*修改说明(2026-06-22): 150ms轮询+应答后再发；main.c定时器已改为150ms
*输   入: void *arg  未使用
*输   出: 无
*返   回: 无
*/
void ReadRotatorValuePeriodic(void * arg)
{
    (void)arg;
    if (OtaUpgrade_IsBusy()) {
        return;
    }

    /* [修改] 库仑计读指令等待应答时，让出RS485总线 */
    if (CoulombHasPendingFlag())
    {
        return;
    }

    /* [修改] 上一条读指令未应答时不重复发送，避免COM5请求堆积 */
    if (g_u8RotatorReadPending)
    {
        g_u32RotatorPendingTicks++;
        if (g_u32RotatorPendingTicks < ROTATOR_PENDING_TIMEOUT)
        {
            return;
        }
        LOG(LOG_WARNING, "Rotator read pending timeout, resend");
        g_u8RotatorReadPending = 0;
        g_u32RotatorPendingTicks = 0;
        /* 铅鱼转子通信超时 → 上报传感器出错（每故障周期只报一次） */
        g_stErrorState.bSensorError = 1;
        if (g_u8RotatorFaultReported == 0) {
            g_u8RotatorFaultReported = 1;
            SendReportSensorError(ROTATOR, DTU_TYPE);
        }
    }

    // [修改] 从寄存器0x0004起连续读3个: reg4=DI状态, reg5/6=计数值
    if (SUCCESS == SensorSendDataCmd(g_stDeviceAddr.u8Rotator_addr, READCODE, 0x0004, ROTATOR_COMBINED_READ_REG_NUM))
    {
        g_u8RotatorReadPending = 1;
        g_u32RotatorPendingTicks = 0;
        g_rotatorPeriodicUpdated = 0;
    }
}

int SensorRecDataProc(KFIFO_ID pData)
{
	
    uint16_t u16RecCRC = 0;
    uint8_t tempbuff[128] ={0};
    uint32_t u32fifolen = 0;
    u32fifolen = kfifo_len (pData);
	if(u32fifolen < 4)
	{
		return ERROR;
	}
	/* [修改] 解析前先丢弃本机Modbus读请求TX回显 */
	SensorSkipTxEchoFrames(pData);
	u32fifolen = kfifo_len(pData);
	if(u32fifolen < 4)
	{
		return ERROR;
	}
	int DeviceType = uartFindDeviceType(pData,g_stDeviceAddr.u8Rotator_addr,READCODE,g_stDeviceAddr.u8CoulombAddr,READ_RELAY_CODE);
	//LOG(LOG_INFO,"DeviceType is %d",DeviceType);
	if(DeviceType == 1) //转子流速仪
	{
		if(u32fifolen <  sizeof(RotatorRecDIStatus))
		{
			//LOG(LOG_DEBUG,"u32fifolen is %d < sizeof(RotatorRecValue) %d",u32fifolen,sizeof(RotatorRecDIStatus));
			return ERROR;
		}
		kfifo_preview(pData,tempbuff,3);
		if(tempbuff[2] == 0x06) /* [修改] 组合读应答: DI+CountH+CountL，更新g_stSwitchStatus与g_u32RotatorPeriodicCount */
		{
			if(u32fifolen < sizeof(RotatorRecCombined))
			{
				return ERROR;
			}
			RotatorRecCombined stCombined;
			kfifo_get(pData, tempbuff, sizeof(RotatorRecCombined));
			memcpy(&stCombined, tempbuff, sizeof(RotatorRecCombined));
			u16RecCRC = CRC16_Modbus(tempbuff, sizeof(RotatorRecCombined) - 2);
			if(u16RecCRC != swap_u16(stCombined.u16CRC))
			{
				LOG(LOG_ERROR, "Rotator combined recData CRC error");
				LOG_ARRAY(LOG_DEBUG, tempbuff, sizeof(RotatorRecCombined), "%02x");
				g_u8RotatorReadPending = 0;
				g_u32RotatorPendingTicks = 0;
				kfifo_get(pData, tempbuff, 1);
				return ERROR;
			}
			/* [修改] 保存周期性转子计数值与DI(供WaitRotatorSecondSignal/fsm_measureWaitDuration/触底DI) */
			g_u32RotatorPeriodicCount = (swap_u16(stCombined.u16CountH) << 16) | swap_u16(stCombined.u16CountL);
			// 保存DI状态
			g_u16RotatorPeriodicDI = swap_u16(stCombined.u16DIStatus);
			// 更新DI开关量
			memcpy((uint16_t*)&g_stSwitchStatus, &g_u16RotatorPeriodicDI, sizeof(SwitchStatus));
			//LOG(LOG_DEBUG,"g_u32RotatorPeriodicCount is %d,",g_u32RotatorPeriodicCount);
			// 设置更新标志
			g_rotatorPeriodicUpdated = 1;
			g_u8RotatorReadPending = 0;
			g_u32RotatorPendingTicks = 0;
			g_stErrorState.bSensorError = 0;
			g_u8RotatorFaultReported = 0;
			g_SensorNum--;
		}
		else if(tempbuff[2] == 0x04)//读取单个计数值指令返回的数据(向后兼容)
		{
			pthread_mutex_lock(&g_lock[g_stDevicelist.Sensor]);
			g_stSensorFlag.u8Ratator_value_falg = 0;
			pthread_mutex_unlock(&g_lock[g_stDevicelist.Sensor]);
			kfifo_get(pData,tempbuff,sizeof(RotatorRecValue));//读取9个字节
			LOG(LOG_INFO,"Ratator Rec Value is :");
			LOG_ARRAY(LOG_INFO,tempbuff,sizeof(RotatorRecValue),"%02x");
			memcpy(&g_stRotatorRecValue,tempbuff,sizeof(RotatorRecValue));
			u16RecCRC = CRC16_Modbus(tempbuff,sizeof(RotatorRecValue) -2);
			if(u16RecCRC != swap_u16( g_stRotatorRecValue.u16CRC))
			{
				LOG(LOG_ERROR,"Ratator recData CRC is note Equal");
				memset(&g_stRotatorRecValue,0, sizeof(RotatorRecValue));
				return ERROR;
			}
			g_stRotatorRecValue.u32Value = swap_u32(g_stRotatorRecValue.u32Value);
			LOG(LOG_DEBUG,"g_stRotatorRecValue is %08x",g_stRotatorRecValue.u32Value);
			g_SensorNum--;
			 
				
		}	
		else if(tempbuff[2] == 0x02) //读取端口状态返回的数据
		{
			g_stSensorFlag.u8Rotator_DI_flag = 0;
			kfifo_get(pData,tempbuff,sizeof(RotatorRecDIStatus));//读取7个字节
			LOG(LOG_INFO,"Ratator Rec DI Status is :");
			LOG_ARRAY(LOG_INFO,tempbuff,sizeof(RotatorRecDIStatus),"%02x");
			memcpy(&g_stRotatorRecDIStatus,tempbuff,sizeof(RotatorRecDIStatus));
			u16RecCRC = CRC16_Modbus(tempbuff,sizeof(RotatorRecDIStatus) -2);
			if(u16RecCRC != swap_u16( g_stRotatorRecDIStatus.u16CRC))
			{
				LOG(LOG_ERROR,"Ratator recData CRC is note Equal");
				return ERROR;
			}
			LOG(LOG_INFO,"read Sensor DI Staus recv data is  %04x",g_stRotatorRecDIStatus.u16RecData );
			g_stRotatorRecDIStatus.u16RecData = g_stRotatorRecDIStatus.u16RecData ;
			memcpy((uint16_t*)&g_stSwitchStatus,(uint16_t*)&g_stRotatorRecDIStatus.u16RecData,sizeof(SwitchStatus));
			
		}
		else
		{
			/* [修改] 非预期转子帧逐字节滑窗，避免FIFO死锁 */
			kfifo_get(pData, tempbuff, 1);
			return ERROR;
		}
	}
	else if(DeviceType == 0)
	{
		kfifo_get(pData, tempbuff, 1);
		return ERROR;
	}
	else if(DeviceType == 2)  /* [修改] 库仑计应答: 与转子分时复用ttyS5，else-if按等待标志解析 */
	{
		if(u32fifolen <  sizeof(BatteryRecStatus))
		{
			return ERROR;
		}
		if(1 == g_stSensorFlag.u8Coulomb_SOC_Flag)
		{
			g_stSensorFlag.u8Coulomb_SOC_Flag = 0;
			kfifo_get(pData,tempbuff,7);
			printData("Battery  SOCRec Data is :",tempbuff,sizeof(BatteryRecStatus));
			memcpy(&g_stBatteryRecStatus,tempbuff,sizeof(BatteryRecStatus));
			u16RecCRC = CRC16_Modbus(tempbuff,sizeof(BatteryRecStatus) -2);
			if(u16RecCRC != swap_u16( g_stBatteryRecStatus.u16CRC))
			{
				LOG(LOG_ERROR,"Battery SOC Rec Data CRC is note Equal");
				return ERROR;
			}
			g_stReportState.u8Soc = (uint8_t)(swap_u16(g_stBatteryRecStatus.u16RecData) / 10);
			LOG(LOG_INFO,"Battery SOC is %.1f %%",(float)swap_u16(g_stBatteryRecStatus.u16RecData) / 10);
		}
		else if(1 == g_stSensorFlag.u8Coulomb_voltage_flag)
		{
			g_stSensorFlag.u8Coulomb_voltage_flag = 0;
			kfifo_get(pData,tempbuff,7);
			printData("Battery voltage_ Rec Data is :",tempbuff,sizeof(BatteryRecStatus));
			memcpy(&g_stBatteryRecStatus,tempbuff,sizeof(BatteryRecStatus));
			u16RecCRC = CRC16_Modbus(tempbuff,sizeof(BatteryRecStatus) -2);
			if(u16RecCRC != swap_u16( g_stBatteryRecStatus.u16CRC))
			{
				LOG(LOG_ERROR,"Battery voltage recData CRC is note Equal");
				return ERROR;
			}
			/* [修改] H56库仑计FC04: 电压系数0.01V，寄存器4800=48.00V；与SL651 0.01V单位一致 */
			g_stReportState.u16CarVol = swap_u16(g_stBatteryRecStatus.u16RecData);
			g_stReportState.u16RTUVol = g_stReportState.u16CarVol;
			LOG(LOG_INFO,"Battery Voltage is %.2f V",(float)g_stReportState.u16CarVol / 100);
		}
		else if(1 == g_stSensorFlag.u8Coulomb_current_flag)
		{
			g_stSensorFlag.u8Coulomb_current_flag = 0;
			kfifo_get(pData,tempbuff,7);
			printData("Battery current Rec Data is :",tempbuff,sizeof(BatteryRecStatus));
			memcpy(&g_stBatteryRecStatus,tempbuff,sizeof(BatteryRecStatus));
			u16RecCRC = CRC16_Modbus(tempbuff,sizeof(BatteryRecStatus) -2);
			if(u16RecCRC != swap_u16( g_stBatteryRecStatus.u16CRC))
			{
				LOG(LOG_ERROR,"Battery current recData CRC is note Equal");
				return ERROR;
			}
			g_stReportState.u16Current = swap_u16(g_stBatteryRecStatus.u16RecData);
			LOG(LOG_INFO,"Battery Current is %.2f A",(float)g_stReportState.u16Current / 100);
		}
		else if(1 == g_stSensorFlag.u8Coulomb_ChargeState_flag)
		{
			g_stSensorFlag.u8Coulomb_ChargeState_flag = 0;
			kfifo_get(pData,tempbuff,7);
			printData("Battery ChargeStat Rec Data is :",tempbuff,sizeof(BatteryRecStatus));
			memcpy(&g_stBatteryRecStatus,tempbuff,sizeof(BatteryRecStatus));
			u16RecCRC = CRC16_Modbus(tempbuff,sizeof(BatteryRecStatus) -2);
			if(u16RecCRC != swap_u16( g_stBatteryRecStatus.u16CRC))
			{
				LOG(LOG_ERROR,"Battery ChargeStat recData CRC is note Equal");
				return ERROR;
			}
			g_stReportState.u8ChargeState = swap_u16(g_stBatteryRecStatus.u16RecData ) & 0xff;
			LOG(LOG_INFO,"Battery charge state  is %d (0:静止 1:放电 2:充电) ",g_stReportState.u8ChargeState);
		}
		else
		{
			kfifo_get(pData, tempbuff, 1);
			return ERROR;
		}
	}
	
	if(100 <= u32fifolen)
	{
		kfifo_clear(pData);//全部读取
	}
	
	return SUCCESS;
    
}

int DMA_5213_SendDataCmd(uint8_t SubAddr,uint8_t FuncCode, uint16_t RegAddr,uint16_t RegNum)
{
	uint8_t buffer[24] = {0};
	MODBUS_FRAME stSendFrame;
	stSendFrame.u8SlaveAddr = SubAddr;
	stSendFrame.u8FunctionCode = FuncCode;
	stSendFrame.u16StartAddr = RegAddr;
	stSendFrame.u16data = RegNum;
	stSendFrame.u16StartAddr = swap_u16(stSendFrame.u16StartAddr);
    stSendFrame.u16data = swap_u16(stSendFrame.u16data);
	stSendFrame.u16CRC = CRC16_Modbus((uint8_t *)&stSendFrame, sizeof(MODBUS_FRAME) - 2);
	stSendFrame.u16CRC = swap_u16(stSendFrame.u16CRC);
	memcpy(buffer,(uint8_t * )&stSendFrame,sizeof(MODBUS_FRAME));
	LOG(LOG_DEBUG,"Sensor send data is");
	LOG_ARRAY(LOG_DEBUG,buffer,sizeof(MODBUS_FRAME),"%02x");
   
    write(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd, buffer, sizeof(MODBUS_FRAME));

	return SUCCESS;
}

void DMA5213_RelayCtrl(int relayseri, int relaystatus)
{
    if (relaystatus < 0 || relaystatus > 1) {
		LOG(LOG_ERROR, "DMA5213_RelayCtrl: input relaystatus error");
        return;
    }

    if(relaystatus == RELAY_ON)
    {
        switch (relayseri)
        {
            case 1:
                g_stDOStatus.u16DO0_STATUS |= (uint16_t)relaystatus;
                break;
            case 2:
                g_stDOStatus.u16DO1_STATUS |= (uint16_t)relaystatus;
                break;
            case 3:
                g_stDOStatus.u16DO2_STATUS |= (uint16_t)relaystatus;
                break;
            case 4:
                g_stDOStatus.u16DO3_STATUS |= (uint16_t)relaystatus;
                break;
            default:
                return;
        }

    }
    else if(relaystatus == RELAY_OFF)
    {
        switch (relayseri)
        {
            case 1:
                g_stDOStatus.u16DO0_STATUS &= (uint16_t)relaystatus;
                break;
            case 2:
                g_stDOStatus.u16DO1_STATUS &= (uint16_t)relaystatus;
                break;
            case 3:
                g_stDOStatus.u16DO2_STATUS &= (uint16_t)relaystatus;
                break;
            case 4:
                g_stDOStatus.u16DO3_STATUS &= (uint16_t)relaystatus;
                break;
            default:
                return;
        }
    }
    
    DMA_5213_SendDataCmd(g_stDeviceAddr.u8Electroc_door, WRITECODE, SWITCH_DO_STATUS, *(uint16_t *)&g_stDOStatus);
}

void DMA_5213_POWER_ON(int relay_com)
{ 
    LOG(LOG_DEBUG, "DMA_5213_POWER_ON,COM IS %s",relay_com == DOOR_OPEN ? "DOOR_OPEN" : "DOOR_CLOSE");
	g_bDoorCtrlSendFlag = true;
    DMA5213_RelayCtrl(relay_com,RELAY_ON);
}

void DMA_5213_POWER_OFF(int relay_com)
{
    LOG(LOG_DEBUG, "DMA_5213_POWER_OFF,COM IS %s",relay_com == DOOR_OPEN ? "DOOR_OPEN" : "DOOR_CLOSE");
	g_bDoorCtrlSendFlag = true;
    DMA5213_RelayCtrl(relay_com, RELAY_OFF);
}

uint8_t read_door_status_buffer[] = { 0x03, 0x03, 0x00, 0x04, 0x00, 0x01, 0xc4, 0x29};

/* 开关门脉冲后短暂禁读 DI，避免与写 DO/清继电器抢总线 */
#define DOOR_DI_QUIET_AFTER_CMD_MS   400U
#define DOOR_DI_POLL_BUSY_MS         500U
#define DOOR_DI_POLL_IDLE_MS         1000U
#define DOOR_PULSE_HOLD_MS           150U

#if ENABLE_ELECTRIC_DOOR
static volatile bool g_bDoorStatusThreadRunning = true;
static uint64_t s_doorDiQuietUntilMs = 0;

static uint64_t door_now_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static void door_mark_cmd_sent(void)
{
	s_doorDiQuietUntilMs = door_now_ms() + DOOR_DI_QUIET_AFTER_CMD_MS;
}

static void doorStatusPollOnce(void)
{
	if (!g_bDoorCtrlSendFlag) {
		return;
	}
	/* 脉冲刚发出时先让路，再读限位 */
	if (door_now_ms() < s_doorDiQuietUntilMs) {
		return;
	}
	LOG(LOG_DEBUG, "Door TX read DI:");
	LOG_ARRAY(LOG_DEBUG, read_door_status_buffer, sizeof(read_door_status_buffer), "%02x");
	write(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd, read_door_status_buffer, sizeof(read_door_status_buffer));
}

static void *DoorStatusThread(void *arg)
{
	(void)arg;
	LOG(LOG_INFO, "DoorStatusThread started");
	while (g_bDoorStatusThreadRunning)
	{
		if (g_bDoorCtrlSendFlag)
		{
			LOG(LOG_DEBUG, "DoorStatusThread tick, doorPollFlag=1 (send read DI)");
		}
		doorStatusPollOnce();
		mSleep(g_bDoorCtrlSendFlag ? DOOR_DI_POLL_BUSY_MS : DOOR_DI_POLL_IDLE_MS);
	}
	return NULL;
}
#endif

int StartDoorStatusThread(void)
{
#if !ENABLE_ELECTRIC_DOOR
	LOG(LOG_INFO, "StartDoorStatusThread: ENABLE_ELECTRIC_DOOR=0, skip");
	return SUCCESS;
#else
	pthread_t tid;
	if(pthread_create(&tid, NULL, DoorStatusThread, NULL) != 0)
	{
		LOG(LOG_ERROR, "StartDoorStatusThread: pthread_create failed");
		return ERROR;
	}
	pthread_detach(tid);
	LOG(LOG_INFO, "Door status thread started");
	return SUCCESS;
#endif
}

uint8_t open_cmd_buffer[] =		{ 0x03, 0x06, 0x00, 0x09, 0x00, 0x01, 0x99, 0xEA};
uint8_t close_cmd_buffer[] = 	{ 0x03, 0x06, 0x00, 0x09, 0x00, 0x02, 0xD9, 0xEB};
uint8_t close_all_relay[] = 	{ 0x03, 0x06, 0x00, 0x09, 0x00, 0x00, 0x58, 0x2A};

void Door_ReadDiFresh(void)
{
#if !ENABLE_ELECTRIC_DOOR
	return;
#else
	g_bDoorCtrlSendFlag = true;
	mSleep(200);
	LOG(LOG_DEBUG, "Door TX read DI fresh:");
	LOG_ARRAY(LOG_DEBUG, read_door_status_buffer, sizeof(read_door_status_buffer), "%02x");
	write(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd,
	      read_door_status_buffer, sizeof(read_door_status_buffer));
	mSleep(300);
	LOG(LOG_INFO,
	    "Door DI fresh: open=%u close=%u (0=到位)",
	    (unsigned)g_stDoorSwitchStatus.u16watersurface_1,
	    (unsigned)g_stDoorSwitchStatus.u16watersurface_2);
#endif
}

void Door_Open_cmd()
{
#if !ENABLE_ELECTRIC_DOOR
	g_stDoorSwitchStatus.u16watersurface_1 = DOOR_DI_POWERED;
	g_bDoorCtrlSendFlag = false;
	LOG(LOG_INFO, "Electric door disabled, skip open (treat as opened)");
	return;
#endif
	g_bDoorCtrlSendFlag = true;
	/* 不再把开限位强行清成未到位，避免轮询丢包时永远等不到开到位 */
	LOG(LOG_DEBUG, "Door TX open:");
	LOG_ARRAY(LOG_DEBUG, open_cmd_buffer, sizeof(open_cmd_buffer), "%02x");
	write(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd, open_cmd_buffer, sizeof(open_cmd_buffer));
	mSleep(DOOR_PULSE_HOLD_MS);
	LOG(LOG_DEBUG, "Door TX clear DO:");
	LOG_ARRAY(LOG_DEBUG, close_all_relay, sizeof(close_all_relay), "%02x");
	write(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd, close_all_relay, sizeof(close_all_relay));
#if ENABLE_ELECTRIC_DOOR
	door_mark_cmd_sent();
#endif
}

void Door_Close_cmd()
{
#if !ENABLE_ELECTRIC_DOOR
	g_stDoorSwitchStatus.u16watersurface_2 = DOOR_DI_POWERED;
	g_bDoorCtrlSendFlag = false;
	LOG(LOG_INFO, "Electric door disabled, skip close (treat as closed)");
	return;
#endif
	g_bDoorCtrlSendFlag = true;
	/* 同开门：不清本地关限位，靠真实 DI + 调用方延时防抖 */
	LOG(LOG_DEBUG, "Door TX close:");
	LOG_ARRAY(LOG_DEBUG, close_cmd_buffer, sizeof(close_cmd_buffer), "%02x");
	write(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd, close_cmd_buffer, sizeof(close_cmd_buffer));
	mSleep(DOOR_PULSE_HOLD_MS);
	LOG(LOG_DEBUG, "Door TX clear DO:");
	LOG_ARRAY(LOG_DEBUG, close_all_relay, sizeof(close_all_relay), "%02x");
	write(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd, close_all_relay, sizeof(close_all_relay));
#if ENABLE_ELECTRIC_DOOR
	door_mark_cmd_sent();
#endif
}

void Door_Toggle_cmd(void)
{
#if !ENABLE_ELECTRIC_DOOR
	LOG(LOG_INFO, "Electric door disabled, skip toggle");
	return;
#else
	uint64_t t0;
	uint64_t now;
	int want_open; /* 1=等到开到位, 0=等到关到位 */
	const uint64_t quiet_pre_ms = 250U;
	const uint64_t wait_max_ms = 30000U;

	/* 与加测门控一致：先占总线，水位/状态/触摸屏让路 */
	g_bDoorCtrlSendFlag = true;
	LOG(LOG_INFO, "Door toggle: ZigBee quiet begin");
	mSleep((unsigned int)quiet_pre_ms);

	LOG(LOG_DEBUG, "Door TX read DI before toggle:");
	LOG_ARRAY(LOG_DEBUG, read_door_status_buffer, sizeof(read_door_status_buffer), "%02x");
	write(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd,
	      read_door_status_buffer, sizeof(read_door_status_buffer));
	mSleep(300);

	LOG(LOG_INFO,
	    "Door toggle DI: open=%u close=%u (0=到位)",
	    (unsigned)g_stDoorSwitchStatus.u16watersurface_1,
	    (unsigned)g_stDoorSwitchStatus.u16watersurface_2);

	if (DOOR_OPEN_REACHED()) {
		LOG(LOG_INFO, "Door toggle: opened -> close");
		want_open = 0;
		Door_Close_cmd();
	} else if (DOOR_CLOSE_REACHED()) {
		LOG(LOG_INFO, "Door toggle: closed -> open");
		want_open = 1;
		Door_Open_cmd();
	} else {
		LOG(LOG_WARNING, "Door toggle: mid/unknown, default close");
		want_open = 0;
		Door_Close_cmd();
	}

	/* 保持静默直到到位或超时，再恢复水位/状态/触摸屏 */
	t0 = door_now_ms();
	for (;;) {
		now = door_now_ms();
		if (want_open) {
			if (DOOR_OPEN_REACHED() && (now - t0) >= 1000U) {
				break;
			}
		} else {
			if (DOOR_CLOSE_REACHED() && (now - t0) >= 1000U) {
				break;
			}
		}
		if (now - t0 >= wait_max_ms) {
			LOG(LOG_WARNING,
			    "Door toggle: wait timeout %us, openDI=%u closeDI=%u",
			    (unsigned)(wait_max_ms / 1000U),
			    (unsigned)g_stDoorSwitchStatus.u16watersurface_1,
			    (unsigned)g_stDoorSwitchStatus.u16watersurface_2);
			break;
		}
		mSleep(200);
	}

	g_bDoorCtrlSendFlag = false;
	LOG(LOG_INFO,
	    "Door toggle: ZigBee quiet end, openDI=%u closeDI=%u",
	    (unsigned)g_stDoorSwitchStatus.u16watersurface_1,
	    (unsigned)g_stDoorSwitchStatus.u16watersurface_2);
#endif
}
