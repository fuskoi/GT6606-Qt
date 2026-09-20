#ifndef __ZIGBEE_DATAPROC_H__
#define __ZIGBEE_DATAPROC_H__

#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>

#include "pthread.h"
#include "Common.h"

#define FALSE 0
#define TRUE 1
#define ZIGBEE_HEAD_FLAG 0x78563412 // 小端模式->12 34 56 78
#define ZIGBEE_TAIL_FLAG 0x7856     // 小端模式-> 56 78
#define ZIGBEE_CLIENT_ADDR 1
#define MAX_WATERLEVEL_COUNT  12// 最大12个水位值
#define CAR_RPM   3000
#define FISH_RPM   2000
#define SPEED_DOWN_DENTENCE   50    //减速距离


typedef uint32_t DWORD;
#pragma pack(push, 1)
typedef struct
{
    DWORD dwHeadFlag; // 报头
    WORD wLength;     // 报文总长度
    BYTE nZigbeeAddr; // zigbee地址

} zigbee_head_t, *pzigbee_head_t;

typedef struct
{
    WORD wSeqNum;      // 报文序号
    BYTE nNeedRespond; // 是否需要回复的标记

} zigbee_respond_info_t, *pzigbee_respond_info_t;

typedef struct
{
    WORD wAddValue; // 累加校验 - 数据部分(序号+回复标记+数据)
    WORD wTailFlag; // 报尾

} zigbee_tail_t, *pzigbee_tail_t;

typedef struct
{
    BYTE portID; // 第几个串口
    WORD commandLength;

} CommandFromPort_head_t, *pCommandFromPort_head_t;

#define UPDATELOAD_SIZE 512
typedef struct UpdateSoftPacket
{
    char name[32];
    BYTE filetype;
    DWORD dwFileSize;
    DWORD dwCurPos;
    WORD wDataLength;
    WORD addValue;
    BYTE dataBuffer[UPDATELOAD_SIZE];
} UpdateSoftPacket_t, *pUpdateSoftPacket_t;
#define SUBUPDATELOAD_SIZE 128
typedef struct SubUpdateSoftPacket
{
    char name[32];
    BYTE filetype;
    DWORD dwFileSize;
    DWORD dwCurPos;
    WORD wDataLength;
    WORD addValue;
    BYTE dataBuffer[SUBUPDATELOAD_SIZE];
} SubUpdateSoftPacket_t, *pSubUpdateSoftPacket_t;
typedef struct UpdateSoftTask
{
    bool bRunFlag;
    char name[32];
    DWORD dwRecvSize;
    DWORD dwCurPos;
    FILE *pNew1;
    FILE *pNew2;
} UpdateSoftTask_t, *pUpdateSoftTask_t;


typedef struct _CommandProcParam
{   
    BYTE *pData;//数据
    uint16_t u16datalen;//数据长度
    int Devicetype;//设备类型
} CommandProcParam, *pCommandProcParam_t;
//触摸屏回复数据指令格式
//地址   功能码   数据长度   数据1高字节   数据1低字节   数据2高字节   数据2低字节   数据3高字节   数据3低字节  CRC校验 低   CRC校验高
//10 03 06 12 34 56 78 9a bc A9 13

typedef struct _TouchScreenReportBody
{
    uint16_t u16StartPosData;
    uint16_t u16WaterLevelData;
    uint16_t u16SpeedData;
    uint16_t u16WaterFlow;//水流量
    uint16_t u16CarPos;//车辆位置
    uint16_t u16FishPos;//鱼位置
}TouchScreenReportBody;

typedef struct _TouchScreenReportData
{
    uint8_t u8Addr;
    uint8_t u8FunctionCode;
    uint8_t u8DataLen;
    TouchScreenReportBody stDataBody;
    uint16_t u16CRC;

}TouchScreenReportData;
extern TouchScreenReportBody g_stTouchScreenReportData;
//触摸屏回复指令格式
typedef struct _TouchScreenReportCmd
{
    uint8_t u8Addr;
    uint8_t u8FunctionCode;
    uint16_t u16RegAddr;
    uint16_t u16RegCount;
    uint16_t u16CRC;

}TouchScreenReportCmd;

