#ifndef _WEBSERIVER_H
#define _WEBSERIVER_H

#include "Common.h"

#define HEAD1  0X7E
#define HEAD2  0X7E
#define STX 0X02  //传输正文起始
#define SYN 0x16  //多包传输正文起始
#define ETB 0x17  //报文结束，后续有报文
#define ETX 0x03  //报文结束，后续无报文
#define ACK 0x06  //肯定确认，继续发送
#define NAK 0x15  //否定应答，反馈重发
#define ENQ 0X05  //询问
#define EOT 0X04  //传输结束，退出
#define ESC 0X1B  //传输结束，终端保持在线

#define MAX_LINE_NUM   100  //最大测线数量
#define MAX_DEPTH_NUM   4	//最大深度数量
#define MAX_MODE_NUM   20   //最大测量模式数量
#define MAX_DETAIL_NUM  128 //最大详细结果数量
#define MAX_FILE_LEN   4096  //最大配置文件长度
#define MAX_SUB_DEEP_NUM  128// 最大子深度个数
#define MAX_ELEMENT_NUM  6   //最大元素个数

#define DOWN_EXPAND_CODE 0xf1

// ---- 运动控制状态机 ----
typedef enum {
    MOTION_IDLE = 0,
    // Fish_up（铅鱼上升碰上限位）
    MOTION_FISH_UP_POWERON,
    MOTION_FISH_UP_SLEEP,
    MOTION_FISH_UP_RAISE,
    MOTION_FISH_UP_WAIT_LIMIT,
    MOTION_FISH_UP_STOP,
    // Car_Goback_Home（小车后退碰原点限位）
    MOTION_CAR_HOME_POWERON,
    MOTION_CAR_HOME_SLEEP,
    MOTION_CAR_HOME_BACK,
    MOTION_CAR_HOME_WAIT_LIMIT,
    MOTION_CAR_HOME_STOP,
    // CarMovetoAim（小车前往目标位置）
    MOTION_CAR_MOVE_POWERON,
    MOTION_CAR_MOVE_SLEEP,
    MOTION_CAR_MOVE_RUN,
    // FishMovetoAim（铅鱼前往目标位置）
    MOTION_FISH_MOVE_POWERON,
    MOTION_FISH_MOVE_SLEEP,
    MOTION_FISH_MOVE_RUN,
    // 完成/错误
    MOTION_DONE,
    MOTION_ERROR
} MotionState;

typedef struct {
    MotionState state;
    int timerId;                /* Tick定时器ID */
    int timeoutTimerId;         /* 超时定时器ID */
    float targetPos;            /* 目标位置（位置型运动） */
    int direction;              /* 运动方向（AMPS_FWD=1 / AMPS_BWD=-1） */
    int debounceCount;          /* 限位消抖计数 */
    int sleepStep;              /* 延时等待步数 */
    int motorAddr;              /* 电机地址 */
    int powerRelay;             /* 驱动器上电继电器 */
    int brakeAddr;              /* 刹车继电器地址 */
    int devType;                /* 设备类型 */
} MotionCtx;

extern MotionState g_motionState;
extern MotionCtx g_motionCtx;

// ---- 加测控制状态机状态定义 ----
typedef enum {
    FSM_IDLE = 0,
    DOOR_OPEN_WAIT,               // 非阻塞等开门到位（超时/重试）
    FSM_START,                    // 开始：驱动器上电
    FISH_UP_TO_TOP_INIT,          // 初始收起铅鱼到顶
    CAR_BACK_TO_HOME_INIT,        // 初始小车回原点
    CAR_MOVE_TO_LINE,             // 小车前往第N条垂线
    FISH_DOWN_TO_DEPTH,           // 铅鱼下降到当前深度
    MEASURE_WAIT_DURATION,        // 等待测量时长(全程使用周期性转子值)
    MEASURE_POST_READ,            // 读取测量后值 + 计算上报 + 插入链表
    MEASURE_NCS,                  // 非接触式测流(NCS/雷达)等待
    NEXT_DEPTH,                   // 同一垂线下一个深度
    FISH_UP_AFTER_LINE,           // 该垂线全部深度完成，上升到水面50cm
    NEXT_LINE,                    // 前往下一条垂线
    ALL_LINE_DONE,                // 所有垂线完成
    FISH_UP_TO_TOP_FINAL,         // 最终收起铅鱼
    CAR_BACK_TO_HOME_FINAL,       // 最终小车回仓
    FISH_DOWN_TO_PLATFORM,        // 测量完成降落到平台
    SEND_RESULT_FINAL,            // 遍历链表发送最终结果
    FSM_PAUSED,                   // 暂停状态
    FSM_COMPLETE,                 // 完成
    FSM_ERROR                     // 错误
} AddMeasureState;


// ---------------------------------
#define UP_EXPAND_CODE  0Xf2		//上行扩展功能码
#define UP_SIGNATURE 	0x63		//上报特征码，固定99
#define DOWN_SIGNATURE 	0x1f		//下发特征码，固定31
#define uint32_t unsigned int
#define uint8_t unsigned char
#define uint16_t unsigned short

#define MAX_HORI_SPEED  150 /* 水平线速度上限 cm/s (=1.5 m/s) */
#define MAX_VERO_SPEED  50  /* 垂直线速度上限 cm/s (=0.5 m/s) */
#define MANU_SPEED_LEVEL_MIN  1
#define MANU_SPEED_LEVEL_MAX  7
#define REDUCTION_RATIO  6000
#define MAX_MOTOR_RPM   3000
#define DEEP_PARA_NUM  15
#define MAX_WAITTIME   ( 1* 60 * 1000) //最长超时时间  1min
#define IS_RUNNING_STATE 2
#define IS_STOP_STATE    1
#define IS_ERROR_STATE   0
#define MAX_SOFTWARE_SIZE  (2*1024*1024)

#pragma pack(push, 1)

