// #include "pro_head.h"
/*
 * [2026-06-22 修改说明 - Command_proc.c]
 * 1. 手动测流下放: 增加转子触底DI(u16TouchGround)停转
 * 2. 测完回仓: 链式下放铅鱼到平台(Fish_up_then_home_platform_nonblock)
 * 3. DTU/触摸屏/网页: 编码器距离 - i16StartPointDes
 * 4. 手动测流停止: WaitRotatorSecondSignal后再取基准值
 */
#include "zigbee_dataproc.h"
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
#include "WebSeriver.h"
#include "SocketLink.h"
#include "Encoder.h"
#include "SL651_2014.h"
#include "Timertask.h"
#include "AMPSTechMotor.h"
#include "TaskQueue.h"
#include <sys/reboot.h>
#include "LinkList.h"
#include "OtaUpgrade.h"
#include "UplinkOffline.h"

extern List *g_pstAutoRotatorDepthList;   // 分层转子测流数据链表
uint8_t g_sendcount = 0;
/* OTA ACK/结果帧：busy 时仍打上行 hex，便于对照助手抓包 */
static int g_forceUplinkHexLog = 0;

/* OTA 期间关掉整包 hex；日志上报期间保留上下行 hex，便于确认收发 */
static int web_hex_quiet(void)
{
    return OtaUpgrade_IsBusy();
}

/* 扩展帧：长度字段含 FrameSeri+BCD时间+Signature；其后为业务体+ETX+CRC */
#define EXTEND_DATA_LEN_OVERHEAD  (sizeof(uint16_t) + sizeof(BCD_TIME) + sizeof(uint8_t))
/* 协议文档远程升级单包内容可达 8192，帧缓冲需大于头+体+尾 */
#define MAX_EXTEND_FRAME_LEN      9000U

static uint16_t extend_body_len(uint16_t dataLen)
{
    if (dataLen <= EXTEND_DATA_LEN_OVERHEAD) {
        return 0;
    }
    return (uint16_t)(dataLen - EXTEND_DATA_LEN_OVERHEAD);
}

static uint16_t extend_frame_len(uint16_t dataLen)
{
    uint32_t bodyLen = extend_body_len(dataLen);
    uint32_t total = (uint32_t)sizeof(DOWNFrameHead) + bodyLen + (uint32_t)sizeof(DOWNFrametail);
    if (total > MAX_EXTEND_FRAME_LEN) {
        return 0;
    }
    return (uint16_t)total;
}

/* 旧业务下行：按 FlagAndLen 估算整帧长度（含 DOWNFrametail） */
static uint16_t down_business_frame_len(uint16_t dataLen)
{
    uint32_t total;

    if (dataLen == 0) {
        return 0;
    }
    if (dataLen == sizeof(BCD_TIME) + sizeof(uint16_t)) {
        /* 短确认无 Signature：头到 Signature 前 + ETX + CRC */
        total = (uint32_t)sizeof(DOWNFrameHead) - sizeof(uint8_t) +
                (uint32_t)sizeof(DOWNFrametail);
    } else {
        uint16_t ctx = (uint16_t)(dataLen - sizeof(BCD_TIME) - sizeof(uint16_t) -
                                  sizeof(uint8_t) + sizeof(DOWNFrametail));
        total = (uint32_t)sizeof(DOWNFrameHead) + ctx;
    }
    if (total > MAX_EXTEND_FRAME_LEN) {
        return 0;
    }
    return (uint16_t)total;
}

/*
 * 仅合法网页业务下行才允许校时：
 * - 功能码必须是下行扩展 0xF1（粘包常见 0xFF 等一律拒绝）
 * - DataStart 必须是 STX/ETB/ETX（错位时常变成 0x7E）
 * - 密码 0x1234
 * - BCD 年月日时分秒合法
 */
static int downlink_frame_ok_for_timesync(const DOWNFrameHead *h)
{
    if (h == NULL) {
        return 0;
    }
    if (h->u8FunctionCode != DOWN_EXPAND_CODE) {
        return 0;
    }
    if (h->u8DataStart != STX && h->u8DataStart != ETB && h->u8DataStart != ETX) {
        return 0;
    }
    /* 线上为 12 34，小端读入为 0x3412 */
    if (h->u16Password != swap_u16(0x1234)) {
        return 0;
    }
    if (!bcd_time_is_valid(&h->BCD_time)) {
        return 0;
    }
    return 1;
}

/**
 * 扩展上行：Signature=命令字，正文后仅 ETX+CRC（无 0xFFFFFFFF 业务尾）
 */
uint16_t SendUpLinkToServer(uint8_t u8FunctionCode, uint8_t u8CommandCode,
                            uint8_t *pData, uint16_t datalen, int type)
{
    uint8_t *buffer = NULL;
    UPFrameHead stUpframehead;
    uint8_t u8EOF = ETX;
    uint16_t u16CRC = 0;
    uint16_t u16Seri = 0;
    int bufferDataLen = 0;
    int fd = -1;
    int viaSocket = 0;

    if (pData == NULL || datalen == 0) {
        return 0xFFFFU;
    }
    if (type == DTU_TYPE) {
        viaSocket = WebLink_IsSocketMode();
        if (viaSocket) {
            if (!SocketLink_IsConnected()) {
                return 0xFFFFU;
            }
        } else {
            fd = g_stDevicelist.UartList[g_stDevicelist.HOST_COMPUTER].i32fd;
            if (fd <= 0) {
                return 0xFFFFU;
            }
        }
    } else if (type == ZIGBEE_TYPE) {
        fd = g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd;
        if (fd <= 0) {
            return 0xFFFFU;
        }
    } else {
        return 0xFFFFU;
    }

    buffer = (uint8_t *)malloc((size_t)datalen + sizeof(UPFrameHead) + sizeof(DOWNFrametail));
    if (buffer == NULL) {
        return 0xFFFFU;
    }

    memset(&stUpframehead, 0, sizeof(stUpframehead));
    stUpframehead.u16SOH = 0x7E7E;
    stUpframehead.u8CentreStaID = g_CenterStationID;
    memcpy(stUpframehead.aRTUID, g_RTUID, sizeof(g_RTUID));
    stUpframehead.u16Password = swap_u16(0x1234);
    stUpframehead.u8FunctionCode = u8FunctionCode;
    stUpframehead.u8Signature = u8CommandCode;
    get_bcd_time((uint8_t *)&stUpframehead.BCD_time);
    stUpframehead.u8DataStart = STX;
    u16Seri = (uint16_t)g_u32CommansSeri++;
    stUpframehead.u16FrameSeri = swap_u16(u16Seri);
    {
        uint16_t lenField = (uint16_t)((datalen + EXTEND_DATA_LEN_OVERHEAD) & 0x0FFF);
        stUpframehead.u16FlagAndLen = swap_u16(lenField);
    }

    memcpy(buffer, &stUpframehead, sizeof(UPFrameHead));
    memcpy(buffer + sizeof(UPFrameHead), pData, datalen);
    memcpy(buffer + sizeof(UPFrameHead) + datalen, &u8EOF, 1);
    bufferDataLen = (int)(sizeof(UPFrameHead) + datalen + 1);
    u16CRC = CRC16_Modbus(buffer, (uint16_t)bufferDataLen);
    memcpy(buffer + bufferDataLen, &u16CRC, sizeof(u16CRC));
    bufferDataLen += (int)sizeof(u16CRC);

    if (type == DTU_TYPE) {
        if (WebLink_Send(buffer, bufferDataLen) != bufferDataLen) {
            free(buffer);
            return 0xFFFFU;
        }
    } else if (write(fd, buffer, (size_t)bufferDataLen) != (size_t)bufferDataLen) {
        free(buffer);
        return 0xFFFFU;
    }
    if (!web_hex_quiet()) {
        LOG(LOG_DEBUG, "SendUpLinkToServer cmd=%u len=%u via=%s",
            (unsigned)u8CommandCode, (unsigned)datalen,
            (type == DTU_TYPE && viaSocket) ? "socket" : "uart");
        LOG_ARRAY(LOG_DEBUG, buffer, bufferDataLen, "%02x");
    }
    free(buffer);
    return u16Seri;
}
pthread_mutex_t g_upMutex;
pthread_mutex_t g_zigbeeRespondQueueMutex;
uint8_t g_waterLevelCount = 0;
uint8_t g_u8HoriCtrlState = IS_STOP_STATE;
uint8_t g_u8CatState = IS_STOP_STATE;
uint8_t g_u8FishState = IS_STOP_STATE;

int g_sendSeqNum = -1;                 // 发送的需要响应的报文的序号
int g_recvSeqNum = -1;                 // 接收到的响应报文中响应的报文的序号
int g_zigbeePacketCount = 1;           // 报文的报文序号
int g_localState = 0;
short g_localDistance = 0;
int g_localCurPos = 0;
uint32_t g_u32CommansSeri = 0;
volatile int emergency_stop = 0;
uint16_t g_u16WaterLevelArray[MAX_WATERLEVEL_COUNT] = {0};
RotatorManuMeaStart g_stRotatormeasureStart;
KFIFO_ID g_MultiFrameFifo;   // 多帧数据接收缓冲区
uint16_t g_u16PrevFrameSeri = 0;// 上一帧的帧序号
/* 最近一帧下行 SL651 流水号（与线上字节序一致，用于链路确认回显） */
static uint16_t g_u16LastDownFrameSeri = 0;
/* 当前 OTA 会话对应的下行流水号（stall 重发时 LastDown 可能已被其它帧覆盖） */
static uint16_t g_u16OtaDownFrameSeri = 0;
/* SendZigbeeData：下一帧用回显流水号（OTA 结果应答） */
static int g_bEchoDownFrameSeri = 0;
static uint16_t g_u16EchoFrameSeri = 0;

void OtaUpgrade_PrepareResultEcho(void)
{
    g_u16EchoFrameSeri = (g_u16OtaDownFrameSeri != 0U)
                             ? g_u16OtaDownFrameSeri
                             : g_u16LastDownFrameSeri;
    g_bEchoDownFrameSeri = 1;
}

/*
 * SL651 链路短确认：回显下行 FrameSeri，长度=8（流水号+BCD），无业务体、无 Signature、无 0xFFFFFFFF。
 * 网页对状态上报也用对称的 F2 80 08；设备对下行命令回 F2 00 08。
 */
