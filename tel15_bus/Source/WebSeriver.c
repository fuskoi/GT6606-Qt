#include "WebSeriver.h"
#include "RS485_ICD.h"
#include "Encoder.h"
#include "CRC.h"
#include "Timertask.h"
#include <math.h>
#include "Relay.h"
#include "Sensor.h"
#include "LinkList.h"
#include <sys/reboot.h>
#include "timer_manager.h"
#include "log.h"
#include <stdlib.h>
#include <time.h>
#include "zigbee_dataproc.h"
#include "AMPSTechMotor.h"
/*全局变量区*/
extern uint8_t g_sendcount;
SetSection g_stSection;//断面信息
SetSection_new g_stSection_new;//断面信息
SetCableway g_stSetCableway;//缆道参数
static uint8_t g_u8ManuSpeedLevel = MANU_SPEED_LEVEL_MIN;
AutoMeasure g_stAutoMeasure;//自动测量参数
ElemDeepth g_DeepthPara;//各个元素深度参数
uint32_t g_u32waterLevel = 0;//水位
uint32_t g_WebSendWaterLevel = 1000000;//服务器发送的水位
MeasureMode g_ChoosedMode;//选择的测量模式
struct _ResultPara g_stResultPara;//测量结果参数
uint8_t g_u8Mode = 0;
time_t MeasureStart_sec = 0;
NotouchSpeedData g_stAutoNCSArray[MAX_LINE_NUM];//自动非接触测流数据
AutoMudsData g_stAutoMudsArray[MAX_LINE_NUM];//自动测沙数据
AutoADCPData g_stAutoADCPArray[MAX_LINE_NUM];//单波束ADCP自动流量
AutoRotatorData g_stAutoRotatorArray[MAX_LINE_NUM];//转子自动流量
CurrMudsData g_stCurrMudsArray[MAX_LINE_NUM];//泥沙即时数据
ReportManuRotatorBody g_stManuRotatorResultBody;
bool g_bRunning_State = false;  // 运行状态
WaterFlowPara g_stWaterFLowPara[MAX_LINE_NUM];// 流量参数

List *g_pstAutoNCSList;
List *g_pstAutoMudsList;
List *g_pstAutoADCPList;
List *g_pstAutoRotatorList;
List *g_pstAutoRotatorDepthList = NULL;   // 分层转子测流数据链表

uint8_t g_GetResultFlag = 0;
uint8_t g_TimeoutFlag = 0;
SLTNode* cur = NULL;

float g_CarEncoderDistence = 0;//小车编码器每转一圈周长
float g_FishEncoderDistence = 0;//铅鱼编码器每转一圈周长

// ---- 加测控制状态机全局变量 ----
AddMeasureState g_addMeasureState = FSM_IDLE;
AddMeasureContext g_addMeasureCtx;

/*
 * [2026-06-22 修改说明 - WebSeriver.c 测流/运动]
 * 1. 测完回仓: fsm_fishDownToPlatform改用平台高度+正转限位；AutoMeasureCtrl回仓后FishMovetoPlatform
 * 2. 小车位置: 回仓复位水平编码器，触摸屏显示0
 * 3. 触底停转: 增加转子采集板u16TouchGround检测
 * 4. 测流时序: 取消固定20s，改为等到第2个转子信号再开始计时
 * 5. Web手动移动(FISH_HERO_MOVE): motion_pos_reached按FWD/BWD判到位；垂直RUN仅FWD限位，不用触底DI
 *
 * [2026-07-08 修改说明 - WebSeriver.c 本地流量计算]
 * 1. LogLocalWaterFlow: 平台同款4区法(Q=Σ部分面积×部分流速)，原始测点流速参与计算
 * 2. 水文修约: 四舍六入五成双；距离2位、垂线/部分流速2位、面积1位、流量2位、平均流速3位
 * 3. 断面: ReadSectionPara_new后FlowSyncSectionFromNew同步到g_stSection(修复NEW/OLD格式错位)
 * 4. 河底脏数据: FlowSanitizeBedCm剔除主槽内误写的测线距(如bed=775)
 * 5. 触摸屏: 加测结束本地算Q推送TouchScreenPushReportData；关门到位后再上行0xFFFFFFED(门控期间ZigBee静默)
 */
/* [修改] 本地流量系数默认值，ReadConfig读tyls.conf后覆盖 */
FlowCalcConfig g_stFlowCalcConfig = {
    1.0f,  /* fLeftBankCoef  → FlowLeftBankCoef */
    1.0f,  /* fRightBankCoef → FlowRightBankCoef */
    1.0f,  /* fLineCoef      → FlowLineCoef */
    30     /* u32MatchTolCm  → FlowMatchTolCm */
};
int g_fsmTimerId = -1;
static int g_fsmMeasureTimerId = -1;
static int g_fsmMeasureDepthIndex = 0;
static float g_fsmMeasureLineTargetPos = 0.0f;
static float g_fsmMeasureDepthTargetPos = 0.0f;

/*
 * [2026-06-22 修改说明 - WebSeriver.c 测流/运动]
 * 1. 测完回仓: fsm_fishDownToPlatform改用平台高度+正转限位；AutoMeasureCtrl回仓后FishMovetoPlatform
 * 2. 小车位置: 回仓复位水平编码器，触摸屏显示0
 * 3. 触底停转: 增加转子采集板u16TouchGround检测
 * 4. 测流时序: 取消固定20s，改为等到第2个转子信号再开始计时
 * 5. Web手动移动(FISH_HERO_MOVE): motion_pos_reached按FWD/BWD判到位；垂直RUN仅FWD限位，不用触底DI
 */
int g_bFishDownAfterHome = 0;          /* [修改] 手动测流停止后，小车回仓完成需下放铅鱼到平台 */
static int g_motionChainCarHomeAfterFishUp = 0; /* [修改] 收鱼完成后链式启动回仓 */
static int g_motionCheckTouchGround = 0;      /* [修改] 测流后下放平台时启用触底DI */

#define ROTATOR_COUNT_PER_SIGNAL   2     /* [修改] 硬件每1个流速信号=计数值+2，跳过第1个从第2个起计 */
#define ROTATOR_ARM_TIMEOUT_MS     5000  /* [修改] 等第2信号最长5s，超时则强制开始测流 */
#define FISH_PLATFORM_ENCODER_TOL  3.0f  /* [修改] 平台下放编码器容差(cm)；FishDistenseAdjust=15会在45cm误判 */
#define DOOR_WAIT_TIMEOUT_MS       50000
#define DOOR_CLOSE_RETRY_INTERVAL_MS  3000
#define DOOR_CLOSE_MAX_RETRY       10
#define DOOR_OPEN_SETTLE_MS        4000

static void fsm_resetCarHomePosition(void);
static bool fsm_checkRotatorTouchGround(void);
static void fsm_beginRotatorMeasure(bool bHasNCS, bool bHasDepth);
static void fsm_doorOpenWait(void);
static uint8_t s_sendResultStep = 0;
static volatile int s_finalResultAcked = 0;
static uint8_t s_finalResultRetry = 0;
static uint64_t s_finalResultSendMs = 0;
static uint16_t s_finalResultElement = 0;
static int s_finalResultWaitActive = 0;
static struct {
    int valid;
    float qM3s;
    float areaM2;
    float avgVMs;
} g_lastLocalFlow;
static uint64_t s_doorCloseWaitStartMs = 0;
static uint64_t s_doorCloseLastLogMs = 0;
static uint64_t s_doorCloseLastCmdMs = 0;
static uint8_t s_doorCloseRetryCount = 0;
static uint64_t s_doorOpenWaitStartMs = 0;
static uint64_t s_doorOpenLastLogMs = 0;
static uint64_t s_doorOpenLastCmdMs = 0;
static uint64_t s_doorOpenSettleStartMs = 0;
static uint8_t s_doorOpenRetryCount = 0;
static uint8_t s_doorOpenSettleActive = 0;
// ----------------------------------

void RunningTimeout(void * arg)
{

	LOG(LOG_DEBUG,"%s RunningTimeout",(uint8_t * )arg);
	g_bRunning_State = false;
}
/**
 * 计算编码器距离
 * 
 * 根据车轮直径计算编码器应该测量的距离
 * 
 * @param wheelDm 车轮直径，单位为米
 * @return 返回编码器测量的距离，单位为米
 */
float GenerateEncoderDistence(float wheelDm)
{
    // 返回车轮周长，即编码器测量的距离
    return (float)(wheelDm) * M_PI;
}

/*
*函数名称:SetSectionPara
*函数功能:设置断面参数,将断面参数保存到duanmian.data文件中，起始字节是断面序号
*输   入: BYTE *pData  接收数据     ,   uint16_t u16datalen   数据长度
*输   出: SetSection  *pstSection 断面参数
*返   回: SUCCESS - 成功  ERROR - 错误
*/

int SetSectionPara(BYTE *pData,SetSection  *pstSection,uint16_t u16datalen)
{
	int ret = ERROR;
    char FileDir[256];
    memset(FileDir,0,256);
    GetAppDir(FileDir);
    strncat(FileDir,"/duanmian.data",strlen("/duanmian.data"));
	printf("SectionDataLen is %d\n",u16datalen);
	memcpy(pstSection,pData,u16datalen);
	int i = 0;
	printf("pdata is :");
	for(i = 0;i< u16datalen;i++)
	{
		printf(" %02x",pData[i]);
	}
	printf("\npstSection->u16SectionNum is %d\n",swap_u16( pstSection->u16SectionNum));
    FILE* pFile = fopen(FileDir,"wb+");
    if(NULL != pFile)
    {
        ret = fwrite(pData,1,u16datalen,pFile);
		LOG(LOG_INFO,"Set Section file write bytes is %d",ret);
        fclose(pFile);
		return SUCCESS;
    }
    else
    {
        LOG(LOG_ERROR,"open duanmian file error");
		return ERROR;
    }
	
}


/*
*函数名称:ReadSectionPara
*函数功能:读取断面参数，将读取到的参数保存在pstSection中，读取到的字节以断面序号开始
*输   入: 无
*输   出:  SectionPara *pstSection)
*返   回: SUCCESS - 成功  ERROR - 错误
*/
int ReadSectionPara(SetSection *pstSection)
{
	int ret = ERROR;
	int i = 0;
    char FileDir[256];
    memset(FileDir,0,256);
    GetAppDir(FileDir);
    strncat(FileDir,"/duanmian.data",strlen("/duanmian.data"));
    FILE* pFile = fopen(FileDir,"rb");
    if(NULL != pFile)
    {
		int file_size;   //保存文件字符数
    	char* tmp;       //将文件内容读到此指针位置
		fseek(pFile, 0, SEEK_END);      //将文件指针指向该文件的最后
		file_size = ftell(pFile);       //根据指针位置，此时可以算出文件的字符数
		printf("file_size %d\n", file_size);
		tmp = (char*)malloc(file_size * sizeof(char) + 1); //根据文件大小为tmp动态分配空间
		memset(tmp, '\0', file_size * sizeof(char));  //初始化此控件内容，否则可能会有乱码
		fseek(pFile, 0, SEEK_SET);                 //重新将指针指向文件首部
		fread(tmp, sizeof(char), file_size, pFile); //开始读取整个文件
		printf("duanmian.data is");

		for(i= 0;i< file_size;i++)
		{
			printf(" %02x",tmp[i]);
		}
		printf("\n");
		fclose(pFile);
		
		memcpy(&pstSection->u16SectionSeri,tmp,file_size);
		printf("SectionNum is %04x\n",pstSection->u16SectionNum);
	
		for(i = 0;i< (pstSection->u16SectionNum);i++)
		{
			printf("pstSection->stSection[%d].u32StartDestence is %08x\n",i,( pstSection->stSection[i].u32StartDestence));
		}
		free(tmp);
		tmp = NULL;
		ret = SUCCESS;
    }
	else
	{
		LOG(LOG_ERROR,"open duanmian file error");
		
	}
	return ret;
}

int SetSectionPara_new(BYTE *pData,SetSection_new  *pstSection,uint16_t u16datalen)
{
	int ret = ERROR;
    char FileDir[256];
    memset(FileDir,0,256);
    GetAppDir(FileDir);
    strncat(FileDir,"/duanmian.data",strlen("/duanmian.data"));
	printf("SectionDataLen is %d\n",u16datalen);
	memcpy(pstSection,pData,u16datalen);
	int i = 0;
	printf("pdata is :");
	for(i = 0;i< u16datalen;i++)
	{
		printf(" %02x",pData[i]);
	}
	printf("\npstSection->u16SectionNum is %d\n",swap_u16( pstSection->u16SectionNum));
    FILE* pFile = fopen(FileDir,"wb+");
    if(NULL != pFile)
    {
        ret = fwrite(pData,1,u16datalen,pFile);
		LOG(LOG_INFO,"Set Section file write bytes is %d",ret);
        fclose(pFile);
		return SUCCESS;
    }
    else
    {
        LOG(LOG_ERROR,"open duanmian file error");
		return ERROR;
    }
	
}


/*
 * [修改] 平台下发SET_SECTION_NEW(12B/点)写入duanmian.data；
 * 流量计算用g_stSection(8B/点)，读新格式后同步，避免字段错位(如775测线距误当河底)
 */
static void FlowSyncSectionFromNew(void)
{
    int i;
    int n = (int)g_stSection_new.u16SectionNum;

    if (n <= 0 || n > MAX_LINE_NUM)
    {
        g_stSection.u16SectionNum = 0;
        return;
    }

    g_stSection.u16SectionSeri = g_stSection_new.u16SectionSeri;
    g_stSection.u16SectionNum = g_stSection_new.u16SectionNum;
    for (i = 0; i < n; i++)
    {
        int32_t d = g_stSection_new.stSection[i].u32StartDestence;
        int32_t b = g_stSection_new.stSection[i].u32RiverBedElevation;

        g_stSection.stSection[i].u32StartDestence =
            (uint32_t)(d >= 0 ? d : 0);
        g_stSection.stSection[i].u32RiverBedElevation =
            (uint32_t)(b >= 0 ? b : 0);
    }
    for (i = n; i < MAX_LINE_NUM; i++)
    {
        g_stSection.stSection[i].u32StartDestence = 0;
        g_stSection.stSection[i].u32RiverBedElevation = 0;
    }
}

/*
*函数名称:ReadSectionPara
*函数功能:读取断面参数，将读取到的参数保存在pstSection中，读取到的字节以断面序号开始
*输   入: 无
*输   出:  SectionPara *pstSection)
*返   回: SUCCESS - 成功  ERROR - 错误
*/
int ReadSectionPara_new(SetSection_new *pstSection)
{
	int ret = ERROR;
	int i = 0;
    char FileDir[256];
    memset(FileDir,0,256);
    GetAppDir(FileDir);
    strncat(FileDir,"/duanmian.data",strlen("/duanmian.data"));
    FILE* pFile = fopen(FileDir,"rb");
    if(NULL != pFile)
    {
		int file_size;   //保存文件字符数
    	char* tmp;       //将文件内容读到此指针位置
		fseek(pFile, 0, SEEK_END);      //将文件指针指向该文件的最后
		file_size = ftell(pFile);       //根据指针位置，此时可以算出文件的字符数
		printf("file_size %d\n", file_size);
		tmp = (char*)malloc(file_size * sizeof(char) + 1); //根据文件大小为tmp动态分配空间
		memset(tmp, '\0', file_size * sizeof(char));  //初始化此控件内容，否则可能会有乱码
		fseek(pFile, 0, SEEK_SET);                 //重新将指针指向文件首部
		fread(tmp, sizeof(char), file_size, pFile); //开始读取整个文件
		printf("duanmian.data is");

		for(i= 0;i< file_size;i++)
		{
			printf(" %02x",tmp[i]);
		}
		printf("\n");
		fclose(pFile);
		
		memcpy(&pstSection->u16SectionSeri,tmp,file_size);
		printf("SectionNum is %04x\n",pstSection->u16SectionNum);
	
		for(i = 0;i< (pstSection->u16SectionNum);i++)
		{
			printf("pstSection->stSection[%d].u32StartDestence is %08x\n",i,( pstSection->stSection[i].u32StartDestence));
		}
		free(tmp);
		tmp = NULL;
		ret = SUCCESS;
		FlowSyncSectionFromNew(); /* [修改] 读断面后同步到g_stSection供流量计算 */
    }
	else
	{
		LOG(LOG_ERROR,"open duanmian file error");
		
	}
	return ret;
}

/*
*函数名称:SetCablewayPara
*函数功能:设置缆道参数,将缆道参数保存在landao.data中，初始字节从缆车高度开始
*输   入: BYTE *pData  接收数据     ,   uint16_t u16datalen   数据长度
*输   出:  SetCableway    *pstSetCableway,  缆道参数
*返   回: SUCCESS - 成功  ERROR - 错误
*/
int SetCablewayPara(BYTE *pData,SetCableway  *pstSetCableway,uint16_t u16datalen)
{
	int ret = ERROR;
    char FileDir[256];
    uint16_t bodyLen;
    memset(FileDir,0,256);
    GetAppDir(FileDir);
    strncat(FileDir,"/landao.data",strlen("/landao.data"));
    /* pData 仅为缆道体(无命令头)；勿整结构 memcpy，否则速度字段错位 */
    if (pData == NULL || pstSetCableway == NULL || u16datalen < sizeof(SetCableWayBody)) {
        LOG(LOG_ERROR, "SetCablewayPara invalid len=%u need=%u",
            (unsigned)u16datalen, (unsigned)sizeof(SetCableWayBody));
        return ERROR;
    }
    bodyLen = (uint16_t)sizeof(SetCableWayBody);
    memcpy(&pstSetCableway->stSetCablewayBody, pData, bodyLen);
    FILE* pFile = fopen(FileDir,"wb+");
    if(NULL != pFile)
    {
        ret = (int)fwrite(pData, 1, bodyLen, pFile);
		LOG(LOG_DEBUG,"write Cableway bytes is %d",ret);
        fclose(pFile);
        g_stHoriDIStatusFlag.i32SetRPM = GetCarRunRpm();
        g_stVeroDIStatusFlag.i32SetRPM = GetFishRunRpm();
        {
            uint16_t horiSpd = pstSetCableway->stSetCablewayBody.u16HoriSpeed;
            uint16_t veriSpd = pstSetCableway->stSetCablewayBody.u16VeriSpeed;
            if (horiSpd > MAX_HORI_SPEED) {
                horiSpd = MAX_HORI_SPEED;
            }
            if (veriSpd > MAX_VERO_SPEED) {
                veriSpd = MAX_VERO_SPEED;
            }
            LOG(LOG_WARNING,
                "cableway speed applied: hori=%u cm/s(%.2fm/s,cap1.50)->rpm=%d, veri=%u cm/s(%.2fm/s,cap0.50)->rpm=%d",
                pstSetCableway->stSetCablewayBody.u16HoriSpeed,
                horiSpd / 100.0f,
                g_stHoriDIStatusFlag.i32SetRPM,
                pstSetCableway->stSetCablewayBody.u16VeriSpeed,
                veriSpd / 100.0f,
                g_stVeroDIStatusFlag.i32SetRPM);
        }
        SyncManuSpeedLevelFromCableway();
		return SUCCESS;
    }
    else
    {
        LOG(LOG_ERROR,"open landao file error");
		return ERROR;
    }

}

/*
 * 手动速度等级 1-7 → 水平/垂直线速度(cm/s)
 * 等级7对应 MAX_HORI_SPEED / MAX_VERO_SPEED
 */
int ApplyManuSpeedLevel(uint8_t level)
{
    uint16_t hori;
    uint16_t veri;

    if (level < MANU_SPEED_LEVEL_MIN || level > MANU_SPEED_LEVEL_MAX) {
        LOG(LOG_ERROR, "ApplyManuSpeedLevel invalid level=%u", (unsigned)level);
        return ERROR;
    }

    hori = (uint16_t)((MAX_HORI_SPEED * (uint32_t)level) / MANU_SPEED_LEVEL_MAX);
    veri = (uint16_t)((MAX_VERO_SPEED * (uint32_t)level) / MANU_SPEED_LEVEL_MAX);
    if (hori == 0) {
        hori = 1;
    }
    if (veri == 0) {
        veri = 1;
    }

    g_u8ManuSpeedLevel = level;
    g_stSetCableway.stSetCablewayBody.u16HoriSpeed = hori;
    g_stSetCableway.stSetCablewayBody.u16VeriSpeed = veri;
    g_stHoriDIStatusFlag.i32SetRPM = GetCarRunRpm();
    g_stVeroDIStatusFlag.i32SetRPM = GetFishRunRpm();

    LOG(LOG_WARNING,
        "manu speed level=%u -> hori=%u cm/s rpm=%d, veri=%u cm/s rpm=%d",
        (unsigned)level, hori, g_stHoriDIStatusFlag.i32SetRPM,
        veri, g_stVeroDIStatusFlag.i32SetRPM);

    return SaveCablewayBodyFile();
}

uint8_t GetManuSpeedLevel(void)
{
    return g_u8ManuSpeedLevel;
}

void SyncManuSpeedLevelFromCableway(void)
{
    uint16_t hori = g_stSetCableway.stSetCablewayBody.u16HoriSpeed;
    uint8_t level;

    if (hori == 0 || MAX_HORI_SPEED == 0) {
        level = MANU_SPEED_LEVEL_MIN;
    } else {
        level = (uint8_t)(((uint32_t)hori * MANU_SPEED_LEVEL_MAX + MAX_HORI_SPEED / 2)
                          / MAX_HORI_SPEED);
        if (level < MANU_SPEED_LEVEL_MIN) {
            level = MANU_SPEED_LEVEL_MIN;
        }
        if (level > MANU_SPEED_LEVEL_MAX) {
            level = MANU_SPEED_LEVEL_MAX;
        }
    }
    g_u8ManuSpeedLevel = level;
    LOG(LOG_DEBUG, "SyncManuSpeedLevelFromCableway hori=%u -> level=%u",
        hori, (unsigned)level);
}

int SaveCablewayBodyFile(void)
{
    char FileDir[256];
    FILE *pFile;
    size_t nwrite;

    memset(FileDir, 0, sizeof(FileDir));
    GetAppDir(FileDir);
    strncat(FileDir, "/landao.data", strlen("/landao.data"));
    pFile = fopen(FileDir, "wb+");
    if (pFile == NULL) {
        LOG(LOG_ERROR, "SaveCablewayBodyFile open landao error");
        return ERROR;
    }
    nwrite = fwrite(&g_stSetCableway.stSetCablewayBody, 1,
                    sizeof(SetCableWayBody), pFile);
    fclose(pFile);
    if (nwrite != sizeof(SetCableWayBody)) {
        LOG(LOG_ERROR, "SaveCablewayBodyFile write fail n=%u", (unsigned)nwrite);
        return ERROR;
    }
    return SUCCESS;
}

void ReportManuModeSpeedFrame(uint32_t commandseri, int type)
{
    ReportManuModeSpeed stRpt;

    memset(&stRpt, 0, sizeof(stRpt));
    stRpt.stCommandHead.u32CommandSeri = commandseri;
    stRpt.stCommandHead.u32CommandLen =
        (uint32_t)(sizeof(ReportManuModeSpeed) + sizeof(SeriverCommandTail));
    stRpt.stCommandHead.u32Command = REPORT_MANU_MODE_SPEED;
    memcpy(stRpt.stCommandHead.au8StationID, g_aStationID, sizeof(g_aStationID));
    stRpt.u8SpeedLevel = g_u8ManuSpeedLevel;
    LOG(LOG_INFO, "ReportManuModeSpeed level=%u type=%d",
        (unsigned)stRpt.u8SpeedLevel, type);
    SendZigbeeData(UP_EXPAND_CODE, (uint8_t *)&stRpt, sizeof(ReportManuModeSpeed), type);
}


/*
*函数名称:ReadCablewayPara
*函数功能:读取缆道参数
*输   入: 无
*输   出:  SetCableway *pstSetCableway
*返   回: SUCCESS - 成功  ERROR - 错误
*/
int ReadCablewayPara(SetCableway *pstSetCableway)
{
	int ret = ERROR;
    char FileDir[256];
    memset(FileDir,0,256);
    GetAppDir(FileDir);
    strncat(FileDir,"/landao.data",strlen("/landao.data"));
    FILE* pFile = fopen(FileDir,"rb");
    if(NULL != pFile)
    {
		int file_size;   //保存文件字符数
    	char* tmp;       //将文件内容读到此指针位置
		fseek(pFile, 0, SEEK_END);      //将文件指针指向该文件的最后
		file_size = ftell(pFile);       //根据指针位置，此时可以算出文件的字符数
		printf("file_size %d\n", file_size);
		tmp = (char*)malloc(file_size * sizeof(char) + 1); //根据文件大小为tmp动态分配空间
		memset(tmp, '\0', file_size * sizeof(char));  //初始化此控件内容，否则可能会有乱码
		fseek(pFile, 0, SEEK_SET);                 //重新将指针指向文件首部
		fread(tmp, sizeof(char), file_size, pFile); //开始读取整个文件
		LOG(LOG_DEBUG,"landao.data is");
		LOG_ARRAY(LOG_DEBUG,tmp,file_size,"%02x");
		fclose(pFile);
		
		memcpy(&pstSetCableway->stSetCablewayBody,tmp,sizeof(SetCableWayBody));
		/* printf("Read cableway para 16HoriWheelDm is %04x\n",swap_u16( pstSetCableway->stSetCablewayBody.u16HoriWheelDm));
		printf("Read cableway para 16VeriWheelDm is %04x\n",swap_u16( pstSetCableway->stSetCablewayBody.u16VeriWheelDm));
		printf("Read cableway para HoriEncoderDm is %04x\n",swap_u16( pstSetCableway->stSetCablewayBody.u16HoriEncoderDm));
		printf("Read cableway para VeriEncoderDm is %04x\n",swap_u16( pstSetCableway->stSetCablewayBody.u16VeriEncoderDm));
		printf("Read cableway para CableHight is %08x\n",swap_u32( pstSetCableway->stSetCablewayBody.u32CableHight));
		printf("orgional data is %08x\n",pstSetCableway->stSetCablewayBody.u32CableHight); */
		SyncManuSpeedLevelFromCableway();
		ret = SUCCESS;
		free(tmp);
		tmp = NULL;
    }
	else
	{
		LOG(LOG_ERROR,"open landao file error");	
	}
	return ret;
}


