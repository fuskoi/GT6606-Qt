#ifndef __ZIGEBEE_COM_H__
#define __ZIGEBEE_COM_H__

#include "uart.h"

#define FALSE 0
#define TRUE 1
#define ZIGBEE_HEAD_FLAG 0x78563412 // 小端模式->12 34 56 78
#define ZIGBEE_TAIL_FLAG 0x7856     // 小端模式-> 56 78
#define ZIGBEE_CLIENT_ADDR 1
#define TIME_LENGTH 30
#define MAX_VELO_POS 32
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

typedef struct
{
    DWORD dwShuiWeiStart;
    DWORD dwShuiWeiStop;
    BYTE shuiWeiMode;
    BYTE lineCount;

} UpLoadVeloHead_t;

typedef struct
{
    WORD wVelocity;
    BYTE nSNR;
    WORD wAngle;
    char time[TIME_LENGTH];
    DWORD dwWaterLevel;

} VelocityInfoWithTimeWL_t;

typedef struct
{
    bool missionRuning;         // 小车任务正在进行的标志
    uint32_t distanceArray[32]; // 保存目标位置的数组
    int pluseArray[32];    // 保存到目标位置需要的脉冲数
    int posCount;               // 目标位置的数量
    int curPosNum;              // 当前在第几个点位
    int measCount;              // 测量点需要测量的次数
    int curNumOfMeas;           // 当前点位的测量次数
    // car_mission_step_t eCurStep; // 整个任务中的当前步骤
    UpLoadVeloHead_t veloHead; // 测流完成时 上传的报文的数据头部
    VelocityInfoWithTimeWL_t upLoadVelocity[MAX_VELO_POS];

} velo_task_info;


#pragma pack(pop)

extern pthread_mutex_t g_upMutex;
extern pthread_mutex_t g_zigbeeRespondQueueMutex;
extern list_queue_t g_zigbeeRespondTaskQueue;
extern common_item_t g_curSendItem;
extern int g_zigbeePacketCount;
extern UpdateSoftTask_t g_updateSoftTask;
/**
 * @brief 	 【简介】延时函数
 * @param 	 ms [in], 延时时间 单位ms
 */
void localSleep(int ms);
/**
 * @brief 	 【简介】检查从zigbee中接收到的数据是否完整且符合协议
 * @param 	 pData [in],  被检查的数据
 * @param 	 wLen [in],  被检查的数据的长度
 * @return 	 【解释返回值】 返回值-1：报头或报尾有误 1：报文不完整 -2：报文长度大于600字节
 */
int ZigbeeDataCheck(uint8_t *pData, uint16_t wLen);
/**
 * @brief 	 【简介】解析从zigbee接收到的数据
 * @param 	 pData [in],  数据的指针
 * @param 	 wLen [in],  数据长度
 * @param 	 pUsed [out],  在这个函数中处理过的字节数
 * @return 	 【解释返回值】1：数据不完整 0：数据处理完成 -1：数据错误(不符合协议)
 */
int ProcZigbeeData(uint8_t *pData, uint16_t wLen, WORD *pUsed);
/**
 * @brief 	 【简介】把写入zigbee的数据添加上报文序号和是否需要响应的标志位 并根据这条报文是否需要响应选择不同的发送报文的功能
 * @param 	 pData [in], 要写入zigbee的数据
 * @param 	 nLength [in], 数据的长度
 * @param 	 nResNum [in], 这条报文的序号
 * @param 	 nResponeFlag [in], 这条报文是否需要响应的标志位
 * @return 	 【解释返回值】成功返回0
 */
int SendDataPacketToZigbee(BYTE *pData, int nLength, int nResNum, uint8_t nResponeFlag);
/**
 * @brief 	 【简介】用互斥锁保护想要发送的报文数据
 * @param 	 pData [in], 报文数据部分
 * @param 	 nLength [in], 报文数据长度
 */
void SendZigbeeDataWithMutex(BYTE *pData, int nLength);
/**
 * @brief 	 【简介】组成完整报文并将报文写入串口
 * @param 	 pData [in], 报文数据部分
 * @param 	 nLength [in], 报文数据长度
 */
void SendZigbeeData(BYTE *pData, int nLength);
/**
 * @brief 	 【简介】发送需要收到响应的报文，等待收到响应后再发送下一条报文，否则重复发送上一条报文
 * @param 	 arg [in/out], 无
 * @return 	 【解释返回值】无
 */
void *SendZigbeeDataAndWaitRespond(void *arg);
/**
 * @brief 	 【简介】将需要响应的报文加入到报文队列中
 * @param 	 pData [in], 报文的数据部分
 * @param 	 wLength [in], 数据长度
 * @return 	 【解释返回值】成返回true 失败返回false
 */
bool SendNeedRespondZigbeeData(BYTE *pData, int wLength);
/**
 * @brief 	 【简介】将收到的响应报文中包含的响应包序号赋值给g_recvSeqNum
 * @param 	 nSeqNum [in], 被响应的报文序号
 */
void GetZigbeeRespondSeqNum(int nSeqNum);
/**
 * @brief 	 【简介】解析接收到的报文的数据部分，根据不同数据选择不同功能
 * @param 	 pData [in], 接收到的报文数据部分
 * @param 	 dwLength [in], 数据长度
 * @return 	 【解释返回值】
 */
int HandleRtuDataRequest(BYTE *pData, DWORD dwLength);
/**
 * @brief 	 【简介】初始化报文队列，线程相关的资源
 * @param 	 nQueLen [in], 队列最大长度
 */
void InitNeedRespondZigbeeDataTaskResources(int nQueLen);
/**
 * @brief 	 【简介】
 * @param 	 pData [in/out], 参数意义
 */
void ProcPortCommand(BYTE *pData);
/**
 * @brief 	 【简介】
 * @param 	 pData [in/out], 参数意义
 * @return 	 【解释返回值】
 */
int SubProcessUpdateSoft(BYTE *pData);
/**
 * @brief 	 【简介】
 * @param 	 pData [in/out], 参数意义
 * @return 	 【解释返回值】
 */
int ProcLinuxCommand(BYTE *pData);
void DelFile(char *pDir);
int ExecShell(char *pShell, char *pResult, short *pResultLength);
void StartCeliu(uint8_t *pData);
void CalcPulseArray(void);
/**
 * @brief 	 【简介】
 * @param 	 pData [in/out], 参数意义
 * @param 	 dwLength [in/out], 参数意义
 * @return 	 【解释返回值】
 */
int ProcLocalControl(BYTE *pData, DWORD dwLength);


#endif