static int SendLinkConfirm(uint16_t u16FrameSeriRaw, int type)
{
    uint8_t buffer[32];
    UPFrameHead stUp;
    uint16_t u16CRC = 0;
    int headLen;
    int fd = -1;
    int total;

    if (type == DTU_TYPE) {
        if (WebLink_IsSocketMode()) {
            if (!SocketLink_IsConnected()) {
                return ERROR;
            }
        } else {
            fd = g_stDevicelist.UartList[g_stDevicelist.HOST_COMPUTER].i32fd;
            if (fd <= 0) {
                return ERROR;
            }
        }
    } else if (type == ZIGBEE_TYPE) {
        fd = g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd;
        if (fd <= 0) {
            return ERROR;
        }
    } else {
        return ERROR;
    }

    memset(&stUp, 0, sizeof(stUp));
    stUp.u16SOH = 0x7E7E;
    stUp.u8CentreStaID = g_CenterStationID;
    memcpy(stUp.aRTUID, g_RTUID, sizeof(g_RTUID));
    stUp.u16Password = swap_u16(0x1234);
    stUp.u8FunctionCode = UP_EXPAND_CODE;
    stUp.u16FlagAndLen = swap_u16(8); /* FrameSeri(2)+BCD(6) */
    stUp.u8DataStart = STX;
    stUp.u16FrameSeri = u16FrameSeriRaw; /* 原样回显，勿再 swap */
    get_bcd_time((uint8_t *)&stUp.BCD_time);

    headLen = (int)((uint8_t *)&stUp.u8Signature - (uint8_t *)&stUp);
    memcpy(buffer, &stUp, (size_t)headLen);
    buffer[headLen] = ETX;
    total = headLen + 1;
    u16CRC = CRC16_Modbus(buffer, (uint16_t)total);
    memcpy(buffer + total, &u16CRC, sizeof(u16CRC));
    total += (int)sizeof(u16CRC);

    if (type == DTU_TYPE) {
        if (WebLink_Send(buffer, total) != total) {
            return ERROR;
        }
    } else if (write(fd, buffer, (size_t)total) != (size_t)total) {
        return ERROR;
    }
    if (!OtaUpgrade_IsBusy()) {
        LOG(LOG_DEBUG, "Send link confirm seri_raw=0x%04x len=%d",
            (unsigned)u16FrameSeriRaw, total);
        LOG_ARRAY(LOG_DEBUG, buffer, total, "%02x");
    }
    return SUCCESS;
}

int ZigbeeDataCheck(uint8_t *pData, uint16_t wLen)
{
    int i = 0;
    uint8_t nCheckCode[] = {0x12, 0x34, 0x56, 0x78};
    if (wLen < 4)
        return 1;
    for (i = 0; i < 4; i++)
    {
        if (pData[i] != nCheckCode[i])
            return -1;
    }
    if (wLen < sizeof(zigbee_head_t) + sizeof(zigbee_tail_t) + 1)
        return 1;
    pzigbee_head_t pHead = (pzigbee_head_t)pData;
    if (pHead->dwHeadFlag != ZIGBEE_HEAD_FLAG)
        return -1;
    if (pHead->wLength > 600)
        return -2;
    if (pHead->wLength > wLen)
        return 1;
    pzigbee_tail_t pTail = (pzigbee_tail_t)(pData + pHead->wLength - sizeof(zigbee_tail_t));
    if (pTail->wTailFlag != ZIGBEE_TAIL_FLAG)
        return -1;
    return 0;
}


/*
*函数名称:Proc_DTU_RecData
*函数功能:处理ZIGBEE接收数据
*输   入:KFIFO_ID pData  存入到缓冲区中的数据
*输   出:WORD *pUsed 已经使用的数据长度
*返   回:SUCCESS - 成功     ERROR  - 错误
*/
int Proc_DTU_RecData(KFIFO_ID pData ,  WORD *pUsed,int type)
{
    int nRet = 0;
    DOWNFrameHead stDownFrameHead;
    uint8_t tempbuff[MAX_EXTEND_FRAME_LEN];
    uint32_t u32fifolen = 0;
    uint16_t dataLen = 0;
    bool  bHeadFlag  = false;
    const int TIME_THRESHOLD = 3; //3秒
    struct tm recv_tm = {0};
    time_t local_time;
    time_t recv_time;
    u32fifolen = kfifo_len(pData);
    if (MAX_SOFTWARE_SIZE <= u32fifolen )
    {
        LOG(LOG_ERROR,"fifo len %d is out of range (max is %d)",u32fifolen,MAX_SOFTWARE_SIZE);
        kfifo_clear(pData);
        return ERROR;
    }
    if(u32fifolen <5)
    {
        return ERROR;
    }
    
    bHeadFlag = uartFindFrameHead(pData,HEAD1,HEAD2);
    if(bHeadFlag) 
    {
        uint16_t frameLen = 0;

        if (u32fifolen < sizeof(DOWNFrameHead)) {
            return ERROR;
        }
        memset(tempbuff, 0, sizeof(tempbuff));
        kfifo_preview(pData, tempbuff, sizeof(DOWNFrameHead));
        memcpy(&stDownFrameHead,tempbuff,sizeof(DOWNFrameHead));
        
        stDownFrameHead.u16FlagAndLen = swap_u16(stDownFrameHead.u16FlagAndLen);
        dataLen = stDownFrameHead.u16FlagAndLen & 0xfff ;

        /*
         * 本机上行 F2 被 DTU 口回环读回时，按 DOWN 解析会把 RTUID 末字节
         * 误读成中心站地址。仅丢弃“上行顺序”(Centre+RTUID) 的 F2；
         * 网页对称短确认 F2 80 08 是下行顺序(RTUID+Centre)，须正常处理。
         * FlagAndLen 偏移在 UP/DOWN 中相同，dataLen 仍可用。
         */
        if (stDownFrameHead.u8FunctionCode == UP_EXPAND_CODE &&
            tempbuff[2] == g_CenterStationID &&
            0 == memcmp(tempbuff + 3, g_RTUID, sizeof(g_RTUID))) {
            frameLen = down_business_frame_len(dataLen);
            if (frameLen == 0) {
                kfifo_get(pData, tempbuff, 1);
                return ERROR;
            }
            if (u32fifolen < frameLen) {
                return ERROR;
            }
            kfifo_get(pData, tempbuff, frameLen);
            return SUCCESS;
        }

        /* 协议文档远程升级命令字 26：Signature 直带 libota 体时也可走此路径 */
        if (stDownFrameHead.u8FunctionCode == DOWN_EXPAND_CODE &&
            stDownFrameHead.u8Signature == (uint8_t)SERVER_FIRMWARE_UPDATE &&
            stDownFrameHead.u8DataStart == STX)
        {
            frameLen = extend_frame_len(dataLen);
            if (frameLen == 0) {
                kfifo_get(pData, tempbuff, 1);
                return ERROR;
            }
            if (u32fifolen < frameLen) {
                return ERROR;
            }
            if (g_CenterStationID != stDownFrameHead.u8CentreStaID ||
                0 != memcmp(stDownFrameHead.aRTUID, g_RTUID, sizeof(g_RTUID))) {
                LOG(LOG_ERROR, "ota frame station mismatch");
                kfifo_get(pData, tempbuff, 1);
                return ERROR;
            }
            kfifo_get(pData, tempbuff, frameLen);
            /* OTA 大包不打整帧 hex，避免刷屏抢 CPU/Flash */
            if (tempbuff[frameLen - 3U] != ETX) {
                LOG(LOG_WARNING, "ota frame missing ETX");
                return ERROR;
            }
            {
                uint16_t bodyLen = extend_body_len(dataLen);
                const uint8_t *pBody = tempbuff + sizeof(DOWNFrameHead);
                if (OtaUpgrade_HandleDownlink(SERVER_FIRMWARE_UPDATE, pBody, bodyLen) == SUCCESS) {
                    if (bodyLen >= 3U) {
                        uint16_t pkt = (uint16_t)(((uint16_t)pBody[1] << 8) | pBody[2]);
                        LOG(LOG_DEBUG, "ota cmd26 accepted pkt=%u body=%u frame=%u",
                            (unsigned)pkt, (unsigned)bodyLen, (unsigned)frameLen);
                    }
                    UplinkOffline_OnDownlink();
                    updateActiveTime();
                    return SUCCESS;
                }
                return ERROR;
            }
        }

        /* 旧业务帧：半包等待 */
        frameLen = down_business_frame_len(dataLen);
        if (frameLen == 0) {
            kfifo_get(pData, tempbuff, 1);
            return ERROR;
        }
        if (u32fifolen < frameLen) {
            return ERROR;
        }
        /* 网页下行 hex：OTA 期间不打；日志上报期间照常打印 */
        if (!web_hex_quiet()) {
            uint16_t previewLen = frameLen;
            if (previewLen > sizeof(tempbuff))
                previewLen = sizeof(tempbuff);
            kfifo_preview(pData, tempbuff, previewLen);
            LOG(LOG_DEBUG, "Web downlink full frame func=0x%02x centre=0x%02x dataLen=%u frameLen=%u:",
                stDownFrameHead.u8FunctionCode,
                stDownFrameHead.u8CentreStaID,
                (unsigned)dataLen,
                (unsigned)previewLen);
            LOG_ARRAY(LOG_DEBUG, tempbuff, previewLen, "%02x");
        }
        /* 站址校验须在剥帧头之前：失败只丢 1 字节重同步，避免残留半帧破坏后续 OTA 包 */
        if (g_CenterStationID != stDownFrameHead.u8CentreStaID)
        {
            LOG(LOG_ERROR,"Received Center Stationid %02x is not Equal to g_CenterStationID %02x",
                stDownFrameHead.u8CentreStaID, g_CenterStationID);
            kfifo_get(pData, tempbuff, 1);
            return ERROR;
        }
        if (0 != memcmp(stDownFrameHead.aRTUID, g_RTUID, sizeof(g_RTUID)))
        {
            LOG(LOG_ERROR,"Received RTUID is not equal to tyls.conf stationID");
            kfifo_get(pData, tempbuff, 1);
            return ERROR;
        }
        /* 短链路确认（含网页 F2 80 08）：无业务体，仍算有效下行 */
        if(dataLen == sizeof(BCD_TIME) + sizeof(uint16_t))
        {
            frameLen = down_business_frame_len(dataLen);
            if (frameLen == 0 || u32fifolen < frameLen) {
                return ERROR;
            }
            kfifo_get(pData, tempbuff, frameLen);
            g_u16LastDownFrameSeri = stDownFrameHead.u16FrameSeri;
            /* 心跳/上报的网页短确认：只刷新在线，不打断待机 */
            UplinkOffline_OnDownlink();
            if (!OtaUpgrade_IsBusy()) {
                LOG(LOG_INFO, "Web downlink link-confirm func=0x%02x seri=0x%04x len=%u",
                    stDownFrameHead.u8FunctionCode,
                    (unsigned)swap_u16(stDownFrameHead.u16FrameSeri),
                    (unsigned)frameLen);
            }
            if(stDownFrameHead.u16FrameSeri == g_u32CommansSeri)
            {
                pthread_mutex_lock(&g_lock[g_stDevicelist.ZigBee]);
                //g_RespondFlag = 1;
                pthread_mutex_unlock(&g_lock[g_stDevicelist.ZigBee]);
            }
            return SUCCESS;
        }
        kfifo_get(pData, tempbuff, sizeof(DOWNFrameHead));
        g_u16LastDownFrameSeri = stDownFrameHead.u16FrameSeri;

        /*
         * 平台最终流量 SEND_FINAL_DATA(27)：作为最终流量上报确认。
         * 仍跳过 BCD 校时（部分平台把 0x1b 放在特征码位）。
         */
        if (stDownFrameHead.u8Signature == (uint8_t)SEND_FINAL_DATA) {
            uint16_t payloadLen = 0;
            if (stDownFrameHead.u8DataStart == STX) {
                uint16_t ContextDataLen = dataLen - sizeof(BCD_TIME) - sizeof(uint16_t) -
                                         sizeof(uint8_t) + sizeof(DOWNFrametail);
                if (ContextDataLen > sizeof(tempbuff)) {
                    LOG(LOG_ERROR, "STX ContextDataLen %d exceeds tempbuff size %d",
                        ContextDataLen, (int)sizeof(tempbuff));
                    return ERROR;
                }
                kfifo_get(pData, tempbuff, ContextDataLen);
                payloadLen = ContextDataLen;
            } else if (stDownFrameHead.u8DataStart == ETB ||
                       stDownFrameHead.u8DataStart == ETX) {
                uint16_t readLen = dataLen + sizeof(PackedInfo) + sizeof(DOWNFrametail);
                if (readLen > sizeof(tempbuff)) {
                    LOG(LOG_ERROR, "ETB/ETX readLen %d exceeds tempbuff size %d",
                        readLen, (int)sizeof(tempbuff));
                    return ERROR;
                }
                kfifo_get(pData, tempbuff, readLen);
                payloadLen = readLen;
            }
            UplinkOffline_OnDownlink();
            updateActiveTime();
            if (payloadLen > 0) {
                FinalResult_OnDownlink(tempbuff, payloadLen);
            }
            return SUCCESS;
        }

        // ==== 时间设置：仅合法 F1 业务帧 + 合法 BCD 才校时 ====
        if (!downlink_frame_ok_for_timesync(&stDownFrameHead)) {
            LOG(LOG_WARNING,
                "skip timesync: func=0x%02x dataStart=0x%02x pwd=0x%04x "
                "bcd=%02x-%02x-%02x %02x:%02x:%02x",
                stDownFrameHead.u8FunctionCode,
                stDownFrameHead.u8DataStart,
                (unsigned)swap_u16(stDownFrameHead.u16Password),
                stDownFrameHead.BCD_time.u8Year,
                stDownFrameHead.BCD_time.u8Month,
                stDownFrameHead.BCD_time.u8Day,
                stDownFrameHead.BCD_time.u8Hour,
                stDownFrameHead.BCD_time.u8Minutes,
                stDownFrameHead.BCD_time.u8Second);
        }
        else if (!OtaUpgrade_IsBusy())
        {
            memset(&recv_tm, 0, sizeof(recv_tm));
            recv_tm.tm_year = bcd_to_dec(stDownFrameHead.BCD_time.u8Year) + 100;
            recv_tm.tm_mon  = bcd_to_dec(stDownFrameHead.BCD_time.u8Month) - 1;
            recv_tm.tm_mday = bcd_to_dec(stDownFrameHead.BCD_time.u8Day);
            recv_tm.tm_hour = bcd_to_dec(stDownFrameHead.BCD_time.u8Hour);
            recv_tm.tm_min  = bcd_to_dec(stDownFrameHead.BCD_time.u8Minutes);
            recv_tm.tm_sec  = bcd_to_dec(stDownFrameHead.BCD_time.u8Second);

            recv_time = mktime(&recv_tm);
            if (recv_time == (time_t)-1)
            {
                LOG(LOG_ERROR, "Invalid received time, conversion failed");
            }
            else
            {
                local_time = time(NULL);
                {
                    int time_diff = abs((int)(local_time - recv_time));

                    LOG(LOG_DEBUG,
                        "Local time: %lld, Received time: %lld, Diff: %d seconds",
                        (long long)local_time, (long long)recv_time, time_diff);

                    if (time_diff > TIME_THRESHOLD)
                    {
                        if (SUCCESS != set_system_time(&stDownFrameHead.BCD_time))
                        {
                            LOG(LOG_ERROR, "Set system time failed");
                        }
                        else
                        {
                            LOG(LOG_INFO, "System time updated (diff: %d seconds)",
                                time_diff);
                        }
                    }
                    else
                    {
                        LOG(LOG_DEBUG,
                            "Time difference (%d seconds) within threshold, no update",
                            time_diff);
                    }
                }
            }
        }
        // ==== 时间设置结束 ====




        //memcpy(g_RTUID,stDownFrameHead.aRTUID,sizeof(g_RTUID));
        if(stDownFrameHead.u8DataStart == STX)  // 单帧数据
        {
            uint16_t ContextDataLen = dataLen - sizeof(BCD_TIME) - sizeof (uint16_t) - sizeof(uint8_t)  + sizeof (DOWNFrametail) ; //减去时间、帧序列号、全量城特征码
            /* 防栈溢出：dataLen 来源于报文头 u16FlagAndLen[11:0]，最大 4095，
             * 但 tempbuff 只有 2048 字节 */
            if (ContextDataLen > sizeof(tempbuff)) {
                LOG(LOG_ERROR, "STX ContextDataLen %d exceeds tempbuff size %d", ContextDataLen, sizeof(tempbuff));
                return ERROR;
            }
            kfifo_get(pData,tempbuff,ContextDataLen);
            if (!web_hex_quiet()) {
                LOG(LOG_DEBUG,"DTU Received need proc data is:");
                LOG_ARRAY(LOG_DEBUG,tempbuff,ContextDataLen,"%02x");
            }
            UplinkOffline_OnDownlink();
            updateActiveTime();
            
            nRet = CommandCodeProc(tempbuff , ContextDataLen,type);
        }           
        else if(stDownFrameHead.u8DataStart == ETB || stDownFrameHead.u8DataStart  == ETX)
        {
            uint16_t readLen = dataLen + sizeof(PackedInfo) + sizeof(DOWNFrametail);
            /* 防栈溢出 */
            if (readLen > sizeof(tempbuff)) {
                LOG(LOG_ERROR, "ETB/ETX readLen %d exceeds tempbuff size %d", readLen, sizeof(tempbuff));
                return ERROR;
            }
            kfifo_get(pData,tempbuff,readLen); // 包总数及序列号 + 校验位 + 结束符
            nRet = MultiFrameProc(stDownFrameHead.u8DataStart,tempbuff,dataLen,type);
            UplinkOffline_OnDownlink();
            updateActiveTime();
        }


          
    }


    return nRet;
}