/**上行报文帧头结构框架*/
typedef struct _BCD_TIME
{ 
	uint8_t u8Year;				//年
	uint8_t u8Month;			//月
	uint8_t u8Day;				//日
	uint8_t u8Hour;				//时
	uint8_t u8Minutes;			//分
	uint8_t u8Second;			//秒  
}BCD_TIME;
/**上行报文帧头结构框架*/
typedef struct _UPFrameHead
{
	uint16_t u16SOH;			//帧起始符
	uint8_t u8CentreStaID;		//中心站地址
	uint8_t aRTUID[5];			//遥测站地址
	uint16_t u16Password;		//密码
	uint8_t u8FunctionCode;		//功能码
	uint16_t u16FlagAndLen;		//报文上下行标识
	uint8_t  u8DataStart;		//报文起始符	
	uint16_t u16FrameSeri;		//帧序号
	BCD_TIME  BCD_time;			//时间
	uint8_t u8Signature;			//特征码
}UPFrameHead;   //上行报文帧头结构框架


/**下行报文帧头结构框架*/
typedef struct _DOWNFrameHead
{
	uint16_t u16SOH;			//帧起始符
	
	uint8_t aRTUID[5];			//遥测站地址
	uint8_t u8CentreStaID;		//中心站地址
	uint16_t u16Password;		//密码
	uint8_t u8FunctionCode;		//功能码   F3	设置铅鱼参数
	uint16_t u16FlagAndLen;		//报文上下行标识
	uint8_t  u8DataStart;		//报文起始符
	uint16_t u16FrameSeri;		//帧序号
	BCD_TIME  BCD_time;			//时间
	uint8_t u8Signature;			//特征码

}DOWNFrameHead;//下行报文帧头结构框架

typedef struct {
	uint8_t u8EOF;
	uint16_t u16CRC;

}DOWNFrametail;

typedef struct {
    uint16_t Number : 12;  // 高 12 位（占用前 1.5 字节）
    uint16_t Serier : 12;  // 低 12 位（占用后 1.5 字节）
} PackedInfo;
//自定协议服务器发送数据统一报文头，包含命令序号、命令长度、命令字、站码
typedef struct _SeriverCommandHead
{
	uint32_t u32CommandSeri;
	uint32_t u32CommandLen;
	uint32_t  u32Command; //命令字
    uint8_t   au8StationID[12];  //站码
}SeriverCommandHead;
typedef struct _SeriverCommandTail
{ 
	uint32_t u32CommandTail;
}SeriverCommandTail;

#define SeriverCommandtail_value 0xffffffff    //报文尾


/*上报执行结果指令*/
typedef struct _ReportResult
{
	uint32_t u32CommandSeri;
	uint32_t u32CommandLen;
	uint32_t  u32Command; //命令字
	uint32_t u32ExeCommmand;
	uint32_t u32EceCmandSeri;
	uint8_t  au8StationID[12];  //站码
	uint8_t  u8Result;
	uint16_t  u16ResultDetail;	//执行结果详情(非0就报错误码)
	//uint32_t u32CommandTail;
	
	
}ReportResult;

/* 上报固件升级结果：通用头(序号+长度) + 命令字4字节0xFFFFFFE4 + 3.1.28内容 */
#define OTA_UPGRADE_STATUS_BUSY   0U  /* 升级中 */
#define OTA_UPGRADE_STATUS_ERROR  1U  /* 升级出错 */
typedef struct _ReportOtaUpgrade
{
	uint32_t u32CommandSeri;   /* 指令序号 */
	uint32_t u32CommandLen;    /* 指令长度（含本结构+指令尾） */
	uint32_t u32Command;       /* REPORT_OTA_UPGRADE = 0xFFFFFFE4 */
	uint8_t  au8StationID[12]; /* 站码 */
	uint8_t  u8UpgradeType;    /* 升级类型（子设备） */
	uint8_t  u8UpgradeStatus;  /* 0 升级中，1 升级出错 */
	uint16_t u16UpgradeSeri;   /* 升级序号（2 字节） */
} ReportOtaUpgrade;
/*上报基本状态*/
typedef struct _ReportState
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];  //站码
	uint8_t u8Mode;//模式
	uint16_t u16RTUVol;//RTU电压
	uint16_t u16CarVol;//升降车电压
	uint16_t u16FishVol;//铅鱼电压
	int32_t i32CarHoriDist;//运行车水平位置
	uint16_t u16CarVeriHight;//运行车向下高度
	uint16_t u16FishToCarhight;//铅鱼距离运行车高度
	uint16_t u16FishInwaterDeep;//铅鱼入水深度
	uint8_t  u8HoriCtrlState;//水平控制状态
	uint8_t  u8CarState;//运行车状态
	uint8_t u8FishState;//铅鱼状态
	uint16_t u16HoriCarSpeed;//水平小车速度
	uint16_t u16VeriUpSpeed;//垂直提升速度
	uint32_t u32CurWaterLevel;//当前水位
	uint16_t u16Current;//当前电流
	uint8_t u8Soc;//电量百分比
	uint8_t u8ChargeState;//充电状态
	
	
}ReportState;
extern ReportState g_stReportState;

typedef struct _DetailResult
{
	uint8_t u8Hour;
	uint8_t u8Minute;
	uint8_t u8Second;
	uint32_t u32Result;

}DetailResult;


/*上报手动测沙*/
typedef struct _ReportManuMuds
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];  //站码
	int32_t i32StartDistance;//起点距
	uint8_t u8Deepth;//深度
	uint32_t u32AvagMuds;//平均含沙量
	uint8_t u8detailResultNum;//详细数据数量
	DetailResult atDtailResult[MAX_DETAIL_NUM];
	
}ReportManuMuds;


typedef struct _ReportManuRotatorBody
{
	int32_t i32StartDistance;//起点距
	uint8_t  u8Deepth;//深度
	uint16_t u16Speed;//流速
	uint8_t u8Duration;//时长
	uint16_t u16SignalNum;//信号数
	
}ReportManuRotatorBody;