/*
*函数名称:SetAutoMeasurePara
*函数功能:设置自动测量参数,将自动测量参数保存在automeasure.data中，初始字节从自动运行参数序列号开始
*输   入: BYTE *pData  接收数据     ,   uint16_t u16datalen   数据长度
*输   出: AutoMeasure  *pstAutoMeasure,  自动测量参数
*返   回: SUCCESS - 成功  ERROR - 错误
*/
int SetAutoMeasurePara(BYTE *pData,AutoMeasure  *pstAutoMeasure,uint16_t u16datalen)
{

	int ret = ERROR;
    char FileDir[256];
    memset(FileDir,0,256);
    GetAppDir(FileDir);
    strncat(FileDir,"/automeasure.data",strlen("/automeasure.data"));
	memcpy(pstAutoMeasure,pData,u16datalen);
    FILE* pFile = fopen(FileDir,"wb+");
    if(NULL != pFile)
    {
        ret = fwrite(pData,1,u16datalen,pFile);
		LOG(LOG_INFO,"automeasure write  bytes is %d",ret);
        fclose(pFile);
		return SUCCESS;
    }
    else
    {
        LOG(LOG_ERROR,"open automeasure file error");
		return ERROR;
    }
}


/*
*函数名称:ReadCablewayPara
*函数功能:读取缆道参数
*输   入: 无
*输   出:  SetCableway *pstSetCableway
*返   回: SUCCESS - 成功  ERROR - 错误
*/
int ReadAutoMeasurePara(AutoMeasure  *pstAutoMeasure)
{
	int ret = ERROR;
	int tempModeNum = 0;//用于循环条件
	MeasureMode modeArray[MAX_MODE_NUM];//模式数组
	MeasureLine lineArray[MAX_LINE_NUM];//测线数组
    char FileDir[256];
    memset(FileDir,0,256);
	uint32_t u32offset = 0;  //数据偏移量
	uint16_t u16GetLineNum = 0;//从文件读取到的测量数量
	memset(&modeArray,0,sizeof(MeasureMode) * MAX_MODE_NUM);
	memset(&lineArray,0, sizeof(MeasureLine) * MAX_LINE_NUM);
    GetAppDir(FileDir);
    strncat(FileDir,"/automeasure.data",strlen("/automeasure.data"));
    FILE* pFile = fopen(FileDir,"rb");
    if(NULL != pFile)
    {
		int file_size;   //保存文件字符数
    	char* tmp;       //将文件内容读到此指针位置
		fseek(pFile, 0, SEEK_END);      //将文件指针指向该文件的最后
		file_size = ftell(pFile);       //根据指针位置，此时可以算出文件的字符数
		tmp = (char*)malloc(file_size * sizeof(char) + 1); //根据文件大小为tmp动态分配空间
		memset(tmp, '\0', file_size * sizeof(char));  //初始化此控件内容，否则可能会有乱码
		fseek(pFile, 0, SEEK_SET);                 //重新将指针指向文件首部
		fread(tmp, sizeof(char), file_size, pFile); //开始读取文件全部内容
		LOG(LOG_DEBUG,"automeasure.data  file_size is %d,data is ",file_size);
		LOG_ARRAY(LOG_DEBUG,tmp,file_size,"%02x");
		
		fclose(pFile);
		//free(tmp);
		//计算模式数量所在在偏移量
		u32offset = sizeof(pstAutoMeasure->stAutoMeaHead.u16ParaSeri) + sizeof(pstAutoMeasure->stAutoMeaHead.u8ModeNum);//
		memcpy((uint8_t *)&pstAutoMeasure->stAutoMeaHead.u16ParaSeri,tmp,u32offset);
		LOG(LOG_DEBUG,"Read automeasure file ModeNum is %d",pstAutoMeasure->stAutoMeaHead.u8ModeNum);
		//tmp = tmp + u32offset;
		//u32offset = 0;
		for(tempModeNum = 0;tempModeNum < pstAutoMeasure->stAutoMeaHead.u8ModeNum;tempModeNum ++)
		{
			//累计偏移量 =自动测量参数头部信息 +                每个模式测线数量 * 测线要素所占字节 + 模式头部信息所占字节 *模式数量
			u32offset += u16GetLineNum * sizeof(MeasureLine)  + sizeof(MeasureModeHead) * (tempModeNum );
			LOG(LOG_DEBUG,"u32offset is %d\nsizeof(MeasureLine) is %d\n",u32offset,sizeof(MeasureLine));
			//复制数据到模式头部信息
			memcpy((uint8_t*)&pstAutoMeasure->stMode[tempModeNum].stMeaModeHead,tmp + u32offset,sizeof(MeasureModeHead));
			
			u16GetLineNum = ( pstAutoMeasure->stMode[tempModeNum].stMeaModeHead.u16LineNum);
			LOG(LOG_DEBUG,"Mode[%d] LineNum is %02x",tempModeNum, u16GetLineNum);
			//复制数据到模式测线信息
			memcpy((uint8_t*)&pstAutoMeasure->stMode[tempModeNum].stLine,tmp + u32offset + sizeof(MeasureModeHead),sizeof(MeasureLine) * u16GetLineNum);
			int i = 0;
			for(i = 0;i< u16GetLineNum;i++)
			{
				LOG(LOG_DEBUG,"Mode[%d].Line[%d] StartDistance is %d,Depthpersent %08x",tempModeNum,i,
					(pstAutoMeasure->stMode[tempModeNum].stLine[i].i32StartDistance),
					(pstAutoMeasure->stMode[tempModeNum].stLine[i].u32RotatorDeepth)
				);
			}
			
			//memcpy(modeArray + u32offset + tempModeNum * sizeof(pstAutoMeasure->stMode[tempModeNum].stMeaModeHead),&pstAutoMeasure->stMode[tempModeNum].stMeaModeHead,sizeof(pstAutoMeasure->stMode[tempModeNum].stMeaModeHead));
			
		}
		free(tmp);
		tmp = NULL;
		ret = SUCCESS;
		
    }
	else
	{
		LOG(LOG_ERROR,"open automeasure file error\n");
		
	}
	return ret;
}


/*
*函数名称:SwitchoperaMode
*函数功能:切换控制模式
*输   入: BYTE *pData  接收到的数据,uint16_t u16datalen 数据长度
*输   出:  无
*返   回: SUCCESS - 成功  ERROR - 错误
*/
int SwitchoperaMode(BYTE *pData,uint16_t u16datalen)
{
	uint8_t u8Mode;
	int ret = 0;
	frameBody stMotorFrame;
	memcpy(&u8Mode,pData,sizeof(u8Mode));
	LOG(LOG_DEBUG,"SwitchoperaMode  mode is %02x",u8Mode);
	switch (u8Mode)
	{
		case 0://自动模式
			g_u8Mode = 0;
			break;
		case 1://手动模式
			g_u8Mode = 1;
			/* 切入手动必须停加测 Tick，否则 50ms 状态机会把垂直点动立刻刹停 */
			PauseAddMeasure();
			break;
		case 2://手动控制前进、后退、上升、下降
			/* 加测过程中手动点动：先暂停状态机，避免50ms Tick把铅鱼立刻刹停 */
			PauseAddMeasure();
			memcpy(&stMotorFrame,pData + sizeof(u8Mode),sizeof(frameBody));
			Relay_MotorUnbreak(stMotorFrame.u8MotorAddr -1);

			if(STOP != ( stMotorFrame.u16RegData))
			{
				if(stMotorFrame.u8MotorAddr == g_stDeviceAddr.u8Motor_Drive_fish)
				{
					if(stMotorFrame.u16RegData == DOWN)
					{
                        if (FishMoveAllowed(AMPS_FWD))
                        {
						    AMPS_MotorMove(stMotorFrame.u8MotorAddr ,(AMPS_FWD) * GetFishRunRpm() * 10) ;
                        }
					}
					else if(stMotorFrame.u16RegData == UP)
					{
                        if (FishMoveAllowed(AMPS_BWD))
                        {
						    AMPS_MotorMove(stMotorFrame.u8MotorAddr ,(AMPS_BWD) * GetFishRunRpm() * 10) ;
                        }
					}

				}
				else if(stMotorFrame.u8MotorAddr == g_stDeviceAddr.u8Motor_Drive_car)
				{
					if(stMotorFrame.u16RegData == FWD)
					{
                        if (CarMoveAllowed(AMPS_FWD))
                        {
						    AMPS_MotorMove(stMotorFrame.u8MotorAddr ,(AMPS_FWD) * GetCarRunRpm() * 10) ;
                        }
					}
					else if(stMotorFrame.u16RegData == BWD)
					{
                        if (CarMoveAllowed(AMPS_BWD))
                        {
						    AMPS_MotorMove(stMotorFrame.u8MotorAddr ,(AMPS_BWD) * GetCarRunRpm() * 10) ;
                        }
					}

				}
				
				//WriteTwoMotorRegs(stMotorFrame.u8MotorAddr,CTRL_ADDR,2,( stMotorFrame.u16RegData),3000);
				
			}
			
				
			
			
			if(STOP ==  ( stMotorFrame.u16RegData))
			{
				//sleep(1);
				g_bRunning_State = 0;
				Relay_MotorBreak(stMotorFrame.u8MotorAddr -1);
				AMPS_MotorStop(stMotorFrame.u8MotorAddr );
			}
			break;
		case 3:
			memcpy(&stMotorFrame,pData + sizeof(u8Mode),sizeof(frameBody));
			LOG(LOG_DEBUG,"SwitchoperaMode  stMotorFrame.u16RegAddr is %04x",stMotorFrame.u16RegAddr);
			if(stMotorFrame.u16RegAddr == RELAY_COM0_ADDR)
			{
				Relay_MotorUnbreak(RELAY_COM0_ADDR);
			}
			else if(stMotorFrame.u16RegAddr == RELAY_COM1_ADDR)
			{
				Relay_MotorUnbreak(RELAY_COM1_ADDR);
			}
		break;	
		default:
			break;
	}
	return ret;

}


void SwitchoperaModeProc(void * arg)
{
	assert(arg != NULL);
	CommandProcParam stCommandPara = *(CommandProcParam *)arg;
	SwitchoperaMode(stCommandPara.pData,stCommandPara.u16datalen);
}


/*
*函数名称:CarMovetoAim
*函数功能:小车运行到指定位置
*输   入: BYTE *pData  接收到的数据
*输   出:  无
*返   回: SUCCESS - 成功  ERROR - 错误
*/

int CarMovetoAim(BYTE *pData)
{
	CarMove stCarMove;
	memset(&stCarMove,0,sizeof(CarMove));
	memcpy(&stCarMove.i32StartDistence,pData,sizeof(stCarMove.i32StartDistence));
	//stCarMove.i32StartDistence = swap_u32(stCarMove.i32StartDistence);
	LOG(LOG_DEBUG,"CarMove to aim Distence is %d",stCarMove.i32StartDistence);
	Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power,RELAY_COM0_ADDR);//驱动器1上电
    sleep(1);
	g_bRunning_State = true;
	int timerfd = add_timer(MAX_WAITTIME * 5, 0, TIMER_ONCE, 0, RunningTimeout, "CarMovetoAim");
	//目标距离大于当前所在位置  
	//g_CarEncoderDistence   * g_HoriTotalSpace   (小车编码器每转一圈周长 * 	圈数)
	int32_t i32NeedToMoveDistence  = stCarMove.i32StartDistence + g_stSetCableway.stSetCablewayBody.i16StartPointDes;
	LOG(LOG_DEBUG,"Car  need to Move %d cm",i32NeedToMoveDistence);
	if(i32NeedToMoveDistence > (g_CarEncoderDistence   * g_HoriTotalSpace)  && g_bRunning_State)
	{
		Relay_MotorUnbreak(RELAY_COM0_ADDR);
		AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car,(AMPS_FWD)*(GetCarRunRpm() * 10)) ;
		//WriteTwoMotorRegs(g_stDeviceAddr.u8Motor_Drive_car,CTRL_ADDR,2,FWD,1500);//前进
		
		while( abs(i32NeedToMoveDistence - (g_CarEncoderDistence   * g_HoriTotalSpace) ) > g_CarDistenceAdjust )
		{
			LOG(LOG_INFO,"CarMove Position is %f",(g_CarEncoderDistence   * g_HoriTotalSpace));
		}
		mSleep(50);
		AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
		//DC_MotorMoveCtrl(g_stDeviceAddr.u8Motor_Drive_car, STOP);//停转
		// while( abs(i32NeedToMoveDistence - (g_CarEncoderDistence   * g_HoriTotalSpace)) > 5   && g_bRunning_State)
		// {
			
		// }
		Relay_MotorBreak(RELAY_COM0_ADDR);
		
		
		//sleep(1);
		
		return SUCCESS;
		
	}
	//目标距离小于当前所在位置
	else if(i32NeedToMoveDistence < (g_CarEncoderDistence   * g_HoriTotalSpace))
	{
		Relay_MotorUnbreak(RELAY_COM0_ADDR);
		
		AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car, (AMPS_BWD) * GetCarRunRpm() * 10) ;
		
		while(abs(i32NeedToMoveDistence - (g_CarEncoderDistence   * g_HoriTotalSpace)) > g_CarDistenceAdjust && g_bRunning_State )
		{
			LOG(LOG_INFO,"CarMove Position is %f",(g_CarEncoderDistence   * g_HoriTotalSpace));
		}
		mSleep(50);
		AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
		Relay_MotorBreak(RELAY_COM0_ADDR);
		return SUCCESS;

	}
	
	if(false != g_bRunning_State)
	{
		remove_timer(timerfd);
	}
	return ERROR;
	
}

void CarMovetoAimProc(void * arg)
{
	assert(arg != NULL);
	CommandProcParam stCommandPara = *(CommandProcParam *)arg;
    CarMovetoAim(stCommandPara.pData);
}



/*
*函数名称:FishMovetoAim
*函数功能:铅鱼运行到指定位置
*输   入: BYTE *pData  接收到的数据
*输   出:  无
*返   回: SUCCESS - 成功  ERROR - 错误
*/

int FishMovetoAim(BYTE *pData)
{
	FishMove stFishMove;
	memset(&stFishMove,0,sizeof(FishMove));
	memcpy(&stFishMove.u16FishPos,pData,sizeof(stFishMove.u16FishPos));
	//stFishMove.u16FishPos = swap_u16(stFishMove.u16FishPos);
	LOG(LOG_DEBUG,"stFishMove.u16FishPos is %04x",stFishMove.u16FishPos);
	
	Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power,RELAY_COM1_ADDR);//驱动器2上电
    sleep(1);
	g_bRunning_State = true;
	int timerfd = add_timer(MAX_WAITTIME* 2, 0, TIMER_ONCE, 0, RunningTimeout, "FishMovetoAim");
	//目标距离大于当前所在位置
	if(stFishMove.u16FishPos > g_FishEncoderDistence * g_VeroTotalSpace)
	{
		Relay_MotorUnbreak(RELAY_COM1_ADDR);
		//WriteTwoMotorRegs(g_stDeviceAddr.u8Motor_Drive_fish,CTRL_ADDR,2,DOWN,200);//下降
		AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish,(AMPS_FWD)*(GetFishRunRpm() * 10));
		while( abs(stFishMove.u16FishPos - g_FishEncoderDistence * g_VeroTotalSpace)  > g_FishDistenceAdjust  && g_bRunning_State )
		{
			
		}
		AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
		
		LOG(LOG_DEBUG,"Fish move to aim is end ,Move distence is %d",(uint16_t)g_FishEncoderDistence * g_VeroTotalSpace);
		Relay_MotorBreak(RELAY_COM1_ADDR);
		return SUCCESS;
		
	}
	//目标距离小于当前所在位置
	else if(stFishMove.u16FishPos <= g_FishEncoderDistence * g_VeroTotalSpace)
	{
		Relay_MotorUnbreak(RELAY_COM1_ADDR);
		AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish,(AMPS_BWD)*(GetFishRunRpm() * 10));//上升
		//WriteTwoMotorRegs(g_stDeviceAddr.u8Motor_Drive_fish,CTRL_ADDR,2,UP,1000 );//上升
		while(abs(stFishMove.u16FishPos - (g_FishEncoderDistence * g_VeroTotalSpace))  > g_FishDistenceAdjust && g_bRunning_State  )
		{
			

		}		
		Relay_MotorBreak(RELAY_COM1_ADDR);
		mSleep(50);
		AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
		//DC_MotorMoveCtrl(g_stDeviceAddr.u8Motor_Drive_fish, STOP);//停转

		LOG(LOG_DEBUG,"Fish move to aim is end ,Move distence is %d",(uint16_t)g_FishEncoderDistence * g_VeroTotalSpace);
		return SUCCESS;

	}
	if(false != g_bRunning_State)
	{
		remove_timer(timerfd);
	}
	return ERROR;
	
}

void FishMovetoAimProc(void * arg)
{
	assert(arg != NULL);
	CommandProcParam stCommandPara = *(CommandProcParam *)arg;
    FishMovetoAim(stCommandPara.pData);
}


void  FishMovetoPlatform(uint32_t platformdistence)
{
	/* [修改] 下放平台时增加转子触底DI判断，与电机限位并行 */
	LOG(LOG_DEBUG,"platform High is %d,now position is %f",platformdistence,g_FishEncoderDistence * g_VeroTotalSpace);
	if(platformdistence > g_FishEncoderDistence * g_VeroTotalSpace)
	{
		Relay_MotorUnbreak(RELAY_COM1_ADDR);
		AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish,(AMPS_FWD)* GetFishRunRpm() * 10);
		g_bRunning_State = true;
    	//int timerfd = add_timer(MAX_WAITTIME, 0, TIMER_ONCE, 0, RunningTimeout, "FishMovetoPlatform");
		while(g_bRunning_State )
		{
			if(abs(platformdistence - g_FishEncoderDistence * g_VeroTotalSpace)  <g_FishDistenceAdjust  )
			{

				break;
			}
			if(g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG || g_stSwitchStatus.u16TouchGround)
			{
				mSleep(50);
				
				if(g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG || g_stSwitchStatus.u16TouchGround)
				{
					LOG(LOG_DEBUG,"fish has Touch platform");
					g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG = 0;
					mSleep(50);
					AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
					Relay_MotorBreak(RELAY_COM1_ADDR);
					Relay_LightCtrl(GREEN_LIGHT,TURN_ON);
					break;
				}
			}
		}
		
		mSleep(50);
		Relay_MotorBreak(RELAY_COM1_ADDR);
		AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
		/* if(false != g_bRunning_State)
		{
			remove_timer(timerfd);
		} */
		return ;
		
	}
	else
	{
		mSleep(50);
		Relay_MotorBreak(RELAY_COM1_ADDR);
		AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
		return ;
	}
	
	
}



int GetCarConfigFile(uint32_t commandseri,int type)
{
	char configFileName[256];
    memset(configFileName,0,256);
    if(!GetAppDir(configFileName))
    {
        LOG(LOG_ERROR, "GetConfigAppDir Error");
        return ERROR;
    }
    sprintf(configFileName,"%s%s",configFileName,CONFIG_FILE_PATH);
    LOG(LOG_INFO,"config file Path is %s",configFileName);
    FILE* pFile = fopen(configFileName,"rb");
    if(NULL == pFile)
    {
        LOG(LOG_ERROR,"config file open error!");
        return ERROR;
    }
	// 获取文件大小
	struct stat st;
	if (stat(configFileName, &st) < 0) 
	{
		LOG(LOG_ERROR,"stat error!");
		return ERROR;
	}
	size_t file_size = st.st_size;
	LOG(LOG_DEBUG,"file size is %d",file_size);

	// 使用 ReportSubDeviceConfig + 柔性数组，一次分配
	uint32_t data_len = sizeof(ReportSubDeviceConfig) + file_size;
	ReportSubDeviceConfig *pReport = (ReportSubDeviceConfig *)malloc(data_len);
	if (!pReport) 
	{
		LOG(LOG_ERROR,"malloc error!");
		fclose(pFile);
		return ERROR;
	}
	memset(pReport, 0, data_len);

	pReport->stCommandHead.u32CommandSeri = g_u32CommansSeri++;
	pReport->stCommandHead.u32Command = REPORT_SUB_DEVICE_CONFIG;
	pReport->stCommandHead.u32CommandLen = sizeof(SeriverCommandHead) + sizeof(uint8_t) + sizeof(uint16_t) + file_size+ 4;
	memcpy(pReport->stCommandHead.au8StationID, g_aStationID, sizeof(g_aStationID));

	pReport->u8SubDevice = 3;  // 子设备类型固定为3
	pReport->u16FileLen = file_size;

	// 读取文件内容到柔性数组
	if (fread(pReport->u8pFileData, 1, file_size, pFile) != file_size)
	{
        LOG(LOG_ERROR,"fread error!");
		free(pReport);
		fclose(pFile);
		return ERROR;
    }
	fclose(pFile);

	LOG(LOG_DEBUG,"Need to Send Data is ");
	LOG_ARRAY(LOG_DEBUG,(uint8_t *)pReport,data_len,"%02x");
	
	SendMultiframe(UP_EXPAND_CODE, (uint8_t *)pReport, sizeof(SeriverCommandHead) + sizeof(uint8_t) + sizeof(uint16_t) + file_size, type);
	free(pReport);
	return SUCCESS;

}


/* 远程升级：协议文档命令字 26 + libota A/B 切槽 */

void  ProcFinalResult(BYTE *pData,uint16_t u16datalen)
{
	FinalResult_OnDownlink(pData, u16datalen);
}







/*
*函数名称:
*函数功能:
*输   入: struct AutoMeasure AutoMeasurePara  自动测量参数
*输   出:  SetCableway *pstSetCableway
*返   回: SUCCESS - 成功  ERROR - 错误
*/
MeasureMode GetMaeasureMode(AutoMeasure AutoMeasurePara)
{
	int i = 0;
	int getFlag = 0;
    uint32_t currentLevel = 0;
	MeasureMode ChoosedMode;//选择的测量模式
	memset(&ChoosedMode,0,sizeof(MeasureMode));
	
	for(i = 0;i< 24;i++)
    {
        ChoosedMode.stMeaModeHead.au16AutoMeaTime[i] = 0x0018 ;
    }
	
	currentLevel = GetCurrentWaterLevel();//获取当前水位
	
	LOG(LOG_INFO,"Curent water Level is %d, AutoMeasurePara ModeNum is",currentLevel,AutoMeasurePara.stAutoMeaHead.u8ModeNum);
	for(i = 0;i< AutoMeasurePara.stAutoMeaHead.u8ModeNum;i++)//遍历水位每个模式的水位，找到与当前水位匹配的测量模式
	{
		LOG(LOG_INFO,"stMode[%d].stMeaModeHead.u32StartWaterlevel is %d,stMode[%d].stMeaModeHead.u32EndWaterlevel is %d",i, AutoMeasurePara.stMode[i].stMeaModeHead.u32StartWaterlevel,i, AutoMeasurePara.stMode[i].stMeaModeHead.u32EndWaterlevel);
	    if(currentLevel >= ( AutoMeasurePara.stMode[i].stMeaModeHead.u32StartWaterlevel) && currentLevel < ( AutoMeasurePara.stMode[i].stMeaModeHead.u32EndWaterlevel))
	    {
			LOG(LOG_INFO," measure mode NO.%d is choosed",i +1);
	    	memcpy(&ChoosedMode,&AutoMeasurePara.stMode[i],sizeof(MeasureMode)) ;
			getFlag = 1;
	    }
	
	}

	if(0 == getFlag)
	{
		LOG(LOG_INFO,"No mode is choosen,please check water Level!");
	}
    return ChoosedMode;
}



//走航式ADCP流程处理
int VE_ADCPProc(uint32_t u32Destence)
{

	return SUCCESS;
}