int MultiFrameProc(uint8_t StartFlag, BYTE *pData, DWORD dwLength, int type)
{
    /*
     * SL651 多帧：网页将单条命令（含升级内容≤8192）拆成 ETB…ETX。
     * 缓冲布局（Proc_DTU 已读出）：PackedInfo + 分片正文 + 0xFFFFFFFF + DOWNFrametail
     * dwLength 为 FlagAndLen 的 dataLen；拼包后交给 CommandCodeProc。
     */
    PackedInfo stPackedInfo;
    const uint8_t *frag;
    uint16_t fragLen;
    uint32_t bufNeed;
    uint16_t seri;

    if (pData == NULL || g_MultiFrameFifo == NULL) {
        return ERROR;
    }

    bufNeed = (uint32_t)dwLength + (uint32_t)sizeof(PackedInfo) + (uint32_t)sizeof(DOWNFrametail);
    if (dwLength < 4U || bufNeed > MAX_EXTEND_FRAME_LEN) {
        LOG(LOG_ERROR, "MultiFrameProc bad dataLen=%u", (unsigned)dwLength);
        kfifo_clear(g_MultiFrameFifo);
        g_u16PrevFrameSeri = 0;
        return ERROR;
    }

    memcpy(&stPackedInfo, pData, sizeof(stPackedInfo));
    seri = (uint16_t)stPackedInfo.Serier;

    /* 分片正文：PackedInfo 之后；若带业务尾 0xFFFFFFFF 则剥掉（与单帧 STX 一致） */
    frag = pData + sizeof(PackedInfo);
    fragLen = (uint16_t)dwLength;
    if (fragLen >= 4U) {
        const uint8_t *tail4 = frag + fragLen - 4U;
        if (tail4[0] == 0xFFU && tail4[1] == 0xFFU &&
            tail4[2] == 0xFFU && tail4[3] == 0xFFU) {
            fragLen = (uint16_t)(fragLen - 4U);
        }
    }
    if (fragLen == 0U) {
        LOG(LOG_ERROR, "MultiFrameProc empty fragment seri=%u", (unsigned)seri);
        kfifo_clear(g_MultiFrameFifo);
        g_u16PrevFrameSeri = 0;
        return ERROR;
    }

    if (g_u16PrevFrameSeri == 0U) {
        /* 新会话：接受首片（常见 Serier 从 1 起） */
        kfifo_clear(g_MultiFrameFifo);
        g_u16PrevFrameSeri = seri;
    } else if (seri != (uint16_t)(g_u16PrevFrameSeri + 1U)) {
        LOG(LOG_ERROR, "MultiFrameProc frame seri expect=%u got=%u",
            (unsigned)(g_u16PrevFrameSeri + 1U), (unsigned)seri);
        kfifo_clear(g_MultiFrameFifo);
        g_u16PrevFrameSeri = 0;
        return ERROR;
    } else {
        g_u16PrevFrameSeri = seri;
    }

    if (kfifo_put(g_MultiFrameFifo, (uint8_t *)frag, fragLen) != fragLen) {
        LOG(LOG_ERROR, "MultiFrameProc fifo full, drop session");
        kfifo_clear(g_MultiFrameFifo);
        g_u16PrevFrameSeri = 0;
        return ERROR;
    }

    if (!OtaUpgrade_IsBusy()) {
        LOG(LOG_DEBUG, "MultiFrameProc got seri=%u/%u frag=%u fifo=%u flag=0x%02x",
            (unsigned)seri, (unsigned)stPackedInfo.Number, (unsigned)fragLen,
            (unsigned)kfifo_len(g_MultiFrameFifo), (unsigned)StartFlag);
    }

    if (StartFlag == ETX) {
        uint32_t assembledLen = kfifo_len(g_MultiFrameFifo);
        uint8_t *assembled = NULL;
        int ret = ERROR;

        /* 拼包上限：命令头+8192内容+业务尾，再留余量 */
        if (assembledLen == 0U || assembledLen > (sizeof(SeriverCommandHead) + OTA_CHUNK_MAX + 64U)) {
            LOG(LOG_ERROR, "MultiFrameProc assembled len=%u invalid", (unsigned)assembledLen);
            kfifo_clear(g_MultiFrameFifo);
            g_u16PrevFrameSeri = 0;
            return ERROR;
        }

        assembled = (uint8_t *)malloc(assembledLen);
        if (assembled == NULL) {
            kfifo_clear(g_MultiFrameFifo);
            g_u16PrevFrameSeri = 0;
            return ERROR;
        }
        if (kfifo_get(g_MultiFrameFifo, assembled, assembledLen) != assembledLen) {
            free(assembled);
            kfifo_clear(g_MultiFrameFifo);
            g_u16PrevFrameSeri = 0;
            return ERROR;
        }
        kfifo_clear(g_MultiFrameFifo);
        g_u16PrevFrameSeri = 0;

        if (!OtaUpgrade_IsBusy()) {
            LOG(LOG_DEBUG, "MultiFrameProc complete len=%u, CommandCodeProc",
                (unsigned)assembledLen);
        }
        ret = CommandCodeProc(assembled, assembledLen, type);
        free(assembled);
        return ret;
    }

    (void)type;
    return SUCCESS;
}