//上报手动转子流速仪测流结果
typedef struct _ReportManuRotatorSpeed
{
	SeriverCommandHead stCommandHead;

	ReportManuRotatorBody stResultBody;
	
}ReportManuRotatorSpeed;

//上报手动非接触流速测流
typedef struct _ReportManuNoTouchSpeed
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];  //站码
	int32_t i32StartDistance;//起点距
	uint16_t u16Speed;//流速
	uint8_t u8SignalForce;//信号强度
	uint16_t u16Angle;//角度
}ReportManuNoTouchSpeed;

//上报手动单波束流速
typedef struct _ReportManuADCPSpeed
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];  //站码
	int32_t i32StartDistance;//起点距
	uint16_t u16Speed;//流速
	uint8_t  u8Deepth;//深度
	
}ReportManuADCPSpeed;

/* 上报手动模式速度等级(1-7)，命令字 REPORT_MANU_MODE_SPEED */
typedef struct _ReportManuModeSpeed
{
	SeriverCommandHead stCommandHead;
	uint8_t u8SpeedLevel;
} ReportManuModeSpeed;
//上报走航ADCP流速
typedef struct _ReportveADCPSpeed
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];  //站码
	int32_t i32StartDistance;//起点距
	uint16_t u16Speed;//流速
}ReportveADCPSpeed;

//上报运行车本地配置参数
typedef struct _ReportCarconig
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];  //站码
	uint16_t u16FileLen;//文件长度
	uint8_t *fileBuffer;//文件内容	
}ReportCarconig;
typedef struct {
    SeriverCommandHead stCommandHead;  // 命令头
    uint8_t u8SubDevice;               // 子设备类型
    uint16_t u16FileLen;               // 配置文件长度
    uint8_t u8pFileData[];             // 配置文件内容（柔性数组）
} ReportSubDeviceConfig;

/* 上报运行日志文件 REPORT_LOG=0xFFFFFFEC */
#define LOG_REPORT_CHUNK       2048
#define LOG_LEVEL_ALL          1U
#define LOG_LEVEL_WARNING      2U
#define LOG_LEVEL_ERROR_ONLY   3U
#define LOG_LEVEL_DEBUG_ONLY   4U
#define LOG_DEVICE_CAR_HOST    3U   /* 行车主机 */
#define LOG_ACK_TIMEOUT_MS     35000
#define LOG_ACK_MAX_RETRY      3
/* ACK 成功后、发下一包前间隔，减轻平台积压；0=不延时 */
#define LOG_UPLOAD_PKT_GAP_MS  0

/* 加测结束上报最终流量(0xFFFFFFED)；关门不等平台ACK，重发与关门并行 */
#define FINAL_RESULT_ACK_TIMEOUT_MS  5000
#define FINAL_RESULT_ACK_MAX_RETRY   3

typedef struct {
    SeriverCommandHead stCommandHead;
    uint8_t  u8DeviceType;             /* 设备类型 */
    uint8_t  u8LogLevel;               /* 日志级别 */
    uint32_t u32FileLen;               /* 日志文件总长度 */
    uint16_t u16TotalPkts;             /* 总包数，从1 */
    uint16_t u16PktSeri;               /* 本包序号，从1 */
    uint8_t  u8pFileData[];
} ReportLog;

//上报铅鱼本地配置文件
typedef struct _ReportFishconig
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];  //站码
	uint16_t u16FileLen;//文件长度
	uint8_t fileBuffer[MAX_FILE_LEN];//文件内容	
}ReportFishconig;

//上报测流和断面序号
typedef struct _ReportMesureSeri
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];	//站码
	uint16_t u16SectionSeri;
	uint16_t u16SpeedSeri;
}ReportMesureSeri;
/***********************/
//自动非接触测流测线数据
typedef struct _NotouchSpeedData
{
	int32_t i32StartDistance;//起点距
	uint32_t u32WaterLevel;//水位
	uint8_t u8Year;//年
	uint8_t u8Month;//月
	uint8_t u8Day;//日
	uint8_t u8Hour;//时
	uint8_t u8Minutes;//分
	uint8_t u8Second;//秒
	uint16_t u16Speed;//流速
	uint8_t u8SingalForce;//信号强度
	uint16_t u16Angle;//角度
	uint8_t u8CV;//系数
	
}NotouchSpeedData;
//上报自动非接触测流数据
typedef struct _ReportAutoNoTouchSpeed
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];	//站码
	uint16_t u16SectionSeri;//断面序号
	uint16_t u16SpeedSeri;//测流序号
	uint8_t u8LineNum;//测线数量
	NotouchSpeedData stSpeedData[MAX_LINE_NUM];

}ReportAutoNoTouchSpeed;
/***********************/


/***********************/
//测线含沙量信息
typedef struct _AutoMudsData
{
	int32_t i32StartDistance;//起点距
	uint32_t U32WaterLevel;//水位
	uint8_t u8Year;//年
	uint8_t u8Month;//月
	uint8_t u8Day;//日
	uint8_t u8Hour;//时
	uint8_t u8Minutes;//分
	uint8_t u8Second;//秒
	uint32_t u32AvgMuds;//平均含沙量
	uint8_t u8SubDeepNum;//子深度数量
	uint32_t SubDeepMuds[MAX_SUB_DEEP_NUM];//子深度测量数据
		
}AutoMudsData;
//上报自动测沙数据

typedef struct _ReportAutoMuds
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];	//站码
    AutoMudsData stMudsData[MAX_LINE_NUM];//测线测量数据
}ReportAutoMuds;
/***********************/