/*
*函数名称:AutoMeasureCtrl
*函数功能:自动测量处理
*输   入: MeasureMode stMasureMode  自动测量模式
*输   出: 无
*返   回: SUCCESS - 成功  ERROR - 错误
*/
int AutoMeasureCtrl(MeasureMode stMeasureMode,int type)
{	
	LOG(LOG_INFO,"type is %d",type);
	LOG(LOG_DEBUG,"Start AutoMeasureCtrl");
	int templine = 0;
	//uint16_t HoriRpm = 0;
	//uint16_t VoriRpm = 0;
	uint16_t u16tempLoopNum = 0;
    int i32DowninWaterDeep = 0;//入水深度
    
	u16tempLoopNum = ( stMeasureMode.stMeaModeHead.u16LineNum);
	LOG(LOG_DEBUG,"Auto MeasureMode LineNum is %04d",( stMeasureMode.stMeaModeHead.u16LineNum));    
	uint32_t u32CurrentWaterLevel =   GetCurrentWaterLevel();
	
    Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power,RELAY_COM0_ADDR);//驱动器1上电
    sleep(2);
    Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power,RELAY_COM1_ADDR);//驱动器2上电
	sleep(2);
	while(0 !=  u16tempLoopNum )
    {   

		u16tempLoopNum -- ;
		memset(&g_DeepthPara,0 ,sizeof(ElemDeepth));
		g_DeepthPara = DeepthProc(stMeasureMode.stLine[templine]);//获取当前测线的高度
    	
    	//HoriRpm = GenarateRpm(( g_stSetCableway.stSetCablewayBody.u16HoriWheelDm),( g_stSetCableway.stSetCablewayBody.u16HoriSpeed));
		mSleep(50);
		Relay_MotorUnbreak(RELAY_COM0_ADDR);
		LOG(LOG_DEBUG,"1500 IS %d",1500);
		AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car,(AMPS_FWD) *  GetCarRunRpm() * 10) ;//前进
		
		//WriteTwoMotorRegs(g_stDeviceAddr.u8Motor_Drive_car,CTRL_ADDR,2,FWD,HoriRpm);
		LOG(LOG_DEBUG,"MeasureMode.Line[%d].i32StartDistance is %02x",templine,( stMeasureMode.stLine[templine].i32StartDistance));
		LOG(LOG_DEBUG,"car is moving forward");
		g_bRunning_State = true;
		//int timerfd1 = add_timer(MAX_WAITTIME * 5, 0, TIMER_ONCE, 0, RunningTimeout, NULL);
		float fcurrentpos = g_HoriTotalSpace * g_CarEncoderDistence;
		float faimpos = (float)stMeasureMode.stLine[templine].i32StartDistance + g_stSetCableway.stSetCablewayBody.i16StartPointDes;
		while(g_bRunning_State)
		{		
			mSleep(50);
			fcurrentpos = g_HoriTotalSpace * g_CarEncoderDistence;
			if(fabs( fcurrentpos  - faimpos)  < (float)SPEED_DOWN_DENTENCE )
			{
				AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car,(AMPS_FWD) *  GetCarRunRpm() * 5) ;//速度降低到正常的1/2
			}

			if( fabs( fcurrentpos  - faimpos)  < (float)g_CarDistenceAdjust   ||  (fcurrentpos  >= faimpos-  g_CarDistenceAdjust  ))// 当前位置与目标位置之间的差值小于调整值，或者当前位置大于目标位置
			{

				AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
				sleep(2);
				Relay_MotorBreak(RELAY_COM0_ADDR);				
				LOG(LOG_DEBUG,"Car is stop");
				break;
			}
			if(g_stHoriDIStatusFlag.u8FWD_LIMIT_FLAG)//到达正向限位（最远处）
			{
				LOG(LOG_DEBUG,"warinng!!!FWD_LIMITED!!!");
				mSleep(50);
				AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);//停转
				sleep(2);//电机减速时间2秒，延时2秒，避免电机急刹车
				Relay_MotorBreak(RELAY_COM0_ADDR);
				g_stHoriDIStatusFlag.u8FWD_LIMIT_FLAG = 0;
				LOG(LOG_DEBUG,"Car is stop");
				/* if(false != g_bRunning_State)
				{
					remove_timer(timerfd1);
				} */

				return ERROR;
			}
			LOG(LOG_INFO,"Car is moving Forward...");
			Read_DI_Status(g_stDeviceAddr.u8Motor_Drive_car);

		}
		
		while(1)
		{
			
			if(NULL == g_pDeepList)
			{
				LOG(LOG_ERROR,"DeepList is NULL");
				break;;
			}
			cur = g_pDeepList;
			LOG(LOG_DEBUG,"cur->data.u32Element is %08x,cur->data.u8Deepth is %02x",cur->data.u32Element,cur->data.u8Deepth);

			if( (0 == cur->data.u8Deepth) && (0 == (cur->data.u32Element & 0x01) )  &&  (0 == ( (cur->data.u32Element >> 3)& 0x01) ) )
			{	//深度为0，但是不是非接触式或者ADCP
				SLTPopFront(&g_pDeepList);
				LOG(LOG_ERROR,"deepth is 0,but not NCS or ADCP!");
				continue;
			}
			
			else if(((0 == cur->data.u8Deepth) && (cur->data.u32Element & 0x01)) || ((0 == cur->data.u8Deepth) && ( (cur->data.u32Element >> 3)& 0x01)))
			{
				//如果是非接触式或者或者ADCP
				LOG(LOG_DEBUG,"this is NCS  or ADCP deepth");
				if(cur->data.u32Element & 0x01) // 非接触式
				{
					
					sendMeasureResult(NCS,type);
				}
				
				while (1)
				{
					if((0 == g_SensorNum) || g_TimeoutFlag)
					{
						g_TimeoutFlag = 0;
						SLTPopFront(&g_pDeepList);
						break;
					}
				}
				
				continue;

			}
			
			////深度不为0
			mSleep(50);
			SectionPara stSectionArray[MAX_LINE_NUM];
			memset(stSectionArray,0 ,sizeof(SectionPara) * MAX_LINE_NUM);
			memcpy(stSectionArray,g_stSection.stSection,sizeof(SectionPara) * ( g_stSection.u16SectionNum));
			
			
			int i = 0;
			LOG(LOG_DEBUG,"Section  Num is %04x", g_stSection.u16SectionNum);
			
			for(i = 0;i< g_stSection.u16SectionNum;i++)
			{
				if( g_stSection.stSection[i].u32StartDestence == stMeasureMode.stLine[templine].i32StartDistance )
				{
					LOG(LOG_DEBUG," g_stSection.stSection[i].u32RiverBedElevation is %08x\n",( g_stSection.stSection[i].u32RiverBedElevation));
					i32DowninWaterDeep = GenerateinWaterDeepth( g_stSection.stSection[i].u32RiverBedElevation , u32CurrentWaterLevel , cur->data.u8Deepth);
					if(ERROR == i32DowninWaterDeep)
					{
						LOG(LOG_ERROR,"GenerateinWaterDeepth Error");
						break;
					}
					if((cur->data.u32Element & 0x01) ) //NCS
					{
						i32DowninWaterDeep = -(( g_stSetCableway.stSetCablewayBody.u32CableHight));
					}
					if( (cur->data.u32Element >>3) & 0x01 ) //ve_adcp
					{
						i32DowninWaterDeep = 0;
					}
					
				}
				
				
			}
			if(ERROR ==  i32DowninWaterDeep )
			{
				i32DowninWaterDeep = -( g_stSetCableway.stSetCablewayBody.u32CableHight);
			}
			LOG(LOG_DEBUG,"DowninWaterDeep is %d",i32DowninWaterDeep);
			//VoriRpm = GenarateRpm( g_stSetCableway.stSetCablewayBody.u16VeriEncoderDm, g_stSetCableway.stSetCablewayBody.u16VeriSpeed);
			Relay_MotorUnbreak(RELAY_COM1_ADDR);//松刹车
			AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish,(AMPS_FWD) *  GetFishRunRpm() * 10) ;//下降
			LOG(LOG_INFO,"cur->data.u8Deepth is %02x",cur->data.u8Deepth);
			LOG(LOG_INFO,"( g_stSetCableway.stSetCablewayBody.u32CableHight) is %08x",( g_stSetCableway.stSetCablewayBody.u32CableHight));
			g_bRunning_State = true;
			//int timerfd2 = add_timer(MAX_WAITTIME *3, 0, TIMER_ONCE, 0, RunningTimeout, NULL);
			int iFishNeedMove = i32DowninWaterDeep + g_stSetCableway.stSetCablewayBody.u32CableHight-GetCurrentWaterLevel()+FISH_BODY_HIGHT;
			LOG(LOG_DEBUG,"Fish Need to Down %d cm",iFishNeedMove);
			while(g_bRunning_State )
			{
				
				mSleep(50);
				if(fabs((g_VeroTotalSpace * g_FishEncoderDistence) - iFishNeedMove)  < SPEED_DOWN_DENTENCE)
				{
					AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish,(AMPS_FWD) *  GetFishRunRpm() * 3) ;//速度降低到正常速度30%
				}
				bool flag = fabs((g_VeroTotalSpace * g_FishEncoderDistence) - iFishNeedMove)  < g_FishDistenceAdjust;
				if(flag  || (g_VeroTotalSpace * g_FishEncoderDistence) > iFishNeedMove -g_FishDistenceAdjust)
				{
					LOG(LOG_DEBUG,"fish has arrived position");
					break;
				}
				/* [修改] 触底: 电机正转限位 + 转子采集板触底DI，防止钢丝绳全松 */
				else if(g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG || g_stSwitchStatus.u16TouchGround)
				{
					mSleep(50);
					
					if(g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG || g_stSwitchStatus.u16TouchGround)
					{
						LOG(LOG_DEBUG,"fish has Touch RiverBed");
						g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG = 0;
						mSleep(50);
						AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
						Relay_MotorBreak(RELAY_COM1_ADDR);
						Relay_LightCtrl(RED_LIGHT,TURN_ON);
						break;
					}
					
					
				}
				LOG(LOG_INFO,"Fish is moving down...\n");	
				Read_DI_Status(g_stDeviceAddr.u8Motor_Drive_fish);
				
			}//下降结束
			//remove_timer(timerfd2);
			mSleep(50);
			
			AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);			
			Relay_MotorBreak(RELAY_COM1_ADDR);		
			LOG(LOG_DEBUG,"Fish has Stop");	
			/* [修改] 取消固定sleep(20)，改为等第2个转子信号(传统缆道计流方式) */
			WaitRotatorSecondSignal();

			MeasureStart_sec = GetSysTime();
			struct tm now;
    		localtime_r(&MeasureStart_sec, &now);

			g_stResultPara.u8Year = now.tm_year + 1900 -2000;
			g_stResultPara.u8Month = now.tm_mon + 1;
			g_stResultPara.u8Day = now.tm_mday;
			g_stResultPara.u8Hour = now.tm_hour;
			g_stResultPara.u8Minutes = now.tm_min;
			g_stResultPara.u8Second = now.tm_sec;
			g_stResultPara.u32MeaElement = cur->data.u32Element;
			g_stResultPara.u16LineSeri = templine;
			g_stResultPara.u8Deepth = cur->data.u8Deepth;
			g_stResultPara.i32StartDistance = (stMeasureMode.stLine[templine].i32StartDistance);
							
			LOG(LOG_DEBUG,"g_stResultPara.u32MeaElement is %08x,g_stResultPara.u16LineSeri is %04x,g_stResultPara.i32StartDistance is %08x",
							g_stResultPara.u32MeaElement,
							g_stResultPara.u16LineSeri,
							g_stResultPara.i32StartDistance);
						
			
			
			ReadRotatorSpeedValue(NULL);//读取测量之前的值

			g_SensorNum ++;
			while (0 != g_stSensorFlag.u8Ratator_value_falg)//等待测量之前的值返回
			{
				
			}
			g_stResultPara.u32PrevSignalNum = g_stRotatorRecValue.u32Value;
			LOG(LOG_DEBUG,"PrevSignalNum is %d",g_stResultPara.u32PrevSignalNum);
			
			sleep(g_stRotatorConfig.u16Meaduration);//等待测量	

			ReadRotatorSpeedValue(NULL);
			sleep(3);
			g_SensorNum ++;
			g_GetResultFlag = 0;
			GetRatatorResult(&g_stResultPara);
			
			//GetMeasureResult(&g_stResultPara);
			
			int timerfd3 = add_timer(MAX_WAITTIME,0,TIMER_ONCE,0,TimeoutProc,NULL);
			
			//获取结果
			while (1)
			{
				if(0 ==  g_SensorNum || g_TimeoutFlag )
				{
					SendMeasureResultManu(g_stResultPara.u32MeaElement,DTU_TYPE);
					g_GetResultFlag = 0;
					g_TimeoutFlag = 0;
					
					break;
				}
			}	
			if(true != g_TimeoutFlag)	
			{
				remove_timer(timerfd3);
			}

			
			SLTPopFront(&g_pDeepList); // 当前深度测量完毕，去下一个深度
		}
		//单个测线测量完毕
		
		int64_t i64CurrentPos = g_FishEncoderDistence * g_VeroTotalSpace;//获取当前位置
		Relay_MotorUnbreak(RELAY_COM1_ADDR);
    	AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish,(AMPS_BWD)*(GetFishRunRpm() * 10));//上升
		sleep(3);
		g_bRunning_State = true;
		//int timerfd3 = add_timer(MAX_WAITTIME * 3, 0, TIMER_ONCE, 0, RunningTimeout, NULL);
		mSleep(50);
		while(!g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG && g_bRunning_State)
		{
			mSleep(50);
			Read_DI_Status(g_stDeviceAddr.u8Encoder_fish);
			LOG(LOG_INFO,"Fish is uping...,fish. u8REV_LIMIT_FLAG is %d",g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG);
			if(0 == u16tempLoopNum)//最后一根线测量完毕，则上升到最高处
			{
				if( g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG == 1)
				{
					break;
				}
			}
			else//测量其他线，上升到离水面50cm高
			{
				i64CurrentPos = g_FishEncoderDistence * g_VeroTotalSpace;
				if( i64CurrentPos <  g_stSetCableway.stSetCablewayBody.u32CableHight-GetCurrentWaterLevel() - 50)
				{
					
					break;
				}
			}
		}
		g_bRunning_State = false;
		AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);

		Relay_MotorBreak(RELAY_COM1_ADDR); 
		//remove_timer(timerfd3);
		g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG = 0;
		templine++;
		
		
		
		//SendMeasureResultManu(cur->data.u32Element,ZIGBEE_TYPE);
		

    }//end of  while(0 != stMeasureMode.u16LineNum )


    //整个测量完毕
	mSleep(50);
	LOG(LOG_DEBUG,"Measure End,car is going back***************");

	Relay_MotorUnbreak(RELAY_COM0_ADDR);
	mSleep(50);
	AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car, (AMPS_BWD) * GetCarRunRpm() * 10) ;//向后
	
	g_bRunning_State = true;
	//int timerfd4 = add_timer(MAX_WAITTIME * 5, 0, TIMER_ONCE, 0, RunningTimeout, NULL);
    while(g_bRunning_State)
	{
		
		mSleep(50);
		
		if( g_stHoriDIStatusFlag.u8REV_LIMIT_FLAG)
		{
			/* [修改] 测量结束回仓: 复位编码器，触摸屏小车位置归零 */
			fsm_resetCarHomePosition();
			break;
		}
		printf("Car is going back...\n");
		Read_DI_Status(g_stDeviceAddr.u8Motor_Drive_car);
		
	}
	/* if(false != g_bRunning_State)
	{
		remove_timer(timerfd4);
	} */
	mSleep(50);
	AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);//停转
	Relay_MotorBreak(RELAY_COM0_ADDR);
	/* [修改] 全部测线完成后回仓，下放铅鱼到平台高度 */
	FishMovetoPlatform(g_stSetCableway.stSetCablewayBody.u16PlatToCableHight);
	SendMeasureResultAuto(DTU_TYPE); //发送测量结果
	LOG(LOG_DEBUG,"Measure End,Car has stop");
	g_WebSendWaterLevel = 1000000;
    return SUCCESS;
}
/*
*函数名称:AddMeauCtrl
*函数功能:加测控制
*输   入: BYTE *pData,  数据     uint16_t u16datalen   数据长度
*输   出: 无
*返   回: SUCCESS - 成功  ERROR - 错误
*/


// //int AddMeauCtrl(BYTE *pData,uint16_t u16datalen,int type)
// void*  AddMeauCtrl(void * arg)
// {
// 	assert(arg != NULL);
// 	CommandProcParam stCommandPara = *(CommandProcParam *)arg;
// 	BYTE *pData = stCommandPara.pData;
// 	uint16_t u16datalen = stCommandPara.u16datalen;
// 	int type = stCommandPara.Devicetype;


// 	LOG(LOG_INFO,"type is %d",type);
// 	LOG(LOG_DEBUG,"start AddMeauCtrl,data len is %d",u16datalen);
// 	AddMeasureCtrl stAddmeau;
// 	int i = 0;
// 	uint32_t u32MeasElement = 0;
// 	if(ERROR ==   Fish_up())
// 	{
// 		return (void*)ERROR;
// 	} 
	
// 	 sleep(1);
// 	if(ERROR == Car_Goback_Home())
// 	{
// 		return (void*)ERROR;
// 	} 

// 	ReadSectionPara(&g_stSection);
// 	ReadCablewayPara(&g_stSetCableway);
// 	ReadAutoMeasurePara(&g_stAutoMeasure);
// 	ReadRotatorConfig(&g_stRotatorConfig);
	
// 	LOG(LOG_INFO,"g_stAutoMeasure.stMode[0].stLine[0].i32StartDistance is %08x",(  g_stAutoMeasure.stMode[0].stLine[0].i32StartDistance));
// 	g_StartMeaFlag= 1;
// 	memcpy(&g_WebSendWaterLevel,pData,sizeof(g_WebSendWaterLevel));
// 	LOG(LOG_DEBUG,"g_WebSendWaterLevel is %d",g_WebSendWaterLevel);
// 	g_ChoosedMode = GetMaeasureMode(g_stAutoMeasure);//获取当前测量模式
	
// 	for(i = 0;i<24 ;i++)
//     {
//         printf("au16AutoMeaTime[%d] is %04x\n",i,g_ChoosedMode.stMeaModeHead.au16AutoMeaTime[i]);
//     } 

// 	memcpy(&stAddmeau,pData,sizeof(AddMeasureCtrl));
	
// 	LOG(LOG_DEBUG,"AddMeasure  Speed Ctrl element  is %d",stAddmeau.u8SpeedCtrl);
// 	switch(stAddmeau.u8SpeedCtrl)
// 	{
// 		case 0://正常测流模式，不改变原有测量要素
// 			u32MeasElement = ALLDEVICE;
// 			break;
// 		case 1:
// 			u32MeasElement = NCS;//只非接触式转子测流
// 			break;
// 		case 2:
// 			u32MeasElement = ROTATOR;//只转子测流
// 			break;
// 		case 3:
// 			u32MeasElement = VE_ADCP;//只走航ADCP测流
// 			break;				
// 	}
// 	//g_ChoosedMode = GetMaeasureMode(g_stAutoMeasure);//获取当前测量模式
// 	LOG(LOG_DEBUG ,"AddMeaCtrl Final measureElement is %08x",u32MeasElement);
// 	for(i = 0;i< MAX_LINE_NUM;i++)
// 	{
// 		if(ALLDEVICE !=  u32MeasElement)
// 		{
// 			g_ChoosedMode.stLine[i].u32MeaElement = u32MeasElement ;
// 		}
		
// 	}
	
// 	if(SUCCESS ==AutoMeasureCtrl(g_ChoosedMode,type))//开始自动测量
// 	{
// 		ResetEncoder(g_stDeviceAddr.u8Encoder_fish);
// 		sleep(1);
// 		FishMovetoPlatform(g_stSetCableway.stSetCablewayBody.u16PlatToCableHight );
// 		ResetRotatorValue(NULL);//重置采集小板的值
// 	}
	

// 	return (void*)SUCCESS;
// }



/*
*函数名称:DeepthProc
*函数功能:当前测线的深度处理,按照从小到大排序,根据不同深度添加到链表里，相同深度不同要素则将要素添加至该深度的节点中
*输   入: MeasureLine stLine  测线参数
*输   出: 无
*返   回: DeepthPara  测线高度结构体数组
*/

ElemDeepth DeepthProc(MeasureLine stLine)
{
	ElemDeepth stDeepthPara;
	DeepthPara stParaArray[DEEP_PARA_NUM];
	SLTNode* pTempNode = NULL;
	int i = 0;
	int j = 0;
	memset(&stDeepthPara,0,sizeof(stDeepthPara));
	memset(stParaArray,0,sizeof(stParaArray));
	LOG(LOG_DEBUG,"DeepthProc stLine.u32MeaElement is %08x", stLine.u32MeaElement);//3f
	//there is problom

	if(stLine.u32MeaElement & 0x01 )
	{
		stDeepthPara.NCS.u32Element = NCS;
		stDeepthPara.NCS.u8Deepth = 0;
	}
	if((stLine.u32MeaElement>>1)  & 0x01)//转子
	{
		for(i = 0;i<4;i++)
		{
			
			stDeepthPara.RotatorDeepth[i].u32Element = ROTATOR;
			stDeepthPara.RotatorDeepth[i].u8Deepth = stLine.u32RotatorDeepth >> (i * 8);
			LOG(LOG_DEBUG,"RotatorDeepth[%d].u32Element is %08x;RotatorDeepth[%d].u8Deepth is %02X",i,stDeepthPara.RotatorDeepth[i].u32Element,i,stDeepthPara.RotatorDeepth[i].u8Deepth);
		
		}
	}
	if((stLine.u32MeaElement>>2)  & 0x01)//单波束ADCP
	{
		for(i = 0;i<4;i++)
		{
			
			stDeepthPara.ADCPDeepth[i].u32Element = ADCP;
			stDeepthPara.ADCPDeepth[i].u8Deepth = stLine.au16ADCPDeepth[i] >> 8;
		
		}
	}
	if((stLine.u32MeaElement>>3)  & 0x01) //走航ADCP
	{
		stDeepthPara.VE_ADCP.u32Element = VE_ADCP;
		stDeepthPara.VE_ADCP.u8Deepth = 0;
	}

	if((stLine.u32MeaElement>>4)  & 0x01)//泥沙
	{
		for(i = 0;i<4;i++)
		{
			
			stDeepthPara.MudsDeepth[i].u32Element = MUDS;
			stDeepthPara.MudsDeepth[i].u8Deepth = stLine.u32MudDeepth >> (i * 8);
		
		}
	}
	if((stLine.u32MeaElement>>5)  & 0x01)//水温
	{
		stDeepthPara.WaterDeepth.u32Element = TEMPRATURE;
		stDeepthPara.WaterDeepth.u8Deepth = stLine.u8WaterTempraDeepth;
	}
	
	memcpy(stParaArray,&stDeepthPara,sizeof(DeepthPara) * DEEP_PARA_NUM);
	for(i = 0;i< sizeof(stParaArray) /sizeof(DeepthPara );i++)
	{
		
		LOG(LOG_INFO,"Before sort stParaArray[%d].u32Element is %08x,stParaArray[%d].u8Deepth is %02x",i,stParaArray[i].u32Element,i,stParaArray[i].u8Deepth);
	}
	QuickSort(stParaArray,0,sizeof(stParaArray) /sizeof(DeepthPara ) - 1);
	SLTNode* cur = NULL;
	for(j = 0;j<sizeof(stParaArray) /sizeof(DeepthPara );j++)
	{
		LOG(LOG_INFO,"After sort stParaArray[%d].u32Element is %08x,stParaArray[%d].u8Deepth is %02x",j,stParaArray[j].u32Element,j,stParaArray[j].u8Deepth);
		/* 跳过 memset 空槽；转子/ADCP/泥沙未填的 0% 也不入链表，避免和 NCS 水面点合成后在仓顶开测 */
		if (stParaArray[j].u32Element == 0)
		{
			continue;
		}
		if ((stParaArray[j].u8Deepth == 0)
			&& ((stParaArray[j].u32Element & (ROTATOR | ADCP | MUDS | TEMPRATURE)) != 0)
			&& ((stParaArray[j].u32Element & (NCS | VE_ADCP)) == 0))
		{
			continue;
		}
		cur = SLTFind(g_pDeepList, stParaArray[j]);//如果链表中没有这个深度的节点，从尾部添加一个深度节点
		if(NULL == cur)
		{
			
			SLTPushBack(&g_pDeepList, stParaArray[j]);
		}
		else //如果链表中有这个深度节点，将要素信息添加到这个节点中,并修改链表
		{
			pTempNode = cur;
			pTempNode->data.u32Element = cur->data.u32Element;
			pTempNode->data.u32Element |= stParaArray[j].u32Element;
			pTempNode->data.u8Deepth = cur->data.u8Deepth;
			SLTChangeData(&g_pDeepList,cur->data,pTempNode->data);
		}	
	}

	SLTPrint(&g_pDeepList);
	return stDeepthPara;
}

int GenerateinWaterDeepth(uint32_t RiverBedHigh,uint32_t WaterLevel,uint8_t DeepPercent)
{
	int i32DownDeep = 0;
	LOG(LOG_DEBUG,"WaterLevel is %d,RiverBedHigh is %d,DeepPercent is %d\n",WaterLevel,RiverBedHigh,DeepPercent);
	if(WaterLevel < RiverBedHigh)
	{
		LOG(LOG_ERROR,"WaterLevel is lower than RiverBedHight!");
		return ERROR;
	}
	i32DownDeep =(int32_t) ((WaterLevel - RiverBedHigh) * (float)DeepPercent / 10);
	return i32DownDeep;
}

int GetCurrentPosRiverbedHigh(SetSection_new *pSetsection,uint32_t u32Startdentence)
{
	int i = 0;
	for(i = 0;i< pSetsection->u16SectionNum;i++)
	{
		if(u32Startdentence == pSetsection->stSection[i].u32StartDestence)
		{
			return pSetsection->stSection[i].u32RiverBedElevation;
		}
	}

	return ERROR;
}



void ReStart(BYTE *pData)
{
	Restart stResart;
	memset(&stResart,0,sizeof(Restart));
	memcpy(&stResart.u8Device,pData,sizeof(stResart.u8Device));
	if(CAR == stResart.u8Device)
	{
		sync();
    	reboot(RB_AUTOBOOT);
	}
	return ;
}





// 交换函数
void swap(DeepthPara *a, DeepthPara *b) {
    DeepthPara temp = *a;
    *a = *b;
    *b = temp;
}