// 紧急停止处理
void * emergency_stop_handler(void * arg) 
{
    LOG(LOG_WARNING,"emergency_stop_trigger!!!");
    
    // 1. 停止所有水平和垂直移动
    Relay_MotorBreak(RELAY_COM0_ADDR);
    AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
    mSleep(50);
    
    Relay_MotorBreak(RELAY_COM1_ADDR);
    AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);
    mSleep(50);
    
    Relay_MotorDrivePowerOFF(g_stDeviceAddr.u8Relay_2_port_power,RELAY_COM0_ADDR);//驱动器1下电
    mSleep(500);
    Relay_MotorDrivePowerOFF(g_stDeviceAddr.u8Relay_2_port_power,RELAY_COM1_ADDR);//驱动器2下电
	mSleep(500);
    
    // 2. 停止所有运行标志，打断正在执行的循环
    g_bRunning_State = false;
    g_u8CatState = IS_STOP_STATE;
    g_u8FishState = IS_STOP_STATE;
    
    // 3. 如果正在执行加测状态机，将其置为完成状态，不再继续执行
    if (g_addMeasureState != FSM_IDLE && g_addMeasureState != FSM_COMPLETE) 
    {
        g_addMeasureCtx.stopRequested = 1;
        g_addMeasureState = FSM_COMPLETE;
        g_bDoorCtrlSendFlag = false; /* 避免紧急停止后门轮询标志遗留 */
        g_WebSendWaterLevel = 1000000; /* 恢复传感器水位 */
        if (g_fsmTimerId != -1) {
            remove_timer(g_fsmTimerId);
            g_fsmTimerId = -1;
        }
    }
    
    // 4. 打开红色警示灯
    Relay_LightCtrl(RED_LIGHT,TURN_ON);
    
    return (void *)SUCCESS;
    
}


/*
*函数名称:SendZigbeeData
*函数功能:通过ZIGBEE向服务器发送数据，数据遵守651协议
*输   入:uint8_t u8FunctionCode   功能码 ,uint8_t * pData  需要发送的数据,uint16_t  datalen          需要发送的数据长度
*输   出:无
*返   回:无
*/
void SendZigbeeData(uint8_t u8FunctionCode,uint8_t * pData,uint16_t  datalen,int type)
{
    int bufferDataLen = 0;
    int sent = -1;
    uint16_t u16CRC = 0;
    uint8_t u8EOF = 0X03;
    uint8_t *buffer = NULL;
    UPFrameHead stUpframehead;
    uint32_t u32FrameTail = 0xffffffff;
    uint32_t cmd = 0;
    int needPersist = 0;

    // 参数合法性检查
    if (pData == NULL) {
        return;
    }

    if (datalen >= (uint16_t)(sizeof(uint32_t) * 3U)) {
        memcpy(&cmd, pData + sizeof(uint32_t) * 2U, sizeof(cmd));
        needPersist = UplinkOffline_ShouldPersist(cmd);
    }

    // 内存分配并检查
    buffer = (uint8_t *)malloc(datalen + sizeof(UPFrameHead) + sizeof(DOWNFrametail) + 4);
    if (buffer == NULL) {
        return;  // 内存分配失败
    }

    // 按结构体字段顺序赋值
    stUpframehead.u16SOH = 0x7e7e;
    stUpframehead.u8CentreStaID = g_CenterStationID;
    memcpy(stUpframehead.aRTUID, g_RTUID, sizeof(g_RTUID));
    stUpframehead.u16Password = 0x1234;
    stUpframehead.u8FunctionCode = u8FunctionCode;
    stUpframehead.u8Signature = UP_SIGNATURE;
    get_bcd_time((uint8_t *)&stUpframehead.BCD_time);
    stUpframehead.u8DataStart = 0x02;
    stUpframehead.u16Password = swap_u16(stUpframehead.u16Password);
    /* OTA 应答回显下行流水号；其余仍自增 */
    if (g_bEchoDownFrameSeri) {
        stUpframehead.u16FrameSeri = g_u16EchoFrameSeri;
        g_bEchoDownFrameSeri = 0;
    } else {
        stUpframehead.u16FrameSeri = (uint16_t)g_u32CommansSeri++;
        stUpframehead.u16FrameSeri = swap_u16(stUpframehead.u16FrameSeri);
    }

    /* FlagAndLen[11:0]：流水号～指令尾（不含 STX/ETX） */
    {
        uint16_t lenField = (uint16_t)((datalen + 4 + sizeof(BCD_TIME) +
                                        sizeof(uint16_t) + sizeof(uint8_t)) & 0x0fff);
        stUpframehead.u16FlagAndLen = swap_u16(lenField + (0 << 12));
    }

    // 拷贝帧头
    memcpy(buffer, &stUpframehead, sizeof(UPFrameHead));
    memcpy(buffer + sizeof(UPFrameHead), pData, datalen);
    memcpy(buffer + sizeof(UPFrameHead) + datalen, &u32FrameTail, 4);
    memcpy(buffer + sizeof(UPFrameHead) + datalen + 4, &u8EOF, sizeof(u8EOF));

    bufferDataLen = sizeof(UPFrameHead) + datalen + 4 + sizeof(u8EOF);
    u16CRC = CRC16_Modbus(buffer, bufferDataLen);
    memcpy(buffer + bufferDataLen, &u16CRC, sizeof(u16CRC));
    bufferDataLen += sizeof(u16CRC);

    //! 发送报文
    if(type == DTU_TYPE)
    {
        if (!web_hex_quiet() || g_forceUplinkHexLog) {
            LOG(LOG_DEBUG,"Send Data to WebSeriver(%s)",
                WebLink_IsSocketMode() ? "socket" : "dtu");
        }
        sent = WebLink_Send(buffer, bufferDataLen);
    }
    else if(type== ZIGBEE_TYPE)
    {
        if (!web_hex_quiet() || g_forceUplinkHexLog) {
            LOG(LOG_DEBUG,"Send Data to ZigBee");
        }
        sent = (int)write(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd,
                          buffer, (size_t)bufferDataLen);
    }

    if (!web_hex_quiet() || g_forceUplinkHexLog) {
        LOG_ARRAY(LOG_DEBUG,buffer,bufferDataLen ,"%02x");
    }

    /*
     * 白名单报文落盘：
     * - 已离线（无网页确认/下行）：本地 write 成功也落盘，等恢复后补发
     * - 在线但本地发送失败：同样落盘
     */
    if (needPersist && (sent != bufferDataLen || !UplinkOffline_IsOnline())) {
        (void)UplinkOffline_Enqueue(UPLINK_OFFLINE_KIND_WEB, cmd, pData, datalen, type);
    }

    free(buffer);
}

int SendSingleFrame(uint8_t u8FunctionCode,uint8_t * pData,uint16_t  datalen,int type,int StopFlag)
{
    int bufferDataLen = 0;
    int writeByte = 0;
    uint16_t u16CRC = 0;
    uint8_t u8EOF = 0X03;
    BYTE buffer[1024];
    memset(buffer, 0, 1024);
    UPFrameHead stUpframehead;
    uint32_t u32FrameTail = 0xffffffff;
 
    stUpframehead.u16SOH = 0x7e7e;
    stUpframehead.u8CentreStaID = g_CenterStationID;
    memcpy(stUpframehead.aRTUID,g_RTUID,sizeof(g_RTUID));
    stUpframehead.u16Password = 0x1234;
    stUpframehead.u8FunctionCode = u8FunctionCode;
    stUpframehead.u16FlagAndLen = ( ( datalen + 4 ) & 0x0fff ) + (0 << 12);
    stUpframehead.u8DataStart = StopFlag;
    //u16Temp = ((stUpframehead.u16Up_DownFlag & 0xf) < 12) + stUpframehead.u16FrameLen;
    stUpframehead.u16FlagAndLen = swap_u16(stUpframehead.u16FlagAndLen);
    //stUpframehead.u16Password = swap_u16(stUpframehead.u16Password);
    memcpy(buffer,&stUpframehead,sizeof(UPFrameHead));
    memcpy(buffer+sizeof(UPFrameHead),pData,datalen);
    memcpy(buffer+sizeof(UPFrameHead)+ datalen,&u32FrameTail,4);
    
    memcpy(buffer+sizeof(UPFrameHead)+ datalen+ 4,&u8EOF,sizeof(u8EOF));
    bufferDataLen = sizeof(UPFrameHead) + datalen +4 + sizeof(u8EOF);
    u16CRC = CRC16_Modbus(buffer,bufferDataLen);
    //u16CRC = swap_u16(u16CRC);
    memcpy(buffer+bufferDataLen,&u16CRC,sizeof(u16CRC));

    //! 发送报文
    if(type == DTU_TYPE)
    {
        if (!web_hex_quiet()) {
            LOG(LOG_DEBUG,"Send Data to WebSeriver(%s)",
                WebLink_IsSocketMode() ? "socket" : "dtu");
        }
        writeByte = WebLink_Send(buffer, (int)(bufferDataLen + sizeof(u16CRC)));
    }
    else if(type== ZIGBEE_TYPE)
    {
        if (!web_hex_quiet()) {
            LOG(LOG_DEBUG,"Send Data to ZigBee");
        }
        writeByte = write(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd, buffer, bufferDataLen + sizeof(u16CRC));
    }

    if (!web_hex_quiet()) {
        LOG_ARRAY(LOG_DEBUG,buffer,bufferDataLen + sizeof(u16CRC),"%02x");
    }
    return writeByte;
}

/**
 * 发送多帧数据
 * 
 * 当数据长度超过单帧最大长度时，会将数据分割成多帧发送
 * 每帧数据的发送间隔为500毫秒
 * 
 * @param u8FunctionCode 功能码，指示发送数据的类型或目的
 * @param pData 指向待发送数据的指针
 * @param datalen 待发送数据的长度
 * @param type 数据类型，可能影响发送过程或数据处理方式
 * @return 发送的总字节数
 */