/***********************/
//ADCP自动测流相关数据
typedef struct _AutoADCPData
{
	SeriverCommandHead stCommandHead;
	int32_t i32StartDistance;//起点距
	uint32_t U32WaterLevel;//水位
	uint8_t u8Year;//年
	uint8_t u8Month;//月
	uint8_t u8Day;//日
	uint8_t u8Hour;//时
	uint8_t u8Minutes;//分
	uint8_t u8Second;//秒
	uint16_t u16Speed;//流速
	uint8_t u8SignalFroce;//信号强度
	uint8_t u8CV;//系数
}AutoADCPData;
//上报单波束ADCP自动流量
typedef struct _ReportAtuoADCP
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];	//站码
	uint16_t u16SectionSeri;//断面序号
	uint16_t u16SpeedSeri;//测流序号
	uint8_t u8LineNum;//测线数量
	AutoADCPData stAutoADCPData[MAX_LINE_NUM];
}ReportAtuoADCP;
/***********************/

/***********************/
//上报转子自动流量
typedef struct _AutoRotatorData
{
	int32_t i32StartDistance;//起点距
	uint32_t U32WaterLevel;//水位
	uint8_t u8Year;//年
	uint8_t u8Month;//月
	uint8_t u8Day;//日
	uint8_t u8Hour;//时
	uint8_t u8Minutes;//分
	uint8_t u8Second;//秒
	uint16_t u16Speed;//流速
	uint8_t u8Duration;//历时
	uint16_t u16SignalNum;//信号数
}AutoRotatorData;

typedef struct _ReportAutoRotatorSpeed
{
	SeriverCommandHead stCommandHead;
	uint16_t u16SectionSeri;//断面序号
	uint16_t u16SpeedSeri;//测流序号
	uint8_t u8LineNum;//测线数量
	AutoRotatorData stRotatorData[MAX_LINE_NUM];
}ReportAutoRotatorSpeed;
/***********************/

/***********************/
//上报转子自动流量-带层数
typedef struct _AutoRotatorData_depth
{
	int32_t i32StartDistance;//起点距
	uint32_t U32WaterLevel;//水位
	uint8_t u8Year;//年
	uint8_t u8Month;//月
	uint8_t u8Day;//日
	uint8_t u8Hour;//时
	uint8_t u8Minutes;//分
	uint8_t u8Second;//秒
	uint16_t u16Speed;//流速
	uint8_t u8Duration;//历时
	uint16_t u16SignalNum;//信号数
	uint8_t u8DepthPersen;//水深位置百分比
}AutoRotatorData_depth;	//	每个深度的数据

typedef struct _perLinePara
{
	AutoRotatorData_depth astRrelinePara[MAX_DEPTH_NUM];
}perLinePara;		//	每个垂线包含的多个深度数据


typedef struct _ReportAutoRotatorSpeed_depth
{
	SeriverCommandHead stCommandHead;
	uint16_t u16SectionSeri;//断面序号
	uint16_t u16SpeedSeri;//测流序号
	uint8_t u8LineNum;//测线数量
	perLinePara stlinesRotatorData[MAX_LINE_NUM];		//	所有垂线包含的多个深度的数据
}ReportAutoRotatorSpeed_depth;
/***********************/






/***********************/
typedef struct _CurrMudsData
{
	uint8_t u8Hour;
	uint8_t u8Minteus;
	uint8_t u8Seconds;
	uint32_t u32Muds;
}CurrMudsData;
typedef struct _ReportCurrMuds
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];	//站码
	uint16_t CurrDataNum;
	CurrMudsData stMudsData[MAX_LINE_NUM];
	
}ReportCurrMuds;
/***********************/

//上报传感器出错
typedef struct _ReportSensorError
{
	SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
	//uint8_t  au8StationID[12];	//站码
	uint32_t u32factorType;//要素类型
	uint16_t u16ErrorCode;//错误代码
}ReportSensorError;







//自动测量参数测量模式头部信息
typedef struct _MeasureModeHead
{	
	uint32_t u32StartWaterlevel;//起点水位
	uint32_t u32EndWaterlevel;//结束水位
	uint8_t u8LevelChange;//变幅测
	uint16_t au16AutoMeaTime[24]; //自动测时间
	uint16_t u16LineNum;//测线数量

}MeasureModeHead;
/***********************/
/*每个测线测量要素结构体*/
typedef struct _MeasureLine
{
	int32_t i32StartDistance;//起点距
	uint32_t u32MeaElement; //测量要素
	uint8_t  u8NoTouchSpeedCV;//非接触式测流系数
	uint32_t u32RotatorDeepth;//转子测流深度
	uint16_t au16ADCPDeepth[4] ;//单波束ADCP测流深度
	uint32_t u32MudDeepth;//泥沙深度
	uint8_t u8WaterTempraDeepth;//水温深度
	uint32_t au32Reserved[5];//预留参数1-5
}MeasureLine;
/*每个测量模式定义结构体*/
typedef struct  _MeasureMode
{
	MeasureModeHead stMeaModeHead;//测量模式头部信心
	MeasureLine stLine[MAX_LINE_NUM];// 每个测线的要素
	
	
}MeasureMode;

//自动测量参数帧头部信息
typedef struct _AutoMeaHead
{
	SeriverCommandHead stCommandHead;
	uint16_t  u16ParaSeri;//自动运行参数序列号
	uint8_t  u8ModeNum;//模式数量

}AutoMeaHead;
/*自动测量参数定义结构体*/	
typedef struct _AutoMeasure
{
	AutoMeaHead  stAutoMeaHead;//自动测量参数报头信息
	MeasureMode  stMode[MAX_MODE_NUM];//模式信息
	   
}AutoMeasure;
/***********************/
typedef struct _ResultPara
{
	int32_t i32StartDistance;//起点距
	uint32_t u32MeaElement; //测量要素
	uint32_t u32RiverBedElevation;//河底高程
	uint16_t u16LineSeri;
	uint8_t u8Deepth;
	uint8_t u8Year;//年
	uint8_t u8Month;//月
	uint8_t u8Day;//日
	uint8_t u8Hour;//时
	uint8_t u8Minutes;//分
	uint8_t u8Second;//秒;
	uint32_t u32PrevSignalNum;//上一帧信号数
}*RESULTPARA;