// 分区函数
int partition(DeepthPara arr[], int low, int high) {
    uint8_t pivot = arr[high].u8Deepth;
    int i = (low - 1);
	int j = 0;
    for (j = low; j <= high - 1; j++) {
        if (arr[j].u8Deepth <= pivot) {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[high]);
    return (i + 1);
}

/*
*函数名称:QuickSort
*函数功能:将深度信息进行快速排序
*输   入: DeepthPara *a    深度信息数据, int low,   第一个元素的下标             int high   最后一个元素的下标
*输   出: DeepthPara *a    深度信息数据
*返   回: SUCCESS 成功
*/
void QuickSort(DeepthPara arr[], int low, int high) {
    if (low < high) {
        int pi = partition(arr, low, high);
        QuickSort(arr, low, pi - 1);
        QuickSort(arr, pi + 1, high);
    }
}

void TimeoutProc(void * arg)
{
	LOG(LOG_DEBUG,"TimeoutProc g_SensorNum is %d",g_SensorNum);
	if(0 != g_SensorNum )
	{
		g_TimeoutFlag = true;

		LOG(LOG_ERROR,"time out ,%d Sensor havent return result",g_SensorNum);
		g_SensorNum = 0;
	}
	
}


void GetMeasureResult(void *arg)
{

	RESULTPARA stresultpara = (RESULTPARA)arg;
	pthread_mutex_lock(&g_lock[g_stDevicelist.Sensor]);
	LOG(LOG_DEBUG,"Current Measure element is %08x,start distence is %08x",stresultpara->u32MeaElement,stresultpara->i32StartDistance);
	if(stresultpara->u32MeaElement &0x01)//非接触式测流类型
	{	
		
		g_SensorNum ++;

	}
	if( (stresultpara->u32MeaElement >>1)& 0x01 ) //转子流速仪类型
	{ 
		g_SensorNum ++;
		add_timer(g_stRotatorConfig.u16Meaduration * 1000,0,TIMER_ONCE,0,ReadRotatorSpeedValue,NULL);
		add_timer(g_stRotatorConfig.u16Meaduration * 1000 + 2000,0,TIMER_ONCE,0,GetRatatorResult,stresultpara);
		g_GetResultFlag = 0;	
		LOG(LOG_DEBUG,"g_SensorNum is %d",g_SensorNum);	
	}
	if( (stresultpara->u32MeaElement >>2)& 0x01) //ADCP类型
	{
		g_SensorNum ++;
		
	}
	if( (stresultpara->u32MeaElement >>3)& 0x01) //走航式ADCP类型
	{
		
		g_SensorNum ++;
	}
	if( (stresultpara->u32MeaElement >>4)& 0x01)//泥沙类型
	{
		g_SensorNum ++;
	}
	if( (stresultpara->u32MeaElement >>5)& 0x01)//温度类型
	{
		g_SensorNum ++;
	}

	pthread_mutex_unlock(&g_lock[g_stDevicelist.Sensor]);
	

}
void GetRatatorResult(void * arg)
{
	time_t sec;
	RESULTPARA stresultpara = (RESULTPARA)arg;
	uint32_t u32signalNum  = 0;
	sec = GetSysTime();
	struct tm now;
	localtime_r(&sec, &now);
	float temp = 0.0;
	memset(&g_stManuRotatorResultBody,0,sizeof(ReportManuRotatorSpeed));
	
	if(0 == g_stSensorFlag.u8Ratator_value_falg )
	{
		uint32_t value;
		g_stSensorFlag.u8Ratator_value_falg = 0;
		if(0 == g_stRotatorRecValue.u32Value)
		{
			value = 0;
		}
		else
		{
			
			u32signalNum = g_stRotatorRecValue.u32Value - stresultpara->u32PrevSignalNum;
			LOG(LOG_DEBUG,"stresultpara->u32PrevSignalNum is %d,current singalNum is %d,start distence is %08x",stresultpara->u32PrevSignalNum,g_stRotatorRecValue.u32Value,stresultpara->i32StartDistance);
			temp = g_stRotatorConfig.fa_value + g_stRotatorConfig.fb_value * (( (float)u32signalNum  /2 )* (float)g_stRotatorConfig.u16n_value / (float)(g_stRotatorConfig.u16Meaduration +3 )); 
			LOG(LOG_DEBUG,"temp is %f",temp);
			
			temp = temp *10000;
			value = (uint32_t)temp;
			LOG(LOG_DEBUG,"value is %d",value);
			if((value  %  10 )  % 2 == 0) 
			{
				value = value  / 10 ;

			}
			else
			{
				value = value  /10 + 1;
			}
			LOG(LOG_DEBUG,"value is %d",value);
		}
		


		g_stAutoRotatorArray[stresultpara->u16LineSeri].i32StartDistance = stresultpara->i32StartDistance;
		g_stAutoRotatorArray[stresultpara->u16LineSeri].U32WaterLevel = GetCurrentWaterLevel();
		g_stAutoRotatorArray[stresultpara->u16LineSeri].u8Year = stresultpara->u8Year;
		g_stAutoRotatorArray[stresultpara->u16LineSeri].u8Month = stresultpara->u8Month;
		g_stAutoRotatorArray[stresultpara->u16LineSeri].u8Day = stresultpara->u8Day;
		g_stAutoRotatorArray[stresultpara->u16LineSeri].u8Hour = stresultpara->u8Hour;
		g_stAutoRotatorArray[stresultpara->u16LineSeri].u8Minutes = stresultpara->u8Minutes;
		g_stAutoRotatorArray[stresultpara->u16LineSeri].u8Second = stresultpara->u8Second;
		g_stAutoRotatorArray[stresultpara->u16LineSeri].u16Speed = value;
		g_stAutoRotatorArray[stresultpara->u16LineSeri].u8Duration = g_stRotatorConfig.u16Meaduration;
		g_stAutoRotatorArray[stresultpara->u16LineSeri].u16SignalNum = u32signalNum / 2;
		insertSingleNodeByTail(&g_stAutoRotatorArray[stresultpara->u16LineSeri],g_pstAutoRotatorList);

		//
		g_stManuRotatorResultBody.u8Duration = g_stRotatorConfig.u16Meaduration;
		g_stManuRotatorResultBody.u16SignalNum = u32signalNum / 2;
		g_stManuRotatorResultBody.i32StartDistance = stresultpara->i32StartDistance;
		g_stManuRotatorResultBody.u8Deepth = stresultpara->u8Deepth;
		g_stManuRotatorResultBody.u16Speed = (uint16_t)value; 


	
		g_stTouchScreenReportData.u16SpeedData = (uint16_t)value ;
		g_stTouchScreenReportData.u16StartPosData = (uint16_t)(int16_t)stresultpara->i32StartDistance;
		g_GetResultFlag =1;


		g_stWaterFLowPara[stresultpara->u16LineSeri].u32StartDestence = stresultpara->i32StartDistance;
		int i = 0;
		for(i = 0;i<g_stSection.u16SectionNum;i++)
		{
			if(g_stSection.stSection[i].u32StartDestence == g_stWaterFLowPara[stresultpara->u16LineSeri].u32StartDestence   )
			{
				g_stWaterFLowPara[stresultpara->u16LineSeri].u32RiverBedElevation = g_stSection.stSection[i].u32RiverBedElevation;
			}
		}
		g_stWaterFLowPara[stresultpara->u16LineSeri].u16Speed = (uint16_t)value;
	}
	

}

/*
 * [2026-07-08 修改说明 - 本地流量计算 LogLocalWaterFlow]
 * 算法: 左岸+测线间(n-1)+右岸共n+1区；Q=Σround(部分面积×部分流速,2)
 * 左/右岸: round(raw×岸系数,2)；中间区: round((round(垂线左)+round(垂线右))/2,2)
 * 面积: 测线间profile积分合并(如10.2=5.1+5.1)；系数见tyls.conf
 */
/* 内部存储 cm（与网页/编码器一致）；转子流速 u16Speed = m/s × 1000 */
static float FlowSpeedRawToMs(uint16_t raw)
{
    return (float)raw / 1000.0f;
}

static float FlowCmToM(uint32_t cm)
{
    return (float)cm / 100.0f;
}

/*
 * [修改] 水文修约：四舍六入五成双；距离2位/垂线2位/面积1位/流量2位/平均流速3位
 */
#define FLOW_ROUND_AREA_DEC     1
#define FLOW_ROUND_VEL_DEC      2   /* 垂线/部分平均流速 */
#define FLOW_ROUND_Q_DEC        2
#define FLOW_ROUND_DIST_DEC     2   /* 起点距、间距、干滩长度 */
#define FLOW_ROUND_AVG_VEL_DEC  3   /* 断面平均流速 */

static double FlowDecScale(int decimals)
{
    static const double tbl[] = {1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0};

    if (decimals < 0)
        decimals = 0;
    if (decimals > 5)
        decimals = 5;
    return tbl[decimals];
}

/*
 * 水文修约：四舍六入五成双（奇进偶不进）
 */
static float FlowRoundHydrol(int decimals, float val)
{
    double scale;
    double x;
    double ip;
    double frac;
    long long intpart;
    int d1;
    double tail;

    if (val == 0.0f)
        return 0.0f;

    scale = FlowDecScale(decimals);
    x = (double)val * scale;
    if (x < 0.0)
        return 0.0f;

    frac = modf(x + 1e-12, &ip);
    intpart = (long long)ip;
    d1 = (int)floor(frac * 10.0 + 1e-9);
    if (d1 < 5)
        return (float)((double)intpart / scale);
    if (d1 > 5)
        return (float)((double)(intpart + 1) / scale);

    tail = frac * 10.0 - (double)d1;
    if (tail > 1e-9)
        return (float)((double)(intpart + 1) / scale);
    if ((intpart & 1LL) == 0)
        return (float)((double)intpart / scale);
    return (float)((double)(intpart + 1) / scale);
}

static float FlowRoundAreaM2(float areaM2)
{
    return FlowRoundHydrol(FLOW_ROUND_AREA_DEC, areaM2);
}

static float FlowRoundVelMs(float velMs)
{
    return FlowRoundHydrol(FLOW_ROUND_VEL_DEC, velMs);
}

static float FlowRoundDistM(float distM)
{
    return FlowRoundHydrol(FLOW_ROUND_DIST_DEC, distM);
}

static float FlowRoundAvgVelMs(float velMs)
{
    return FlowRoundHydrol(FLOW_ROUND_AVG_VEL_DEC, velMs);
}

static float FlowDistCmToM(uint32_t cm)
{
    return FlowRoundDistM(FlowCmToM(cm));
}

static uint32_t FlowRoundDistCm(uint32_t cm)
{
    float m = FlowDistCmToM(cm);
    return (uint32_t)(m * 100.0f + 0.5f);
}

static float FlowRoundQM3s(float qM3s)
{
    return FlowRoundHydrol(FLOW_ROUND_Q_DEC, qM3s);
}

/* [修改] 修约后Q×1000写入触摸屏u16WaterFlow */
static uint16_t FlowM3sToTouchRaw(float qM3s)
{
    float q;
    float raw;

    q = FlowRoundQM3s(qM3s);
    if (q <= 0.0f)
        return 0;
    raw = q * 1000.0f;
    if (raw > 65535.0f)
        return 65535;
    return (uint16_t)(raw + 0.5f);
}

/* [修改] 加测结束将本地Q推送到触摸屏，不依赖平台下发 */
static void FlowPushLocalToTouchScreen(float qM3s, float areaM2, float avgVMs)
{
    uint16_t flowRaw = FlowM3sToTouchRaw(qM3s);

    g_stTouchScreenReportData.u16WaterFlow = flowRaw;
    TouchScreenPushReportData();
    LOG(LOG_INFO,
        "TouchScreen local flow: Q=%.2f m3/s (raw=%u), area=%.1f m2, avgV=%.3f m/s",
        FlowRoundQM3s(qM3s), flowRaw, FlowRoundAreaM2(areaM2), FlowRoundAvgVelMs(avgVMs));
}

/* 按标准流量计算表（系数见 tyls.conf: FlowLeftBankCoef 等）：
 * 左/右岸部分平均流速 = 测速垂线垂线平均 × 岸边系数
 * 中间部分平均流速 = (v左 + v右) / 2
 * 部分面积 = 部分平均水深 × 间距
 */
static float FlowFindBankRates(float *pLeftDryLenM, float *pRightDryLenM,
                               uint32_t *pLeftSeri, uint32_t *pRightSeri)
{
    uint32_t leftSeri = 0;
    uint32_t rightSeri = g_stSection.u16SectionNum - 1;
    float leftRate = 0.0f;
    float rightRate = 0.0f;
    int i;

    for (i = 0; i < (int)g_stSection.u16SectionNum - 1; i++)
    {
        if (g_stSection.stSection[i + 1].u32RiverBedElevation <
            g_stSection.stSection[i].u32RiverBedElevation)
        {
            leftSeri = (uint32_t)i;
            break;
        }
    }

    for (i = (int)g_stSection.u16SectionNum - 1; i > 0; i--)
    {
        if (g_stSection.stSection[i - 1].u32RiverBedElevation <
            g_stSection.stSection[i].u32RiverBedElevation)
        {
            rightSeri = (uint32_t)i;
            break;
        }
    }

    if (g_stSection.stSection[leftSeri].u32StartDestence !=
        g_stSection.stSection[leftSeri + 1].u32StartDestence)
    {
        float bedDiff = (float)((int32_t)g_stSection.stSection[leftSeri].u32RiverBedElevation -
                                (int32_t)g_stSection.stSection[leftSeri + 1].u32RiverBedElevation);
        float distDiff = (float)((int32_t)g_stSection.stSection[leftSeri + 1].u32StartDestence -
                                 (int32_t)g_stSection.stSection[leftSeri].u32StartDestence);
        leftRate = fabsf(bedDiff / distDiff);
    }

    if (g_stSection.stSection[rightSeri].u32StartDestence !=
        g_stSection.stSection[rightSeri - 1].u32StartDestence)
    {
        float bedDiff = (float)((int32_t)g_stSection.stSection[rightSeri].u32RiverBedElevation -
                                (int32_t)g_stSection.stSection[rightSeri - 1].u32RiverBedElevation);
        float distDiff = (float)((int32_t)g_stSection.stSection[rightSeri].u32StartDestence -
                                 (int32_t)g_stSection.stSection[rightSeri - 1].u32StartDestence);
        rightRate = fabsf(bedDiff / distDiff);
    }

    *pLeftSeri = leftSeri;
    *pRightSeri = rightSeri;
    if (pLeftDryLenM)
    {
        if (leftRate > 0.0f)
        {
            float dryLenMm = fabsf((float)((int32_t)g_stSection.stSection[leftSeri].u32RiverBedElevation -
                                           (int32_t)GetCurrentWaterLevel())) / leftRate;
            *pLeftDryLenM = dryLenMm / 100.0f;
        }
        else
        {
            *pLeftDryLenM = 0.0f;
        }
    }
    if (pRightDryLenM)
    {
        if (rightRate > 0.0f)
        {
            float dryLenMm = fabsf((float)((int32_t)g_stSection.stSection[rightSeri].u32RiverBedElevation -
                                           (int32_t)GetCurrentWaterLevel())) / rightRate;
            *pRightDryLenM = dryLenMm / 100.0f;
        }
        else
        {
            *pRightDryLenM = 0.0f;
        }
    }
    return leftRate;
}

static uint32_t FlowMatchToleranceCm(void)
{
    if (g_stFlowCalcConfig.u32MatchTolCm == 0)
        return 30;
    return g_stFlowCalcConfig.u32MatchTolCm;
}

static int FlowDistNearCm(uint32_t a, uint32_t b, uint32_t tolCm)
{
    uint32_t diff = (a >= b) ? (a - b) : (b - a);
    return diff <= tolCm;
}

static int FlowIsBankDistCm(uint32_t distCm, uint32_t leftSeri, uint32_t rightSeri,
                            uint32_t rightEdgeDist)
{
    uint32_t tol = FlowMatchToleranceCm();
    int n = (int)g_stSection.u16SectionNum;

    if (leftSeri < (uint32_t)n &&
        FlowDistNearCm(distCm, g_stSection.stSection[leftSeri].u32StartDestence, tol))
        return 1;
    if (rightSeri < (uint32_t)n &&
        FlowDistNearCm(distCm, g_stSection.stSection[rightSeri].u32StartDestence, tol))
        return 1;
    if (rightEdgeDist > 0 && FlowDistNearCm(distCm, rightEdgeDist, tol))
        return 1;
    return 0;
}

/*
 * [修改] 剔除断面脏数据：误把测线起点距写入河底高程时(如775cm)，主槽内按bed=0处理
 */
static uint32_t FlowSanitizeBedCm(uint32_t distCm, uint32_t bedCm,
                                  uint32_t leftSeri, uint32_t rightSeri,
                                  uint32_t leftBankBed, uint32_t rightBankBed,
                                  uint32_t rightEdgeDist)
{
    uint32_t wl;
    uint32_t bankCap;

    if (bedCm == 0)
        return 0;

    wl = GetCurrentWaterLevel();

    if (bedCm > wl + 300)
        return 0;

    if (!FlowIsBankDistCm(distCm, leftSeri, rightSeri, rightEdgeDist) && bedCm > wl)
        return 0;

    bankCap = (leftBankBed > rightBankBed) ? leftBankBed : rightBankBed;
    if (FlowIsBankDistCm(distCm, leftSeri, rightSeri, rightEdgeDist) &&
        bankCap > 0 && bedCm > bankCap + FlowMatchToleranceCm())
        return bankCap;

    return bedCm;
}

/*
 * 按断面测深点插值河底高程(cm)。
 * 左/右岸为 2.60m、主槽为 0 时，dist=0 且 bed=0 多为左水边缺数，用左岸点高程补全。
 */
static uint32_t FlowGetBedAtDistCm(uint32_t distCm, uint32_t leftSeri, uint32_t rightSeri)
{
    int n = (int)g_stSection.u16SectionNum;
    int i;
    int lo = -1;
    int hi = -1;
    uint32_t leftBankBed = 0;
    uint32_t rightBankBed = 0;
    uint32_t rightEdgeDist = 0;
    uint32_t bed;

    if (n <= 0)
        return 0;

    rightEdgeDist = g_stSection.stSection[n - 1].u32StartDestence;
    if (leftSeri < (uint32_t)n)
        leftBankBed = g_stSection.stSection[leftSeri].u32RiverBedElevation;
    if (rightSeri < (uint32_t)n)
        rightBankBed = g_stSection.stSection[rightSeri].u32RiverBedElevation;

    for (i = 0; i < n; i++)
    {
        if (g_stSection.stSection[i].u32StartDestence != distCm)
            continue;
        if (distCm == 0 && leftBankBed > 0)
            return leftBankBed;
        if (g_stSection.stSection[i].u32RiverBedElevation > 0)
        {
            bed = FlowSanitizeBedCm(distCm, g_stSection.stSection[i].u32RiverBedElevation,
                                    leftSeri, rightSeri, leftBankBed, rightBankBed,
                                    rightEdgeDist);
            if (bed > 0)
                return bed;
            if (!FlowIsBankDistCm(distCm, leftSeri, rightSeri, rightEdgeDist))
                return 0;
        }
        else
            return 0;
    }

    if (distCm == 0 && leftBankBed > 0)
        return leftBankBed;

    if (rightEdgeDist > 0 &&
        distCm + FlowMatchToleranceCm() >= rightEdgeDist &&
        rightBankBed > 0)
        return rightBankBed;

    for (i = 0; i < n; i++)
    {
        uint32_t sd = g_stSection.stSection[i].u32StartDestence;
        if (sd == 0 && i != 0)
            continue;
        if (sd <= distCm)
            lo = i;
        if (sd >= distCm && hi < 0)
            hi = i;
    }

    if (lo >= 0 && hi >= 0 && lo != hi)
    {
        uint32_t d0 = g_stSection.stSection[lo].u32StartDestence;
        uint32_t d1 = g_stSection.stSection[hi].u32StartDestence;
        uint32_t b0 = g_stSection.stSection[lo].u32RiverBedElevation;
        uint32_t b1 = g_stSection.stSection[hi].u32RiverBedElevation;

        if (d0 == 0 && b0 == 0 && leftBankBed > 0)
            b0 = leftBankBed;
        b0 = FlowSanitizeBedCm(d0, b0, leftSeri, rightSeri, leftBankBed, rightBankBed,
                               rightEdgeDist);
        b1 = FlowSanitizeBedCm(d1, b1, leftSeri, rightSeri, leftBankBed, rightBankBed,
                               rightEdgeDist);
        if (d1 != d0)
        {
            return b0 + (uint32_t)((uint64_t)(b1 - b0) * (distCm - d0) / (d1 - d0));
        }
    }

    if (lo >= 0)
    {
        uint32_t sd = g_stSection.stSection[lo].u32StartDestence;
        uint32_t b = g_stSection.stSection[lo].u32RiverBedElevation;
        b = FlowSanitizeBedCm(sd, b, leftSeri, rightSeri, leftBankBed, rightBankBed,
                              rightEdgeDist);
        return (b > 0) ? b : 0;
    }
    return 0;
}

static uint32_t FlowGetMeasureLineDist(int lineIdx)
{
    if (lineIdx >= 0 && lineIdx < g_addMeasureCtx.totalLineCount)
    {
        if (g_stWaterFLowPara[lineIdx].u32StartDestence != 0)
            return g_stWaterFLowPara[lineIdx].u32StartDestence;
        if (g_stAutoRotatorArray[lineIdx].i32StartDistance > 0)
            return (uint32_t)g_stAutoRotatorArray[lineIdx].i32StartDistance;
        if (g_addMeasureCtx.measureMode.stLine[lineIdx].i32StartDistance > 0)
            return (uint32_t)g_addMeasureCtx.measureMode.stLine[lineIdx].i32StartDistance;
    }
    return 0;
}

static uint16_t FlowGetMeasureLineSpeedRaw(int lineIdx)
{
    if (lineIdx >= 0 && lineIdx < g_addMeasureCtx.totalLineCount)
    {
        if (g_stWaterFLowPara[lineIdx].u16Speed > 0)
            return g_stWaterFLowPara[lineIdx].u16Speed;
        if (g_stAutoRotatorArray[lineIdx].u16Speed > 0)
            return g_stAutoRotatorArray[lineIdx].u16Speed;
    }
    return 0;
}

static float FlowGetLineCoef(uint32_t dist, int num_lines)
{
    (void)dist;
    (void)num_lines;
    if (g_stFlowCalcConfig.fLineCoef <= 0.0f)
        return 1.0f;
    return g_stFlowCalcConfig.fLineCoef;
}

typedef struct {
    uint32_t distMm;
    uint32_t bedMm;
} FlowProfilePt;

static int FlowProfileAddPoint(FlowProfilePt *pts, int *cnt, int maxPts,
                               uint32_t dist, uint32_t bed)
{
    int i;

    for (i = 0; i < *cnt; i++)
    {
        uint32_t d = (pts[i].distMm > dist) ? (pts[i].distMm - dist) : (dist - pts[i].distMm);
        if (d < 50)
        {
            if (bed > 0)
                pts[i].bedMm = bed;
            return *cnt;
        }
    }
    if (*cnt >= maxPts)
        return *cnt;
    pts[*cnt].distMm = dist;
    pts[*cnt].bedMm = bed;
    (*cnt)++;
    return *cnt;
}

static int FlowProfilePtCompare(const void *a, const void *b)
{
    const FlowProfilePt *pa = (const FlowProfilePt *)a;
    const FlowProfilePt *pb = (const FlowProfilePt *)b;
    if (pa->distMm < pb->distMm)
        return -1;
    if (pa->distMm > pb->distMm)
        return 1;
    return 0;
}

/* 合并断面测深点 + 测线位置（测几条线就有几个测速点，不要求与断面点重合） */
static int FlowBuildProfile(int num_lines, FlowProfilePt *pts, int maxPts)
{
    int cnt = 0;
    int i;
    int k;
    int n = (int)g_stSection.u16SectionNum;

    for (i = 0; i < n; i++)
    {
        uint32_t sd = g_stSection.stSection[i].u32StartDestence;
        uint32_t bed = g_stSection.stSection[i].u32RiverBedElevation;
        if (sd == 0 && i != 0)
            continue;
        FlowProfileAddPoint(pts, &cnt, maxPts, sd, bed);
    }

    for (k = 0; k < num_lines; k++)
    {
        uint32_t md = FlowGetMeasureLineDist(k);
        uint32_t bed = g_stWaterFLowPara[k].u32RiverBedElevation;
        if (md == 0)
            continue;
        if (bed == md)
            bed = 0; /* [修改] bed==起点距视为未填河底 */
        if (bed == 0)
        {
            for (i = 0; i < n; i++)
            {
                if (g_stSection.stSection[i].u32StartDestence == md)
                {
                    bed = g_stSection.stSection[i].u32RiverBedElevation;
                    break;
                }
            }
        }
        FlowProfileAddPoint(pts, &cnt, maxPts, md, bed);
    }

    if (n > 0)
    {
        uint32_t rightDist = g_stSection.stSection[n - 1].u32StartDestence;
        uint32_t rightBed = g_stSection.stSection[n - 1].u32RiverBedElevation;
        if (rightDist > 0)
            FlowProfileAddPoint(pts, &cnt, maxPts, rightDist, rightBed);
    }

    if (cnt > 1)
        qsort(pts, (size_t)cnt, sizeof(FlowProfilePt), FlowProfilePtCompare);
    return cnt;
}

static void FlowApplySectionBed(FlowProfilePt *pts, int ptCnt,
                                uint32_t leftSeri, uint32_t rightSeri)
{
    int i;
    for (i = 0; i < ptCnt; i++)
        pts[i].bedMm = FlowGetBedAtDistCm(pts[i].distMm, leftSeri, rightSeri);
}

/* [修改] 保留原始测点流速(m/s)，不在加载时修约；垂线平均在分区时修约 */
static int FlowLoadMeasureLinesRaw(int num_lines, uint32_t *mDist, float *mVelRaw)
{
    int k;
    int n = 0;

    for (k = 0; k < num_lines; k++)
    {
        uint32_t d = FlowGetMeasureLineDist(k);
        uint16_t raw = FlowGetMeasureLineSpeedRaw(k);
        if (d == 0 || raw == 0)
            continue;
        mDist[n] = FlowRoundDistCm(d);
        mVelRaw[n] = FlowSpeedRawToMs(raw) * FlowGetLineCoef(d, num_lines);
        n++;
    }

    for (k = 0; k < n - 1; k++)
    {
        int j;
        for (j = k + 1; j < n; j++)
        {
            if (mDist[j] < mDist[k])
            {
                uint32_t td = mDist[k];
                float tv = mVelRaw[k];
                mDist[k] = mDist[j];
                mVelRaw[k] = mVelRaw[j];
                mDist[j] = td;
                mVelRaw[j] = tv;
            }
        }
    }
    return n;
}

static float FlowDepthAtProfileCm(uint32_t distCm, const FlowProfilePt *profile,
                                  int ptCnt, uint32_t wl)
{
    int i;
    uint32_t bed;

    if (ptCnt <= 0)
        return 0.0f;

    if (distCm <= profile[0].distMm)
    {
        bed = profile[0].bedMm;
        return FlowCmToM(wl > bed ? wl - bed : 0);
    }

    for (i = 0; i < ptCnt - 1; i++)
    {
        uint32_t d0 = profile[i].distMm;
        uint32_t d1 = profile[i + 1].distMm;

        if (distCm > d1)
            continue;
        if (distCm == d0)
            bed = profile[i].bedMm;
        else if (distCm == d1)
            bed = profile[i + 1].bedMm;
        else if (d1 != d0)
        {
            bed = profile[i].bedMm +
                (uint32_t)((uint64_t)(profile[i + 1].bedMm - profile[i].bedMm) *
                           (distCm - d0) / (d1 - d0));
        }
        else
            bed = profile[i].bedMm;
        return FlowCmToM(wl > bed ? wl - bed : 0);
    }

    bed = profile[ptCnt - 1].bedMm;
    return FlowCmToM(wl > bed ? wl - bed : 0);
}

/* 累计 profile 上 [zoneLeftCm, zoneRightCm] 内的部分面积（间距/水深修约后） */
static float FlowZoneAreaBetween(uint32_t zoneLeftCm, uint32_t zoneRightCm,
                                 const FlowProfilePt *profile, int ptCnt,
                                 uint32_t wl)
{
    float total = 0.0f;
    int i;

    if (zoneRightCm <= zoneLeftCm || ptCnt < 2)
        return 0.0f;

    for (i = 0; i < ptCnt - 1; i++)
    {
        uint32_t segL = profile[i].distMm;
        uint32_t segR = profile[i + 1].distMm;
        uint32_t oL = (segL > zoneLeftCm) ? segL : zoneLeftCm;
        uint32_t oR = (segR < zoneRightCm) ? segR : zoneRightCm;
        float spacingM;
        float depthLM;
        float depthRM;
        float avgDepthM;
        float partArea;

        if (oR <= oL)
            continue;

        spacingM = FlowRoundDistM(FlowCmToM(oR - oL));
        depthLM = FlowDepthAtProfileCm(oL, profile, ptCnt, wl);
        depthRM = FlowDepthAtProfileCm(oR, profile, ptCnt, wl);
        avgDepthM = (depthLM + depthRM) / 2.0f;
        if (avgDepthM <= 0.0f)
            continue;

        partArea = FlowRoundAreaM2(spacingM * avgDepthM);
        total += partArea;
    }
    return FlowRoundAreaM2(total);
}

static void FlowDumpMeasureContext(int num_lines, int nMeas,
                                  const uint32_t *mDist, const float *mVelRaw)
{
    int i;

    LOG(LOG_DEBUG,
        "LogLocalWaterFlow: measLines=%d speedLines=%d matchTol=%ucm (4-zone, raw speed)",
        num_lines, nMeas, FlowMatchToleranceCm());
    for (i = 0; i < nMeas; i++)
    {
        LOG(LOG_DEBUG,
            "  speedLine[%d] dist=%.2fm raw=%.3f avg=%.2f m/s",
            i, FlowDistCmToM(mDist[i]), mVelRaw[i], FlowRoundVelMs(mVelRaw[i]));
    }
}

/*
 * 本地流量：平台同款 4 区法 + 原始测点流速，修约后推触摸屏
 * 左/右岸：raw×岸系数；中间区：round((round(垂线左)+round(垂线右))/2)；面积按测线间合并
 */
void LogLocalWaterFlow(int num_lines)
{
    int z;
    int nMeas;
    int nZones;
    uint32_t wl;
    float leftDryLenM = 0.0f;
    float rightDryLenM = 0.0f;
    uint32_t leftSeri = 0;
    uint32_t rightSeri = 0;
    uint32_t leftWaterCm;
    uint32_t rightWaterCm;
    uint32_t rightEdgeCm;
    float totalFlow = 0.0f;
    float totalArea = 0.0f;
    FlowProfilePt profile[MAX_LINE_NUM * 2];
    uint32_t mDist[MAX_LINE_NUM];
    float mVelRaw[MAX_LINE_NUM];
    int ptCnt;

    ReadSectionPara_new(&g_stSection_new);

    if (num_lines <= 0 || g_stSection.u16SectionNum < 2)
    {
        LOG(LOG_DEBUG, "LogLocalWaterFlow: skip, num_lines=%d sectionNum=%u",
            num_lines, g_stSection.u16SectionNum);
        return;
    }

    wl = GetCurrentWaterLevel();
    ptCnt = FlowBuildProfile(num_lines, profile, MAX_LINE_NUM * 2);
    nMeas = FlowLoadMeasureLinesRaw(num_lines, mDist, mVelRaw);
    if (ptCnt < 2 || nMeas <= 0)
    {
        LOG(LOG_DEBUG, "LogLocalWaterFlow: skip, profilePts=%d measSpeedLines=%d",
            ptCnt, nMeas);
        return;
    }

    FlowFindBankRates(&leftDryLenM, &rightDryLenM, &leftSeri, &rightSeri);
    leftDryLenM = FlowRoundDistM(leftDryLenM);
    rightDryLenM = FlowRoundDistM(rightDryLenM);
    FlowApplySectionBed(profile, ptCnt, leftSeri, rightSeri);
    FlowDumpMeasureContext(num_lines, nMeas, mDist, mVelRaw);

    rightEdgeCm = g_stSection.stSection[g_stSection.u16SectionNum - 1].u32StartDestence;
    leftWaterCm = (uint32_t)(leftDryLenM * 100.0f + 0.5f);
    rightWaterCm = (rightEdgeCm > (uint32_t)(rightDryLenM * 100.0f + 0.5f)) ?
        (rightEdgeCm - (uint32_t)(rightDryLenM * 100.0f + 0.5f)) : rightEdgeCm;

    nZones = nMeas + 1;

    LOG(LOG_DEBUG,
        "LogLocalWaterFlow: begin wl=%.3fm leftWater=%.2fm rightWater=%.2fm "
        "bankCoef L/R=%.2f/%.2f lineCoef=%.2f zones=%d",
        FlowCmToM(wl), leftDryLenM, FlowCmToM(rightWaterCm),
        g_stFlowCalcConfig.fLeftBankCoef, g_stFlowCalcConfig.fRightBankCoef,
        g_stFlowCalcConfig.fLineCoef, nZones);

    for (z = 0; z < nZones; z++)
    {
        uint32_t zoneLeftCm;
        uint32_t zoneRightCm;
        float zoneArea;
        float zoneVel;
        float partFlow;
        const char *zoneName;

        if (z == 0)
        {
            zoneLeftCm = leftWaterCm;
            zoneRightCm = mDist[0];
            zoneVel = FlowRoundVelMs(mVelRaw[0] * g_stFlowCalcConfig.fLeftBankCoef);
            zoneName = "leftBank";
        }
        else if (z == nZones - 1)
        {
            zoneLeftCm = mDist[nMeas - 1];
            zoneRightCm = rightWaterCm;
            zoneVel = FlowRoundVelMs(mVelRaw[nMeas - 1] *
                                     g_stFlowCalcConfig.fRightBankCoef);
            zoneName = "rightBank";
        }
        else
        {
            float vLineL;
            float vLineR;

            zoneLeftCm = mDist[z - 1];
            zoneRightCm = mDist[z];
            /* 中间区：先修约垂线平均，再求部分平均（与平台表一致） */
            vLineL = FlowRoundVelMs(mVelRaw[z - 1]);
            vLineR = FlowRoundVelMs(mVelRaw[z]);
            zoneVel = FlowRoundVelMs((vLineL + vLineR) / 2.0f);
            zoneName = "mid";
        }

        zoneArea = FlowZoneAreaBetween(zoneLeftCm, zoneRightCm, profile, ptCnt, wl);
        if (zoneArea <= 0.0f || zoneVel <= 0.0f)
            continue;

        partFlow = FlowRoundQM3s(zoneArea * zoneVel);
        totalArea += zoneArea;
        totalFlow += partFlow;

        LOG(LOG_DEBUG,
            "  zone[%d] %s dist %.2f~%.2f m, vPart=%.2f, area %.1f m2, dQ %.2f m3/s",
            z, zoneName, FlowDistCmToM(zoneLeftCm), FlowDistCmToM(zoneRightCm),
            zoneVel, zoneArea, partFlow);
    }

    {
        float avgVMs;
        float qTotal;
        float areaTotal;

        areaTotal = FlowRoundAreaM2(totalArea);
        qTotal = FlowRoundQM3s(totalFlow);
        avgVMs = (areaTotal > 0.0f) ? FlowRoundAvgVelMs(qTotal / areaTotal) : 0.0f;

        LOG(LOG_DEBUG,
            "LogLocalWaterFlow: TOTAL area=%.1f m2, avgV=%.3f m/s, Q=%.2f m3/s",
            areaTotal, avgVMs, qTotal);
        g_lastLocalFlow.qM3s = qTotal;
        g_lastLocalFlow.areaM2 = areaTotal;
        g_lastLocalFlow.avgVMs = avgVMs;
        g_lastLocalFlow.valid = 1;
        FlowPushLocalToTouchScreen(qTotal, areaTotal, avgVMs);
    }
}

static uint32_t flow_q_to_wire(float qM3s)
{
    float q = FlowRoundQM3s(qM3s);
    if (q <= 0.0f) {
        return 0;
    }
    return (uint32_t)(q * 1000.0f + 0.5f);
}

static uint32_t flow_avg_v_to_wire(float avgVMs)
{
    float v = FlowRoundAvgVelMs(avgVMs);
    if (v <= 0.0f) {
        return 0;
    }
    return (uint32_t)(v * 1000.0f + 0.5f);
}

static uint32_t flow_area_to_wire(float areaM2)
{
    float a = FlowRoundAreaM2(areaM2);
    if (a <= 0.0f) {
        return 0;
    }
    return (uint32_t)(a * 10.0f + 0.5f);
}

static void final_result_on_ack(uint16_t element)
{
    if (!s_finalResultWaitActive) {
        LOG(LOG_DEBUG, "FinalResult_OnAck drop (inactive) element=%04x",
            (unsigned)element);
        return;
    }
    if (element != s_finalResultElement) {
        LOG(LOG_WARNING,
            "FinalResult_OnAck ignore element=%04x (expect %04x)",
            (unsigned)element, (unsigned)s_finalResultElement);
        return;
    }
    s_finalResultAcked = 1;
    LOG(LOG_DEBUG, "FinalResult_OnAck ok element=%04x", (unsigned)element);
}

static int send_final_result_uplink(uint16_t element, uint32_t flow,
                                    uint32_t avgSpd, uint32_t area, int type)
{
    FinalResutl stRpt;

    memset(&stRpt, 0, sizeof(stRpt));
    stRpt.stCommandHead.u32CommandSeri = g_u32CommansSeri++;
    stRpt.stCommandHead.u32Command = REQUEST_FINAL_RESULT;
    stRpt.stCommandHead.u32CommandLen =
        (uint32_t)(sizeof(FinalResutl) + sizeof(SeriverCommandTail));
    memcpy(stRpt.stCommandHead.au8StationID, g_aStationID,
           sizeof(stRpt.stCommandHead.au8StationID));
    stRpt.stFinalResultBdoy.u16Element = element;
    stRpt.stFinalResultBdoy.u32WaterFlow = flow;
    stRpt.stFinalResultBdoy.u32AvagerSpeed = avgSpd;
    stRpt.stFinalResultBdoy.u32Area = area;

    LOG(LOG_INFO,
        "SendFinalResult: element=%04x flow=%u avgSpd=%u area=%u cmdSeri=%u",
        (unsigned)element, (unsigned)flow, (unsigned)avgSpd, (unsigned)area,
        (unsigned)stRpt.stCommandHead.u32CommandSeri);
    SendZigbeeData(UP_EXPAND_CODE, (uint8_t *)&stRpt, sizeof(FinalResutl), type);
    return 0;
}

static void final_result_begin_wait(uint16_t element, int type)
{
    if (!g_lastLocalFlow.valid) {
        LOG(LOG_WARNING, "SendFinalResult: no local flow, skip uplink");
        s_finalResultWaitActive = 0;
        return;
    }

    s_finalResultElement = element;
    s_finalResultAcked = 0;
    s_finalResultRetry = 0;
    s_finalResultWaitActive = 1;
    send_final_result_uplink(element,
                           flow_q_to_wire(g_lastLocalFlow.qM3s),
                           flow_avg_v_to_wire(g_lastLocalFlow.avgVMs),
                           flow_area_to_wire(g_lastLocalFlow.areaM2),
                           type);
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        s_finalResultSendMs =
            (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
    }
}

void FinalResult_OnDownlink(const uint8_t *pData, uint16_t len)
{
    FinalResutlBddy stFinalBody;

    if (pData == NULL || len < sizeof(FinalResutlBddy)) {
        LOG(LOG_ERROR, "FinalResult_OnDownlink: short len=%u", (unsigned)len);
        return;
    }

    if (len >= sizeof(SeriverCommandHead) + sizeof(FinalResutlBddy)) {
        memcpy(&stFinalBody, pData + sizeof(SeriverCommandHead),
               sizeof(stFinalBody));
    } else {
        memcpy(&stFinalBody, pData, sizeof(stFinalBody));
    }

    LOG(LOG_DEBUG,
        "FinalResult_OnDownlink element=%04x flow=%u avgSpeed=%u area=%u",
        stFinalBody.u16Element, stFinalBody.u32WaterFlow,
        stFinalBody.u32AvagerSpeed, stFinalBody.u32Area);
    final_result_on_ack(stFinalBody.u16Element);
}

static void final_result_begin_door_close(uint64_t nowMs)
{
    Door_Close_cmd();
    s_doorCloseWaitStartMs = nowMs;
    s_doorCloseLastLogMs = nowMs;
    s_doorCloseLastCmdMs = nowMs;
    s_doorCloseRetryCount = 0;
}

/**
 * 计算水流量
 * 
 * 根据河段参数和测流断面数据，计算指定河段的水流量
 * 
 * @param sections 指向WaterFlowPara结构体数组的指针，包含测流断面数据
 * @param num_lines 测流断面的数量
 * @return 返回计算得到的水流量，如果输入参数非法或计算过程中出现错误，返回ERROR
 * 
 * 此函数首先检查输入参数的有效性，然后根据河床地形数据确定左端起始点和右端结束点
 * 接着计算左岸和右岸的斜率，以及左端和右端的无水河堤长度
 * 最后，根据测流断面数据和计算得到的斜率，使用梯形公式计算每个断面的流量，并累加得到总流量
 */
float GenerateWaterFlow(WaterFlowPara* sections, int num_lines)
{
    if (sections == NULL || num_lines < 0 || g_stSection.u16SectionNum <= 1) 
	{
        return ERROR; // 输入参数非法或数据不足以计算
    }

    int i = 0;
    float fLeftRate = 0.0;  // 左岸斜率
    float fRightRate = 0.0; // 右岸斜率
    uint32_t u32LeftStartSeri = 0; // 左端起始序号
    uint32_t u32RightEndSeri = 0; // 右端结束序号
    SectionPara stLeftStartSec = g_stSection.stSection[0];  // 左端起始断面
    SectionPara stRightEndSec = g_stSection.stSection[g_stSection.u16SectionNum - 1]; // 右端结束断面
    float fLeftDryLen;  // 左端无水河堤长度
    float fRightDryLen; // 右端无水河堤长度
    float fTotal_flow = 0.0;
    WaterFlowPara *pSectionpara = malloc((num_lines + 2) * sizeof(WaterFlowPara)); // 动态分配内存

    if (pSectionpara == NULL) 
	{
        return ERROR; // 内存分配失败
    }

    // 获取左端起始点
    for (i = 0; i < g_stSection.u16SectionNum - 1; i++) 
	{
        if (g_stSection.stSection[i + 1].u32RiverBedElevation < g_stSection.stSection[i].u32RiverBedElevation) 
		{
            stLeftStartSec = g_stSection.stSection[i];
            u32LeftStartSeri = i;
            break;
        }
    }

    // 获取右端结束点
    for (i = g_stSection.u16SectionNum - 1; i > 0; i--) 
	{
        if (g_stSection.stSection[i - 1].u32RiverBedElevation < g_stSection.stSection[i].u32RiverBedElevation) 
		{
            stRightEndSec = g_stSection.stSection[i];
            u32RightEndSeri = i;
            break;
        }
    }

    LOG(LOG_DEBUG, "u32LeftStartSeri is %d, u32RightEndSeri is %d", u32LeftStartSeri, u32RightEndSeri);

	LOG(LOG_DEBUG,"first start is %d, second start is %d",g_stSection.stSection[u32LeftStartSeri].u32StartDestence,g_stSection.stSection[u32LeftStartSeri +1].u32StartDestence);
	LOG(LOG_DEBUG ,"first bed is %d,second bed is %d",g_stSection.stSection[u32LeftStartSeri].u32RiverBedElevation,g_stSection.stSection[u32LeftStartSeri +1].u32RiverBedElevation);
    // 计算左岸斜率
    if (g_stSection.stSection[u32LeftStartSeri].u32StartDestence != g_stSection.stSection[u32LeftStartSeri + 1].u32StartDestence) 
	{
        fLeftRate = abs((g_stSection.stSection[u32LeftStartSeri].u32RiverBedElevation - g_stSection.stSection[u32LeftStartSeri + 1].u32RiverBedElevation) /
                        (g_stSection.stSection[u32LeftStartSeri +1].u32StartDestence - g_stSection.stSection[u32LeftStartSeri ].u32StartDestence));
    } 
	else 
	{
        fLeftRate = 0.0; // 防止除零
    }

    // 计算右岸斜率
    if (g_stSection.stSection[u32RightEndSeri].u32StartDestence != g_stSection.stSection[u32RightEndSeri - 1].u32StartDestence) 
	{
        fRightRate = abs((g_stSection.stSection[u32RightEndSeri].u32RiverBedElevation - g_stSection.stSection[u32RightEndSeri - 1].u32RiverBedElevation) /
                         (g_stSection.stSection[u32RightEndSeri].u32StartDestence - g_stSection.stSection[u32RightEndSeri - 1].u32StartDestence));
    } 
	else 
	{
        fRightRate = 0.0; // 防止除零
    }

    LOG(LOG_DEBUG, "fLeftRate is %.3f, fRightRate is %.3f", fLeftRate, fRightRate);

    // 计算左端和右端无水河堤长度
    if (fLeftRate > 0.0) 
	{
        fLeftDryLen = (stLeftStartSec.u32RiverBedElevation - g_u32waterLevel) / fLeftRate;
    } 
	else 
	{
        fLeftDryLen = 0.0; // 防止除零
    }

    if (fRightRate > 0.0) 
	{
        fRightDryLen = (stRightEndSec.u32RiverBedElevation - g_u32waterLevel) / fRightRate;
    } 
	else 
	{
        fRightDryLen = 0.0; // 防止除零
    }

    // 初始化岸边垂线
    pSectionpara[0].u32StartDestence = stLeftStartSec.u32StartDestence;
    pSectionpara[0].u32RiverBedElevation = stLeftStartSec.u32RiverBedElevation;
    pSectionpara[0].u16Speed = (uint16_t)(sections[0].u16Speed / fLeftRate);

    for (i = 0; i < num_lines; i++) 
	{
        pSectionpara[i + 1] = sections[i];
    }

    pSectionpara[num_lines].u32RiverBedElevation = stRightEndSec.u32RiverBedElevation;
    pSectionpara[num_lines].u32StartDestence = stRightEndSec.u32StartDestence;
    pSectionpara[num_lines].u16Speed = (uint16_t)(sections[num_lines - 1].u16Speed / fRightRate);

    // 计算总流量
    for (i = 0; i < g_stSection.u16SectionNum - 1; i++) 
	{
        float spacing = 0.0f;
        float depth_left = g_u32waterLevel - pSectionpara[i].u32RiverBedElevation;
        float depth_right = g_u32waterLevel - pSectionpara[i + 1].u32RiverBedElevation;
        float vel_left = pSectionpara[i].u16Speed;
        float vel_right = pSectionpara[i + 1].u16Speed;

        if (i == 0) 
		{
            spacing = pSectionpara[i + 1].u32StartDestence - pSectionpara[i].u32StartDestence - fLeftDryLen;
        } 
		else if (i == g_stSection.u16SectionNum - 2) 
		{
            spacing = pSectionpara[i + 1].u32StartDestence - pSectionpara[i].u32StartDestence - fRightDryLen;
        } 
		else 
		{
            spacing = pSectionpara[i + 1].u32StartDestence - pSectionpara[i].u32StartDestence;
        }

        // 数据有效性检查
        if (spacing <= 0.0f || depth_left < 0.0f || depth_right < 0.0f || vel_left < 0.0f || vel_right < 0.0f) 
		{
            free(pSectionpara);
            return ERROR;
        }

        // 计算部分面积（梯形公式）
        const float avg_depth = (depth_left + depth_right) / 2.0f;
        const float area = spacing * avg_depth;

        // 计算部分平均流速
        const float avg_velocity = (vel_left + vel_right) / 2.0f;

        // 累加部分流量
        fTotal_flow += area * avg_velocity;
    }

    free(pSectionpara); // 释放动态分配的内存
	LOG(LOG_DEBUG,"Total flow is  %04f",fTotal_flow);
    return fTotal_flow;
}

int SetRotatorpara(BYTE *pData, ROTATOR_CONFIG *pstRotator,uint16_t u16datalen)
{
    int ret = ERROR;
    char FileDir[256];
    memset(FileDir,0,256);
    GetAppDir(FileDir);
    strncat(FileDir,"/rotator.data",strlen("/rotator.data"));
	printf("rotator.data len is %d\n",u16datalen);
	memcpy(pstRotator,pData,sizeof(ROTATOR_CONFIG));
	//LOG(LOG_DEBUG,"pdata is :");
	//LOG_ARRAY(LOG_DEBUG,pData,u16datalen,"%02x");
	
    FILE* pFile = fopen(FileDir,"wb+");
    if(NULL != pFile)
    {
        ret = fwrite(pData,1,u16datalen,pFile);
		printf("Set rotator config file write bytes is %d",ret);

        fclose(pFile);
		return SUCCESS;
    }
    else
    {
        LOG(LOG_ERROR,"open rotator file error");
		return ERROR;
    }

    
}


int ReadRotatorConfig(ROTATOR_CONFIG *pstRotatorconfig)
{
	int ret = ERROR;
    char FileDir[256];
    memset(FileDir,0,256);
    GetAppDir(FileDir);
    strncat(FileDir,"/rotator.data",strlen("/rotator.data"));
    FILE* pFile = fopen(FileDir,"rb");
    if(NULL != pFile)
    {
		int file_size;   //保存文件字符数
    	char* tmp;       //将文件内容读到此指针位置
		fseek(pFile, 0, SEEK_END);      //将文件指针指向该文件的最后
		file_size = ftell(pFile);       //根据指针位置，此时可以算出文件的字符数
		printf("file_size %d\n", file_size);
		tmp = (char*)malloc(file_size * sizeof(char) + 1); //根据文件大小为tmp动态分配空间
		memset(tmp, '\0', file_size * sizeof(char));  //初始化此控件内容，否则可能会有乱码
		fseek(pFile, 0, SEEK_SET);                 //重新将指针指向文件首部
		fread(tmp, sizeof(char), file_size, pFile); //开始读取整个文件
		LOG(LOG_DEBUG,"rotator.data is");
		LOG_ARRAY(LOG_DEBUG,tmp,file_size,"%02x");
		fclose(pFile);
		
		memcpy(pstRotatorconfig,tmp,sizeof(ROTATOR_CONFIG));
		LOG(LOG_DEBUG,"sizeof(ROTATOR_CONFIG) IS %d",  sizeof(ROTATOR_CONFIG));
		ret = SUCCESS;
	free(tmp);
	tmp = NULL;
    }
	else
	{
		LOG(LOG_ERROR,"open rotator file error");	
	}
	return ret;
}

// ============================================================
// 加测控制状态机 - 非阻塞方式
// ============================================================

void RemoveResultsByStartDistance(uint32_t targetDist)
{
    if (g_pstAutoRotatorList == NULL || g_pstAutoRotatorList->size == 0) {
        return;
    }
    Node* curNode = g_pstAutoRotatorList->firstNode.next;
    Node* prev = &g_pstAutoRotatorList->firstNode;
    while (curNode != NULL) {
        AutoRotatorData *pData = (AutoRotatorData *)curNode->data;
        if (pData != NULL && pData->i32StartDistance == targetDist) {
            prev->next = curNode->next;
            free(curNode->data);
            free(curNode);
            curNode = prev->next;
            g_pstAutoRotatorList->size--;
        } else {
            prev = curNode;
            curNode = curNode->next;
        }
    }
}

void* AddMeauCtrl(void * arg)
{
    assert(arg != NULL);
    CommandProcParam stCommandPara = *(CommandProcParam *)arg;
    BYTE *pData = stCommandPara.pData;
    uint16_t u16datalen = stCommandPara.u16datalen;
    int type = stCommandPara.Devicetype;

    LOG(LOG_INFO, "AddMeauCtrl start, data len=%d, type=%d", u16datalen, type);
    /* COMPLETE/ERROR/IDLE 均可重新开测；ERROR 以前被当成“仍在跑”导致一直拒收 */
    if (g_addMeasureState != FSM_IDLE
        && g_addMeasureState != FSM_COMPLETE
        && g_addMeasureState != FSM_ERROR) {
        LOG(LOG_ERROR, "AddMeasure state machine is already running (state=%d)", g_addMeasureState);
        return (void*)ERROR;
    }
    if (g_addMeasureState == FSM_ERROR || g_addMeasureState == FSM_COMPLETE) {
        LOG(LOG_INFO, "AddMeasure restart from state=%d", (int)g_addMeasureState);
        if (g_fsmTimerId != -1) {
            remove_timer(g_fsmTimerId);
            g_fsmTimerId = -1;
        }
        g_addMeasureState = FSM_IDLE;
    }

    ReadSectionPara_new(&g_stSection_new);
    ReadCablewayPara(&g_stSetCableway);
    ReadAutoMeasurePara(&g_stAutoMeasure);
    ReadRotatorConfig(&g_stRotatorConfig);

	if(g_pstAutoRotatorDepthList == NULL) 
	{
		g_pstAutoRotatorDepthList = initLinkList();
	}
    memset(&g_addMeasureCtx, 0, sizeof(AddMeasureContext));
    g_addMeasureCtx.deviceType = type;
    g_addMeasureCtx.stopRequested = 0;

    memcpy(&g_WebSendWaterLevel, pData, sizeof(g_WebSendWaterLevel));
    g_ChoosedMode = GetMaeasureMode(g_stAutoMeasure);

    AddMeasureCtrl stAddmeau;
    memcpy(&stAddmeau, pData, sizeof(AddMeasureCtrl));
    uint32_t u32MeasElement = ALLDEVICE;
    switch (stAddmeau.u8SpeedCtrl) {
        case 0: u32MeasElement = ALLDEVICE; break;
        case 1: u32MeasElement = NCS;       break;
        case 2: u32MeasElement = ROTATOR;   break;
        case 3: u32MeasElement = VE_ADCP;   break;
    }
    for (int i = 0; i < MAX_LINE_NUM; i++) {
        if (u32MeasElement != ALLDEVICE) {
            g_ChoosedMode.stLine[i].u32MeaElement = u32MeasElement;
        }
    }

    ReadWaterLevel(NULL);
    sleep(2);

    memcpy(&g_addMeasureCtx.measureMode, &g_ChoosedMode, sizeof(MeasureMode));
    g_addMeasureCtx.totalLineCount = g_ChoosedMode.stMeaModeHead.u16LineNum;
    g_addMeasureCtx.currentLineIndex = 0;
    s_sendResultStep = 0;
    s_finalResultAcked = 0;
    s_finalResultRetry = 0;
    s_finalResultWaitActive = 0;
    g_lastLocalFlow.valid = 0;
    s_doorCloseWaitStartMs = 0;
    s_doorCloseLastLogMs = 0;
    s_doorCloseLastCmdMs = 0;
    s_doorCloseRetryCount = 0;
    s_doorOpenRetryCount = 0;
    s_doorOpenSettleActive = 0;

    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint64_t nowMs = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
        s_doorOpenWaitStartMs = nowMs;
        s_doorOpenLastLogMs = nowMs;
        s_doorOpenLastCmdMs = nowMs;
    }

#if ENABLE_ELECTRIC_DOOR
    /* 先静默读 DI：已开到位则跳过开门脉冲，直接稳门 */
    Door_ReadDiFresh();
    if (DOOR_OPEN_REACHED()) {
        LOG(LOG_DEBUG, "AddMeasure: door already open, skip open cmd, settle");
        g_bDoorCtrlSendFlag = false;
        s_doorOpenSettleActive = 1;
        s_doorOpenSettleStartMs = s_doorOpenWaitStartMs;
    } else {
        LOG(LOG_DEBUG, "AddMeasure: door not open, send Door_Open");
        Door_Open_cmd();
        {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            s_doorOpenLastCmdMs =
                (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
        }
    }
#else
    Door_Open_cmd();
#endif
    g_addMeasureState = DOOR_OPEN_WAIT;

    if (g_fsmTimerId != -1) remove_timer(g_fsmTimerId);
    g_fsmTimerId = add_timer(50, 50, TIMER_REPEAT, 0, AddMeasureFSM_Tick, NULL);
    if (g_fsmTimerId == -1) {
        LOG(LOG_ERROR, "AddMeasureFSM: failed to create timer");
        g_addMeasureState = FSM_IDLE;
        return (void*)ERROR;
    }
    LOG(LOG_INFO, "AddMeasureFSM started, timerId=%d, totalLines=%d",
        g_fsmTimerId, g_addMeasureCtx.totalLineCount);
    return (void*)SUCCESS;
}

/* 暂停/继续 */
void PauseAddMeasure(void)
{
    if (g_addMeasureState == FSM_IDLE || g_addMeasureState == FSM_COMPLETE || g_addMeasureState == FSM_PAUSED) return;
    AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
    AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
    Relay_MotorBreak(RELAY_COM0_ADDR);
    Relay_MotorBreak(RELAY_COM1_ADDR);
    if (g_fsmMeasureTimerId != -1) { remove_timer(g_fsmMeasureTimerId); g_fsmMeasureTimerId = -1; }
    g_addMeasureState = FSM_PAUSED;
}

void ResumeAddMeasure(void)
{
    if (g_addMeasureState != FSM_PAUSED) return;
    uint32_t targetDist = g_addMeasureCtx.measureMode.stLine[g_addMeasureCtx.currentLineIndex].i32StartDistance;
    memset(&g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex], 0, sizeof(AutoRotatorData));
    memset(&g_stWaterFLowPara[g_addMeasureCtx.currentLineIndex], 0, sizeof(WaterFlowPara));
    RemoveResultsByStartDistance(targetDist);
    g_pDeepList = NULL;
    g_addMeasureState = CAR_MOVE_TO_LINE;
}