int SendMultiframe(uint8_t u8FunctionCode,uint8_t * pData,uint16_t  datalen,int type)
{
    // 定义单帧数据的最大长度
    const uint16_t SingleMaxlen = 128;
    // 初始化已发送字节数
    uint16_t SendByte = 0;
    
    LOG(LOG_INFO,"need to send %d bytes",datalen);
    // 循环发送数据，直到所有数据发送完毕
    while (datalen >0)
    {
        uint16_t frameSize = (datalen > SingleMaxlen) ? SingleMaxlen : datalen;
        int endType = (datalen > SingleMaxlen) ? ETB : ETX;

        // 发送一帧数据
        SendSingleFrame(u8FunctionCode, pData + SendByte, frameSize, type, endType);
        uint16_t sentBytes = frameSize;
        // 检查发送结果
        if (sentBytes < 0)
        {
            // 如果发送失败，停止发送并返回
            return ERROR;
        }

        // 更新已发送字节数和剩余数据长度
        SendByte += sentBytes;
        datalen -= sentBytes;    
        LOG(LOG_INFO,"Send %d bytes,remain %d bytes",SendByte,datalen);
        // 每发送一帧数据后，等待500毫秒
        mSleep(500);
    }
    return SUCCESS;
}


/*
*函数名称:SendResultFrame
*函数功能:通过ZIGBEE向服务器发送执行结果
*输   入:uint32_t ExeCommand    执行的命令,uint32_t commandseri       执行的命令序号,uint8_t resaut   执行结果
*输   出:无
*返   回:成功
*/
int SendResultFrame(uint32_t ExeCommand,uint32_t commandseri,uint8_t resaut,int type)
{   
    int isOtaAck = (ExeCommand == UPDATE_SOFTWARE);

    /* OTA 期间其它上行静默，但 ACK/结果帧要打出来便于对端对照 */
    if (!OtaUpgrade_IsBusy() || isOtaAck) {
        LOG(LOG_INFO, "Send ResultFrame ExeCmd=%u seri=%u result=%u type=%d%s",
            (unsigned)ExeCommand, (unsigned)commandseri, (unsigned)resaut, type,
            isOtaAck ? " (ota ack)" : "");
    }
    
    ReportResult stResult;
    memset(&stResult, 0, sizeof(stResult));
    stResult.u32CommandSeri = commandseri;
    stResult.u32CommandLen = sizeof(ReportResult) + sizeof(SeriverCommandTail);
    memcpy(stResult.au8StationID, g_aStationID, sizeof(g_aStationID));
    stResult.u32Command = REPORT_RESULT;
    stResult.u32ExeCommmand = ExeCommand;
    stResult.u32EceCmandSeri = commandseri;
    /* 文档：0成功，非0失败；失败时 resaut 填协议错误码(如15/16)，勿再传 ERROR(-1) 除非仅要非0 */
    stResult.u8Result = resaut;
    stResult.u16ResultDetail = (resaut == (uint8_t)SUCCESS) ? 0U : (uint16_t)resaut;
    if (isOtaAck) {
        g_forceUplinkHexLog = 1;
    }
    SendZigbeeData(UP_EXPAND_CODE, (uint8_t *)&stResult, sizeof(ReportResult), type);
    g_forceUplinkHexLog = 0;
    return SUCCESS;

}

/*
 * 上报固件升级结果：指令序号+长度 + 命令字0xFFFFFFE4(4字节) + 站码/类型/状态/升级序号(包序号2字节)
 * 指令尾 0xFFFFFFFF 由 SendZigbeeData 追加
 * u8Status: 0=升级中，1=升级出错（协议 3.1.28）
 */
int SendOtaUpgradeResultFrame(uint8_t u8UpgradeType, uint8_t u8Status,
                              uint32_t u32CommandSeri, uint16_t u16PktSeri, int type)
{
    ReportOtaUpgrade stRpt;

    memset(&stRpt, 0, sizeof(stRpt));
    stRpt.u32CommandSeri = u32CommandSeri;
    stRpt.u32CommandLen = (uint32_t)(sizeof(ReportOtaUpgrade) + sizeof(SeriverCommandTail));
    stRpt.u32Command = (uint32_t)REPORT_OTA_UPGRADE;
    memcpy(stRpt.au8StationID, g_aStationID, sizeof(g_aStationID));
    stRpt.u8UpgradeType = u8UpgradeType;
    stRpt.u8UpgradeStatus = u8Status;
    stRpt.u16UpgradeSeri = u16PktSeri;

    LOG(LOG_INFO,
        "Send OtaUpgradeResult type=%u status=%u(%s) cmdSeri=%u pkt=%u link=%d",
        (unsigned)u8UpgradeType, (unsigned)u8Status,
        (u8Status == OTA_UPGRADE_STATUS_ERROR) ? "error" : "busy",
        (unsigned)u32CommandSeri, (unsigned)u16PktSeri, type);

    g_forceUplinkHexLog = 1;
    SendZigbeeData(UP_EXPAND_CODE, (uint8_t *)&stRpt, sizeof(stRpt), type);
    g_forceUplinkHexLog = 0;
    return SUCCESS;
}

/*
*函数名称:SendStatusFrame
*函数功能:发送状态信息
*输   入:无
*输   出:无
*返   回:无
*/
void  SendStatusFrame(void * arg)
{
    if (OtaUpgrade_IsBusy() || LogUpload_IsBusy()) {
        return;
    }
    /* 开关门期间让出 ZigBee：不读水位、不上报状态大包 */
    if (g_bDoorCtrlSendFlag) {
        return;
    }
    g_sendcount ++;
    if(g_u8CatState == IS_RUNNING_STATE || g_u8FishState == IS_RUNNING_STATE || g_sendcount == 10)
    {
        int type = (int)arg;
        ReportState stReStatus;
        g_sendcount = 0;
        memcpy(&stReStatus,&g_stReportState,sizeof(ReportState));
        stReStatus.stCommandHead.u32CommandSeri = g_u32CommansSeri++;
        stReStatus.stCommandHead.u32CommandLen = sizeof(ReportState);
        memcpy(stReStatus.stCommandHead.au8StationID,g_aStationID,sizeof(g_aStationID)) ;
        stReStatus.stCommandHead.u32Command = REPORT_STATE;
        stReStatus.u16CarVol = g_stReportState.u16CarVol / 10;
        stReStatus.u16RTUVol = g_stReportState.u16RTUVol / 10;
        /* 手动加测水位优先；结束后 g_WebSendWaterLevel 清回 1000000，恢复传感器 */
        if (g_WebSendWaterLevel != 1000000U) {
            stReStatus.u32CurWaterLevel = GetCurrentWaterLevel();
        } else {
            stReStatus.u32CurWaterLevel = ReadWaterLevelFresh();
        }
        stReStatus.i32CarHoriDist = GetCarHoriPosCm();
        stReStatus.u16CarVeriHight = g_VeroTotalSpace * g_FishEncoderDistence;
        stReStatus.u16FishToCarhight = g_VeroTotalSpace * g_FishEncoderDistence;
        stReStatus.u16FishInwaterDeep = g_VeroTotalSpace * g_FishEncoderDistence - g_stSetCableway.stSetCablewayBody.u32CableHight + stReStatus.u32CurWaterLevel-FISH_BODY_HIGHT;
        stReStatus.u16HoriCarSpeed = g_stSetCableway.stSetCablewayBody.u16HoriSpeed;
        stReStatus.u16VeriUpSpeed = g_stSetCableway.stSetCablewayBody.u16VeriSpeed;
        stReStatus.u16Current = g_stReportState.u16Current;
        stReStatus.u8Mode = g_u8Mode;
        stReStatus.u8HoriCtrlState = g_u8CatState;
        stReStatus.u8CarState = g_u8FishState;
        stReStatus.u8FishState = 1; 
        
       
        LOG(LOG_DEBUG,"SendStatusFrame is,type is %d",type);
        SendZigbeeData(UP_EXPAND_CODE,(uint8_t * )&stReStatus,sizeof(ReportState),type);
    
    }
    

}

/*
 * 上报传感器出错 REPORT_SENSOR_ERROR(0xfffffff0)
 * 要素类型：本项目铅鱼转子 = ROTATOR(2)
 * 错误代码(2字节)：协议填「时分秒」；字段仅2字节，按BCD存放时、分（秒见上行帧头BCD时间）
 */
void SendReportSensorError(uint32_t u32ElementType, int type)
{
    ReportSensorError stErr;
    time_t now;
    struct tm *tm;
    uint8_t bcd_h;
    uint8_t bcd_m;

    memset(&stErr, 0, sizeof(stErr));
    stErr.stCommandHead.u32CommandSeri = g_u32CommansSeri++;
    stErr.stCommandHead.u32CommandLen = sizeof(ReportSensorError) + sizeof(SeriverCommandTail);
    stErr.stCommandHead.u32Command = REPORT_SENSOR_ERROR;
    memcpy(stErr.stCommandHead.au8StationID, g_aStationID, sizeof(g_aStationID));
    stErr.u32factorType = u32ElementType;

    now = time(NULL);
    tm = localtime(&now);
    if (tm != NULL) {
        bcd_h = (uint8_t)(((tm->tm_hour / 10) << 4) | (tm->tm_hour % 10));
        bcd_m = (uint8_t)(((tm->tm_min / 10) << 4) | (tm->tm_min % 10));
        /* 小端：低地址=BCD时，高地址=BCD分 */
        stErr.u16ErrorCode = (uint16_t)bcd_h | ((uint16_t)bcd_m << 8);
    }

    LOG(LOG_DEBUG, "SendReportSensorError element=%u time_code=0x%04x type=%d",
        (unsigned)u32ElementType, (unsigned)stErr.u16ErrorCode, type);
    SendZigbeeData(UP_EXPAND_CODE, (uint8_t *)&stErr, sizeof(ReportSensorError), type);
}

/*
*函数名称:sendSpeedAndSectionSeir
*函数功能:发送测流和断面序号
*输   入:无
*输   出:无
*返   回:无
*/
void sendSpeedAndSectionSeir(void * arg)
{
    int type = (int)arg;
    LOG(LOG_INFO,"type is %d",type);
    ReportMesureSeri reposeri;

    reposeri.stCommandHead.u32CommandSeri = g_u32CommansSeri++;
    reposeri.stCommandHead.u32CommandLen = sizeof(ReportMesureSeri);
    memcpy(reposeri.stCommandHead.au8StationID,g_aStationID,sizeof(g_aStationID)) ;
    reposeri.stCommandHead.u32Command = REPORT_MEASUREANDSECTION_SERI;

    //reposeri.stCommandHead.u32CommandSeri = swap_u32(reposeri.stCommandHead.u32CommandSeri);
    //reposeri.stCommandHead.u32CommandLen = swap_u32(reposeri.stCommandHead.u32CommandLen);
    //reposeri.stCommandHead.u32Command = swap_u32(reposeri.stCommandHead.u32Command);
    reposeri.u16SectionSeri= g_stSection_new.u16SectionSeri;// swap_u16( g_stSection_new.u16SectionSeri);
    reposeri.u16SpeedSeri = g_stAutoMeasure.stAutoMeaHead.u16ParaSeri;//swap_u16( g_stAutoMeasure.stAutoMeaHead.u16ParaSeri);
    SendZigbeeData(UP_EXPAND_CODE,(uint8_t * )&reposeri,sizeof(ReportMesureSeri),type);
    LOG(LOG_DEBUG,"Send Section Seri is %04x,SpeedPara Seri is %04x ",( g_stSection_new.u16SectionSeri),( g_stAutoMeasure.stAutoMeaHead.u16ParaSeri));


}