/*断面参数*/
typedef struct _SectionPara
{
	uint32_t u32StartDestence;//起点距
	uint32_t u32RiverBedElevation;//河底高程
	
}SectionPara;

/*设置断面*/
typedef struct _SetSection
{
	SeriverCommandHead stCommandHead;
    
	uint16_t  u16SectionSeri;//断面序号
	uint16_t  u16SectionNum;//断面数量
	SectionPara stSection[MAX_LINE_NUM];//断面参数	
}SetSection;

/*断面参数(新)*/
typedef struct _SectionPara_new
{
	int32_t u32StartDestence;			//起点距
	int32_t i32CableLength;				//缆长
	int32_t u32RiverBedElevation;		//河底高程	
}SectionPara_new;

/*设置断面*/
typedef struct _SetSection_new
{
	SeriverCommandHead stCommandHead;
    
	uint16_t  u16SectionSeri;//断面序号
	uint16_t  u16SectionNum;//断面数量
	SectionPara_new stSection[MAX_LINE_NUM];//断面参数	
}SetSection_new;

typedef struct  _SetCableWayBody
{
	uint32_t  u32CableHight;//缆车高度
    uint32_t  u32MaxDistance;//最远距离
    uint32_t  u32PullForce;//保证拉力
	uint16_t  u16HoriWheelDm;//水平轮直径
	uint16_t  u16HoriEncoderDm;//水平编码器直径
	uint16_t  u16HoriSpeed;//水平运动速度
	uint16_t  u16VeriWheelDm;//升降轮直径
	uint16_t  u16VeriEncoderDm;//升降编码器直径
	uint16_t  u16VeriSpeed;//垂直运动速度
	uint16_t  u16HoriLowestVol;//水平运动最低电压
	uint16_t  u16VeriLowestVol;//垂直运动最低电压
	uint16_t  u16PlatToCableHight; // 平台距离主缆高度
	int16_t  i16StartPointDes;//坐标起点距离
}SetCableWayBody;


/*设置缆道参数*/
typedef struct _SetCableway
{
	SeriverCommandHead stCommandHead;
    SetCableWayBody stSetCablewayBody;
	
}SetCableway;


//加测控制
typedef struct _AddMeasureCtrl
{
	//SeriverCommandHead stCommandHead;
	//uint32_t  u32Command; //命令字
    //uint8_t   au8StationID[12];  //站码
    uint32_t u32WaterLevel;//水位
    uint8_t u8SpeedCtrl;//测流控制
}AddMeasureCtrl;



//切换控制模式
typedef struct _SwitchMode
{
    SeriverCommandHead stCommandHead;
    //uint32_t  u32Command; //命令字
    //uint8_t   au8StationID[12];  //站码
    uint8_t   u8Mode;
}SwitchMode;
//重启设备
typedef struct _Restart
{
    SeriverCommandHead stCommandHead;
    //uint32_t  u32Command; //命令字
    //uint8_t   au8StationID[12];  //站码
    uint8_t   u8Device;//设备
}Restart;
//收起铅鱼
typedef struct _FishBack
{
    SeriverCommandHead stCommandHead;
    //uint32_t  u32Command; //命令字
    //uint8_t   au8StationID[12];  //站码
}FishBack;
//召回小车
typedef struct _CarBack
{
    SeriverCommandHead stCommandHead;
    //uint32_t  u32Command; //命令字
    //uint8_t   au8StationID[12];  //站码
}CarBack;

//运行车水平运动
typedef struct _CarMove
{
    SeriverCommandHead stCommandHead;
    //uint32_t  u32Command; //命令字
    //uint8_t   au8StationID[12];  //站码
    int32_t i32StartDistence;//起点距
}CarMove;


//铅鱼垂直运动
typedef struct _FishMove
{
    SeriverCommandHead stCommandHead;
    //uint32_t  u32Command; //命令字
    //uint8_t   au8StationID[12];  //站码
    uint16_t u16FishPos;
}FishMove;

//触底
typedef struct _TouchGround
{
    SeriverCommandHead stCommandHead;
    //uint32_t  u32Command; //命令字
    //uint8_t   au8StationID[12];  //站码
}TouchGround;


typedef struct _RotatorManuMeaS
{
    SeriverCommandHead stCommandHead;
    uint32_t u32StartDistence;//起点距
	uint8_t u8Deepth;
    
}RotatorManuMeaStart,RotatorManuMeaStop;

typedef struct _SetManuSpeed
{
    SeriverCommandHead stCommandHead;
    //uint32_t  u32Command; //命令字
    //uint8_t   au8StationID[12];  //站码
    uint8_t   u8SpeedGrade;//速度级
}SetManuSpeed;

/*深度参数*/
typedef struct _DeepthPara
{
	uint32_t u32Element;//要素
	uint8_t u8Deepth;//深度
}DeepthPara;

/*要素深度*/
typedef struct _ElemDeepth
{
	DeepthPara NCS ;
	DeepthPara RotatorDeepth[4];//转子深度
	DeepthPara ADCPDeepth[4];//ADCP深度
	DeepthPara VE_ADCP;
	DeepthPara MudsDeepth[4];//泥沙深度
	DeepthPara WaterDeepth;//水温深度
}ElemDeepth;

typedef struct _FinalResutlBddy
{
	uint16_t u16Element;
    uint32_t u32WaterFlow;
	uint32_t u32AvagerSpeed;
	uint32_t u32Area;
}FinalResutlBddy;
typedef struct _FinalResutl
{
	SeriverCommandHead stCommandHead;
	FinalResutlBddy stFinalResultBdoy;

}FinalResutl;


// 设置子设备本地配置文件命令结构
typedef struct {
    SeriverCommandHead stCommandHead;  // 命令头
    uint8_t u8SubDevice;               // 子设备类型
    uint16_t u16FileLen;               // 配置文件长度
    uint8_t u8pFileData[];             // 配置文件内容（柔性数组）
} SetSubDeviceConfig;