/* 运动辅助函数 */
static void fsm_stopMove(int isCar)
{
    if (isCar) 
    { 
        AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car); 
        Relay_MotorBreak(RELAY_COM0_ADDR); 
        g_u8CatState = IS_STOP_STATE;
    }
    else 
    { 
        AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish); 
        Relay_MotorBreak(RELAY_COM1_ADDR); 
        g_u8FishState = IS_STOP_STATE;
    }
    g_addMeasureCtx.moveCmdSent = 0; 
    g_addMeasureCtx.moveRetryCount = 0;
}

static void fsm_sendMoveOnce(int isCar, int dir, uint16_t speed)
{
    if (g_addMeasureCtx.moveCmdSent && g_addMeasureCtx.moveLastDir == dir && g_addMeasureCtx.moveLastSpeed == speed) 
        return;
    struct timeval tv; 
    gettimeofday(&tv, NULL);
    g_addMeasureCtx.moveCmdTimeMs = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    if (isCar) 
    { 
        g_addMeasureCtx.moveLastPos = g_HoriTotalSpace * g_CarEncoderDistence; 
        Relay_MotorUnbreak(RELAY_COM0_ADDR); 
        AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car, dir * speed); 
    }
    else 
    { 
        g_addMeasureCtx.moveLastPos = g_VeroTotalSpace * g_FishEncoderDistence; 
        Relay_MotorUnbreak(RELAY_COM1_ADDR); 
        AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish, dir * speed); 
    }
    g_addMeasureCtx.moveCmdSent = 1; 
    g_addMeasureCtx.moveRetryCount = 0;
    g_addMeasureCtx.moveLastDir = dir; 
    g_addMeasureCtx.moveLastSpeed = speed;
    g_bRunning_State = true;
}