int SendManuMudsResult(uint32_t commandseri,uint8_t resaut)
{
    return 1;
}
void SendNCSResult(void *arg)
{

}

/*
*函数名称:SendRotatorResultAuto
*函数功能:发送转子流速仪自动测流结果
*输   入:无
*输   出:无
*返   回:无
*/
void SendRotatorResultAuto(void * arg)
{
    int type = (int)arg;
    LOG(LOG_INFO,"type is %d",type);
    ReportAutoRotatorSpeed stReportRotator;
    stReportRotator.stCommandHead.u32Command = REPORT_AUTO_ROTATOR_DATA;
    stReportRotator.stCommandHead.u32CommandSeri = g_u32CommansSeri++;
    stReportRotator.stCommandHead.u32CommandLen = sizeof(ReportAutoRotatorSpeed) + sizeof(AutoRotatorData)  * ((g_ChoosedMode.stMeaModeHead.u16LineNum) - MAX_LINE_NUM) + 4;
    memcpy(stReportRotator.stCommandHead.au8StationID,g_aStationID,sizeof(g_aStationID)) ;
    stReportRotator.u16SpeedSeri = g_stAutoMeasure.stAutoMeaHead.u16ParaSeri;
    stReportRotator.u8LineNum = g_ChoosedMode.stMeaModeHead.u16LineNum;
    memcpy(&stReportRotator.stRotatorData,g_stAutoRotatorArray,sizeof(AutoRotatorData ) * (g_ChoosedMode.stMeaModeHead.u16LineNum) );
    
    LOG(LOG_DEBUG,"SendRotatorResultAuto ");
    SendZigbeeData(UP_EXPAND_CODE,(uint8_t * )&stReportRotator,sizeof(ReportAutoRotatorSpeed) + sizeof(AutoRotatorData)  * ((g_ChoosedMode.stMeaModeHead.u16LineNum) - MAX_LINE_NUM),type);

}


/*
*函数名称:SendRotatorResultAuto_depth
*函数功能:发送转子流速仪自动测流结果（分层上报，带水深位置百分比）
*         根据链表中的实际节点数动态计算报文长度
*输   入:void * arg  类型指针（DTU_TYPE / ZIGBEE_TYPE）
*输   出:无
*返   回:无
*
* 数据来源：
*   测量过程中，每完成一个深度的测量，将 AutoRotatorData_depth 节点
*   通过 insertSingleNodeByTail 插入到 g_pstAutoRotatorDepthList 链表中。
*   本函数遍历该链表，按 i32StartDistance（起点距）将节点分组到各垂线。
*
* 报文长度计算：
*   固定部分 = sizeof(SeriverCommandHead) + sizeof(u16SectionSeri)
*            + sizeof(u16SpeedSeri) + sizeof(u8LineNum)
*   可变部分 = sum_{i=0}^{totalLineNum-1} (depthCountPerLine[i] * sizeof(AutoRotatorData_depth))
*   总长度 = 固定部分 + 可变部分 + 尾部(4字节 0xFFFFFFFF)
*
* 注意：如果链表为空，直接返回，不发送数据。
*/
void SendRotatorResultAuto_depth(void * arg)
{
    int type = (int)arg;
    LOG(LOG_INFO, "SendRotatorResultAuto_depth type is %d", type);

    // 1. 链表为空或没有节点则不发送
    if (g_pstAutoRotatorDepthList == NULL || g_pstAutoRotatorDepthList->size == 0)
    {
        LOG(LOG_WARNING, "SendRotatorResultAuto_depth: depth list is NULL or empty, skip send");
        return;
    }

    // 2. totalLineNum = 链表中的节点总数（即所有深度的测量结果总数）
    uint8_t totalLineNum = (uint8_t)g_pstAutoRotatorDepthList->size;

    // 3. 没有节点则不发送
    if (totalLineNum == 0)
    {
        LOG(LOG_WARNING, "SendRotatorResultAuto_depth: no valid depth node found, skip send");
        return;
    }

    // 4. 计算实际发送长度
    //    每个节点对应一个 AutoRotatorData_depth
    //    固定头部: SeriverCommandHead + u16SectionSeri + u16SpeedSeri + u8LineNum
    //    可变主体: totalLineNum 个 AutoRotatorData_depth
    //    总长度 = 固定头部 + 可变主体 + 尾部(4字节)
    uint32_t fixedHeadLen = sizeof(SeriverCommandHead) + 
                            sizeof(uint16_t) + 
                            sizeof(uint16_t) + 
                            sizeof(uint8_t);
    uint32_t variableBodyLen = totalLineNum * sizeof(AutoRotatorData_depth);
    uint32_t totalSendLen = fixedHeadLen + variableBodyLen + sizeof(SeriverCommandTail);

    // 5. 分配缓冲区
    uint8_t *pSendBuf = (uint8_t *)malloc(totalSendLen);
    if (pSendBuf == NULL)
    {
        LOG(LOG_ERROR, "SendRotatorResultAuto_depth malloc(%d) failed", totalSendLen);
        return;
    }
    memset(pSendBuf, 0, totalSendLen);

    // 6. 组装命令头
    SeriverCommandHead stCmdHead;
    memset(&stCmdHead, 0, sizeof(SeriverCommandHead));
    stCmdHead.u32Command     = REPORT_AUTO_ROTATOR_DATA_DEPTH;
    stCmdHead.u32CommandSeri = g_u32CommansSeri++;
    stCmdHead.u32CommandLen  = totalSendLen;
    memcpy(stCmdHead.au8StationID, g_aStationID, sizeof(g_aStationID));

    // 7. 逐字节拼装
    uint32_t offset = 0;

    // 7a. 命令头
    memcpy(pSendBuf + offset, &stCmdHead, sizeof(SeriverCommandHead));
    offset += sizeof(SeriverCommandHead);

    // 7b. 断面序号
    memcpy(pSendBuf + offset, &g_stSection_new.u16SectionSeri, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    // 7c. 测流序号
    memcpy(pSendBuf + offset, &g_stAutoMeasure.stAutoMeaHead.u16ParaSeri, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    // 7d. 测线数量
    memcpy(pSendBuf + offset, &totalLineNum, sizeof(uint8_t));
    offset += sizeof(uint8_t);

    // 7e. 按链表顺序，逐个节点输出 AutoRotatorData_depth
    Node* curNode = g_pstAutoRotatorDepthList->firstNode.next;
    while (curNode != NULL)
    {
        AutoRotatorData_depth *pData = (AutoRotatorData_depth *)curNode->data;
        memcpy(pSendBuf + offset, pData, sizeof(AutoRotatorData_depth));
        offset += sizeof(AutoRotatorData_depth);
        curNode = curNode->next;
    }

    // 8f. 报文尾部 (0xFFFFFFFF)
    uint32_t tail = 0xFFFFFFFF;
    memcpy(pSendBuf + offset, &tail, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    // 9. 发送（totalSendLen已包含尾部，-4因为SendZigbeeData内部会加尾部）
    LOG(LOG_DEBUG, "SendRotatorResultAuto_depth LineNum=%d, totalSendLen=%d",
        totalLineNum, totalSendLen);
    
    
    SendZigbeeData(UP_EXPAND_CODE, pSendBuf, totalSendLen - 4, type);

    // 10. 释放缓冲区
    free(pSendBuf);
    pSendBuf = NULL;
}

void SendRotatorResultManual(void * arg)
{
    int type = (int)arg;
    LOG(LOG_DEBUG,"SendRotatorResultManual ,type is %d",type);
    ReportManuRotatorSpeed stReportRotatorManu;
    stReportRotatorManu.stCommandHead.u32Command = REPORT_MANU_ROTATOR_DATA;
    stReportRotatorManu.stCommandHead.u32CommandSeri = g_u32CommansSeri++;
    stReportRotatorManu.stCommandHead.u32CommandLen = sizeof(ReportManuRotatorSpeed)  + 4;
    memcpy(stReportRotatorManu.stCommandHead.au8StationID,g_aStationID,sizeof(g_aStationID)) ;
    memcpy(&stReportRotatorManu.stResultBody,&g_stManuRotatorResultBody,sizeof(ReportManuRotatorBody));
    SendZigbeeData(UP_EXPAND_CODE,(uint8_t * )&stReportRotatorManu,sizeof(ReportManuRotatorSpeed),type);
}
/*
*函数名称:Fish_up
*函数功能:收起铅鱼
*输   入:无
*输   出:无
*返   回:SUCCESS - 成功     ERROR  - 错误
*/
void Fish_up_proc(void * arg)
{
	(void)arg;
    Fish_up_nonblock();
}

void Car_Goback_HomeProc(void * arg)
{
    (void)arg;
    Car_Goback_Home_nonblock();
}

int Rotator_manuMeasureStart(BYTE *pData,uint16_t u16datalen)
{
    LOG(LOG_INFO,"Rotator_manuMeasureStart rec datalen is %d,sizoef(RotatorManuMeaStart) is %d",u16datalen,sizeof(RotatorManuMeaStart));
    assert(u16datalen >= sizeof(RotatorManuMeaStart) - sizeof(SeriverCommandHead));
    memcpy(&g_stRotatormeasureStart.u32StartDistence,pData,sizeof(RotatorManuMeaStart) - sizeof(SeriverCommandHead));
    g_stRotatormeasureStart.u32StartDistence = (g_stRotatormeasureStart.u32StartDistence);
    LOG(LOG_DEBUG,"g_stRotatormeasureStart.u32StartDistence %d",g_stRotatormeasureStart.u32StartDistence);
    MeasureStart_sec = GetSysTime();
    Relay_MotorUnbreak(RELAY_COM0_ADDR);    
    //WriteTwoMotorRegs(g_stDeviceAddr.u8Motor_Drive_car,CTRL_ADDR,2,FWD,1500);//
    AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car,(AMPS_FWD) * GetCarRunRpm() * 10);
    g_bRunning_State = true;
    int timerfd1 = add_timer(MAX_WAITTIME * 10, 0, TIMER_ONCE, 0, RunningTimeout, NULL);
    while ((g_HoriTotalSpace * g_CarEncoderDistence <=    g_stRotatormeasureStart.u32StartDistence   ) && (!g_stHoriDIStatusFlag.u8FWD_LIMIT_FLAG) && g_bRunning_State)
    {
        if(fabs( g_HoriTotalSpace * g_CarEncoderDistence - g_stRotatormeasureStart.u32StartDistence) < SPEED_DOWN_DENTENCE )
        {
            AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_car,(AMPS_FWD) * GetCarRunRpm() * 5);
        }
        printf("Car is going fwd...\n");
        mSleep(50);
        Read_DI_Status(g_stDeviceAddr.u8Motor_Drive_car);
        //readComStatus(g_stDeviceAddr.u8Motor_Drive_car);

    }
    LOG(LOG_DEBUG,"Car Stop , Start fish down");
    mSleep(50);
    remove_timer(timerfd1);
    AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_car);
   
    Relay_MotorBreak(RELAY_COM0_ADDR);
    g_stHoriDIStatusFlag.u8FWD_LIMIT_FLAG = 0;
    sleep(1);
    Relay_MotorUnbreak(RELAY_COM1_ADDR);
    AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish,(AMPS_FWD)*(GetFishRunRpm() * 10));//下
    mSleep(50);
    int riverBedHigh = GetCurrentPosRiverbedHigh(&g_stSection_new,g_stRotatormeasureStart.u32StartDistence);
    if(riverBedHigh == ERROR)
    {
        LOG(LOG_ERROR,"GetCurrentPosRiverbedHigh Error");
        return ERROR;
    }
    uint32_t currentwaterlevel = GetCurrentWaterLevel();
    int inwaterdeepth = GenerateinWaterDeepth( riverBedHigh , currentwaterlevel , g_stRotatormeasureStart.u8Deepth);
    float fishneedtomove  = inwaterdeepth + g_stSetCableway.stSetCablewayBody.u32CableHight-currentwaterlevel+FISH_BODY_HIGHT;
    while(g_bRunning_State)
    {
        mSleep(50);
        if(fabs((g_VeroTotalSpace * g_FishEncoderDistence) - fishneedtomove)  < SPEED_DOWN_DENTENCE)
        {
            AMPS_MotorMove(g_stDeviceAddr.u8Motor_Drive_fish,(AMPS_FWD) *  GetFishRunRpm() * 3) ;//速度降低到正常速度30%
        }
        bool flag = fabs((g_VeroTotalSpace * g_FishEncoderDistence) - fishneedtomove)  < g_FishDistenceAdjust;
        if(flag  || (g_VeroTotalSpace * g_FishEncoderDistence) > fishneedtomove -g_FishDistenceAdjust)
        {
            LOG(LOG_DEBUG,"fish has arrived position");
            break;
        }
        /* [修改] 增加转子采集板触底DI(u16TouchGround)，与电机限位并行判断，及时停转 */
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

    }
    AMPS_MotorStop(g_stDeviceAddr.u8Motor_Drive_fish);			
    Relay_MotorBreak(RELAY_COM1_ADDR);		
    LOG(LOG_DEBUG,"Fish has Stop");	

    if(false != g_bRunning_State)
    {
        remove_timer(timerfd1);
    }

  
    LOG(LOG_DEBUG,"Fish Stop , Start rotator measure");
    return SUCCESS;
    

    
}