// ---- 加测控制上下文 ----
typedef struct {
    int         currentLineIndex;          // 当前第几条垂线（0开始），暂停/继续的核心依据
    int         totalLineCount;            // 总垂线数
    uint32_t    currentLineStartDist;      // 当前垂线的起点距（用于清理链表）
    MeasureMode measureMode;               // 测量模式副本
    int         deviceType;
	uint8_t u8DepthPersent;					// 深度百分比
    uint32_t    commandSeri;
    int stopRequested;            // 停止请求

    // 消抖计数器
    int debounceCount;

    // 测量时间相关(基于周期性采集) — [2026-06-22] 改为第2转子信号起计，取消固定20s稳定等待
    uint32_t    measureArrivalCount;       // 到深后计数值(等待第2个信号)
    uint32_t    measureStartCount;         // 第2个信号后的计数值A
    uint64_t    measureStartTimeMs;        // 测量开始时间(ms)
    uint32_t    measureDurationMs;         // 测量持续时间(ms)
    int         measurePhase;              // 0=等第2信号, 1=测时中, 2=等待计数>B
    uint32_t    measureTargetCount;        // B值(时长到达时的计数值)
    int         measureDepthIdx;           // 当前深度索引

    // 超时强制上报标志
    int         measureTimeForceReport;    // 1=测量超时强制上报
    uint64_t    measureTimeoutMs;          // 1.5倍测量超时总时长(ms)
    // 运动指令防重复发送
    uint8_t     moveCmdSent;               // 1=已发送指令, 0=未发送
    uint8_t     moveRetryCount;            // 重试次数
    float       moveLastPos;               // 上次记录的位置
    uint64_t    moveCmdTimeMs;             // 上次发指令的时间
    int         moveLastDir;               // 上次发送的方
    uint16_t    moveLastSpeed;             // 上次发送的速度
} AddMeasureContext;

//下行命令字
typedef enum _DOWN_COMMAND_CODE
{
	
	SET_SECTION = 1,						//设置断面
	SET_CALBEWAY,							//设置缆道参数
	SET_AUTO_MEASURE,						//设置自动测量参数
	SWICH_CTRL_MODE,						//切换控制模式
	RESTART = 5,							//重启
	UP_FISH,								//收起铅鱼
	VERI_CTRI_BACK_NOW,						//水平立即回仓
	CURR_POS_NCS_SPEED,						//当前位置非接触式测流
	CURR_POS_MUDS_START,					//当前位置开始测沙
	CURR_POS_MUDS_END = 10,					//当前位置结束测沙
	CURR_POS_RATATOR_START,					//当前位置转子流速仪开始测流
	CURR_POS_RATATOR_END,					//当前位置转子流速仪结束测流
	CURR_POS_ADCP_SPEED,					//当前位置单波束测流
	CAR_VERI_MOVE,							//运行车水平运动
	FISH_HERO_MOVE = 15,					//铅鱼垂直运动
	SET_CAR_CONFIG,							//设置运行车本地配置文件
	SET_FISH_CONFIG,						//设置铅鱼本地配置文件
	GET_CAR_CONFIG,							//获取运行车本地配置文件
	GET_FISH_CONFIG,						//获取铅鱼本地配置文件
	GET_STAMPLE_CTRL = 20,					//取样控制
	ADD_MEASURE,							//加测控制
	TOUCH_GROUND,							//触底
	SET_MANU_MODE_SPEED,					//设置手动模式速度
	GET_MANU_MODE_SPEED = 24,				//获取手动模式速度	
	EMERGENCE_STOP = 25,					//紧急停止
	UPDATE_SOFTWARE,						/* 远程升级=26（协议文档）；libota 落盘 */
	SEND_FINAL_DATA,						//发送最终数据
	GTE_LOG,								//获取日志
	SET_ROTATOR_PARA,						//设置转子参数
	GOTO_ORIGINAL_POS = 30,					//回原点
	PAUSE_CONTINUE_MEASURE,					/* 31: 1暂停 2继续 3停止加测(电动门开↔关) */
	REMESURE_SINGLE_LINE,					//重新单条测量	
	GET_SUB_DEVICE_CONFIG,					//获取子设备配置文件
	SET_SUB_DEVICE_CONFIG,					//设置子设备配置文件
	CTRL_SEARCH_LIGHT = 35,					//控制探照灯
	CTRL_WARNING_SPEEKER = 36,				//控制警告喇叭
	SET_SECTION_NEW = 37,					//设置断面参数(新)
	GOTO_MOUNT_POS = 38,					//前往拉偏点
	SET_VE_ADCP_SPEED = 39,					//设置走航ADCP速度
	SET_DRIVER_STATE  = 40,					//控制驱动状态
	REPLY_LOG_DATA = 41,					//回复数据（日志）平台逐包确认
	ASK_VEADCP_FILE_DATA,					//获取adcp存储文件
	INNER_START_STOP = 65,					//主机内部开始、停止
	INNER_RECEIVE_RESPONSE_DOWN,			//往下发接收回复，比如获取本地配置文件，获取铅鱼配置文件，每发送一包数据后，小车主机向下发接收标志
	ROTATOR_MANU_MEA_START = 67,
	INNER_SET_SUB_DEVICE_CONFIG,			//设置子设备配置文件(内部通信使用)
	SEND_WATER_LEVEL_INNER,					//发送水位
	SEND_REC_DATA_ACK_INNER = 70,			//发送数据接收确认
	SEND_ASK_FINAL_DATA_INNER,				//发送最终数据请求(不是发流量，是发自动测流结果)
	REQUEST_MEASUREMENT_DATA_INNER,			//请求测量数据
	SENSOR_START_STOP_INNER,				//传感器启动停止
	SEND_ORIGINAL_POINT_LIMIT_INNER,		//发送原点限位
	TRADITIONAL_ROTATOR_GET_VALUE_INNER = 75,		//传统转子获取值
	TRADITIONAL_ROTATOR_RESET_INNER,				//传统转子复位
}DOWN_COMMAND_CODE;