static int fsm_checkMove(float curPos)
{
    struct timeval tv; 
    gettimeofday(&tv, NULL);
    uint64_t nowMs = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    if (fabs(curPos - g_addMeasureCtx.moveLastPos) > 0.5f) 
    {
        g_addMeasureCtx.moveLastPos = curPos; 
        g_addMeasureCtx.moveCmdTimeMs = nowMs; 
        g_addMeasureCtx.moveRetryCount = 0; 
        return 1;
    }
    if (nowMs - g_addMeasureCtx.moveCmdTimeMs >= 3000) 
    {
        g_addMeasureCtx.moveRetryCount++;
        if (g_addMeasureCtx.moveRetryCount >= 5) 
            return -1;
        g_addMeasureCtx.moveCmdSent = 0; 
        g_addMeasureCtx.moveCmdTimeMs = nowMs;
    }
    return 0;
}

static void fsm_resetMoveState(void)
{
    g_addMeasureCtx.moveCmdSent = 0; 
	g_addMeasureCtx.moveRetryCount = 0;
    g_addMeasureCtx.moveLastPos = 0.0f; 
	g_addMeasureCtx.moveCmdTimeMs = 0;
}

static int debounceLimit(uint8_t *pPrev, uint8_t flagValue)
{
    if (flagValue == 1) 
    {
        if (*pPrev == 1) 
        { 
            g_addMeasureCtx.debounceCount++; 
            if (g_addMeasureCtx.debounceCount >= 3) 
            { 
                g_addMeasureCtx.debounceCount = 0; 
                *pPrev = 0; 
                return 1; 
            } 
        }
        else 
        { 
            g_addMeasureCtx.debounceCount = 1; 
        }
    } 
    else 
    { 
        g_addMeasureCtx.debounceCount = 0; 
    }
    *pPrev = flagValue; 
    return 0;
}

static uint64_t GetCurrentTimeMs(void)
{
    struct timeval tv; 
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* [修改] Web手动/平台移动: FWD与BWD到位判断分离，修复BWD误用cur>=target-adjust导致秒停 */
static int motion_pos_reached(float curPos, float targetPos, float adjust, int direction)
{
    float rest = fabsf(targetPos - curPos);

    if (rest <= adjust) {
        return 1;
    }
    if (direction == AMPS_FWD) {
        return curPos >= targetPos - adjust;
    }
    if (direction == AMPS_BWD) {
        return curPos <= targetPos + adjust;
    }
    return 0;
}

/* 小车回仓: 复位编码器，触摸屏位置清零 */
static void fsm_resetCarHomePosition(void)
{
    ResetCarEncoderPosition();
    g_stHoriDIStatusFlag.u8REV_LIMIT_FLAG = 0;
}

static uint8_t s_rotatorTouchGroundDb = 0;

/* [修改] 使用转子采集板触底开关量(DI bit4)，2次消抖后立即判定触底 */
static bool fsm_checkRotatorTouchGround(void)
{
    if (g_stSwitchStatus.u16TouchGround)
    {
        s_rotatorTouchGroundDb++;
        if (s_rotatorTouchGroundDb >= 2)
        {
            s_rotatorTouchGroundDb = 0;
            return true;
        }
    }
    else
    {
        s_rotatorTouchGroundDb = 0;
    }
    return false;
}

/* [修改] 到深停稳后进入测流等待；记录到达时计数值，供“第2信号起计”使用 */
static void fsm_beginRotatorMeasure(bool bHasNCS, bool bHasDepth)
{
    g_addMeasureCtx.measureArrivalCount = g_u32RotatorPeriodicCount;
    g_addMeasureCtx.measureStartTimeMs = GetCurrentTimeMs();
    g_addMeasureCtx.measurePhase = 0;
    g_addMeasureCtx.measureDurationMs = g_stRotatorConfig.u16Meaduration * 1000;
    if (bHasNCS && bHasDepth)
    {
        sendMeasureResult(NCS, g_addMeasureCtx.deviceType);
        uint32_t rotorMs = g_stRotatorConfig.u16Meaduration * 1000;
        uint32_t ncsMs = 30000;
        g_addMeasureCtx.measureDurationMs = (rotorMs > ncsMs) ? rotorMs : ncsMs;
    }
    g_addMeasureState = MEASURE_WAIT_DURATION;
}

/*
 * [修改] 传统AutoMeasureCtrl路径: 替代sleep(20)，等到第2个转子计数值再开始测流
 */
void WaitRotatorSecondSignal(void)
{
    uint32_t arrival = g_u32RotatorPeriodicCount;
    uint64_t startMs = GetCurrentTimeMs();

    while (g_bRunning_State)
    {
        if (g_u32RotatorPeriodicCount >= arrival + ROTATOR_COUNT_PER_SIGNAL)
        {
            break;
        }
        if (GetCurrentTimeMs() - startMs >= ROTATOR_ARM_TIMEOUT_MS)
        {
            LOG(LOG_WARNING, "WaitRotatorSecondSignalLegacy timeout");
            break;
        }
        mSleep(50);
    }
}

/*
 * 按测线起点距取河底高程(cm)。测线往往落在断面点之间，不能用 == 精确匹配；
 * 精确匹配失败时按相邻断面点线性插值，避免 need 被算成负数后“未下放就开测”。
 */
static int fsm_lookupRiverBedCm(uint32_t distCm, uint32_t *pBedCm)
{
    int n;
    int i;
    int lo = -1;
    int hi = -1;

    if (pBedCm == NULL)
    {
        return ERROR;
    }

    n = (int)g_stSection.u16SectionNum;
    for (i = 0; i < n; i++)
    {
        if (g_stSection.stSection[i].u32StartDestence == distCm)
        {
            *pBedCm = g_stSection.stSection[i].u32RiverBedElevation;
            return SUCCESS;
        }
    }
    for (i = 0; i < n; i++)
    {
        uint32_t sd = g_stSection.stSection[i].u32StartDestence;
        if (sd <= distCm)
        {
            lo = i;
        }
        if (sd >= distCm && hi < 0)
        {
            hi = i;
        }
    }
    if (lo >= 0 && hi >= 0 && lo != hi)
    {
        uint32_t d0 = g_stSection.stSection[lo].u32StartDestence;
        uint32_t d1 = g_stSection.stSection[hi].u32StartDestence;
        int32_t b0 = (int32_t)g_stSection.stSection[lo].u32RiverBedElevation;
        int32_t b1 = (int32_t)g_stSection.stSection[hi].u32RiverBedElevation;
        if (d1 != d0)
        {
            int32_t bed = b0 + (int32_t)((int64_t)(b1 - b0) * (int64_t)(distCm - d0) / (int64_t)(d1 - d0));
            *pBedCm = (bed > 0) ? (uint32_t)bed : 0U;
            return SUCCESS;
        }
    }
    if (lo >= 0)
    {
        *pBedCm = g_stSection.stSection[lo].u32RiverBedElevation;
        return SUCCESS;
    }
    if (hi >= 0)
    {
        *pBedCm = g_stSection.stSection[hi].u32RiverBedElevation;
        return SUCCESS;
    }

    n = (int)g_stSection_new.u16SectionNum;
    lo = -1;
    hi = -1;
    for (i = 0; i < n; i++)
    {
        uint32_t sd = (uint32_t)((g_stSection_new.stSection[i].u32StartDestence >= 0)
            ? g_stSection_new.stSection[i].u32StartDestence : 0);
        int32_t bed = g_stSection_new.stSection[i].u32RiverBedElevation;
        if (sd == distCm)
        {
            *pBedCm = (bed > 0) ? (uint32_t)bed : 0U;
            return SUCCESS;
        }
        if (sd <= distCm)
        {
            lo = i;
        }
        if (sd >= distCm && hi < 0)
        {
            hi = i;
        }
    }
    if (lo >= 0 && hi >= 0 && lo != hi)
    {
        uint32_t d0 = (uint32_t)g_stSection_new.stSection[lo].u32StartDestence;
        uint32_t d1 = (uint32_t)g_stSection_new.stSection[hi].u32StartDestence;
        int32_t b0 = g_stSection_new.stSection[lo].u32RiverBedElevation;
        int32_t b1 = g_stSection_new.stSection[hi].u32RiverBedElevation;
        if (d1 != d0)
        {
            int32_t bed = b0 + (int32_t)((int64_t)(b1 - b0) * (int64_t)(distCm - d0) / (int64_t)(d1 - d0));
            *pBedCm = (bed > 0) ? (uint32_t)bed : 0U;
            return SUCCESS;
        }
    }
    if (lo >= 0)
    {
        int32_t bed = g_stSection_new.stSection[lo].u32RiverBedElevation;
        *pBedCm = (bed > 0) ? (uint32_t)bed : 0U;
        return SUCCESS;
    }
    if (hi >= 0)
    {
        int32_t bed = g_stSection_new.stSection[hi].u32RiverBedElevation;
        *pBedCm = (bed > 0) ? (uint32_t)bed : 0U;
        return SUCCESS;
    }
    return ERROR;
}

/* fsm_fishDownToDepth - 铅鱼下降到当前深度（三场景） */
static void fsm_fishDownToDepth(void)
{
    if (g_pDeepList == NULL) 
	{ 
		DeepthProc(g_addMeasureCtx.measureMode.stLine[g_addMeasureCtx.currentLineIndex]); 
		g_fsmMeasureDepthIndex = 0; 
	}

    /* 旧 AutoMeasureCtrl：0 深度填充点直接丢掉；NCS 水面点发雷达后继续下一条，
     * 不能在仓顶进入 MEASURE_NCS 等 30s，否则表现为“不下放铅鱼直接测流”。 */
    while (g_pDeepList != NULL)
    {
        uint32_t skipElem = g_pDeepList->data.u32Element;
        uint8_t skipDp = g_pDeepList->data.u8Deepth;
        bool skipNcs = (skipElem & NCS) != 0;
        bool skipVe = (skipElem & VE_ADCP) != 0;
        bool skipDepthDev = (skipElem & (ROTATOR | ADCP | MUDS | TEMPRATURE)) != 0;

        if (skipElem == 0 || (skipDp == 0 && !skipNcs && !skipVe))
        {
            LOG(LOG_INFO, "fsm_fishDownToDepth: drop empty/padding depth node elem=%08x", skipElem);
            SLTPopFront(&g_pDeepList);
            g_fsmMeasureDepthIndex = 0;
            continue;
        }
        if (skipDp == 0 && skipNcs && !skipDepthDev)
        {
            LOG(LOG_INFO, "fsm_fishDownToDepth: NCS at surface, send radar then lower rotator");
            sendMeasureResult(NCS, g_addMeasureCtx.deviceType);
            SLTPopFront(&g_pDeepList);
            g_fsmMeasureDepthIndex = 0;
            continue;
        }
        if (skipDp == 0 && skipDepthDev)
        {
            if (skipNcs)
            {
                sendMeasureResult(NCS, g_addMeasureCtx.deviceType);
            }
            LOG(LOG_INFO, "fsm_fishDownToDepth: drop unused 0%% depth elem=%08x", skipElem);
            SLTPopFront(&g_pDeepList);
            g_fsmMeasureDepthIndex = 0;
            continue;
        }
        break;
    }

    SLTNode* pc = g_pDeepList; 
	int idx = 0;
    while (pc && idx < g_fsmMeasureDepthIndex) 
	{ 
		pc = pc->next; 
		idx++; 
	}
    if (!pc) 
	{ 
		fsm_resetMoveState();
		g_addMeasureState = FISH_UP_AFTER_LINE; 
		return; 
	}

    uint32_t elem = pc->data.u32Element;
	uint8_t dp = pc->data.u8Deepth;
    bool bHasNCS = (elem & NCS) != 0; 
	bool bHasDepth = (elem & (ROTATOR | ADCP | MUDS | TEMPRATURE)) != 0;

    /* 场景1: 只有NCS */
    if (bHasNCS && !bHasDepth) 
	{
        LOG(LOG_DEBUG, "fsm_fishDownToDepth: NCS only, start radar");
        sendMeasureResult(NCS, g_addMeasureCtx.deviceType);
        g_addMeasureCtx.measureStartTimeMs = GetCurrentTimeMs();
        g_addMeasureCtx.measureDurationMs = 30000;
        g_addMeasureCtx.measurePhase = 0;
        g_addMeasureState = MEASURE_NCS; 
		return;
    }
    /* 场景2: 深度设备+深度0 → 从链表中删除并跳过 */
    if (dp == 0 && bHasDepth) 
    { 
        SLTPopFront(&g_pDeepList);
        g_fsmMeasureDepthIndex = 0; 
        fsm_resetMoveState(); 
        g_addMeasureState = FISH_DOWN_TO_DEPTH; 
        return; 
    }

    uint32_t wl = GetCurrentWaterLevel();
    int need = 0;
    int dw = ERROR;
    uint32_t td = (uint32_t)g_addMeasureCtx.measureMode.stLine[g_addMeasureCtx.currentLineIndex].i32StartDistance;
    uint32_t bedCm = 0;
    float fc;
    float diff;
    static uint8_t ptf = 0;

    g_addMeasureCtx.u8DepthPersent = pc->data.u8Deepth;

    if (fsm_lookupRiverBedCm(td, &bedCm) == SUCCESS)
    {
        dw = GenerateinWaterDeepth(bedCm, wl, dp);
    }
    if (dw == ERROR)
    {
        /* 找不到河底或水位低于河底时，禁止再用 -缆高 把目标算成负数后“原地开测” */
        LOG(LOG_ERROR, "fsm_fishDownToDepth: no bed at dist=%u wl=%u dp=%u, skip depth",
            td, wl, (unsigned)dp);
        SLTPopFront(&g_pDeepList);
        g_fsmMeasureDepthIndex = 0;
        fsm_resetMoveState();
        g_addMeasureState = FISH_DOWN_TO_DEPTH;
        return;
    }
    need = dw + (int)g_stSetCableway.stSetCablewayBody.u32CableHight - (int)wl + FISH_BODY_HIGHT;
    fc = g_VeroTotalSpace * g_FishEncoderDistence;
    g_fsmMeasureDepthTargetPos = (float)need;
    diff = g_fsmMeasureDepthTargetPos - fc;
    LOG(LOG_INFO, "fsm_fishDownToDepth: dist=%u bed=%u wl=%u dp=%u dw=%d need=%d fc=%.1f diff=%.1f",
        td, bedCm, wl, (unsigned)dp, dw, need, fc, diff);

    if (need <= (int)g_FishDistenceAdjust)
    {
        LOG(LOG_ERROR, "fsm_fishDownToDepth: invalid down target %d, skip depth", need);
        SLTPopFront(&g_pDeepList);
        g_fsmMeasureDepthIndex = 0;
        fsm_resetMoveState();
        g_addMeasureState = FISH_DOWN_TO_DEPTH;
        return;
    }
    /* 仅按下放方向判断到位；禁止用 fc>=负数目标 在仓顶直接开测 */
    if (motion_pos_reached(fc, g_fsmMeasureDepthTargetPos, (float)g_FishDistenceAdjust, AMPS_FWD))
    {
        fsm_stopMove(0);
        fsm_beginRotatorMeasure(bHasNCS, bHasDepth);
        return;
    }
    /* 触底/正转限位仅在已真正离开顶部后检测 */
    s_rotatorTouchGroundDb = 0;
    fsm_sendMoveOnce(0, AMPS_FWD, GetFishRunRpm() * 10);
    if (g_addMeasureCtx.moveCmdSent
        && fc > (float)g_FishDistenceAdjust * 2.0f
        && fsm_checkRotatorTouchGround())
    {
        LOG(LOG_DEBUG, "fish touch ground by rotator DI during down");
        fsm_stopMove(0);
        g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG = 0;
        ptf = 0;
        fsm_beginRotatorMeasure(bHasNCS, bHasDepth);
        return;
    }
    if (g_addMeasureCtx.moveCmdSent
        && fc > (float)g_FishDistenceAdjust * 2.0f
        && debounceLimit(&ptf, g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG))
    {
        fsm_stopMove(0);
        g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG = 0;
        ptf = 0;
        fsm_beginRotatorMeasure(bHasNCS, bHasDepth);
        return;
    }
    if (fsm_checkMove(fc) == -1) 
	{ 
		fsm_stopMove(0); 
		g_addMeasureState = FSM_ERROR; 
	}
}

/* fsm_measureNCS - 等待NCS雷达30秒 */
static void fsm_measureNCS(void)
{
    if (GetCurrentTimeMs() >= g_addMeasureCtx.measureStartTimeMs + g_addMeasureCtx.measureDurationMs) {
        LOG(LOG_DEBUG, "fsm_measureNCS: radar complete");
        g_addMeasureState = NEXT_DEPTH;
    }
}

/* fsm_start */
static void fsm_start(void)
{
    static int step = 0; 
    static int cnt = 0;
    if (step == 0) 
    { 
        Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM0_ADDR); 
        step = 1; 
    }
    else if (step == 1) 
    { 
        Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM1_ADDR); 
        step = 2; 
        cnt = 0; 
    }
    else if (step == 2) 
    { 
        cnt++; 
        if (cnt >= 10) 
        { 
            cnt = 0; 
            step = 0; 
            g_addMeasureState = FISH_UP_TO_TOP_INIT; 
        } 
    }
}

/* fsm_fishUpTopInit */
static void fsm_fishUpTopInit(void)
{
    fsm_resetMoveState(); 
    fsm_sendMoveOnce(0, AMPS_BWD, GetFishRunRpm() * 10);
    static uint8_t pf = 0;
    if (debounceLimit(&pf, g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG)) 
    {
        fsm_stopMove(0); 
        ResetEncoder(g_stDeviceAddr.u8Encoder_fish);
        g_VeroTotalSpace = 0;
        /* [修改] 收鱼到顶后清除REV限位标志，避免后续FISH_DOWN_TO_PLATFORM误判已到位 */
        g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG = 0;
        pf = 0;
        if (g_addMeasureState == FISH_UP_TO_TOP_FINAL) 
        {
            g_addMeasureState = CAR_BACK_TO_HOME_FINAL;
        }
        else 
        {
            g_addMeasureState = CAR_BACK_TO_HOME_INIT;
        }
    }
}

/* fsm_carBackHomeInit */
static void fsm_carBackHomeInit(void)
{
    fsm_resetMoveState(); 
    fsm_sendMoveOnce(1, AMPS_BWD, GetCarRunRpm() * 10);
    static uint8_t pf = 0;
    if (debounceLimit(&pf, g_stHoriDIStatusFlag.u8REV_LIMIT_FLAG)) 
    {
        fsm_stopMove(1);
        fsm_resetCarHomePosition();
        pf = 0;
        if (g_addMeasureState == CAR_BACK_TO_HOME_FINAL) 
        {
            fsm_resetMoveState();
            g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG = 0;
            s_rotatorTouchGroundDb = 0;
            g_addMeasureState = FISH_DOWN_TO_PLATFORM;
        }
        else 
        {
            g_addMeasureState = CAR_MOVE_TO_LINE;
        }
        if (g_addMeasureState == CAR_MOVE_TO_LINE) 
        {
            fsm_resetMoveState();
        }
    }
}

/* fsm_carMoveToLine */
static void fsm_carMoveToLine(void)
{
    if (g_addMeasureCtx.currentLineIndex >= g_addMeasureCtx.totalLineCount) 
    { 
        g_addMeasureState = ALL_LINE_DONE; 
        return; 
    }
    float tp = (float)g_addMeasureCtx.measureMode.stLine[g_addMeasureCtx.currentLineIndex].i32StartDistance 
               + g_stSetCableway.stSetCablewayBody.i16StartPointDes;
    float cp = g_HoriTotalSpace * g_CarEncoderDistence;
    g_fsmMeasureLineTargetPos = tp; 
    float diff = tp - cp;
    if (fabs(diff) < g_CarDistenceAdjust || cp >= tp - g_CarDistenceAdjust) 
    {
        fsm_stopMove(1);
        fsm_resetMoveState();
        g_pDeepList = NULL; 
        g_fsmMeasureDepthIndex = 0;
        g_addMeasureCtx.currentLineStartDist = g_addMeasureCtx.measureMode.stLine[g_addMeasureCtx.currentLineIndex].i32StartDistance;
        g_addMeasureState = FISH_DOWN_TO_DEPTH; 
        return;
    }
    if (g_stHoriDIStatusFlag.u8FWD_LIMIT_FLAG) 
    { 
        fsm_stopMove(1); 
        g_addMeasureState = FSM_ERROR; 
        return; 
    }
    fsm_resetMoveState();
    if (fabs(diff) < SPEED_DOWN_DENTENCE) 
    {
        fsm_sendMoveOnce(1, (diff > 0) ? AMPS_FWD : AMPS_BWD, GetCarRunRpm() * 3);
    }
    else 
    {
        fsm_sendMoveOnce(1, (diff > 0) ? AMPS_FWD : AMPS_BWD, GetCarRunRpm() * 10);
    }
    if (fsm_checkMove(cp) == -1) 
    { 
        fsm_stopMove(1); 
        g_addMeasureState = FSM_ERROR; 
    }
}

/* fsm_measureWaitDuration - 三阶段测量等待
 * [2026-06-22修改] 取消Phase0固定20s稳定等待
 * Phase 0: 到深后等待第2个转子信号(跳过第1个，与传统缆道一致)
 * Phase 1: 从第2个信号开始计时测量时长
 * Phase 2: 等待计数值 > B+1→上报→转下一深度（或1.5倍超时强制上报）
 */