typedef enum TOUCH_BUTTON_ADDR  //按键地址
{
    TOUCH_FORWARD = 0x03e7,     //前进
    TOUCH_BACKWARD,             //后退
    TOUCH_CAR_STOP,             //停止
    TOUCH_UP,                   //上升
    TOUCH_DOWN,                 //下降
    TOUCH_FISH_STOP,            //停止
    TOUCH_ADDMEARESE,           //自动加测
    TOUCH_RESTART               //重启
}TOUCH_BUTTON_ADDR;



#pragma pack(pop)

extern pthread_mutex_t g_upMutex;
extern pthread_mutex_t g_zigbeeRespondQueueMutex;
extern int g_zigbeePacketCount;
extern UpdateSoftTask_t g_updateSoftTask;
extern  uint32_t g_u32CommansSeri;
extern uint8_t g_waterLevelCount; 
extern uint8_t g_u8HoriCtrlState ;
extern uint8_t g_u8CatState ;
extern uint8_t g_u8FishState ;
extern uint16_t g_u16WaterLevelArray[12] ;
extern KFIFO_ID g_MultiFrameFifo;   // 多帧数据接收缓冲区

/**
 * @brief 	 【简介】延时函数
 * @param 	 ms [in], 延时时间 单位ms
 */

int ZigbeeDataCheck(uint8_t *pData, uint16_t wLen);//no use

int Proc_DTU_RecData(KFIFO_ID pData ,  WORD *pUsed,int type);
int CommandCodeProc(BYTE *pData, DWORD dwLength,int type);
int SendResultFrame(uint32_t ExeCommand,uint32_t commandseri,uint8_t resaut,int type);
/* OTA 专用：上报固件升级结果（命令字 0xFFFFFFE4）
 * u32CommandSeri=指令序号，u16PktSeri=升级序号(包序号) */
int SendOtaUpgradeResultFrame(uint8_t u8UpgradeType, uint8_t u8Status,
                              uint32_t u32CommandSeri, uint16_t u16PktSeri, int type);
void SendZigbeeData(uint8_t u8FunctionCode,uint8_t * pData,uint16_t  datalen,int type);
uint16_t SendUpLinkToServer(uint8_t u8FunctionCode, uint8_t u8CommandCode,
                            uint8_t *pData, uint16_t datalen, int type);
void SendStatusFrame(void * arg);
void sendSpeedAndSectionSeir(void * arg);
void SendReportSensorError(uint32_t u32ElementType, int type);
void * emergency_stop_handler(void * arg) ;
int  Fish_up(void);
void Fish_up_proc(void * arg);
int  Car_Goback_Home();
void Car_Goback_HomeProc(void * arg);
void SendRotatorResultAuto(void * arg);
void SendRotatorResultAuto_depth(void *arg);
void SendRotatorResultManual(void *arg);
void  ReadWaterLevel(void * arg);
void ReadWaterLevelper5min(void * arg);
uint32_t ReadWaterLevelFresh(void);
int MultiFrameProc(uint8_t StartFlag, BYTE *pData, DWORD dwLength,int type);
int SendMultiframe(uint8_t u8FunctionCode,uint8_t * pData,uint16_t  datalen,int type);
int SendSingleFrame(uint8_t u8FunctionCode,uint8_t * pData,uint16_t  datalen,int type,int StopFlag);
int Proc_Zigbee_RecData(KFIFO_ID pData ,  WORD *pUsed);
int SendDataToScreen(uint8_t * data,uint16_t datalen);
int ReportScreenData(TouchScreenReportBody stDataBody);
void TouchScreenPushReportData(void);
int ReportScreenCmd(uint8_t *data,uint8_t datalen);
int ProcTouchCommand(uint16_t ButtonAddr);
void  * TouchCmdProcThread(void * pParam);
#endif