void Rotator_manuMeasureStartProc(void * arg)
{
    assert(arg != NULL);
	CommandProcParam stCommandPara = *(CommandProcParam *)arg;
    Rotator_manuMeasureStart(stCommandPara.pData,stCommandPara.u16datalen);

}



int Rotator_manuMeasureStop(BYTE *pData,uint16_t u16datalen,int type)
{
    LOG(LOG_INFO,"type is %d",type);
    struct _ResultPara stResultPara;
    assert(u16datalen >= sizeof(RotatorManuMeaStop) - sizeof(SeriverCommandHead));
    RotatorManuMeaStop stRotatorMesureStop;
    memcpy(&stRotatorMesureStop.u32StartDistence,pData,sizeof(RotatorManuMeaStop) - sizeof(SeriverCommandHead));
    stRotatorMesureStop.u32StartDistence = (stRotatorMesureStop.u32StartDistence);
   
    stResultPara.u32MeaElement = ROTATOR;
    stResultPara.u8Deepth = stRotatorMesureStop.u8Deepth;
    stResultPara.i32StartDistance = stRotatorMesureStop.u32StartDistence;
    //GetMeasureResult(&stResultPara);


    /* [修改] 停止测流: 等第2转子信号再取基准值(与AutoMeasure/Legacy一致) */
    WaitRotatorSecondSignal();

    ReadRotatorSpeedValue(NULL);//读取测量之前的值

    g_SensorNum ++;
    while (0 != g_stSensorFlag.u8Ratator_value_falg && g_bRunning_State)//等待测量之前的值返回
    {
        mSleep(10);
    }
    stResultPara.u32PrevSignalNum = g_stRotatorRecValue.u32Value;
    LOG(LOG_DEBUG,"PrevSignalNum is %d",stResultPara.u32PrevSignalNum);
    
    // 分段等待测量，可被紧急停止打断
    {
        uint32_t waitMs = g_stRotatorConfig.u16Meaduration * 1000;
        uint32_t elapsed = 0;
        while (elapsed < waitMs && g_bRunning_State)
        {
            mSleep(100);
            elapsed += 100;
        }
    }

    ReadRotatorSpeedValue(NULL);
    {
        int i;
        for (i = 0; i < 30 && g_bRunning_State; i++)
        {
            mSleep(100);
        }
    }
    g_SensorNum ++;
    g_GetResultFlag = 0;
    GetRatatorResult(&stResultPara);
    
    //GetMeasureResult(&g_stResultPara);
    
    int timerfd3 = add_timer(MAX_WAITTIME,0,TIMER_ONCE,0,TimeoutProc,NULL);
    
    //获取结果（可被紧急停止打断）
    while (g_bRunning_State)
    {
        if(0 ==  g_SensorNum || g_TimeoutFlag )
        {
            SendMeasureResultManu(stResultPara.u32MeaElement,DTU_TYPE);
            g_GetResultFlag = 0;
            g_TimeoutFlag = 0;
            
            break;
        }
        mSleep(10);
    }	
    if(true != g_TimeoutFlag)	
    {
        remove_timer(timerfd3);
    }


    sleep(2);
    SendRotatorResultManual((void *)type);
    
    /* [修改] 收鱼→回仓→下放平台，MotionFSM单链(原Fish_up+Car_Goback并发busy) */
    Fish_up_then_home_platform_nonblock();

    return SUCCESS;

}

void  Rotator_manuMeasureStopProc(void * arg)
{
    assert(arg != NULL);
	CommandProcParam stCommandPara = *(CommandProcParam *)arg;
    Rotator_manuMeasureStop(stCommandPara.pData,stCommandPara.u16datalen,stCommandPara.Devicetype);
}