static void fsm_measureWaitDuration(void)
{
    uint64_t nowMs = GetCurrentTimeMs();
    uint64_t elapsed = nowMs - g_addMeasureCtx.measureStartTimeMs;

    if (g_addMeasureCtx.measurePhase == 0) 
    {
        if (g_u32RotatorPeriodicCount >= g_addMeasureCtx.measureArrivalCount + ROTATOR_COUNT_PER_SIGNAL)
        {
            g_addMeasureCtx.measureStartCount = g_u32RotatorPeriodicCount;
            g_addMeasureCtx.measureStartTimeMs = nowMs;
            g_addMeasureCtx.measureDurationMs = g_stRotatorConfig.u16Meaduration * 1000;
            g_addMeasureCtx.measurePhase = 1;
            LOG(LOG_DEBUG, "rotator 2nd signal detected, start measure");
        }
        else if (elapsed >= ROTATOR_ARM_TIMEOUT_MS)
        {
            g_addMeasureCtx.measureStartCount = g_u32RotatorPeriodicCount;
            g_addMeasureCtx.measureStartTimeMs = nowMs;
            g_addMeasureCtx.measureDurationMs = g_stRotatorConfig.u16Meaduration * 1000;
            g_addMeasureCtx.measurePhase = 1;
            LOG(LOG_WARNING, "rotator 2nd signal timeout, start measure");
        }
        return;
    }

    if (g_addMeasureCtx.measurePhase == 1) 
    {
        uint64_t measureElapsed = nowMs - g_addMeasureCtx.measureStartTimeMs;
        uint64_t timeoutMs = g_addMeasureCtx.measureDurationMs + g_addMeasureCtx.measureDurationMs / 2;

        if (measureElapsed >= timeoutMs)
        {
            /* 超时1.5倍，记录B=当前值，直接进入Phase 2上报 */
            g_addMeasureCtx.measureTargetCount = g_u32RotatorPeriodicCount;
            g_addMeasureCtx.measurePhase = 2;
        }
        else if (measureElapsed >= g_addMeasureCtx.measureDurationMs) 
        {
            /* 测量时长到达，记录目标值B */
            g_addMeasureCtx.measureTargetCount = g_u32RotatorPeriodicCount;
            g_addMeasureCtx.measurePhase = 2;
        }
        return;
    }

    if (g_addMeasureCtx.measurePhase == 2) 
    {
        uint64_t measureElapsed = nowMs - g_addMeasureCtx.measureStartTimeMs;
        uint64_t timeoutMs = g_addMeasureCtx.measureDurationMs + g_addMeasureCtx.measureDurationMs / 2;

        if (g_u32RotatorPeriodicCount >= g_addMeasureCtx.measureTargetCount + 1 || measureElapsed >= timeoutMs) 
        {
            uint32_t sn = g_u32RotatorPeriodicCount - g_addMeasureCtx.measureStartCount;
            uint64_t durMs = measureElapsed;// (g_addMeasureCtx.measureDurationMs > 0) ? g_addMeasureCtx.measureDurationMs :
            if (durMs < 1000) durMs = 1000;

            float v = g_stRotatorConfig.fa_value + g_stRotatorConfig.fb_value * ((float)sn / 2.0f) * (float)g_stRotatorConfig.u16n_value / (float)(durMs / 1000);
            uint32_t val = 0;
            if (v > 0) 
            { 
                v *= 10000.0f; 
                val = (uint32_t)v; 
                if ((val % 10) % 2 == 0) 
                    val /= 10; 
                else 
                    val = val / 10 + 1; 
            }

            g_stManuRotatorResultBody.u8Duration = (uint8_t)(durMs / 1000);
            g_stManuRotatorResultBody.u16SignalNum = sn / 2;
            g_stManuRotatorResultBody.i32StartDistance = g_addMeasureCtx.currentLineStartDist;
            g_stManuRotatorResultBody.u8Deepth = g_addMeasureCtx.u8DepthPersent;
            g_stManuRotatorResultBody.u16Speed = (uint16_t)val;
            g_stTouchScreenReportData.u16SpeedData = (uint16_t)val;
            g_stTouchScreenReportData.u16StartPosData = (uint16_t)(int16_t)g_addMeasureCtx.currentLineStartDist;

            {
                time_t sec = time(NULL);
                struct tm now;
                localtime_r(&sec, &now);
                g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex].u8Year = now.tm_year + 1900 - 2000;
                g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex].u8Month = now.tm_mon + 1;
                g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex].u8Day = now.tm_mday;
                g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex].u8Hour = now.tm_hour;
                g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex].u8Minutes = now.tm_min;
                g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex].u8Second = now.tm_sec;
            }
            g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex].i32StartDistance = g_addMeasureCtx.currentLineStartDist;
            g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex].U32WaterLevel = GetCurrentWaterLevel();
            g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex].u16Speed = (uint16_t)val;
            g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex].u8Duration = (uint8_t)(durMs / 1000);
            g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex].u16SignalNum = sn / 2;

            insertSingleNodeByTail(&g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex], g_pstAutoRotatorList);
            // 修复: 为每个深度独立分配内存，而不是复用 g_stAutoRotatorArray 中的同一地址
            // 这样才能让 g_pstAutoRotatorDepthList 中每个节点持有独立的深度数据
            {
                AutoRotatorData_depth *pDepthNodeData = (AutoRotatorData_depth *)malloc(sizeof(AutoRotatorData_depth));
                if (pDepthNodeData != NULL)
                {
                    memcpy(pDepthNodeData, &g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex], sizeof(AutoRotatorData));
                    pDepthNodeData->u8DepthPersen = g_addMeasureCtx.u8DepthPersent;
                    insertSingleNodeByTail(pDepthNodeData, g_pstAutoRotatorDepthList);
                }
                else
                {
                    LOG(LOG_ERROR, "fsm_measureWaitDuration: malloc AutoRotatorData_depth failed");
                    insertSingleNodeByTail(&g_stAutoRotatorArray[g_addMeasureCtx.currentLineIndex], g_pstAutoRotatorDepthList);
                }
            }

            g_stWaterFLowPara[g_addMeasureCtx.currentLineIndex].u32StartDestence = g_addMeasureCtx.currentLineStartDist;
            g_stWaterFLowPara[g_addMeasureCtx.currentLineIndex].u16Speed = (uint16_t)val;
            for (int i = 0; i < g_stSection.u16SectionNum; i++) 
            {
                if (g_stSection.stSection[i].u32StartDestence == g_addMeasureCtx.currentLineStartDist)
                    g_stWaterFLowPara[g_addMeasureCtx.currentLineIndex].u32RiverBedElevation = g_stSection.stSection[i].u32RiverBedElevation;
            }
            SendMeasureResultManu(ROTATOR, DTU_TYPE);
            g_addMeasureState = NEXT_DEPTH;
        }
    }
}

/* fsm_nextDepth */
static void fsm_nextDepth(void) 
{ 
	SLTPopFront(&g_pDeepList);
    g_fsmMeasureDepthIndex = 0; 
    fsm_resetMoveState(); 
    if (g_pDeepList == NULL) 
    {
        g_addMeasureState = FISH_UP_AFTER_LINE; 
    }
    else 
    {
        g_addMeasureState = FISH_DOWN_TO_DEPTH; 
    }
}

/* fsm_fishUpAfterLine */
static void fsm_fishUpAfterLine(void)
{
    float fp = g_VeroTotalSpace * g_FishEncoderDistence;
    float tp = g_stSetCableway.stSetCablewayBody.u32CableHight - GetCurrentWaterLevel() - 50; 
    if (tp < 0) 
    {
        tp = 0;
    }
    if (fp <= tp || g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG) 
    { 
        fsm_stopMove(0); 
        g_addMeasureState = NEXT_LINE; 
        return; 
    }
    static uint8_t pf = 0;
    if (debounceLimit(&pf, g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG)) 
    { 
        fsm_stopMove(0); 
        pf = 0; 
        g_addMeasureState = NEXT_LINE; 
        return; 
    }
    fsm_resetMoveState(); 
    fsm_sendMoveOnce(0, AMPS_BWD, GetFishRunRpm() * 10);
    if (fsm_checkMove(fp) == -1) 
    { 
        fsm_stopMove(0); 
        g_addMeasureState = FSM_ERROR; 
    }
}

/* fsm_nextLine */
static void fsm_nextLine(void) 
{ 
    g_addMeasureCtx.currentLineIndex++; 
    fsm_resetMoveState(); 
    g_addMeasureState = CAR_MOVE_TO_LINE; 
}

/* fsm_allLineDone */
static void fsm_allLineDone(void) 
{ 
    g_addMeasureState = FISH_UP_TO_TOP_FINAL; 
}

/* fsm_fishDownToPlatform - 测完回仓后下放铅鱼；限位/触底仅在离开顶部后检测 */
static void fsm_fishDownToPlatform(void)
{
    float target = (float)g_stSetCableway.stSetCablewayBody.u16PlatToCableHight;
    float cur = g_VeroTotalSpace * g_FishEncoderDistence; 
    float diff = target - cur;
	LOG(LOG_DEBUG, "fsm_fishDownToPlatform: cur:%f, target:%f, diff:%f", cur, target, diff);
    if (cur >= target - FISH_PLATFORM_ENCODER_TOL)
    {
        LOG(LOG_DEBUG, "fsm_fishDownToPlatform: encoder at platform");
        fsm_stopMove(0); 
        g_addMeasureState = SEND_RESULT_FINAL; 
        return;
    }
    s_rotatorTouchGroundDb = 0;
    fsm_sendMoveOnce(0, AMPS_FWD, GetFishRunRpm() * 10);
    static uint8_t ptf = 0;
    if (g_addMeasureCtx.moveCmdSent
        && cur > (float)g_FishDistenceAdjust * 2.0f
        && fsm_checkRotatorTouchGround())
    {
        LOG(LOG_DEBUG, "fish touch platform by rotator DI");
        fsm_stopMove(0);
        g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG = 0;
        ptf = 0;
        g_addMeasureState = SEND_RESULT_FINAL;
        return;
    }
    if (g_addMeasureCtx.moveCmdSent
        && cur > (float)g_FishDistenceAdjust * 2.0f
        && debounceLimit(&ptf, g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG))
    {
        LOG(LOG_DEBUG, "fish touch platform by FWD limit");
        fsm_stopMove(0);
        g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG = 0;
        ptf = 0;
        g_addMeasureState = SEND_RESULT_FINAL;
        return;
    }
    if (fsm_checkMove(cur) == -1) 
    { 
        fsm_stopMove(0); 
        g_addMeasureState = FSM_ERROR; 
    }
}

/* fsm_doorOpenWait - 非阻塞等开门到位，避免 AddMeauCtrl 阻塞任务线程 */
static void fsm_doorOpenWait(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t nowMs = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    if (s_doorOpenSettleActive)
    {
        if (nowMs - s_doorOpenSettleStartMs >= DOOR_OPEN_SETTLE_MS)
        {
            s_doorOpenSettleActive = 0;
            g_addMeasureState = FSM_START;
            LOG(LOG_DEBUG, "door open settle done, start measure FSM");
        }
        return;
    }

    /* 发开门后至少 1s 再认到位，避免沿用旧 DI 误判；已开跳过脉冲时走 settle 分支 */
    if (DOOR_OPEN_REACHED()
        && (nowMs - s_doorOpenLastCmdMs) >= 1000U)
    {
        LOG(LOG_DEBUG, "DOOR OPENED (openDI=0)");
        g_bDoorCtrlSendFlag = false;
        s_doorOpenRetryCount = 0;
#if ENABLE_ELECTRIC_DOOR
        s_doorOpenSettleActive = 1;
        s_doorOpenSettleStartMs = nowMs;
#else
        g_addMeasureState = FSM_START;
#endif
        return;
    }

    if (s_doorOpenRetryCount < DOOR_CLOSE_MAX_RETRY
        && nowMs - s_doorOpenLastCmdMs >= DOOR_CLOSE_RETRY_INTERVAL_MS)
    {
        s_doorOpenRetryCount++;
        LOG(LOG_INFO, "door open retry %u/%u after %ds, refresh DI then open",
            (unsigned)s_doorOpenRetryCount, (unsigned)DOOR_CLOSE_MAX_RETRY,
            DOOR_CLOSE_RETRY_INTERVAL_MS / 1000);
        Door_ReadDiFresh();
        if (DOOR_OPEN_REACHED()) {
            LOG(LOG_INFO, "DOOR OPENED on retry refresh");
            g_bDoorCtrlSendFlag = false;
            s_doorOpenRetryCount = 0;
            s_doorOpenSettleActive = 1;
            s_doorOpenSettleStartMs = nowMs;
            return;
        }
        Door_Open_cmd();
        s_doorOpenLastCmdMs = nowMs;
    }

    if (nowMs - s_doorOpenLastLogMs >= 5000)
    {
        LOG(LOG_DEBUG, "waiting door open, openDI=%d (接地=1有电=0, 0=opened)",
            g_stDoorSwitchStatus.u16watersurface_1);
        s_doorOpenLastLogMs = nowMs;
    }

    if (nowMs - s_doorOpenWaitStartMs >= DOOR_WAIT_TIMEOUT_MS)
    {
        LOG(LOG_ERROR, "door open wait timeout %ds, abort add-measure", DOOR_WAIT_TIMEOUT_MS / 1000);
        g_bDoorCtrlSendFlag = false;
        s_doorOpenRetryCount = 0;
        g_addMeasureState = FSM_ERROR;
    }
}

/* fsm_sendResultFinal
 * 1) 先关门并独占 ZigBee 等到位（期间禁水位/状态/触摸屏）
 * 2) 停 DI 轮询后再发测点/汇总大包
 * 3) 再等汇总 ACK（与门无关）
 * 旧逻辑关门后约 50ms 就上报，大包仍会挤掉门控。
 */
static void fsm_sendResultFinal(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t nowMs = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    if (s_sendResultStep == 0)
    {
        /* 回仓/放平台已结束：先报静止，再关门静默 */
        g_u8CatState = IS_STOP_STATE;
        g_u8FishState = IS_STOP_STATE;
        g_bRunning_State = false;
        g_sendcount = 9; /* SendStatusFrame 内 ++ 后==10，静止时也会强制发一帧 */
        SendStatusFrame((void *)DTU_TYPE);
        LOG(LOG_INFO, "AddMeasure end: status=STOP then Door_Close");
        final_result_begin_door_close(nowMs);
        s_sendResultStep = 1;
        return;
    }

    /* step1: 只等关门，不上报 */
    if (s_sendResultStep == 1)
    {
        if (DOOR_CLOSE_REACHED()
            && (nowMs - s_doorCloseWaitStartMs) >= 1000U)
        {
            LOG(LOG_DEBUG, "DOOR CLOSED, stop door poll then report");
            g_bDoorCtrlSendFlag = false;
            s_doorCloseRetryCount = 0;
            s_sendResultStep = 2;
            return;
        }

        if (s_doorCloseRetryCount < DOOR_CLOSE_MAX_RETRY
            && nowMs - s_doorCloseLastCmdMs >= DOOR_CLOSE_RETRY_INTERVAL_MS)
        {
            s_doorCloseRetryCount++;
            LOG(LOG_INFO, "door close retry %u/%u after %ds",
                (unsigned)s_doorCloseRetryCount, (unsigned)DOOR_CLOSE_MAX_RETRY,
                DOOR_CLOSE_RETRY_INTERVAL_MS / 1000);
            Door_Close_cmd();
            s_doorCloseLastCmdMs = nowMs;
        }

        if (nowMs - s_doorCloseLastLogMs >= 5000)
        {
            LOG(LOG_DEBUG, "waiting door close, closeDI=%d (接地=1有电=0, 0=closed)",
                g_stDoorSwitchStatus.u16watersurface_2);
            s_doorCloseLastLogMs = nowMs;
        }

        if (nowMs - s_doorCloseWaitStartMs >= DOOR_WAIT_TIMEOUT_MS)
        {
            LOG(LOG_ERROR, "door close wait timeout %ds, report anyway",
                DOOR_WAIT_TIMEOUT_MS / 1000);
            g_bDoorCtrlSendFlag = false;
            s_doorCloseRetryCount = 0;
            s_sendResultStep = 2;
        }
        return;
    }

    /* step2: 门已让路，发测点+汇总 */
    if (s_sendResultStep == 2)
    {
        SendRotatorResultAuto_depth((void *)DTU_TYPE);
        g_lastLocalFlow.valid = 0;
        LogLocalWaterFlow(g_addMeasureCtx.totalLineCount);
        final_result_begin_wait((uint16_t)ROTATOR, DTU_TYPE);
        s_sendResultStep = 3;
        return;
    }

    /* step3: 汇总 ACK 重试（不再碰门） */
    if (s_finalResultWaitActive && !s_finalResultAcked
        && nowMs - s_finalResultSendMs >= FINAL_RESULT_ACK_TIMEOUT_MS)
    {
        s_finalResultRetry++;
        if (s_finalResultRetry > FINAL_RESULT_ACK_MAX_RETRY) {
            LOG(LOG_WARNING,
                "SendFinalResult: ack timeout after %u retries, stop retry",
                (unsigned)FINAL_RESULT_ACK_MAX_RETRY);
            s_finalResultWaitActive = 0;
        } else {
            LOG(LOG_WARNING, "SendFinalResult: ack timeout retry=%u",
                (unsigned)s_finalResultRetry);
            send_final_result_uplink(s_finalResultElement,
                                     flow_q_to_wire(g_lastLocalFlow.qM3s),
                                     flow_avg_v_to_wire(g_lastLocalFlow.avgVMs),
                                     flow_area_to_wire(g_lastLocalFlow.areaM2),
                                     DTU_TYPE);
            s_finalResultSendMs = nowMs;
        }
    }

    if (!s_finalResultWaitActive || s_finalResultAcked
        || s_finalResultRetry > FINAL_RESULT_ACK_MAX_RETRY)
    {
        s_sendResultStep = 0;
        s_finalResultWaitActive = 0;
        g_addMeasureState = FSM_COMPLETE;
    }
}

/* AddMeasureFSM_Tick */
void AddMeasureFSM_Tick(void *arg)
{
    (void)arg;
    if (g_addMeasureState == FSM_IDLE || g_addMeasureState == FSM_PAUSED) return;
    if (g_addMeasureCtx.stopRequested) 
    { 
        fsm_stopMove(1); 
        fsm_stopMove(0); 
        /* 异常/停止不加关门，仅正常 SEND_RESULT_FINAL 收尾关门 */
        g_bDoorCtrlSendFlag = false;
        g_WebSendWaterLevel = 1000000;
        g_addMeasureState = FSM_COMPLETE; 
        return; 
    }
    if (g_addMeasureState == FSM_COMPLETE || g_addMeasureState == FSM_ERROR) 
    { 
        /* 加测结束后不再需要门轮询，避免线程遗留继续发读DI */
        g_bDoorCtrlSendFlag = false;
        if (g_WebSendWaterLevel != 1000000U) {
            LOG(LOG_INFO, "AddMeasure end: restore sensor water level (was %u)",
                (unsigned)g_WebSendWaterLevel);
            g_WebSendWaterLevel = 1000000;
        }
        /* 回到 IDLE 允许再次加测；不在本回调里 remove_timer，避免定时器线程死锁 */
        g_addMeasureState = FSM_IDLE;
        return; 
    }
    switch (g_addMeasureState) {
        case DOOR_OPEN_WAIT:         fsm_doorOpenWait();       break;
        case FSM_START:              fsm_start();              break;
        case FISH_UP_TO_TOP_INIT:    fsm_fishUpTopInit();      break;
        case CAR_BACK_TO_HOME_INIT:  fsm_carBackHomeInit();    break;
        case CAR_MOVE_TO_LINE:       fsm_carMoveToLine();      break;
        case FISH_DOWN_TO_DEPTH:     fsm_fishDownToDepth();    break;
        case MEASURE_WAIT_DURATION:  fsm_measureWaitDuration(); break;
        case MEASURE_NCS:            fsm_measureNCS();         break;
        case NEXT_DEPTH:             fsm_nextDepth();          break;
        case FISH_UP_AFTER_LINE:     fsm_fishUpAfterLine();    break;
        case NEXT_LINE:              fsm_nextLine();           break;
        case ALL_LINE_DONE:          fsm_allLineDone();        break;
        case FISH_UP_TO_TOP_FINAL:   fsm_fishUpTopInit();      break;
        case CAR_BACK_TO_HOME_FINAL: fsm_carBackHomeInit();    break;
        case FISH_DOWN_TO_PLATFORM:  fsm_fishDownToPlatform(); break;
        case SEND_RESULT_FINAL:      fsm_sendResultFinal();    break;
        default: break;
    }
}

// ============================================================
// 非阻塞运动控制状态机 (MotionFSM)
// 基于定时器Tick方式，避免阻塞任务队列
// ============================================================

MotionState g_motionState = MOTION_IDLE;
MotionCtx g_motionCtx;

/* Tick内部停止（不删除任何定时器，避免定时器线程死锁） */
static void motion_stop_self(void)
{
    AMPS_MotorStop(g_motionCtx.motorAddr);
    Relay_MotorBreak(g_motionCtx.brakeAddr);
    g_u8CatState = IS_STOP_STATE;
    g_u8FishState = IS_STOP_STATE;
}

/* 通用运动Tick回调（每50ms调用一次） */
void MotionFSM_Tick(void *arg)
{
    (void)arg;

    if (g_motionState == MOTION_IDLE || g_motionState == MOTION_DONE) return;

    if (!g_bRunning_State) {
        motion_stop_self();
        g_motionState = MOTION_DONE;
        return;
    }

    switch (g_motionState) {
        case MOTION_FISH_UP_POWERON:
            Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM1_ADDR);
            g_motionCtx.sleepStep = 0;
            g_motionState = MOTION_FISH_UP_SLEEP;
            break;

        case MOTION_FISH_UP_SLEEP:
            g_motionCtx.sleepStep++;
            if (g_motionCtx.sleepStep >= 20) {
                Relay_MotorUnbreak(RELAY_COM1_ADDR);
                AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish, (AMPS_BWD) * (GetFishRunRpm() * 10));
                g_u8FishState = IS_RUNNING_STATE;
                g_motionState = MOTION_FISH_UP_WAIT_LIMIT;
            }
            break;

        case MOTION_FISH_UP_WAIT_LIMIT:
            if (g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG) {
                g_motionCtx.debounceCount++;
                if (g_motionCtx.debounceCount >= 3) {
                    g_motionCtx.debounceCount = 0;
                    g_motionState = MOTION_FISH_UP_STOP;
                }
            } else {
                g_motionCtx.debounceCount = 0;
            }
            break;

        case MOTION_FISH_UP_STOP:
            mSleep(50);
            AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
            Relay_MotorBreak(RELAY_COM1_ADDR);
            g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG = 0;
            ResetEncoder(g_stDeviceAddr.u8Encoder_fish);
            g_VeroTotalSpace = 0;
            /* [修改] 手动测流结束: 收鱼完成后链式回仓(原Fish_up+Car_Goback并发busy) */
            if (g_motionChainCarHomeAfterFishUp)
            {
                g_motionChainCarHomeAfterFishUp = 0;
                g_motionCtx.motorAddr = g_stDeviceAddr.u8Motor_Drive_car;
                g_motionCtx.brakeAddr = RELAY_COM0_ADDR;
                g_motionCtx.debounceCount = 0;
                g_motionCtx.sleepStep = 0;
                g_bRunning_State = true;
                if (g_motionCtx.timeoutTimerId != -1)
                {
                    remove_timer(g_motionCtx.timeoutTimerId);
                    g_motionCtx.timeoutTimerId = -1;
                }
                g_motionCtx.timeoutTimerId = add_timer(MAX_WAITTIME * 5, 0, TIMER_ONCE, 0,
                    RunningTimeout, "Car_Goback_Home");
                g_motionState = MOTION_CAR_HOME_POWERON;
                break;
            }
            motion_stop_self();
            g_motionState = MOTION_DONE;
            break;

        case MOTION_CAR_HOME_POWERON:
            Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM0_ADDR);
            g_motionCtx.sleepStep = 0;
            g_motionState = MOTION_CAR_HOME_SLEEP;
            break;

        case MOTION_CAR_HOME_SLEEP:
            g_motionCtx.sleepStep++;
            if (g_motionCtx.sleepStep >= 20) {
                Relay_MotorUnbreak(RELAY_COM0_ADDR);
                AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car, (AMPS_BWD) * GetCarRunRpm() * 10);
                g_u8CatState = IS_RUNNING_STATE;
                g_motionState = MOTION_CAR_HOME_WAIT_LIMIT;
            }
            break;

        case MOTION_CAR_HOME_WAIT_LIMIT:
            if (g_stHoriDIStatusFlag.u8REV_LIMIT_FLAG) {
                g_motionCtx.debounceCount++;
                if (g_motionCtx.debounceCount >= 3) {
                    g_motionCtx.debounceCount = 0;
                    g_motionState = MOTION_CAR_HOME_STOP;
                }
            } else {
                g_motionCtx.debounceCount = 0;
            }
            break;

        case MOTION_CAR_HOME_STOP:
            mSleep(50);
            AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
            sleep(2);
            Relay_MotorBreak(RELAY_COM0_ADDR);
            /* [修改] 非阻塞回仓完成: 复位编码器；若g_bFishDownAfterHome=1则链式下放铅鱼到平台 */
            fsm_resetCarHomePosition();
            if (g_bFishDownAfterHome)
            {
                g_bFishDownAfterHome = 0;
                float target = (float)g_stSetCableway.stSetCablewayBody.u16PlatToCableHight;
                float cur = g_VeroTotalSpace * g_FishEncoderDistence;
                if (fabs(target - cur) >= g_FishDistenceAdjust)
                {
                    g_motionCtx.targetPos = target;
                    g_motionCtx.direction = (target > cur) ? AMPS_FWD : AMPS_BWD;
                    g_motionCtx.motorAddr = g_stDeviceAddr.u8Motor_Drive_fish;
                    g_motionCtx.brakeAddr = RELAY_COM1_ADDR;
                    g_motionCtx.debounceCount = 0;
                    g_motionCtx.sleepStep = 0;
                    g_motionCheckTouchGround = 1;
                    g_motionState = MOTION_FISH_MOVE_POWERON;
                    break;
                }
            }
            motion_stop_self();
            g_motionState = MOTION_DONE;
            break;

        case MOTION_CAR_MOVE_POWERON:
            Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM0_ADDR);
            g_motionCtx.sleepStep = 0;
            g_motionState = MOTION_CAR_MOVE_SLEEP;
            break;

        case MOTION_CAR_MOVE_SLEEP:
            g_motionCtx.sleepStep++;
            if (g_motionCtx.sleepStep >= 20) {
                if (!CarMoveAllowed(g_motionCtx.direction)) {
                    LOG(LOG_WARNING, "Car move blocked by limit, dir=%d", g_motionCtx.direction);
                    motion_stop_self();
                    g_motionState = MOTION_DONE;
                    break;
                }
                Relay_MotorUnbreak(RELAY_COM0_ADDR);
                AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car, g_motionCtx.direction * GetCarRunRpm() * 10);
                g_u8CatState = IS_RUNNING_STATE;
                g_motionState = MOTION_CAR_MOVE_RUN;
            }
            break;

        case MOTION_CAR_MOVE_RUN: {
            /* [修改] 水平手动移动: 使用motion_pos_reached按方向判到位 */
            float curPos = g_HoriTotalSpace * g_CarEncoderDistence;
            float rest = fabsf(g_motionCtx.targetPos - curPos);

            if (rest < SPEED_DOWN_DENTENCE && rest > g_CarDistenceAdjust) {
                AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car, g_motionCtx.direction * GetCarRunRpm() * 5);
            }
            if (motion_pos_reached(curPos, g_motionCtx.targetPos, (float)g_CarDistenceAdjust,
                                  g_motionCtx.direction)) {
                LOG(LOG_DEBUG, "CarMove done: cur=%.1f target=%.1f dir=%d",
                    curPos, g_motionCtx.targetPos, g_motionCtx.direction);
                mSleep(50);
                AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
                Relay_MotorBreak(RELAY_COM0_ADDR);
                motion_stop_self();
                g_motionState = MOTION_DONE;
                break;
            }
            if (g_motionCtx.direction == AMPS_FWD && g_stHoriDIStatusFlag.u8FWD_LIMIT_FLAG) {
                g_motionCtx.debounceCount++;
                if (g_motionCtx.debounceCount >= 3) {
                    AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
                    Relay_MotorBreak(RELAY_COM0_ADDR);
                    motion_stop_self();
                    g_motionState = MOTION_DONE;
                }
            } else if (g_motionCtx.direction == AMPS_BWD && g_stHoriDIStatusFlag.u8REV_LIMIT_FLAG) {
                g_motionCtx.debounceCount++;
                if (g_motionCtx.debounceCount >= 3) {
                    AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
                    Relay_MotorBreak(RELAY_COM0_ADDR);
                    motion_stop_self();
                    g_motionState = MOTION_DONE;
                }
            } else {
                g_motionCtx.debounceCount = 0;
            }
            break;
        }

        case MOTION_FISH_MOVE_POWERON:
            Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM1_ADDR);
            g_motionCtx.sleepStep = 0;
            g_motionState = MOTION_FISH_MOVE_SLEEP;
            break;

        case MOTION_FISH_MOVE_SLEEP:
            g_motionCtx.sleepStep++;
            if (g_motionCtx.sleepStep >= 20) {
                if (!FishMoveAllowed(g_motionCtx.direction)) {
                    LOG(LOG_WARNING, "Fish move blocked by limit, dir=%d", g_motionCtx.direction);
                    motion_stop_self();
                    g_motionState = MOTION_DONE;
                    break;
                }
                Relay_MotorUnbreak(RELAY_COM1_ADDR);
                AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish, g_motionCtx.direction * GetFishRunRpm() * 10);
                g_u8FishState = IS_RUNNING_STATE;
                g_motionState = MOTION_FISH_MOVE_RUN;
            }
            break;

        case MOTION_FISH_MOVE_RUN: {
            /* [修改] 垂直手动移动: motion_pos_reached按方向判到位；仅FWD限位停，不用转子触底DI */
            float curPos = g_VeroTotalSpace * g_FishEncoderDistence;
            float rest = fabsf(g_motionCtx.targetPos - curPos);

            if (rest < SPEED_DOWN_DENTENCE && rest > g_FishDistenceAdjust) {
                AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish, g_motionCtx.direction * GetFishRunRpm() * 3);
            }
            if (motion_pos_reached(curPos, g_motionCtx.targetPos, (float)g_FishDistenceAdjust,
                                  g_motionCtx.direction)) {
                LOG(LOG_DEBUG, "FishMove done: cur=%.1f target=%.1f dir=%d",
                    curPos, g_motionCtx.targetPos, g_motionCtx.direction);
                mSleep(50);
                AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
                Relay_MotorBreak(RELAY_COM1_ADDR);
                g_motionCheckTouchGround = 0;
                motion_stop_self();
                g_motionState = MOTION_DONE;
                break;
            }
            if (g_motionCtx.direction == AMPS_FWD
                && (g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG
                    || (g_motionCheckTouchGround && g_stSwitchStatus.u16TouchGround))) {
                /* [修改] Web手动仅FWD限位；测流后下放平台时增加触底DI */
                g_motionCtx.debounceCount++;
                if (g_motionCtx.debounceCount >= 3) {
                    AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
                    Relay_MotorBreak(RELAY_COM1_ADDR);
                    if (g_motionCheckTouchGround) {
                        g_stVeroDIStatusFlag.u8FWD_LIMIT_FLAG = 0;
                    }
                    Relay_LightCtrl(RED_LIGHT, TURN_ON);
                    g_motionCheckTouchGround = 0;
                    motion_stop_self();
                    g_motionState = MOTION_DONE;
                }
            } else if (g_motionCtx.direction == AMPS_BWD && g_stVeroDIStatusFlag.u8REV_LIMIT_FLAG) {
                g_motionCtx.debounceCount++;
                if (g_motionCtx.debounceCount >= 3) {
                    AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
                    Relay_MotorBreak(RELAY_COM1_ADDR);
                    motion_stop_self();
                    g_motionState = MOTION_DONE;
                }
            } else {
                g_motionCtx.debounceCount = 0;
            }
            break;
        }

        default:
            break;
    }
}

