#include "Common.h"
#include "RS485_ICD.h"
#include "TaskList.h"
#include "zigbee_dataproc.h"
#include "WebSeriver.h"
#include "Encoder.h"
#include "Sensor.h"
#include "SocketLink.h"
#include "log.h"
/*
*函数名称:UartWrite
*函数功能:写入数据到串口
*输   入:int port_fd  串口句柄const unsigned char* data  需要发送的数据 int length 发送的数据长度
*输   出:char *pTimestr  时间字符串
*返   回:写入的字节长度
*/
int UartWrite(int port_fd, const unsigned char* data, int length) 
{
	int written = write(port_fd, data, length); 
	if (written == -1) {
		perror("write");
		return -1;
	}
	return written;
}

/*
*函数名称:UartRead
*函数功能:从串口读取数据
*输   入:int port_fd  串口句柄  int length 需要读取的数据长度
*输   出:unsigned char* data  读取到的数据
*返   回:读取到的数据长度
*/
int UartRead(int port_fd, unsigned char* buffer, int length)
 {
	int bytes_read = read(port_fd, buffer, length); 
	if (bytes_read == -1) {
		perror("read");
		return ERROR;
	}
	return bytes_read;
}

/*
*函数名称:StartDealTaskThread
*函数功能:创建串口工作线程
*输   入:struct Uart_Info* pUartInfo  串口信息
*输   出:无
*返   回:SUCCESS - 创建成功  ERROR - 创建失败
*/
int32_t StartDealTaskThread(struct Uart_Info* pUartInfo)
{

	pthread_t ThreadID;
	if(SUCCESS ==   pthread_create(&ThreadID,NULL,(void*)DealTaskWork,pUartInfo))//创建线程
	{
		pthread_detach(ThreadID);
	    printf("pthread id %d start success!\n",getpid());
		return SUCCESS;

	}
	else
	{
		printf(" uart thread creat error!\n");
		return ERROR;
	}
	
}

/*
*函数名称:startUartReceiveThread
*函数功能:创建串口接收线程
*输   入:struct DEVICE_LIST * devicelist  设备列表
*输   出:无
*返   回:SUCCESS - 创建成功  ERROR - 创建失败
*/
int32_t startUartReceiveThread(struct DEVICE_LIST * devicelist)
{
	pthread_t receiveid;
	if(SUCCESS == pthread_create(&receiveid,NULL,(void*)uartRecvFun,devicelist))
	{
		pthread_detach(receiveid);
		LOG(LOG_INFO,"pthread id %d start success!",getpid());
		return 0;

	}
	else
	{
		LOG(LOG_ERROR," uart thread creat error!");
		return ERROR;
	}
	
}


/*
*函数名称:uartRecvFun
*函数功能:串口接收线程工作函数，用于接收所有串口的信息，放到每个串口对应的环形缓冲区中
*输   入:void * pParam  线程传入参数，devicelist，设备列表
*输   出:无
*返   回:无
*/
void uartRecvFun(void * pParam)
{
	int i = 0;
	//int j = 0;
	int  iReadByte = 0;
	//uint32_t u32fifolen = 0;
	struct DEVICE_LIST * devicelist = (struct DEVICE_LIST * )pParam;
	LOG(LOG_INFO, "Uart receive thread id is: %lu",pthread_self());
	while (1)
	{
		for(i = 0;i< 6;i++)
		{
			iReadByte = UartRead(devicelist->UartList[i].i32fd,devicelist->UartList[i].receiveBuffer,RECEVIE_BUFFER_LENGTH);
			if(iReadByte <= 0)
			{
				
				continue;
			}			
			kfifo_put(devicelist->UartList[i].ringfifo,devicelist->UartList[i].receiveBuffer,iReadByte);
		}
	}
		
}


uint32_t GetCurrentWaterLevel()
{
	uint32_t waterLevel = 0;
	waterLevel = (g_WebSendWaterLevel == 1000000) ? g_u32waterLevel : g_WebSendWaterLevel;
	g_stTouchScreenReportData.u16WaterLevelData = (uint16_t) (waterLevel);
	
	return waterLevel;
}


/*
*函数名称：UartReceiveDataProc
*函数功能：串口接收数据处理，根据串口的类型分别处理
*输   入：enum UartType StaticType 串口类型  KFIFO_ID pData 需要处理的数据缓冲区  WORD* pUserd 已经使用的字节数
*输   出：无
*返   回：SUCCESS - 串口接收数据处理成功  ERROR - 串口接收数据处理有问题
*/
int UartReceiveDataProc(enum UartType StaticType,KFIFO_ID pData,WORD* pUserd)
{
    int ret = 0;
    
    switch (StaticType)
    {
        case COMMON_TYPE:
            /* code */
            break;
        
        case DTU_TYPE:
            /* Socket 全链路：网页走 TCP，串口 DTU 数据丢弃以免双通道抢解析 */
            if (WebLink_IsSocketMode()) {
                uint8_t dump[256];
                while (kfifo_len(pData) > 0U) {
                    unsigned int n = kfifo_len(pData);
                    if (n > sizeof(dump)) {
                        n = (unsigned int)sizeof(dump);
                    }
                    (void)kfifo_get(pData, dump, n);
                }
                return SUCCESS;
            }
			ret  = Proc_DTU_RecData(pData,pUserd,DTU_TYPE);
            break;
        case DC_MOTOR_TYPE:
            /* [修改] Modbus读响应最短7字节，未收齐时不解析 */
            if(kfifo_len(pData) >= 7)
            {
                ret  = MotorRecProc(pData,pUserd);
            }
            break;
		case ZIGBEE_TYPE:
			ret  = Proc_Zigbee_RecData(pData,pUserd);
			break;
		case ENCODER_TYPE:
			ret  =	EncoderRecDataProc(pData);		
			break; 
		case SENSOR_TYPE:
			/* [修改] 库仑计7字节/转子组合读11字节；≥8字节时先SensorPrepareRxFifo剥TX回显 */
			if(kfifo_len(pData) >= 8)
			{
				SensorPrepareRxFifo(pData);
			}
			if(kfifo_len(pData) >= 7)
			{
				ret = SensorRecDataProc(pData);
			}
			break;
        default:
            break;
    }
    return ret;

}