/*
*函数名称:CommandCodeProc
*函数功能:命令类型处理
*输   入:BYTE *pData  需要处理的数据, DWORD dwLength  需要处理的数据长度
*输   出:无
*返   回:SUCCESS - 成功     ERROR  - 错误
*/
/*入参pData已经是只包含数据部分，从站码hou开始，dwLength是数据长度*/
int CommandCodeProc(BYTE *pData, DWORD dwLength,int type)
{   
    LOG(LOG_INFO,"CommandCodeProc type is %d",type);
	int ret = 0;
	BYTE aStation[12];
	uint32_t u32CommandCode;
    uint32_t paraStartdiff ;
    SeriverCommandHead stCommandHead;
    CommandProcParam stCommandPara;
    memset(&stCommandPara,0,sizeof(CommandProcParam));
    memcpy(&stCommandHead,pData,sizeof(SeriverCommandHead));
 
    /* LOG(LOG_DEBUG,"CommandCodeProc DATA is ");
    LOG_ARRAY(LOG_DEBUG,pData,dwLength,"%02x"); */
	if( 0 != memcmp(stCommandHead.au8StationID,g_aStationID,sizeof(aStation)))//站码不匹配，返回错误
	{
        LOG(LOG_ERROR,"Seriver send StationID is not equal to tyls.conf StationID,Seriver send StationID is");
        LOG_ARRAY(LOG_ERROR,stCommandHead.au8StationID,12,"%02x");
		return ERROR;
	}
    
    u32CommandCode = stCommandHead.u32Command;//swap_u32(stCommandHead.u32Command);
    LOG(LOG_INFO," Recevie CommandCode is :%x  data length:%d", u32CommandCode, dwLength); //! 输出数据的类型和数据的长度
    paraStartdiff = sizeof(SeriverCommandHead);
    switch (u32CommandCode)
    {
        case SET_SECTION:   //设置断面（旧）
            LOG(LOG_DEBUG, "Recevie CommandCode is SET_SECTION,submit to queue");
            SendResultFrame(u32CommandCode,stCommandHead.u32CommandSeri,SUCCESS,type);
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_SetSection,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;

        case SET_SECTION_NEW:   //设置断面参数——新
            LOG(LOG_DEBUG, "Recevie CommandCode is SET_SECTION_new,submit to queue");
            SendResultFrame(u32CommandCode,stCommandHead.u32CommandSeri,SUCCESS,type);
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_SetSection_new,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case SET_CALBEWAY:  //设置缆道参数
            LOG(LOG_DEBUG, "Recevie CommandCode is SET_CALBEWAY,submit to queue");
            SendResultFrame(u32CommandCode,stCommandHead.u32CommandSeri,SUCCESS,type);
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_SetCableway,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case SET_AUTO_MEASURE:  //设置自动测量参数
            LOG(LOG_DEBUG, "Recevie CommandCode is SET_AUTO_MEASURE,submit to queue");
            SendResultFrame(u32CommandCode,stCommandHead.u32CommandSeri,SUCCESS,type);
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_SetAutoMeasure,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case SWICH_CTRL_MODE://切换模式
            LOG(LOG_DEBUG, "Recevie CommandCode is SWICH_CTRL_MODE,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_SwitchMode,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case RESTART:   //重启
            LOG(LOG_DEBUG, "Recevie CommandCode is RESTART,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_Restart,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case UP_FISH://收起铅鱼
            LOG(LOG_DEBUG, "Recevie CommandCode is UP_FISH,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_UpFish,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case VERI_CTRI_BACK_NOW://水平回仓
            LOG(LOG_DEBUG, "Recevie CommandCode is VERI_CTRI_BACK_NOW,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_CarBackHome,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case CURR_POS_RATATOR_START://当前位置开始转子测流
            LOG(LOG_DEBUG, "Recevie CommandCode is CURR_POS_RATATOR_START,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_RotatorStart,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case CURR_POS_RATATOR_END://当前位置结束测流
            LOG(LOG_DEBUG, "Recevie CommandCode is CURR_POS_RATATOR_END,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_RotatorStop,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case CAR_VERI_MOVE:     //水平移动
            LOG(LOG_DEBUG, "Recevie CommandCode is CAR_VERI_MOVE,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_CarMove,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case FISH_HERO_MOVE:    //垂直移动
            LOG(LOG_DEBUG, "Recevie CommandCode is FISH_HERO_MOVE,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_FishMove,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case GET_CAR_CONFIG:    //获取行车参数
            LOG(LOG_DEBUG, "Recevie CommandCode is GET_CAR_CONFIG,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_GetCarConfig,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case ADD_MEASURE:       //加测
            LOG(LOG_DEBUG, "Recevie CommandCode is ADD_MEASURE,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_AddMeasure,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case EMERGENCE_STOP:    //紧急停止
            LOG(LOG_DEBUG, "Recevie CommandCode is EMERGENCE_STOP, IMMEDIATELY execute");
            // 紧急停止必须立即执行，不能入队等待（否则当前任务未完成时无法响应）
            emergency_stop_handler(NULL);
            // 清空任务队列中所有未执行的任务
            TaskQueue_Clear(&g_taskQueue);
            SendResultFrame(u32CommandCode,stCommandHead.u32CommandSeri,SUCCESS,type);
        break;
        case UPDATE_SOFTWARE:   /* 协议文档远程升级=26 */
            LOG(LOG_DEBUG, "Recevie CommandCode is UPDATE_SOFTWARE(26), ota handle");
            g_u16OtaDownFrameSeri = g_u16LastDownFrameSeri;
            {
                int otaRet = OtaUpgrade_HandleDocPacket(stCommandHead.u32CommandSeri, type,
                                                        pData + paraStartdiff,
                                                        (uint16_t)(dwLength - paraStartdiff));
                if (otaRet != SUCCESS) {
                    OtaUpgrade_PrepareResultEcho();
                    SendOtaUpgradeResultFrame(0, OTA_UPGRADE_STATUS_ERROR,
                                             stCommandHead.u32CommandSeri,
                                             OtaUpgrade_GetLastPkt(), type);
                }
            }
            /* 成功：ACK/切槽由 port_send → 0xFFFFFFE4（0升级中/1升级出错） */
        break;
        case SET_ROTATOR_PARA:   //设置转子参数
            LOG(LOG_DEBUG, "Recevie CommandCode is SET_ROTATOR_PARA,submit to queue");
            SendResultFrame(u32CommandCode,stCommandHead.u32CommandSeri,SUCCESS,type);
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_SetRotatorPara,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case PAUSE_CONTINUE_MEASURE: /* 31: 1暂停 2继续 3停止本次加测→电动门切换 */
            LOG(LOG_DEBUG, "Recevie CommandCode is PAUSE_CONTINUE_MEASURE(31),submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_PauseMeasure,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        // case RESUME_MEASURE:
        //     LOG(LOG_DEBUG, "Recevie CommandCode is RESUME_MEASURE,submit to queue");
        //     TaskQueue_SubmitSimple(&g_taskQueue, Cmd_ResumeMeasure,
        //                         NULL, 0,
        //                         type, u32CommandCode, stCommandHead.u32CommandSeri);
        // break;
        case CURR_POS_NCS_SPEED://
        break;
        case CURR_POS_MUDS_START://
        break;
        case CURR_POS_MUDS_END://
        break;
        case CURR_POS_ADCP_SPEED://
        break;
        case SET_CAR_CONFIG://
        break;
        case SET_FISH_CONFIG://
        break;
        case GET_FISH_CONFIG://
        break;
        case GET_STAMPLE_CTRL://
        break;
        case TOUCH_GROUND://
        break;
        case SET_MANU_MODE_SPEED://设置手动模式速度等级 1-7
            LOG(LOG_DEBUG, "Recevie CommandCode is SET_MANU_MODE_SPEED");
            if (dwLength < paraStartdiff + 1) {
                SendResultFrame(u32CommandCode, stCommandHead.u32CommandSeri,
                                (uint8_t)ERROR, type);
            } else if (ApplyManuSpeedLevel(pData[paraStartdiff]) != SUCCESS) {
                SendResultFrame(u32CommandCode, stCommandHead.u32CommandSeri,
                                (uint8_t)ERROR, type);
            } else {
                SendResultFrame(u32CommandCode, stCommandHead.u32CommandSeri,
                                SUCCESS, type);
                ReportManuModeSpeedFrame(stCommandHead.u32CommandSeri, type);
            }
        break;
        case GET_MANU_MODE_SPEED://获取手动模式速度等级
            LOG(LOG_DEBUG, "Recevie CommandCode is GET_MANU_MODE_SPEED");
            ReportManuModeSpeedFrame(stCommandHead.u32CommandSeri, type);
        break;
        case GTE_LOG://获取运行日志 -> 上报 REPORT_LOG(0xFFFFFFEC)，等41确认
            LOG(LOG_DEBUG, "Recevie CommandCode is GTE_LOG, submit to queue");
            if (dwLength < paraStartdiff + 2) {
                SendResultFrame(u32CommandCode, stCommandHead.u32CommandSeri,
                                (uint8_t)ERROR, type);
            } else {
                TaskQueue_SubmitSimple(&g_taskQueue, Cmd_SendLogFile,
                                    pData + paraStartdiff, 2,
                                    type, u32CommandCode, stCommandHead.u32CommandSeri);
            }
        break;
        case REPLY_LOG_DATA: //平台对日志包的确认
        {
            uint32_t reportCmd = 0;
            uint8_t deviceType = 0;
            uint8_t logLevel = 0;
            uint16_t pktSeri = 0;
            if (dwLength < paraStartdiff + 4 + 1 + 1 + 2) {
                LOG(LOG_WARNING, "REPLY_LOG_DATA: short payload len=%u", (unsigned)dwLength);
            } else {
                memcpy(&reportCmd, pData + paraStartdiff, sizeof(reportCmd));
                deviceType = pData[paraStartdiff + 4];
                logLevel = pData[paraStartdiff + 5];
                memcpy(&pktSeri, pData + paraStartdiff + 6, sizeof(pktSeri));
                LogUpload_OnAck(reportCmd, deviceType, logLevel, pktSeri);
            }
        }
        break;
        case GET_SUB_DEVICE_CONFIG:       //获取子设备配置文件
            LOG(LOG_DEBUG, "Recevie CommandCode is GET_SUB_DEVICE_CONFIG,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_SendSubDeviceConfigFile,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case SET_SUB_DEVICE_CONFIG:       //设置子设备配置文件
            LOG(LOG_DEBUG, "Recevie CommandCode is SET_SUB_DEVICE_CONFIG,submit to queue");
            TaskQueue_SubmitSimple(&g_taskQueue, Cmd_SetSubDeviceConfigFile,
                                pData + paraStartdiff, dwLength - paraStartdiff,
                                type, u32CommandCode, stCommandHead.u32CommandSeri);
        break;
        case SEND_FINAL_DATA:       //平台最终流量确认(命令27)
            LOG(LOG_DEBUG, "Recevie CommandCode is SEND_FINAL_DATA");
            if (dwLength >= paraStartdiff + sizeof(FinalResutlBddy)) {
                FinalResult_OnDownlink(pData + paraStartdiff,
                                       (uint16_t)(dwLength - paraStartdiff));
            } else {
                LOG(LOG_WARNING, "SEND_FINAL_DATA: short payload len=%u",
                    (unsigned)dwLength);
            }
        break;
        default:
        break;
    }
    return ret;
}


/*
*函数名称:ReadWaterLevel
*函数功能:读取雷达水位计指令
*输   入:void *arg
*输   出:无
*返   回:无
*/
void  ReadWaterLevel(void * arg)
{ 
    uint8_t buff[20] = {0};
    ModbusSendFrame sendData;

    (void)arg;
    /* 开关门占 ZigBee 时跳过读水位，避免挤掉门脉冲/DI */
    if (g_bDoorCtrlSendFlag) {
        LOG(LOG_DEBUG, "ReadWaterLevel skip: door bus busy");
        return;
    }
    
    sendData.u8SubAddr = g_stDeviceAddr.u8WaterLevel;
    sendData.u8Funcode = READCODE;
    if(g_stWaterLevelConfig.u16WaterType == WATER_TYPE_RADAR)  //雷达水位计
    {
        sendData.u16RegAddr = 0x03e8;
        sendData.u16RegData = 0x0001;
    }
    else if(g_stWaterLevelConfig.u16WaterType == WATER_TYPE_PASSIVE)
    {
        sendData.u16RegAddr = 0x0001;
        sendData.u16RegData = 0x0002;
    }
    
    
    
    sendData.u16RegAddr = swap_u16(sendData.u16RegAddr);
    sendData.u16RegData = swap_u16(sendData.u16RegData);
    sendData.u16CRC = CRC16_Modbus((uint8_t * )&sendData,sizeof(ModbusSendFrame) - 2);
    sendData.u16CRC = swap_u16(sendData.u16CRC);
    memcpy(buff,(unsigned char *)&sendData,sizeof(ModbusSendFrame));
    if(UartWrite(g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd,(unsigned char *)&sendData,sizeof(EncoderSendFrame)) < 0)
    {
        LOG(LOG_ERROR,"UartWrite error");
        return ;
    }
    LOG(LOG_DEBUG,"ReadWaterLevel Send data is ");
    LOG_ARRAY(LOG_DEBUG,buff,sizeof(ModbusSendFrame),"%02x");
    g_waterLevelCount ++;
    g_stSensorFlag.u8GetWaterlevel_flag = 0;
    g_waterLevelCount = g_waterLevelCount % 12;
}

/*
 * 发状态帧前读水位：发 Modbus 读指令并短等应答，不用 GetCurrentWaterLevel 缓存/网页指定值。
 */
uint32_t ReadWaterLevelFresh(void)
{
    uint32_t waited_ms = 0;
    const uint32_t step_ms = 20U;
    const uint32_t timeout_ms = 500U;

    if (g_bDoorCtrlSendFlag) {
        LOG(LOG_DEBUG, "ReadWaterLevelFresh skip: door bus busy, use cache=%u",
            (unsigned)g_u32waterLevel);
        return g_u32waterLevel;
    }

    ReadWaterLevel(NULL);
    while (waited_ms < timeout_ms) {
        if (g_stSensorFlag.u8GetWaterlevel_flag) {
            LOG(LOG_DEBUG, "ReadWaterLevelFresh: ok level=%u wait=%ums",
                (unsigned)g_u32waterLevel, waited_ms);
            g_stTouchScreenReportData.u16WaterLevelData = (uint16_t)g_u32waterLevel;
            return g_u32waterLevel;
        }
        mSleep(step_ms);
        waited_ms += step_ms;
    }

    LOG(LOG_WARNING,
        "ReadWaterLevelFresh: timeout %ums, last sensor level=%u",
        timeout_ms, (unsigned)g_u32waterLevel);
    g_stTouchScreenReportData.u16WaterLevelData = (uint16_t)g_u32waterLevel;
    return g_u32waterLevel;
}

/*
*函数名称:ReadWaterLevelper5min
*函数功能:每5秒读取一次水位信息回调函数
*输   入:void *arg
*输   出:无
*返   回:无
*/
void ReadWaterLevelper5min(void * arg)
{
    if (OtaUpgrade_IsBusy()) {
        return;
    }

    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    if((tm->tm_min % 5)== 0 && tm->tm_sec== 0)
    {
        ReadWaterLevel(arg);
        
    }
    else
    {
        return;
    }
}