/* 清理旧运动定时器（从外部线程调用） */
static void motion_cleanup_old(void)
{
    if (g_motionCtx.timerId != -1) {
        remove_timer(g_motionCtx.timerId);
        g_motionCtx.timerId = -1;
    }
    if (g_motionCtx.timeoutTimerId != -1) {
        remove_timer(g_motionCtx.timeoutTimerId);
        g_motionCtx.timeoutTimerId = -1;
    }
}

int Fish_up_nonblock(void)
{
    if (g_motionState != MOTION_IDLE && g_motionState != MOTION_DONE) {
        LOG(LOG_ERROR, "Fish_up: motion state machine busy (state=%d)", g_motionState);
        return ERROR;
    }
    motion_cleanup_old();
    memset(&g_motionCtx, 0, sizeof(MotionCtx));
    g_motionCtx.timerId = -1;
    g_motionCtx.timeoutTimerId = -1;
    g_motionCtx.motorAddr = g_stDeviceAddr.u8Motor_Drive_fish;
    g_motionCtx.brakeAddr = RELAY_COM1_ADDR;

    g_bRunning_State = true;
    g_motionState = MOTION_FISH_UP_POWERON;
    g_motionCtx.timerId = add_timer(10, 50, TIMER_REPEAT, 0, MotionFSM_Tick, NULL);
    if (g_motionCtx.timerId == -1) { g_motionState = MOTION_IDLE; return ERROR; }
    g_motionCtx.timeoutTimerId = add_timer(MAX_WAITTIME * 2, 0, TIMER_ONCE, 0, RunningTimeout, "Fish_up");
    return SUCCESS;
}

/* [修改] 手动测流停止后: 收鱼→回仓→下放平台，MotionFSM单链执行 */
int Fish_up_then_home_platform_nonblock(void)
{
    if (g_motionState != MOTION_IDLE && g_motionState != MOTION_DONE) {
        LOG(LOG_ERROR, "Fish_up_then_home: motion busy (state=%d)", g_motionState);
        return ERROR;
    }
    g_bFishDownAfterHome = 1;
    g_motionChainCarHomeAfterFishUp = 1;
    return Fish_up_nonblock();
}

int Car_Goback_Home_nonblock(void)
{
    if (g_motionState != MOTION_IDLE && g_motionState != MOTION_DONE) {
        LOG(LOG_ERROR, "Car_Goback_Home: motion state machine busy (state=%d)", g_motionState);
        return ERROR;
    }
    motion_cleanup_old();
    memset(&g_motionCtx, 0, sizeof(MotionCtx));
    g_motionCtx.timerId = -1;
    g_motionCtx.timeoutTimerId = -1;
    g_motionCtx.motorAddr = g_stDeviceAddr.u8Motor_Drive_car;
    g_motionCtx.brakeAddr = RELAY_COM0_ADDR;

    g_bRunning_State = true;
    g_motionState = MOTION_CAR_HOME_POWERON;
    g_motionCtx.timerId = add_timer(10, 50, TIMER_REPEAT, 0, MotionFSM_Tick, NULL);
    if (g_motionCtx.timerId == -1) { g_motionState = MOTION_IDLE; return ERROR; }
    g_motionCtx.timeoutTimerId = add_timer(MAX_WAITTIME * 5, 0, TIMER_ONCE, 0, RunningTimeout, "Car_Goback_Home");
    return SUCCESS;
}

int CarMovetoAim_nonblock(BYTE *pData)
{
    PauseAddMeasure();
    if (g_motionState != MOTION_IDLE && g_motionState != MOTION_DONE) {
        LOG(LOG_ERROR, "CarMovetoAim: motion state machine busy (state=%d)", g_motionState);
        return ERROR;
    }
    CarMove stCarMove;
    memset(&stCarMove, 0, sizeof(CarMove));
    memcpy(&stCarMove.i32StartDistence, pData, sizeof(stCarMove.i32StartDistence));

    float targetPos = (float)stCarMove.i32StartDistence + g_stSetCableway.stSetCablewayBody.i16StartPointDes;
    float curPos = g_HoriTotalSpace * g_CarEncoderDistence;
    int dir;
    if (targetPos > curPos) dir = AMPS_FWD;
    else if (targetPos < curPos) dir = AMPS_BWD;
    else return SUCCESS;

    if (!CarMoveAllowed(dir))
    {
        LOG(LOG_WARNING, "CarMovetoAim blocked by limit, dir=%d", dir);
        return ERROR;
    }

    memset(&g_motionCtx, 0, sizeof(MotionCtx));
    g_motionCtx.timerId = -1;
    g_motionCtx.timeoutTimerId = -1;
    g_motionCtx.targetPos = targetPos;
    g_motionCtx.direction = dir;
    g_motionCtx.motorAddr = g_stDeviceAddr.u8Motor_Drive_car;
    g_motionCtx.brakeAddr = RELAY_COM0_ADDR;

    g_bRunning_State = true;
    g_motionState = MOTION_CAR_MOVE_POWERON;
    g_motionCtx.timerId = add_timer(10, 50, TIMER_REPEAT, 0, MotionFSM_Tick, NULL);
    if (g_motionCtx.timerId == -1) { g_motionState = MOTION_IDLE; return ERROR; }
    g_motionCtx.timeoutTimerId = add_timer(MAX_WAITTIME * 5, 0, TIMER_ONCE, 0, RunningTimeout, "CarMovetoAim");
    return SUCCESS;
}

int FishMovetoAim_nonblock(BYTE *pData)
{
    PauseAddMeasure();
    if (g_motionState != MOTION_IDLE && g_motionState != MOTION_DONE) {
        LOG(LOG_ERROR, "FishMovetoAim: motion state machine busy (state=%d)", g_motionState);
        return ERROR;
    }
    FishMove stFishMove;
    memset(&stFishMove, 0, sizeof(FishMove));
    memcpy(&stFishMove.u16FishPos, pData, sizeof(stFishMove.u16FishPos));

    float targetPos = (float)stFishMove.u16FishPos;
    float curPos = g_VeroTotalSpace * g_FishEncoderDistence;
    int dir;
    if (targetPos > curPos) dir = AMPS_FWD;
    else if (targetPos < curPos) dir = AMPS_BWD;
    else return SUCCESS;

    if (!FishMoveAllowed(dir))
    {
        LOG(LOG_WARNING, "FishMovetoAim blocked by limit, dir=%d", dir);
        return ERROR;
    }

    LOG(LOG_INFO, "FishMovetoAim: cur=%.1fcm target=%.1fcm dir=%d adjust=%u",
        curPos, targetPos, dir, (unsigned)g_FishDistenceAdjust);

    memset(&g_motionCtx, 0, sizeof(MotionCtx));
    g_motionCtx.timerId = -1;
    g_motionCtx.timeoutTimerId = -1;
    g_motionCtx.targetPos = targetPos;
    g_motionCtx.direction = dir;
    g_motionCtx.motorAddr = g_stDeviceAddr.u8Motor_Drive_fish;
    g_motionCtx.brakeAddr = RELAY_COM1_ADDR;

    g_bRunning_State = true;
    g_motionState = MOTION_FISH_MOVE_POWERON;
    g_motionCtx.timerId = add_timer(10, 50, TIMER_REPEAT, 0, MotionFSM_Tick, NULL);
    if (g_motionCtx.timerId == -1) { g_motionState = MOTION_IDLE; return ERROR; }
    g_motionCtx.timeoutTimerId = add_timer(MAX_WAITTIME * 3, 0, TIMER_ONCE, 0, RunningTimeout, "FishMovetoAim");
    return SUCCESS;
}


// 辅助函数：发送子设备配置文件
void SendSubDeviceConfigFile(uint8_t subDeviceType, const char* filename, uint32_t command_seri, int type)
{
    
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        // 文件不存在或无法打开
        LOG(LOG_ERROR, "Failed to open file: %s", filename);
        SendResultFrame(GET_SUB_DEVICE_CONFIG, command_seri, ERROR, type);
        return;
    }
    
    // 获取文件大小
    off_t file_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    
    // 分配缓冲区
    uint8_t* buffer = malloc(file_size);
    if (!buffer) 
    {   
        LOG(LOG_ERROR, "Failed to malloc");
        close(fd);
        SendResultFrame(GET_SUB_DEVICE_CONFIG, command_seri, ERROR, type);
        return;
    }
    
    // 读取文件内容
    ssize_t bytes_read = read(fd, buffer, file_size);
    close(fd);
    
    if (bytes_read != file_size) 
    {
        LOG(LOG_ERROR, "Failed to read file");
        free(buffer);
        SendResultFrame(GET_SUB_DEVICE_CONFIG, command_seri, ERROR, type);
        return;
    }
    
    // 构建上报包，结构: SeriverCommandHead(20) + u8SubDevice(1) + u16FileLen(2) + fileData(file_size)
    uint32_t data_len = sizeof(SeriverCommandHead) + sizeof(uint8_t) + sizeof(uint16_t) + file_size;
    ReportSubDeviceConfig *pReport = (ReportSubDeviceConfig *)malloc(data_len);
    if (!pReport) 
    {
        LOG(LOG_DEBUG, "malloc pReport error");
        free(buffer);
        SendResultFrame(GET_SUB_DEVICE_CONFIG, command_seri, ERROR, type);
        return;
    }
    
    // 填充上报包
    pReport->stCommandHead.u32Command = REPORT_SUB_DEVICE_CONFIG;
    pReport->stCommandHead.u32CommandSeri = g_u32CommansSeri++;
    pReport->stCommandHead.u32CommandLen = data_len + 4;
    memcpy(pReport->stCommandHead.au8StationID, g_aStationID, sizeof(g_aStationID));
    
    pReport->u8SubDevice = subDeviceType;
    pReport->u16FileLen = file_size;
    memcpy(pReport->u8pFileData, buffer, file_size);
    LOG(LOG_DEBUG, "SendSubDeviceConfigFile file name is %s,file size is %d",filename,file_size);
    LOG(LOG_DEBUG, "SendFile: %s, %d bytes,data_len is %d",filename,file_size,data_len);
    
    // 发送数据（SendZigbeeData内部会添加4字节尾部0xFFFFFFFF，所以data_len不包含尾部）
    SendZigbeeData(UP_EXPAND_CODE, (uint8_t *)pReport, data_len, type);
    
    // 释放资源
    free(buffer);
    free(pReport);
    
    // 发送执行结果
    SendResultFrame(GET_SUB_DEVICE_CONFIG, command_seri, SUCCESS, type);
}

/*
 * 日志上报会话：发一包等平台命令字41确认后再发下一包。
 */
typedef struct {
    volatile int active;
    uint8_t deviceType;
    uint8_t logLevel;
    uint16_t curPkt;
    volatile uint16_t ackedPkt;
    volatile int abort;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
} LogUploadSession;

static LogUploadSession g_logUp = {
    .active = 0,
    .mtx = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
};

int LogUpload_IsBusy(void)
{
    return g_logUp.active ? 1 : 0;
}

static int log_upload_wait_ack(uint16_t expectPkt, int timeout_ms)
{
    struct timespec ts;
    int rc;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return -1;
    }
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&g_logUp.mtx);
    while (g_logUp.ackedPkt != expectPkt && !g_logUp.abort) {
        rc = pthread_cond_timedwait(&g_logUp.cond, &g_logUp.mtx, &ts);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&g_logUp.mtx);
            return -1;
        }
    }
    rc = (g_logUp.ackedPkt == expectPkt && !g_logUp.abort) ? 0 : -1;
    pthread_mutex_unlock(&g_logUp.mtx);
    return rc;
}

void LogUpload_OnAck(uint32_t reportCmd, uint8_t deviceType, uint8_t logLevel, uint16_t pktSeri)
{
    int cmdOk;

    pthread_mutex_lock(&g_logUp.mtx);
    if (!g_logUp.active) {
        pthread_mutex_unlock(&g_logUp.mtx);
        LOG(LOG_WARNING,
            "LogUpload_OnAck drop (inactive) cmd=0x%x dev=%u lv=%u pkt=%u",
            (unsigned)reportCmd, (unsigned)deviceType, (unsigned)logLevel,
            (unsigned)pktSeri);
        return;
    }
    /* 平台实抓：上报命令字填 REPORT_LOG(0xFFFFFFEC)；文档亦写过 28，两者都认 */
    cmdOk = (reportCmd == (uint32_t)REPORT_LOG || reportCmd == (uint32_t)GTE_LOG);
    if (cmdOk
        && deviceType == g_logUp.deviceType
        && logLevel == g_logUp.logLevel
        && pktSeri == g_logUp.curPkt) {
        g_logUp.ackedPkt = pktSeri;
        pthread_cond_signal(&g_logUp.cond);
        LOG(LOG_DEBUG, "LogUpload_OnAck ok pkt=%u cmd=0x%x",
            (unsigned)pktSeri, (unsigned)reportCmd);
    } else {
        LOG(LOG_WARNING,
            "LogUpload_OnAck ignore cmd=0x%x dev=%u lv=%u pkt=%u (expect dev/lv/pkt %u/%u/%u)",
            (unsigned)reportCmd, (unsigned)deviceType, (unsigned)logLevel,
            (unsigned)pktSeri, (unsigned)g_logUp.deviceType,
            (unsigned)g_logUp.logLevel, (unsigned)g_logUp.curPkt);
    }
    pthread_mutex_unlock(&g_logUp.mtx);
}

static int log_upload_send_one(uint8_t deviceType, uint8_t logLevel,
                               const uint8_t *payload, size_t totalLen,
                               uint16_t totalPkts, uint16_t pktSeri,
                               size_t offset, size_t chunk, int type)
{
    const size_t hdr_extra = sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t)
                           + sizeof(uint16_t) + sizeof(uint16_t);
    uint32_t data_len = (uint32_t)(sizeof(SeriverCommandHead) + hdr_extra + chunk);
    ReportLog *pReport = (ReportLog *)malloc(data_len);
    if (pReport == NULL) {
        return -1;
    }

    memset(pReport, 0, data_len);
    pReport->stCommandHead.u32Command = REPORT_LOG;
    pReport->stCommandHead.u32CommandSeri = g_u32CommansSeri++;
    pReport->stCommandHead.u32CommandLen = data_len + 4;
    memcpy(pReport->stCommandHead.au8StationID, g_aStationID, sizeof(g_aStationID));
    pReport->u8DeviceType = deviceType;
    pReport->u8LogLevel = logLevel;
    pReport->u32FileLen = (uint32_t)totalLen;
    pReport->u16TotalPkts = totalPkts;
    pReport->u16PktSeri = pktSeri;
    memcpy(pReport->u8pFileData, payload + offset, chunk);

    LOG(LOG_DEBUG,
        "SendLogFile: TX REPORT_LOG pkt=%u/%u chunk=%u fileLen=%u cmdSeri=%u",
        (unsigned)pktSeri, (unsigned)totalPkts, (unsigned)chunk,
        (unsigned)totalLen, (unsigned)pReport->stCommandHead.u32CommandSeri);
    SendZigbeeData(UP_EXPAND_CODE, (uint8_t *)pReport, (uint16_t)data_len, type);
    free(pReport);
    return 0;
}

/*
 * 按新协议上报运行日志（0xFFFFFFEC）：
 * 设备类型 + 日志级别 + 总长度4 + 总包数2 + 包序号2 + 内容
 * 每包等待平台命令字41确认，超时重发。
 */
void SendLogFile(uint8_t deviceType, uint8_t logLevel, uint32_t command_seri, int type)
{
    uint8_t *payload = NULL;
    size_t totalLen = 0;
    uint16_t totalPkts;
    uint16_t pkt;
    int fail = 0;

    pthread_mutex_lock(&g_logUp.mtx);
    if (g_logUp.active) {
        pthread_mutex_unlock(&g_logUp.mtx);
        LOG(LOG_WARNING, "SendLogFile: busy");
        SendResultFrame(GTE_LOG, command_seri, (uint8_t)ERROR, type);
        return;
    }
    g_logUp.active = 1;
    g_logUp.deviceType = deviceType;
    g_logUp.logLevel = logLevel;
    g_logUp.curPkt = 0;
    g_logUp.ackedPkt = 0;
    g_logUp.abort = 0;
    pthread_mutex_unlock(&g_logUp.mtx);
    /* 上传期间不写 log.lg，避免把分包/hex 再灌进待传日志 */
    log_set_flash_quiet(1);

    if (log_export_filtered(logLevel, &payload, &totalLen) != 0) {
        LOG(LOG_ERROR, "SendLogFile: export fail");
        fail = 1;
        goto out;
    }

    LOG(LOG_DEBUG, "SendLogFile: dev=%u level=%u len=%u",
        (unsigned)deviceType, (unsigned)logLevel, (unsigned)totalLen);

    if (totalLen == 0) {
        SendResultFrame(GTE_LOG, command_seri, SUCCESS, type);
        goto out;
    }

    totalPkts = (uint16_t)((totalLen + LOG_REPORT_CHUNK - 1) / LOG_REPORT_CHUNK);
    if (totalPkts == 0) {
        totalPkts = 1;
    }

    for (pkt = 1; pkt <= totalPkts; pkt++) {
        size_t offset = (size_t)(pkt - 1) * LOG_REPORT_CHUNK;
        size_t chunk = totalLen - offset;
        int retry;
        int acked = 0;

        if (chunk > LOG_REPORT_CHUNK) {
            chunk = LOG_REPORT_CHUNK;
        }

        pthread_mutex_lock(&g_logUp.mtx);
        g_logUp.curPkt = pkt;
        g_logUp.ackedPkt = 0;
        pthread_mutex_unlock(&g_logUp.mtx);

        for (retry = 0; retry < LOG_ACK_MAX_RETRY; retry++) {
            if (log_upload_send_one(deviceType, logLevel, payload, totalLen,
                                    totalPkts, pkt, offset, chunk, type) != 0) {
                LOG(LOG_ERROR, "SendLogFile: send fail pkt=%u", (unsigned)pkt);
                fail = 1;
                goto out;
            }
            if (log_upload_wait_ack(pkt, LOG_ACK_TIMEOUT_MS) == 0) {
                acked = 1;
                break;
            }
            LOG(LOG_WARNING, "SendLogFile: ack timeout pkt=%u retry=%d",
                (unsigned)pkt, retry + 1);
        }
        if (!acked) {
            fail = 1;
            goto out;
        }
        /* 非末包：ACK 后再歇一会，避免把平台 ACK 队列堆满 */
        if (pkt < totalPkts && LOG_UPLOAD_PKT_GAP_MS > 0) {
            mSleep(LOG_UPLOAD_PKT_GAP_MS);
        }
    }

    SendResultFrame(GTE_LOG, command_seri, SUCCESS, type);
    LOG(LOG_DEBUG, "SendLogFile: done pkts=%u", (unsigned)totalPkts);

out:
    if (fail) {
        SendResultFrame(GTE_LOG, command_seri, (uint8_t)ERROR, type);
    }
    if (payload != NULL) {
        free(payload);
    }
    log_set_flash_quiet(0);
    pthread_mutex_lock(&g_logUp.mtx);
    g_logUp.active = 0;
    g_logUp.abort = 0;
    pthread_mutex_unlock(&g_logUp.mtx);
}


// 备份配置文件
int BackupConfigFile(const char* filename)
{
    char src_path[256];
    char bak_path[256];
    memset(src_path, 0, sizeof(src_path));
    memset(bak_path, 0, sizeof(bak_path));
    
    GetAppDir(src_path);
    strncat(src_path, filename, strlen(filename));
    
    GetAppDir(bak_path);
    strncat(bak_path, filename, strlen(filename));
    strncat(bak_path, "_bak", 4);
    
    // 如果原文件不存在，无需备份
    if (access(src_path, F_OK) != 0)
    {
        LOG(LOG_DEBUG, "BackupConfigFile: source file %s not exist, skip backup", src_path);
        return 0;
    }
    
    // 先删除旧的备份文件
    unlink(bak_path);
    
    // 复制文件
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0)
    {
        LOG(LOG_DEBUG, "BackupConfigFile: open %s failed", src_path);
        return 1;
    }
    
    int bak_fd = open(bak_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (bak_fd < 0)
    {
        LOG(LOG_DEBUG, "BackupConfigFile: create %s failed", bak_path);
        close(src_fd);
        return 1;
    }
    
    char buf[1024];
    ssize_t n;
    while ((n = read(src_fd, buf, sizeof(buf))) > 0)
    {
        write(bak_fd, buf, n);
    }
    
    close(src_fd);
    close(bak_fd);
    
    LOG(LOG_DEBUG, "BackupConfigFile: backup %s -> %s success", src_path, bak_path);
    return 0;
}

// 保存配置文件
int SaveConfigFile(const char* filename, uint16_t file_len, uint8_t* file_data) 
{
    char full_path[256];
    memset(full_path, 0, sizeof(full_path));
    GetAppDir(full_path);
    strncat(full_path, filename, strlen(filename));
    
    // 先备份原文件
    BackupConfigFile(filename);
    
    int fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) 
    {
        LOG(LOG_DEBUG,"%s open failed",full_path);
        return 1;  // 失败
    }
    
    ssize_t written = write(fd, file_data, file_len);
    close(fd);
    
    if (written == file_len) {
        LOG(LOG_DEBUG, "File saved successfully: %s, size: %d", full_path, file_len);
        return 0;  // 成功
    } else {
        LOG(LOG_DEBUG, "File save failed: %s, expected: %d, written: %d", 
            full_path, file_len, written);
        return 1;  // 失败
    }
}