typedef enum _UP_COMMAND_CODE
{
	/* 上报固件升级结果：0xFFFFFFE4 */
	REPORT_OTA_UPGRADE = 0xFFFFFFE4,
	REPORT_ADCP_RAW_DATA = 0xFFFFFFE7,   		// ADCP原始数据上报
	REPORT_SUB_DEVICE_CONFIG = 0xFFFFFFE8,		//上报子设备配置文件
	REPORT_AUTO_ROTATOR_DATA_DEPTH = 0XFFFFFFE9,		//上报自动转子测流值(带层数)
	REPORT_AUTO_DOPPLER_VALUE = 0XFFFFFFEA,		//上报自动单波束ADCP值(带层数)
	REPORT_AUTO_SEDIMENT_VALUE = 0XFFFFFFEB,	//上报泥沙值(带层数)
	REPORT_LOG = 0XFFFFFFEC,					//上报日志
	REQUEST_FINAL_RESULT = 0XFFFFFFED,			//请求最终结果
	REPORT_MANU_MODE_SPEED = 0XFFFFFFEE,		//上报手动模式速度
	REPORT_ADCP_DATA,							//上报ADCP数据
	REPORT_SENSOR_ERROR,						//上报传感器出错
	REPORT_MUDS_CURR_DATA,						//上报泥沙即时数据
	REPORT_AUTO_ROTATOR_DATA = 0XFFFFFFF2,		//上报转子自动流量
	REPORT_AUTO_ADCP_DATA,						//上报单波束ADCP自动流量
	REPORT_AUTO_MUDS_DATA,						//上报自动测沙数据
	REPORT_AUTO_NCS_DATA,						//上报自动非接触测流数据
	REPORT_MEASUREANDSECTION_SERI,				//上报测流和断面序号
	REPORT_FISH_CONFIG = 0XFFFFFFF7,			//上报铅鱼本地配置文件
	REPORT_CAR_CONFIG,							//上报运行车本地配置参数
	REPORT_VE_ADCP_SPEED,						//上报走航ADCP流速
	REPORT_MANU_ADCP_SPEED,						//上报手动单波束流速
	REPORT_MANU_NCS_DATA,						//上报手动非接触流速测流
	REPORT_MANU_ROTATOR_DATA = 0XFFFFFFFC,		//上报手动转子流速仪测流结果
	REPORT_MANU_MUDS_DATA,						//上报手动测沙结果
	REPORT_STATE = 0xFFFFFFFe,					//上报基本状态
	REPORT_RESULT = 0XFFFFFFFF,					//上报执行结果指令
	REPORT_FISH_SURFACE_POS = 0xFFFFAAAA,  		//上报铅鱼水面位置
	REPORT_FISH_LIMIT_POS = 0XFFFFAAA9 , 		//上报铅鱼限位位置	
	REPORT_SUB_DEVICE_CONFIG_INNER = 0xffffaaa8,//内部上报子设备配置文件
	INNER_RECEIVE_RESPONSE_UP = 0xffffaaa7,		//往上发接收回复，比如设置本地配置文件，设置铅鱼配置文件，小车每发送一包数据后，铅鱼主机向上发接收标志
	CMD_REPORT_SENSOR_DATA = 0xFFFFAAA6,		//上报传感器数据
	REPORT_TRADITIONAL_ROTATOR_DATA_INNER = 0xFFFFAAA5,//上报传统转子数据
	REPORT_RADAR_DATA_INNER = 0xFFFFAAA4,			//上报雷达数据

}UP_COMMAND_CODE;


//转子参数结构体
typedef struct _ROTATOR_CONFIG
{ 
	float fa_value;  //转子a值
	float fb_value;  //转子b值
	uint16_t u16n_value;  //转子n值
	uint16_t u16Meaduration;//测流时长
}ROTATOR_CONFIG;
//转子参数结构体
extern ROTATOR_CONFIG g_stRotatorConfig;
/*设备序号*/
typedef enum _ELEMENT_SERI
{
	ALLDEVICE   = 0,
	NCS         = 0x0001,//非接触式测流
	ROTATOR     = 0x0002,//转子测流
	ADCP        = 0x0004,//单波束ADCP
	VE_ADCP     = 0x0008,//走航ADCP
	MUDS        = 0x0010,//泥沙
	TEMPRATURE  = 0x0020//水温
	 

}ELEMENT_SERI;

typedef enum _EQUIPTYPE
{
	RTU = 0,
	CAR = 1,
	FISH = 2
}EQUIPTYPE;



typedef struct _WaterFlowPara  //水流量计算参数
{
	uint32_t u32StartDestence;//起点距
	uint32_t u32RiverBedElevation;//河底高程
	uint16_t u16Speed;//速度
}WaterFlowPara;

extern WaterFlowPara g_stWaterFLowPara[MAX_LINE_NUM];
#pragma pack(pop)


extern struct _ResultPara g_stResultPara;
extern SetSection g_stSection;//断面参数
extern SetSection_new g_stSection_new;//断面信息
extern SetCableway g_stSetCableway;//缆道参数
extern AutoMeasure g_stAutoMeasure;//自动测量参数
extern uint32_t g_u32waterLevel;
extern MeasureMode g_ChoosedMode;//选择的测量模式
extern AutoMudsData g_stAutoMudsArray[MAX_LINE_NUM];
extern float g_CarEncoderDistence ;
extern float g_FishEncoderDistence ;
extern uint8_t g_TimeoutFlag;
extern uint8_t g_GetResultFlag ;

extern uint32_t g_WebSendWaterLevel ;//服务器发送的水位
extern bool g_bRunning_State ;
//extern List *g_pstAutoNCSList;
//extern List *g_pstAutoMudsList;
//extern List *g_pstAutoADCPList;
//extern List *g_pstAutoRotatorList;

extern NotouchSpeedData g_stAutoNCSArray[MAX_LINE_NUM];//自动非接触测流数据
extern AutoMudsData g_stAutoMudsArray[MAX_LINE_NUM];//自动测沙数据
extern AutoADCPData g_stAutoADCPArray[MAX_LINE_NUM];//单波束ADCP自动流量
extern AutoRotatorData g_stAutoRotatorArray[MAX_LINE_NUM];//转子自动流量
extern CurrMudsData g_stCurrMudsArray[MAX_LINE_NUM];//泥沙即时数据
extern RotatorManuMeaStart g_stRotatormeasureStart;
extern uint8_t g_u8Mode;
extern time_t MeasureStart_sec ;
extern ReportManuRotatorBody g_stManuRotatorResultBody;

// ---- 非阻塞运动控制状态机外部变量和函数 ----
extern MotionState g_motionState;
extern MotionCtx g_motionCtx;
void MotionFSM_Tick(void *arg);
int Fish_up_nonblock(void);
int Fish_up_then_home_platform_nonblock(void); /* 收鱼→回仓→下放平台(MotionFSM串行) */
int Car_Goback_Home_nonblock(void);
int CarMovetoAim_nonblock(BYTE *pData);
int FishMovetoAim_nonblock(BYTE *pData);
// -------------------------------------

// ---- 加测控制状态机外部变量和函数 ----
extern AddMeasureState g_addMeasureState;
extern AddMeasureContext g_addMeasureCtx;
extern int g_fsmTimerId;

void PauseAddMeasure(void);
void ResumeAddMeasure(void);
void AddMeasureFSM_Tick(void *arg);
void RemoveResultsByStartDistance(uint32_t targetDist);
// -------------------------------------

int SetSectionPara(BYTE *pData,SetSection  *pstSection,uint16_t u16datalen);
int ReadSectionPara(SetSection *pstSection);
int SetCablewayPara(BYTE *pData,SetCableway  *pstSetCableway,uint16_t u16datalen);
int ReadCablewayPara(SetCableway *pstSetCableway);
int ApplyManuSpeedLevel(uint8_t level);
uint8_t GetManuSpeedLevel(void);
void SyncManuSpeedLevelFromCableway(void);
int SaveCablewayBodyFile(void);
void ReportManuModeSpeedFrame(uint32_t commandseri, int type);
int SetAutoMeasurePara(BYTE *pData,AutoMeasure  *pstAutoMeasure,uint16_t u16datalen);
int ReadAutoMeasurePara(AutoMeasure  *pstAutoMeasure);
int SwitchoperaMode(BYTE *pData,uint16_t u16datalen);
int CarMovetoAim(BYTE *pData);
void CarMovetoAimProc(void * arg);
int FishMovetoAim(BYTE *pData);
void FishMovetoAimProc(void * arg);
void  FishMovetoPlatform(uint32_t platformdistence);
extern int g_bFishDownAfterHome;  /* 1=小车回仓完成后需链式下放铅鱼到平台 */
void WaitRotatorSecondSignal(void); /* 等到第2个转子计数值再测流(替代sleep(20)) */
int GetCarConfigFile(uint32_t commandseri,int type);
MeasureMode GetMaeasureMode(AutoMeasure AutoMeasurePara);
//int AddMeauCtrl(BYTE *pData,uint16_t u16datalen,int type);
void * AddMeauCtrl(void * arg);
void SwitchoperaModeProc(void * arg);
ElemDeepth DeepthProc(MeasureLine stLine);
void QuickSort(DeepthPara arr[], int low, int high);
//int QuickSort(DeepthPara *a, int low, int high);
int AutoMeasureCtrl(MeasureMode stMeasureMode,int type);
int GenerateinWaterDeepth(uint32_t RiverBedHigh,uint32_t WaterLevel,uint8_t DeepPercent);
int GetCurrentPosRiverbedHigh(SetSection_new *pSetsection,uint32_t u32Startdentence);
void ReStart(BYTE *pData);
int Rotator_manuMeasureStart(BYTE *pData,uint16_t u16datalen);
void Rotator_manuMeasureStartProc(void * arg);
int Rotator_manuMeasureStop(BYTE *pData,uint16_t u16datalen,int type);
void  Rotator_manuMeasureStopProc(void * arg);
void RunningTimeout(void * arg);
float GenerateEncoderDistence(float wheelDm);

int SetSectionPara_new(uint8_t *pData, SetSection_new *pstSection,
                       uint16_t u16datalen);

int ReadSectionPara_new(SetSection_new *pstSection);

void GetRatatorResult(void * arg);
void GetMeasureResult(void *arg);
void TimeoutProc(void * arg);
float GenerateWaterFlow(WaterFlowPara* sections, int num_lines);

/* 本地流量计算参数，由 tyls.conf 加载 */
typedef struct _FlowCalcConfig
{
    float fLeftBankCoef;   /* 左岸系数(起点系数) */
    float fRightBankCoef;  /* 右岸系数(终点系数) */
    float fLineCoef;       /* 垂线系数，垂线平均流速 = 实测 × 此值 */
    uint32_t u32MatchTolCm;/* 岸边段识别容差(cm) */
} FlowCalcConfig;

extern FlowCalcConfig g_stFlowCalcConfig;

void LogLocalWaterFlow(int num_lines);
void  ProcFinalResult(BYTE *pData,uint16_t u16datalen);
int SetRotatorpara(BYTE *pData, ROTATOR_CONFIG *pstRotator,uint16_t u16datalen);
int ReadRotatorConfig(ROTATOR_CONFIG *pstRotatorconfig);
void SendSubDeviceConfigFile(uint8_t subDeviceType, const char* filename, uint32_t command_seri, int type);
void SendLogFile(uint8_t deviceType, uint8_t logLevel, uint32_t command_seri, int type);
void LogUpload_OnAck(uint32_t reportCmd, uint8_t deviceType, uint8_t logLevel, uint16_t pktSeri);
int LogUpload_IsBusy(void);
void FinalResult_OnDownlink(const uint8_t *pData, uint16_t len);
#endif
