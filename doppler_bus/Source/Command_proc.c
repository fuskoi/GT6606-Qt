#include "Command_proc.h"
#include "SL651_2014.h"
#include "Sensor.h"
#include "SectionParam.h"
#include "OtaUpgrade.h"
#include "ReportAck.h"
#include "UplinkOffline.h"
#include "HydrologyRound.h"
#include "CRC.h"
#include "log.h"
#include "ringfifo.h"
#include "timer_manager.h"
#include "SocketLink.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

uint32_t g_u32CommansSeri = 1; /* 流水号从 1 起，0 常被平台判非法 */

uint16_t AllocFrameSeri(void)
{
    uint16_t u16Seri = 0;

    if (g_u32CommansSeri == 0U || g_u32CommansSeri > 0xFFFFU) {
        g_u32CommansSeri = 1U;
    }
    u16Seri = (uint16_t)g_u32CommansSeri;
    if (g_u32CommansSeri >= 0xFFFFU) {
        g_u32CommansSeri = 1U;
    } else {
        g_u32CommansSeri++;
    }
    return u16Seri;
}

/* 半包等待：无进度超时则失步重同步（避免大包半截卡死后续下行） */
static uint16_t s_u16PartialNeed = 0;
static uint32_t s_u32PartialHave = 0;
static struct timeval s_stPartialSince;
static uint8_t s_u8RestartPending = 0;

/* F2 回灌/链路确认半包等待（与下行半包分开，避免互相覆盖） */
static uint16_t s_u16UpPartialNeed = 0;
static uint32_t s_u32UpPartialHave = 0;
static struct timeval s_stUpPartialSince;

/* 半包无增长超过此时长则放弃当前声明长度 */
#define DOWNLINK_PARTIAL_STALL_MS  2500U
/* OTA 时 F2 回灌更短：卡太久会挡住后面的 102 大包 */
#define UPLINK_PARTIAL_STALL_MS    800U
#define UPLINK_PARTIAL_STALL_OTA_MS 400U

static void device_reboot_cb(void* arg)
{
    (void)arg;
    LOG(LOG_DEBUG, "device reboot now");
    sync();
    (void)system("reboot");
}

/* 回复上行后再延时 reboot（配置下发 / 网页重启命令共用） */
static void schedule_device_reboot(uint32_t delay_ms, const char* reason)
{
    if (s_u8RestartPending) {
        LOG(LOG_DEBUG, "%s: reboot already pending", reason != NULL ? reason : "reboot");
        return;
    }
    if (delay_ms == 0) {
        delay_ms = 2000U;
    }
    s_u8RestartPending = 1;
    LOG(LOG_DEBUG, "%s: reboot in %u ms", reason != NULL ? reason : "reboot", (unsigned)delay_ms);
    add_timer((uint64_t)delay_ms, 0, TIMER_ONCE, 0, device_reboot_cb, NULL);
}

/* 下行 RESTART_CODE(3)：先回 RTU_RESTART(88)，延时后 reboot */
static int handle_restart_code(const uint8_t* pBody, uint16_t u16BodyLen)
{
    uint8_t u8ErrCode = 0;

    (void)pBody;
    (void)u16BodyLen;

    /* 每次都回 88；若已在等待重启则只补应答、不重复挂 reboot 定时器 */
    SendUpLinkToServer(UP_EXPAND_CODE, (uint8_t)REPORT_RTU_RESTART_CMD,
                       &u8ErrCode, (uint16_t)sizeof(u8ErrCode), DTU_TYPE);

    schedule_device_reboot(2000U, "RESTART_CODE");
    return SUCCESS;
}

static uint32_t cmd_ms_since(const struct timeval* pTv)
{
    struct timeval now;

    if (pTv == NULL) {
        return 0xFFFFFFFFu;
    }
    gettimeofday(&now, NULL);
    if (now.tv_sec < pTv->tv_sec) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)((now.tv_sec - pTv->tv_sec) * 1000L +
                      (now.tv_usec - pTv->tv_usec) / 1000L);
}

/* lenField = 流水号(2) + BCD时间(6) + 指令字(1) + 业务数据 */
#define EXTEND_DATA_LEN_OVERHEAD  (sizeof(uint16_t) + sizeof(BCD_TIME) + sizeof(uint8_t))
#define MAX_EXTEND_FRAME_LEN      4200U

static uint16_t extend_extract_body_len(uint16_t dataLen)
{
    if (dataLen <= EXTEND_DATA_LEN_OVERHEAD) {
        return 0;
    }
    return (uint16_t)(dataLen - EXTEND_DATA_LEN_OVERHEAD);
}

static uint16_t extend_frame_len(uint16_t dataLen)
{
    uint32_t bodyLen = extend_extract_body_len(dataLen);
    uint32_t total = (uint32_t)sizeof(DOWNFrameHead) + bodyLen + (uint32_t)sizeof(DOWNFrametail);

    if (total > MAX_EXTEND_FRAME_LEN) {
        return 0;
    }
    return (uint16_t)total;
}

/* 从任意对齐地址读 2 字节（线序按小端拼成主机 uint16，再 swap 得协议大端值） */
static uint16_t load_u16_unaligned(const uint8_t* p)
{
    uint16_t v = 0;
    if (p == NULL) {
        return 0;
    }
    memcpy(&v, p, sizeof(v));
    return v;
}

static int downlink_crc_ok(const uint8_t* pFrame, uint16_t frameLen, uint16_t* pRecvOut)
{
    uint16_t crcCalc = 0;
    uint16_t crcRaw = 0;

    if (frameLen < 2) {
        return ERROR;
    }

    crcCalc = CRC16_Modbus(pFrame, (uint16_t)(frameLen - 2));
    memcpy(&crcRaw, pFrame + frameLen - 2, sizeof(crcRaw));
    if (crcCalc == crcRaw || crcCalc == swap_u16(crcRaw)) {
        if (pRecvOut != NULL) {
            *pRecvOut = crcRaw;
        }
        return SUCCESS;
    }
    return ERROR;
}

static int discard_fifo_bytes(KFIFO_ID pData, uint32_t count)
{
    uint8_t drop[64];
    uint32_t left = count;

    while (left > 0) {
        uint32_t chunk = (left > sizeof(drop)) ? (uint32_t)sizeof(drop) : left;
        kfifo_get(pData, drop, chunk);
        left -= chunk;
    }
    return SUCCESS;
}

/*
 * 链路层短确认：标志 bit15=1，且长度 < 业务下行开销（无指令字）。
 * 常见：F2 80 08、32 80 08（流水号2 + 时间6 = 8）。
 * 下行站址序；与 F1 业务下行（dataLen>=9）区分。
 * 返回值：0=已处理，1=未收齐，-1=不是此类帧。
 */
static int try_skip_link_confirm(KFIFO_ID pData, uint32_t u32fifolen)
{
    uint8_t head[16];
    uint16_t flag = 0;
    uint16_t dataLen = 0;
    uint16_t frameLen = 0;
    uint16_t pwd = 0;
    uint8_t frame[64];
    uint8_t u8Func = 0;

    if (u32fifolen < 16U) {
        return 1;
    }
    kfifo_preview(pData, head, sizeof(head));
    if (head[0] != HEAD1 || head[1] != HEAD2 || head[13] != STX) {
        return -1;
    }

    /* flag 在 offset 11，奇数地址：禁止 *(uint16_t*)，ARM 会读错/异常 */
    flag = swap_u16(load_u16_unaligned(head + 11));
    if ((flag & 0x8000U) == 0) {
        return -1;
    }
    dataLen = (uint16_t)(flag & 0x0FFFU);
    /* 短确认无 u8Signature；业务下行至少 seri+time+cmd=9 */
    if (dataLen == 0 || dataLen >= (uint16_t)EXTEND_DATA_LEN_OVERHEAD) {
        return -1;
    }

    u8Func = head[10];
    pwd = swap_u16(load_u16_unaligned(head + 8));
    if (pwd != g_u16Password || memcmp(head + 2, g_RTUID, 5) != 0) {
        LOG(LOG_DEBUG,
            "[DOWNLINK] link-confirm reject func=0x%02X pwd=%04X/%04X rtuid mismatch",
            (unsigned)u8Func, (unsigned)pwd, (unsigned)g_u16Password);
        return -1;
    }

    frameLen = (uint16_t)(14U + dataLen + 1U + 2U);
    if (frameLen > sizeof(frame)) {
        return -1;
    }
    if (u32fifolen < frameLen) {
        return 1;
    }

    kfifo_preview(pData, frame, frameLen);
    if (frame[frameLen - 3U] != ETX) {
        LOG(LOG_DEBUG, "[DOWNLINK] link-confirm no ETX func=0x%02X len=%u",
            (unsigned)u8Func, (unsigned)frameLen);
        return -1;
    }
    if (downlink_crc_ok(frame, frameLen, NULL) != SUCCESS) {
        /* 结构已像确认帧：仍整帧丢，避免 skip_uplink 逐字节失步 */
        LOG(LOG_WARNING, "[DOWNLINK] link-confirm CRC fail func=0x%02X len=%u",
            (unsigned)u8Func, (unsigned)frameLen);
        LOG_ARRAY(LOG_WARNING, frame, frameLen, "%02x");
    } else {
        uint16_t u16Seri = swap_u16(load_u16_unaligned(frame + 14));
        LOG(LOG_DEBUG,
            "[DOWNLINK] link-confirm(%s) func=0x%02X seri=%u len=%u",
            WebLink_IsSocketMode() ? "socket" : "dtu",
            (unsigned)u8Func, (unsigned)u16Seri, (unsigned)frameLen);
        LOG_ARRAY(LOG_DEBUG, frame, frameLen, "%02x");
        ReportAck_OnLinkConfirm(u16Seri);
        UplinkOffline_OnDownlink();
    }

    discard_fifo_bytes(pData, frameLen);
    return 0;
}

/*
 * 跳过 DTU 回灌的 F2 上行帧（含 102 ACK）。
 * 返回值：0=已丢弃整帧或已纠偏 1 字节，继续循环；1=帧未收齐，应退出等待后续字节。
 */
static int skip_uplink_frame(KFIFO_ID pData, uint32_t u32fifolen)
{
    uint8_t head[sizeof(UPFrameHead)];
    uint16_t dataLen = 0;
    uint16_t frameLen = 0;
    uint16_t pwd = 0;
    uint8_t cmd = 0;
    uint8_t frameBuf[256];
    uint8_t* pFrame = NULL;
    int needFree = 0;
    uint32_t stallMs = OtaUpgrade_IsBusy() ? UPLINK_PARTIAL_STALL_OTA_MS
                                            : UPLINK_PARTIAL_STALL_MS;

    if (u32fifolen < sizeof(UPFrameHead)) {
        if (s_u16UpPartialNeed != (uint16_t)sizeof(UPFrameHead) ||
            u32fifolen > s_u32UpPartialHave) {
            s_u16UpPartialNeed = (uint16_t)sizeof(UPFrameHead);
            s_u32UpPartialHave = u32fifolen;
            gettimeofday(&s_stUpPartialSince, NULL);
            return 1;
        }
        if (cmd_ms_since(&s_stUpPartialSince) >= stallMs) {
            LOG(LOG_WARNING,
                "uplink head timeout%s have=%u, drop 1",
                OtaUpgrade_IsBusy() ? "(ota)" : "",
                (unsigned)u32fifolen);
            kfifo_get(pData, head, 1);
            s_u16UpPartialNeed = 0;
            s_u32UpPartialHave = 0;
            return 0;
        }
        return 1;
    }

    kfifo_preview(pData, head, sizeof(head));
    if (head[10] != UP_EXPAND_CODE || head[13] != STX) {
        kfifo_get(pData, head, 1);
        s_u16UpPartialNeed = 0;
        s_u32UpPartialHave = 0;
        return 0;
    }

    /* 上行头：中心站(1)+遥测站(5)+密码，与下行头顺序不同 */
    pwd = swap_u16(load_u16_unaligned(head + 8));
    if (pwd != g_u16Password || memcmp(head + 3, g_RTUID, 5) != 0) {
        kfifo_get(pData, head, 1);
        s_u16UpPartialNeed = 0;
        s_u32UpPartialHave = 0;
        return 0;
    }

    dataLen = (uint16_t)(swap_u16(load_u16_unaligned(head + 11)) & 0x0FFF);
    frameLen = extend_frame_len(dataLen);
    if (frameLen == 0) {
        kfifo_get(pData, head, 1);
        s_u16UpPartialNeed = 0;
        s_u32UpPartialHave = 0;
        return 0;
    }
    if (frameLen > pData->size) {
        kfifo_get(pData, head, 1);
        s_u16UpPartialNeed = 0;
        s_u32UpPartialHave = 0;
        return 0;
    }
    if (u32fifolen < frameLen) {
        if (s_u16UpPartialNeed != frameLen || u32fifolen > s_u32UpPartialHave) {
            if (OtaUpgrade_IsBusy()) {
                LOG(LOG_DEBUG, "uplink waiting(ota) need=%u have=%u",
                    (unsigned)frameLen, (unsigned)u32fifolen);
            }
            s_u16UpPartialNeed = frameLen;
            s_u32UpPartialHave = u32fifolen;
            gettimeofday(&s_stUpPartialSince, NULL);
            return 1;
        }
        if (cmd_ms_since(&s_stUpPartialSince) >= stallMs) {
            /*
             * F2 回灌半包若无限等，后面的 F1/102 会永远进不了解析。
             * 只丢 1 字节重同步，尽量保留已到达的下行大包。
             */
            LOG(LOG_WARNING,
                "uplink incomplete timeout%s need=%u have=%u, drop 1 resync",
                OtaUpgrade_IsBusy() ? "(ota)" : "",
                (unsigned)frameLen, (unsigned)u32fifolen);
            kfifo_get(pData, head, 1);
            s_u16UpPartialNeed = 0;
            s_u32UpPartialHave = 0;
            return 0;
        }
        return 1;
    }

    s_u16UpPartialNeed = 0;
    s_u32UpPartialHave = 0;

    if (frameLen <= sizeof(frameBuf)) {
        pFrame = frameBuf;
    } else {
        pFrame = (uint8_t*)malloc(frameLen);
        if (pFrame == NULL) {
            kfifo_get(pData, head, 1);
            return 0;
        }
        needFree = 1;
    }
    kfifo_preview(pData, pFrame, frameLen);
    if (downlink_crc_ok(pFrame, frameLen, NULL) != SUCCESS) {
        LOG(LOG_DEBUG, "[DOWNLINK] tx-echo CRC fail len=%u, drop 1", (unsigned)frameLen);
        if (needFree) {
            free(pFrame);
        }
        kfifo_get(pData, head, 1);
        return 0;
    }

    cmd = pFrame[sizeof(UPFrameHead) - 1U];
    LOG(LOG_DEBUG, "[DOWNLINK] tx-echo(skip) cmd=%u len=%u", (unsigned)cmd, (unsigned)frameLen);
    {
        uint16_t dumpLen = (frameLen > 512U) ? 512U : frameLen;
        LOG_ARRAY(LOG_DEBUG, pFrame, dumpLen, "%02x");
    }
    discard_fifo_bytes(pData, frameLen);
    if (needFree) {
        free(pFrame);
    }
    return 0;
}

uint16_t SendUpLinkToServer(uint8_t u8FunctionCode, uint8_t u8CommandCode,
                            uint8_t* pData, uint16_t datalen, int type)
{
    uint8_t* buffer = NULL;
    UPFrameHead stUpframehead;
    uint8_t u8EOF = ETX;
    uint16_t u16CRC = 0;
    uint16_t u16Seri = 0;
    int bufferDataLen = 0;
    int sent = -1;
    int needPersist = 0;
    int viaSocket = WebLink_IsSocketMode();

    if (pData == NULL || datalen == 0) {
        return 0xFFFFU;
    }
    if (type != DTU_TYPE) {
        return 0xFFFFU;
    }

    needPersist = UplinkOffline_ShouldPersist((uint32_t)u8CommandCode);

    if (viaSocket) {
        if (!SocketLink_IsConnected()) {
            LOG(LOG_DEBUG, "SendUpLinkToServer skipped: socket not connected");
            if (needPersist) {
                (void)UplinkOffline_Enqueue(UPLINK_OFFLINE_KIND_WEB,
                                            (uint32_t)u8CommandCode,
                                            pData, datalen, type);
            }
            return 0xFFFFU;
        }
    } else {
        int hostPort = g_stDevicelist.HOST_COMPUTER;
        if (hostPort < 0 || hostPort >= MAX_UART_NUM ||
            g_stDevicelist.UartList[hostPort].i32fd <= 0) {
            LOG(LOG_DEBUG, "SendUpLinkToServer skipped: hostPort=%d fd=%d",
                hostPort,
                (hostPort >= 0 && hostPort < MAX_UART_NUM) ?
                    g_stDevicelist.UartList[hostPort].i32fd : -1);
            if (needPersist) {
                (void)UplinkOffline_Enqueue(UPLINK_OFFLINE_KIND_WEB,
                                            (uint32_t)u8CommandCode,
                                            pData, datalen, type);
            }
            return 0xFFFFU;
        }
    }

    buffer = (uint8_t*)malloc(datalen + sizeof(UPFrameHead) + sizeof(DOWNFrametail));
    if (buffer == NULL) {
        if (needPersist) {
            (void)UplinkOffline_Enqueue(UPLINK_OFFLINE_KIND_WEB,
                                        (uint32_t)u8CommandCode,
                                        pData, datalen, type);
        }
        return 0xFFFFU;
    }

    memset(&stUpframehead, 0, sizeof(stUpframehead));
    stUpframehead.u16SOH = 0x7E7E;
    stUpframehead.u8CentreStaID = g_CenterStationID;
    memcpy(stUpframehead.aRTUID, g_RTUID, sizeof(g_RTUID));
    stUpframehead.u16Password = swap_u16(g_u16Password);
    stUpframehead.u8FunctionCode = u8FunctionCode;
    stUpframehead.u8Signature = u8CommandCode;
    get_bcd_time((uint8_t*)&stUpframehead.BCD_time);
    stUpframehead.u8DataStart = STX;
    u16Seri = AllocFrameSeri();
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

    sent = WebLink_Send(buffer, bufferDataLen);
    LOG(LOG_DEBUG, "[UPLINK](%s) func=0x%02X cmd=%u seri=%u len=%d %s",
        viaSocket ? "socket" : "dtu",
        (unsigned)u8FunctionCode,
        (unsigned)u8CommandCode,
        (unsigned)u16Seri,
        bufferDataLen,
        (sent == bufferDataLen) ? "ok" : "FAIL");
    LOG_ARRAY(LOG_DEBUG, buffer, bufferDataLen, "%02x");
    free(buffer);

    /*
     * 白名单报文落盘：
     * - 已离线（无网页确认/下行）：本地 send 成功也落盘，等恢复后补发
     * - 在线但本地发送失败：同样落盘
     */
    if (needPersist && (sent != bufferDataLen || !UplinkOffline_IsOnline())) {
        (void)UplinkOffline_Enqueue(UPLINK_OFFLINE_KIND_WEB,
                                    (uint32_t)u8CommandCode,
                                    pData, datalen, type);
    }

    if (sent != bufferDataLen) {
        return 0xFFFFU;
    }
    return u16Seri;
}

static void pack_line_time_decimal(const struct tm* pTm, uint8_t au8Out[6])
{
    if (pTm == NULL || au8Out == NULL) {
        return;
    }
    /* 网页按「十六进制字节转十进制」读取，每字节存实际十进制值（非 BCD） */
    au8Out[0] = (uint8_t)((pTm->tm_year + 1900) % 100);
    au8Out[1] = (uint8_t)(pTm->tm_mon + 1);
    au8Out[2] = (uint8_t)pTm->tm_mday;
    au8Out[3] = (uint8_t)pTm->tm_hour;
    au8Out[4] = (uint8_t)pTm->tm_min;
    au8Out[5] = (uint8_t)pTm->tm_sec;
}

void SendDopplerRealData(void* arg)
{
    TrafficReportSnapshot stSnapshot;
    uint8_t body[sizeof(TrafficTimeDifferenceWire) + SENSOR_LINE_MAX * TRAFFIC_LINE_DETAIL_WIRE_SIZE];
    uint16_t u16BodyLen = 0;
    uint8_t i = 0;
    int force = (arg != NULL) ? 1 : 0;

    if (!force && !g_stMeasureConfig.u8WebReportEnable) {
        return;
    }
    if (OtaUpgrade_IsBusy()) {
        return;
    }

    if (Sensor_GetTrafficReportSnapshot(&stSnapshot) != SUCCESS) {
        if (force) {
            LOG(LOG_WARNING, "START_TEST: no sensor snapshot, skip FLOW_JET_LAG");
        }
        return;
    }

    memset(body, 0, sizeof(body));
    memcpy(body + u16BodyLen, &stSnapshot.i32AvgWaterScaled, sizeof(int32_t));
    u16BodyLen = (uint16_t)(u16BodyLen + sizeof(int32_t));
    body[u16BodyLen++] = stSnapshot.u8WaterMode;
    body[u16BodyLen++] = stSnapshot.u8LineCount;

    for (i = 0; i < SENSOR_LINE_MAX; i++) {
        TrafficTimeDifferenceDetailsWire stLine;
        const SensorTrafficLine* pSensorLine = &stSnapshot.astLines[i];

        if (!pSensorLine->u8Valid) {
            continue;
        }

        memset(&stLine, 0, sizeof(stLine));
        stLine.i32LineWater = hydrology_scale_round_i32((double)pSensorLine->stRaw.fWaterLevel, 2);
        {
            float fVel = pSensorLine->stRaw.fFlowVelocity;
            if (fVel < 0.0f) {
                fVel = 0.0f;
            }
            stLine.i16LineFlowRate = hydrology_scale_round_i16((double)fVel, 3);
        }
        pack_line_time_decimal(&pSensorLine->stCaptureTime, stLine.au8LineTime);
        {
            float fSecFlow = pSensorLine->stRaw.fSecFlow;
            float fHourFlow = pSensorLine->stRaw.fHourFlow;
            if (fSecFlow < 0.0f) {
                fSecFlow = 0.0f;
            }
            if (fHourFlow < 0.0f) {
                fHourFlow = 0.0f;
            }
            stLine.i32LineSecondFlow = hydrology_scale_round_i32((double)fSecFlow, 2);
            stLine.i32LineHourFlow = hydrology_scale_round_i32((double)fHourFlow, 2);
        }

        memcpy(body + u16BodyLen, &stLine, sizeof(stLine));
        u16BodyLen = (uint16_t)(u16BodyLen + sizeof(stLine));
    }

    LOG(LOG_DEBUG,
        "FLOW_JET_LAG report%s: avgWater=%d mode=%u lines=%u bodyLen=%u",
        force ? "(START_TEST)" : "",
        (int)stSnapshot.i32AvgWaterScaled,
        (unsigned)stSnapshot.u8WaterMode,
        (unsigned)stSnapshot.u8LineCount,
        (unsigned)u16BodyLen);

    ReportAck_Send((uint8_t)REPORT_FLOW_JET_LAG_CMD, body, u16BodyLen);
    /* 水位测流覆盖只用这一次上报，随后恢复传感器水位 */
    Sensor_ClearOverrideWaterAfterReport();
}

/* 下行 START_TEST_CODE(1) 加测：先采一轮，采到有效数据后再走 121 */
static int handle_start_test_code(const uint8_t* pBody, uint16_t u16BodyLen)
{
    (void)pBody;
    (void)u16BodyLen;
    Sensor_RequestStartTestReport();
    return SUCCESS;
}

/* 下行 SHUIWEI_CELIU(29)：设置水位(cm) 后按该水位重算并上发 121；
 * 上报成功后自动恢复传感器水位。-10000/-100000 表示立即用传感器水位。 */
static int handle_shuiwei_celiu(const uint8_t* pBody, uint16_t u16BodyLen)
{
    int32_t i32WaterCm = 0;
    static int s_forceFlag = 1;
    TrafficReportSnapshot stSnap;

    if (pBody == NULL || u16BodyLen < sizeof(int32_t)) {
        LOG(LOG_WARNING, "SHUIWEI_CELIU body too short len=%u", (unsigned)u16BodyLen);
        return ERROR;
    }
    memcpy(&i32WaterCm, pBody, sizeof(i32WaterCm));
    Sensor_SetOverrideWaterCm(i32WaterCm);

    if (Sensor_GetTrafficReportSnapshot(&stSnap) == SUCCESS) {
        Sensor_LogLocalDischargeFromSnapshot(&stSnap);
        SendDopplerRealData(&s_forceFlag);
    } else {
        LOG(LOG_DEBUG, "SHUIWEI_CELIU: no snapshot yet, collect then FLOW_JET_LAG");
        Sensor_RequestStartTestReport();
    }
    return SUCCESS;
}

/* 拼出运行目录下 doppler.conf 绝对路径 */
static int config_file_path(char* pathOut, size_t pathSize)
{
    if (pathOut == NULL || pathSize < 16U) {
        return ERROR;
    }
    memset(pathOut, 0, pathSize);
    if (!GetAppDir(pathOut)) {
        return ERROR;
    }
    if (strlen(pathOut) + strlen(CONFIG_FILE_PATH) >= pathSize) {
        return ERROR;
    }
    strcat(pathOut, CONFIG_FILE_PATH);
    return SUCCESS;
}

/* 读本地 doppler.conf 并上行 GET_CONFIG_RESPONSE(83) */
static int uplink_get_config_response(void)
{
    char configPath[MAX_PATH];
    FILE* pFile = NULL;
    long fileSize = 0;
    uint8_t* pBuf = NULL;
    size_t nRead = 0;

    if (config_file_path(configPath, sizeof(configPath)) != SUCCESS) {
        LOG(LOG_ERROR, "config path failed");
        return ERROR;
    }

    pFile = fopen(configPath, "rb");
    if (pFile == NULL) {
        LOG(LOG_ERROR, "open %s failed", configPath);
        return ERROR;
    }
    if (fseek(pFile, 0, SEEK_END) != 0) {
        fclose(pFile);
        return ERROR;
    }
    fileSize = ftell(pFile);
    if (fileSize <= 0 || fileSize > (long)MAX_FILE_LEN) {
        LOG(LOG_ERROR, "bad conf size=%ld", fileSize);
        fclose(pFile);
        return ERROR;
    }
    if (fseek(pFile, 0, SEEK_SET) != 0) {
        fclose(pFile);
        return ERROR;
    }

    pBuf = (uint8_t*)malloc((size_t)fileSize);
    if (pBuf == NULL) {
        fclose(pFile);
        return ERROR;
    }
    nRead = fread(pBuf, 1, (size_t)fileSize, pFile);
    fclose(pFile);
    if (nRead != (size_t)fileSize) {
        LOG(LOG_ERROR, "read %s failed got=%u expect=%ld",
            configPath, (unsigned)nRead, fileSize);
        free(pBuf);
        return ERROR;
    }

    LOG(LOG_DEBUG, "uplink GET_CONFIG_RESPONSE size=%u path=%s",
        (unsigned)nRead, configPath);
    if (SendUpLinkToServer(UP_EXPAND_CODE, (uint8_t)REPORT_GET_CONFIG_RESPONSE,
                           pBuf, (uint16_t)nRead, DTU_TYPE) == 0xFFFFU) {
        free(pBuf);
        return ERROR;
    }
    free(pBuf);
    return SUCCESS;
}

/* 下行 GET_CONFIG(14)：读 doppler.conf，上行 83 回传全文 */
static int handle_get_config(const uint8_t* pBody, uint16_t u16BodyLen)
{
    (void)pBody;
    (void)u16BodyLen;

    if (OtaUpgrade_IsBusy()) {
        LOG(LOG_WARNING, "GET_CONFIG skipped: ota busy");
        return ERROR;
    }
    return uplink_get_config_response();
}

/*
 * 下行 RETRIEVE_LOGS(103)：设备类型(1=rtu) + 日志级别(1全部/2警告/3错误/4debug)。
 * 上行 RETRIEVE_LOGS_RESULT(122)：deviceType + logLevel + fileSize(LE int)
 * + pktTotal(LE) + pktIndex(LE,从1) + 内容。
 * 每包停等 DATA_RESPONSE(150)：净荷为 122 + deviceType + logLevel + pktIndex(LE short)；
 * 10s 未回重发一次；累计 2 分钟仍无回则中止并 WARNING。
 * 独立线程上传，避免堵住收包。
 */
#define RETRIEVE_LOGS_CHUNK_MAX   (3U * 1024U) /* 每包内容上限 3KB（帧长 12 位内可容纳） */
/* 网页拉取只导出最近 1MB（本地环缓可为 2MB） */
#define RETRIEVE_LOGS_EXPORT_MAX  (1U * 1024U * 1024U)
/* 10s 未收到 150 则重发当前包一次；累计 2 分钟仍无回复则中止整次上传 */
#define RETRIEVE_LOGS_ACK_RESEND_MS   10000U
#define RETRIEVE_LOGS_ACK_GIVEUP_MS   120000U

typedef struct RetrieveLogsJob {
    uint8_t* pExport;
    uint8_t* pPktBuf;
    size_t nExport;
    uint16_t u16PktTotal;
    uint8_t u8DeviceType;
    uint8_t u8LogLevel;
} RetrieveLogsJob;

static volatile int s_logUpBusy = 0;
static volatile int s_logUpAck = 0;
static volatile uint8_t s_logUpDeviceType = 0;
static volatile uint8_t s_logUpLogLevel = 0;
static volatile uint16_t s_logUpExpectPkt = 0;

void RetrieveLogs_NotifyDataResponse(const uint8_t* pBody, uint16_t u16BodyLen)
{
    RetrieveLogsAckWire stAck;

    if (!s_logUpBusy) {
        return;
    }
    /* 最短：仅命令字 122（兼容）；完整：122+设备类型+级别+包序号 */
    if (pBody == NULL || u16BodyLen < 1U || pBody[0] != (uint8_t)REPORT_RETRIEVE_LOGS_RESULT) {
        return;
    }
    if (u16BodyLen >= sizeof(RetrieveLogsAckWire)) {
        memset(&stAck, 0, sizeof(stAck));
        memcpy(&stAck, pBody, sizeof(stAck));
        if (stAck.u8DeviceType != s_logUpDeviceType ||
            stAck.u8LogLevel != s_logUpLogLevel ||
            stAck.u16PktIndex != s_logUpExpectPkt) {
            LOG(LOG_DEBUG,
                "RETRIEVE_LOGS: ignore ack device=%u/%u level=%u/%u pkt=%u/%u",
                (unsigned)stAck.u8DeviceType, (unsigned)s_logUpDeviceType,
                (unsigned)stAck.u8LogLevel, (unsigned)s_logUpLogLevel,
                (unsigned)stAck.u16PktIndex, (unsigned)s_logUpExpectPkt);
            return;
        }
    }
    s_logUpAck = 1;
}

static int retrieve_logs_send_one(RetrieveLogsJob* pJob, uint16_t u16Pkt)
{
    RetrieveLogsResultWire* pHead = NULL;
    size_t off = 0;
    size_t chunk = 0;
    uint16_t bodyLen = 0;
    uint32_t waited = 0;
    int resent = 0;

    if (pJob == NULL || pJob->pPktBuf == NULL) {
        return ERROR;
    }

    memset(pJob->pPktBuf, 0, sizeof(RetrieveLogsResultWire) + RETRIEVE_LOGS_CHUNK_MAX);
    pHead = (RetrieveLogsResultWire*)pJob->pPktBuf;
    pHead->u8DeviceType = pJob->u8DeviceType;
    pHead->u8LogLevel = pJob->u8LogLevel;
    pHead->i32FileSize = (int32_t)pJob->nExport;
    pHead->u16PktTotal = pJob->u16PktTotal;
    pHead->u16PktIndex = u16Pkt;

    if (pJob->nExport > 0 && pJob->pExport != NULL) {
        off = (size_t)(u16Pkt - 1U) * (size_t)RETRIEVE_LOGS_CHUNK_MAX;
        if (off < pJob->nExport) {
            chunk = pJob->nExport - off;
            if (chunk > RETRIEVE_LOGS_CHUNK_MAX) {
                chunk = RETRIEVE_LOGS_CHUNK_MAX;
            }
            memcpy(pJob->pPktBuf + sizeof(RetrieveLogsResultWire),
                   pJob->pExport + off, chunk);
        }
    }

    bodyLen = (uint16_t)(sizeof(RetrieveLogsResultWire) + chunk);
    s_logUpExpectPkt = u16Pkt;
    s_logUpDeviceType = pJob->u8DeviceType;
    s_logUpLogLevel = pJob->u8LogLevel;
    s_logUpAck = 0;
    if (SendUpLinkToServer(UP_EXPAND_CODE, (uint8_t)REPORT_RETRIEVE_LOGS_RESULT,
                           pJob->pPktBuf, bodyLen, DTU_TYPE) == 0xFFFFU) {
        LOG(LOG_WARNING, "RETRIEVE_LOGS: uplink fail pkt=%u/%u",
            (unsigned)u16Pkt, (unsigned)pJob->u16PktTotal);
        return ERROR;
    }

    waited = 0;
    while (waited < RETRIEVE_LOGS_ACK_GIVEUP_MS) {
        if (s_logUpAck) {
            s_logUpAck = 0;
            return SUCCESS;
        }
        if (!resent && waited >= RETRIEVE_LOGS_ACK_RESEND_MS) {
            s_logUpAck = 0;
            if (SendUpLinkToServer(UP_EXPAND_CODE, (uint8_t)REPORT_RETRIEVE_LOGS_RESULT,
                                   pJob->pPktBuf, bodyLen, DTU_TYPE) == 0xFFFFU) {
                LOG(LOG_WARNING, "RETRIEVE_LOGS: resend uplink fail pkt=%u/%u",
                    (unsigned)u16Pkt, (unsigned)pJob->u16PktTotal);
                return ERROR;
            }
            resent = 1;
            LOG(LOG_DEBUG, "RETRIEVE_LOGS: resend pkt=%u/%u (no ack in %us)",
                (unsigned)u16Pkt, (unsigned)pJob->u16PktTotal,
                (unsigned)(RETRIEVE_LOGS_ACK_RESEND_MS / 1000U));
        }
        usleep(20000);
        waited += 20U;
    }

    LOG(LOG_WARNING, "RETRIEVE_LOGS: ack timeout pkt=%u/%u, stop upload",
        (unsigned)u16Pkt, (unsigned)pJob->u16PktTotal);
    return ERROR;
}

static void* retrieve_logs_worker(void* arg)
{
    RetrieveLogsJob* pJob = (RetrieveLogsJob*)arg;
    uint16_t u16Pkt = 0;

    if (pJob == NULL) {
        s_logUpBusy = 0;
        return NULL;
    }

    for (u16Pkt = 1; u16Pkt <= pJob->u16PktTotal; u16Pkt++) {
        if (retrieve_logs_send_one(pJob, u16Pkt) != SUCCESS) {
            /* ack timeout 已在 send_one 打过 WARNING，此处不再重复 */
            break;
        }
        if ((u16Pkt % 32U) == 0U || u16Pkt == pJob->u16PktTotal) {
            LOG(LOG_DEBUG, "RETRIEVE_LOGS: progress pkt=%u/%u",
                (unsigned)u16Pkt, (unsigned)pJob->u16PktTotal);
        }
    }

    if (u16Pkt > pJob->u16PktTotal) {
        LOG(LOG_DEBUG, "RETRIEVE_LOGS: done device=%u level=%u pkts=%u export=%u",
            (unsigned)pJob->u8DeviceType, (unsigned)pJob->u8LogLevel,
            (unsigned)pJob->u16PktTotal, (unsigned)pJob->nExport);
    }

    free(pJob->pPktBuf);
    free(pJob->pExport);
    free(pJob);
    s_logUpBusy = 0;
    s_logUpExpectPkt = 0;
    return NULL;
}

static int handle_retrieve_logs(const uint8_t* pBody, uint16_t u16BodyLen)
{
    uint8_t u8DeviceType = (uint8_t)RETRIEVE_LOGS_DEVICE_RTU;
    uint8_t u8LogLevel = (uint8_t)RETRIEVE_LOGS_LEVEL_ALL;
    RetrieveLogsJob* pJob = NULL;
    pthread_t tid;
    int err = 0;

    if (OtaUpgrade_IsBusy()) {
        LOG(LOG_WARNING, "RETRIEVE_LOGS skipped: ota busy");
        return ERROR;
    }
    if (s_logUpBusy) {
        LOG(LOG_WARNING, "RETRIEVE_LOGS skipped: upload busy");
        return ERROR;
    }

    if (pBody != NULL && u16BodyLen >= 2U) {
        u8DeviceType = pBody[0];
        u8LogLevel = pBody[1];
    } else if (pBody != NULL && u16BodyLen >= 1U) {
        /* 兼容旧净荷：仅日志级别 */
        u8LogLevel = pBody[0];
    }

    if (u8DeviceType != (uint8_t)RETRIEVE_LOGS_DEVICE_RTU) {
        LOG(LOG_WARNING, "RETRIEVE_LOGS skipped: deviceType=%u (only rtu=1)",
            (unsigned)u8DeviceType);
        return ERROR;
    }

    pJob = (RetrieveLogsJob*)calloc(1, sizeof(RetrieveLogsJob));
    if (pJob == NULL) {
        LOG(LOG_ERROR, "RETRIEVE_LOGS: malloc job failed");
        return ERROR;
    }
    pJob->u8DeviceType = u8DeviceType;
    pJob->u8LogLevel = u8LogLevel;
    pJob->pExport = (uint8_t*)malloc(RETRIEVE_LOGS_EXPORT_MAX);
    if (pJob->pExport == NULL) {
        free(pJob);
        LOG(LOG_ERROR, "RETRIEVE_LOGS: malloc export failed");
        return ERROR;
    }
    /* 级别筛选暂未做：仍导出全部，级别字段原样回传 */
    pJob->nExport = log_export_recent(pJob->pExport, RETRIEVE_LOGS_EXPORT_MAX);

    if (pJob->nExport == 0) {
        pJob->u16PktTotal = 1;
    } else {
        pJob->u16PktTotal = (uint16_t)((pJob->nExport + RETRIEVE_LOGS_CHUNK_MAX - 1U) /
                                       RETRIEVE_LOGS_CHUNK_MAX);
        if (pJob->u16PktTotal == 0) {
            pJob->u16PktTotal = 1;
        }
    }

    pJob->pPktBuf = (uint8_t*)malloc(sizeof(RetrieveLogsResultWire) + RETRIEVE_LOGS_CHUNK_MAX);
    if (pJob->pPktBuf == NULL) {
        free(pJob->pExport);
        free(pJob);
        LOG(LOG_ERROR, "RETRIEVE_LOGS: malloc pkt failed");
        return ERROR;
    }

    s_logUpBusy = 1;
    s_logUpAck = 0;
    s_logUpDeviceType = u8DeviceType;
    s_logUpLogLevel = u8LogLevel;
    LOG(LOG_DEBUG,
        "RETRIEVE_LOGS: start device=%u level=%u export=%u pktTotal=%u (wait 150)",
        (unsigned)u8DeviceType, (unsigned)u8LogLevel,
        (unsigned)pJob->nExport, (unsigned)pJob->u16PktTotal);

    err = pthread_create(&tid, NULL, retrieve_logs_worker, pJob);
    if (err != 0) {
        s_logUpBusy = 0;
        free(pJob->pPktBuf);
        free(pJob->pExport);
        free(pJob);
        LOG(LOG_ERROR, "RETRIEVE_LOGS: pthread_create failed %d", err);
        return ERROR;
    }
    (void)pthread_detach(tid);
    return SUCCESS;
}

/*
 * 下行 SETCONFIG_FILE(18)：WORD filelength(LE) + CONF 内容 → 覆盖 doppler.conf，
 * 再上行 83；回复发出后延时重启，使串口等配置完整生效。
 */
static int handle_setconfig_file(const uint8_t* pBody, uint16_t u16BodyLen)
{
    uint16_t u16FileLen = 0;
    uint16_t u16Remain = 0;
    char configPath[MAX_PATH];
    FILE* pFile = NULL;
    size_t nWritten = 0;
    const uint8_t* pContent = NULL;
    uint16_t u16ContentLen = 0;
    uint16_t u16Skip = 0;

    if (OtaUpgrade_IsBusy()) {
        LOG(LOG_WARNING, "SETCONFIG_FILE skipped: ota busy");
        return ERROR;
    }
    if (pBody == NULL || u16BodyLen < sizeof(uint16_t)) {
        LOG(LOG_WARNING, "SETCONFIG_FILE body too short len=%u", (unsigned)u16BodyLen);
        return ERROR;
    }

    memcpy(&u16FileLen, pBody, sizeof(u16FileLen));
    u16Remain = (uint16_t)(u16BodyLen - sizeof(uint16_t));
    pContent = pBody + sizeof(uint16_t);

    if (u16FileLen == 0 || u16FileLen > u16Remain) {
        LOG(LOG_WARNING,
            "SETCONFIG_FILE filelength=%u remain=%u, use remain as content",
            (unsigned)u16FileLen, (unsigned)u16Remain);
        u16ContentLen = u16Remain;
    } else {
        u16ContentLen = u16FileLen;
    }
    if (u16ContentLen == 0) {
        LOG(LOG_WARNING, "SETCONFIG_FILE empty content");
        return ERROR;
    }

    /* 跳过正文前非配置文本（直至 '#' 或字母），避免网页多塞的 03 26 等字节 */
    while (u16Skip < u16ContentLen) {
        uint8_t c = pContent[u16Skip];
        if (c == '#' || c == ';' ||
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9')) {
            break;
        }
        u16Skip++;
    }
    if (u16Skip >= u16ContentLen) {
        LOG(LOG_WARNING, "SETCONFIG_FILE no conf text after skip=%u", (unsigned)u16Skip);
        return ERROR;
    }
    if (u16Skip > 0) {
        LOG(LOG_WARNING, "SETCONFIG_FILE skip leading %u junk byte(s)", (unsigned)u16Skip);
        pContent += u16Skip;
        u16ContentLen = (uint16_t)(u16ContentLen - u16Skip);
    }

    if (config_file_path(configPath, sizeof(configPath)) != SUCCESS) {
        LOG(LOG_ERROR, "SETCONFIG_FILE: path failed");
        return ERROR;
    }

    pFile = fopen(configPath, "wb");
    if (pFile == NULL) {
        LOG(LOG_ERROR, "SETCONFIG_FILE: open %s for write failed", configPath);
        return ERROR;
    }
    nWritten = fwrite(pContent, 1, (size_t)u16ContentLen, pFile);
    fflush(pFile);
    fclose(pFile);
    if (nWritten != (size_t)u16ContentLen) {
        LOG(LOG_ERROR, "SETCONFIG_FILE: write failed wrote=%u expect=%u",
            (unsigned)nWritten, (unsigned)u16ContentLen);
        return ERROR;
    }

    LOG(LOG_DEBUG, "SETCONFIG_FILE: saved %s size=%u, reload + uplink 83",
        configPath, (unsigned)u16ContentLen);
    /* 不可直接 ReadConfig：会 memset 掉已打开串口 fd，导致上行 skipped fd=0 */
    if (!ReloadConfigKeepUart()) {
        LOG(LOG_WARNING, "SETCONFIG_FILE: ReloadConfigKeepUart failed after write");
    }
    if (uplink_get_config_response() != SUCCESS) {
        LOG(LOG_WARNING, "SETCONFIG_FILE: uplink 83 failed, still reboot to apply conf");
    }
    /* 先回 83，再延时 reboot，避免应答未发出就断链 */
    schedule_device_reboot(2000U, "SETCONFIG_FILE");
    return SUCCESS;
}

int Proc_DTU_RecData(KFIFO_ID pData, WORD* pUsed, int type)
{
    uint8_t tempbuff[4200];
    DOWNFrameHead stDownFrameHead;
    uint32_t u32fifolen = 0;
    uint16_t dataLen = 0;
    uint16_t frameLen = 0;
    uint16_t crcCalc = 0;
    uint16_t crcRecv = 0;
    bool bHeadFlag = false;
    int handled = 0;

    (void)type;
    (void)pUsed;

    for (;;) {
        /* 短确认整帧 25 字节；勿用 sizeof(DOWNFrameHead) 卡死入口 */
        u32fifolen = kfifo_len(pData);
        if (u32fifolen < 16U) {
            return SUCCESS;
        }
        if (MAX_SOFTWARE_SIZE <= u32fifolen) {
            kfifo_clear(pData);
            return ERROR;
        }

        bHeadFlag = uartFindFrameHead(pData, HEAD1, HEAD2);
        if (!bHeadFlag) {
            return SUCCESS;
        }

        /* 找头可能丢过前导噪声，按当前 FIFO 长度再判 */
        u32fifolen = kfifo_len(pData);
        if (u32fifolen < 16U) {
            return SUCCESS;
        }

        kfifo_preview(pData, tempbuff, (u32fifolen < sizeof(UPFrameHead))
                                           ? (uint16_t)u32fifolen
                                           : (uint16_t)sizeof(UPFrameHead));
        {
            int conf = try_skip_link_confirm(pData, u32fifolen);
            if (conf == 0) {
                handled = 1;
                continue;
            }
            if (conf > 0) {
                return ERROR;
            }
        }

        if (u32fifolen < sizeof(DOWNFrameHead)) {
            /* 不像短确认，又不够业务下行头：等更多字节 */
            return handled ? SUCCESS : ERROR;
        }

        if (tempbuff[10] == UP_EXPAND_CODE) {
            if (skip_uplink_frame(pData, u32fifolen) > 0) {
                /* F2 尚未收齐：退出等待，避免空转占死处理线程 */
                return ERROR;
            }
            handled = 1;
            continue;
        }

        if (tempbuff[13] != STX) {
            kfifo_get(pData, tempbuff, 1);
            continue;
        }

        kfifo_preview(pData, tempbuff, sizeof(DOWNFrameHead));
        memcpy(&stDownFrameHead, tempbuff, sizeof(DOWNFrameHead));
        /* FlagAndLen 在包内奇数偏移，用字节拼避免非对齐 */
        stDownFrameHead.u16FlagAndLen = swap_u16(load_u16_unaligned(tempbuff + 11));
        dataLen = stDownFrameHead.u16FlagAndLen & 0x0FFF;
        /*
         * dataLen < 9：不是带指令字的业务下行。
         * 若带应答位则应已被 try_skip_link_confirm 吃掉；落到这里多半是半包/错序，
         * 再试一次短确认，仍不是则丢 1 字节重同步（避免把 32 80 08 拆碎）。
         */
        if (dataLen < EXTEND_DATA_LEN_OVERHEAD) {
            int conf2 = try_skip_link_confirm(pData, u32fifolen);
            if (conf2 == 0) {
                handled = 1;
                continue;
            }
            if (conf2 > 0) {
                return ERROR;
            }
            kfifo_get(pData, tempbuff, 1);
            continue;
        }

        frameLen = extend_frame_len(dataLen);
        if (frameLen == 0 || frameLen > MAX_EXTEND_FRAME_LEN) {
            kfifo_get(pData, tempbuff, 1);
            continue;
        }
        if (u32fifolen < frameLen) {
            /* 新长度声明，或 FIFO 仍在增长：刷新等待计时 */
            if (s_u16PartialNeed != frameLen || u32fifolen > s_u32PartialHave) {
                if (OtaUpgrade_IsBusy() &&
                    (s_u16PartialNeed != frameLen || s_u32PartialHave == 0)) {
                    /* 大包逐段进 FIFO 属正常，勿当 WARNING 刷屏 */
                    LOG(LOG_DEBUG,
                        "downlink waiting(ota) need=%u have=%u cmd=%u",
                        (unsigned)frameLen, (unsigned)u32fifolen,
                        (unsigned)tempbuff[sizeof(DOWNFrameHead) - 1U]);
                }
                s_u16PartialNeed = frameLen;
                s_u32PartialHave = u32fifolen;
                gettimeofday(&s_stPartialSince, NULL);
            } else if (cmd_ms_since(&s_stPartialSince) >= DOWNLINK_PARTIAL_STALL_MS) {
                /*
                 * 声明长度长期无增长：整段丢弃后重同步。
                 * OTA 亦同样处理——半包已无救，逐字节 skip 只会拖死会话。
                 */
                LOG(LOG_WARNING,
                    "downlink incomplete timeout%s need=%u have=%u, drop %u resync",
                    OtaUpgrade_IsBusy() ? "(ota)" : "",
                    (unsigned)frameLen, (unsigned)u32fifolen,
                    (unsigned)u32fifolen);
                discard_fifo_bytes(pData, u32fifolen);
                s_u16PartialNeed = 0;
                s_u32PartialHave = 0;
                handled = 1;
                continue;
            }
            return ERROR;
        }
        s_u16PartialNeed = 0;
        s_u32PartialHave = 0;

        kfifo_preview(pData, tempbuff, frameLen);
        if (tempbuff[frameLen - 3U] != ETX) {
            kfifo_get(pData, tempbuff, 1);
            continue;
        }
        if (downlink_crc_ok(tempbuff, frameLen, &crcRecv) != SUCCESS) {
            crcCalc = CRC16_Modbus(tempbuff, (uint16_t)(frameLen - 2));
            memcpy(&crcRecv, tempbuff + frameLen - 2, sizeof(crcRecv));
            LOG(LOG_WARNING,
                "downlink CRC fail%s calc=0x%04X recv=0x%04X/%04X len=%u func=0x%02X cmd=%u",
                OtaUpgrade_IsBusy() ? "(ota)" : "",
                crcCalc,
                crcRecv,
                swap_u16(crcRecv),
                (unsigned)frameLen,
                tempbuff[10],
                (unsigned)tempbuff[sizeof(DOWNFrameHead) - 1U]);
            /* 短帧或 OTA：打前 32 字节便于对照助手抓包 */
            if (frameLen <= 64U || OtaUpgrade_IsBusy()) {
                uint16_t dumpLen = (frameLen < 32U) ? frameLen : 32U;
                LOG_ARRAY(LOG_WARNING, tempbuff, dumpLen, "%02x");
            }
            /*
             * ETX 已在声明长度处对齐：整帧丢弃，避免坏 CRC 残留污染下一包。
             * OTA ELF 净荷含大量 7E7E，半包/错包仍只挪 1 字节重同步。
             */
            if (OtaUpgrade_IsBusy()) {
                kfifo_get(pData, tempbuff, 1);
            } else {
                discard_fifo_bytes(pData, frameLen);
            }
            continue;
        }

        kfifo_get(pData, tempbuff, frameLen);
        /* 只打拼好的整帧；OTA 大包打前 96 字节（够看 102 头+内容开头是否 ELF） */
        {
            uint16_t dumpLen = frameLen;
            if (stDownFrameHead.u8Signature == (uint8_t)SERVER_FIRMWARE_UPDATE && dumpLen > 96U) {
                dumpLen = 96U;
            } else if (dumpLen > 512U) {
                dumpLen = 512U;
            }
            LOG(LOG_DEBUG,
                "[DOWNLINK](%s) func=0x%02X cmd=%u seri=%u frameLen=%u%s",
                WebLink_IsSocketMode() ? "socket" : "dtu",
                stDownFrameHead.u8FunctionCode,
                (unsigned)stDownFrameHead.u8Signature,
                (unsigned)swap_u16(stDownFrameHead.u16FrameSeri),
                (unsigned)frameLen,
                (dumpLen < frameLen) ? " (hex truncated)" : "");
            LOG_ARRAY(LOG_DEBUG, tempbuff, dumpLen, "%02x");
        }
        UplinkOffline_OnDownlink();
        handled = 1;

        if (stDownFrameHead.u8FunctionCode == FUNCTION_CODE_SET_STATION_CLOCK) {
            set_system_time((BCD_TIME*)&stDownFrameHead.BCD_time);
            continue;
        }
        if (stDownFrameHead.u8FunctionCode == DOWN_EXPAND_CODE) {
            const uint8_t* pPayload = tempbuff + sizeof(DOWNFrameHead);
            uint16_t payloadLen = extend_extract_body_len(dataLen);
            uint8_t u8Command = stDownFrameHead.u8Signature;

            if (OtaUpgrade_HandleDownlink(u8Command, pPayload, payloadLen) == SUCCESS) {
                if (u8Command == SERVER_FIRMWARE_UPDATE && payloadLen >= 3U) {
                    uint16_t pkt = (uint16_t)(((uint16_t)pPayload[1] << 8) | pPayload[2]);
                    LOG(LOG_DEBUG,
                        "ota fw102 frame accepted pkt=%u body_len=%u frame_len=%u",
                        (unsigned)pkt, (unsigned)payloadLen, (unsigned)frameLen);
                } else if (!OtaUpgrade_IsBusy()) {
                    LOG(LOG_DEBUG, "ota downlink cmd=%u handled, body_len=%u",
                        u8Command, payloadLen);
                }
            } else if (u8Command == SERVER_START_TEST_CODE) {
                if (handle_start_test_code(pPayload, payloadLen) == SUCCESS) {
                    LOG(LOG_DEBUG, "downlink cmd=%u (START_TEST) handled", u8Command);
                }
            } else if (u8Command == SERVER_SHUIWEI_CELIU_CODE) {
                if (handle_shuiwei_celiu(pPayload, payloadLen) == SUCCESS) {
                    LOG(LOG_DEBUG, "downlink cmd=%u (SHUIWEI_CELIU) handled", u8Command);
                }
            } else if (u8Command == SERVER_GET_CONFIG_CODE) {
                if (handle_get_config(pPayload, payloadLen) == SUCCESS) {
                    LOG(LOG_DEBUG, "downlink cmd=%u (GET_CONFIG) handled", u8Command);
                }
            } else if (u8Command == SERVER_RETRIEVE_LOGS_CODE) {
                if (handle_retrieve_logs(pPayload, payloadLen) == SUCCESS) {
                    LOG(LOG_DEBUG, "downlink cmd=%u (RETRIEVE_LOGS) handled", u8Command);
                }
            } else if (u8Command == SERVER_DATA_RESPONSE_CODE) {
                RetrieveLogs_NotifyDataResponse(pPayload, payloadLen);
                ReportAck_OnDataResponse(pPayload, payloadLen);
            } else if (u8Command == SERVER_SETCONFIG_FILE_CODE) {
                if (handle_setconfig_file(pPayload, payloadLen) == SUCCESS) {
                    LOG(LOG_DEBUG, "downlink cmd=%u (SETCONFIG_FILE) handled", u8Command);
                }
            } else if (u8Command == SERVER_RESTART_CODE) {
                if (handle_restart_code(pPayload, payloadLen) == SUCCESS) {
                    LOG(LOG_DEBUG, "downlink cmd=%u (RESTART) handled", u8Command);
                }
            } else if (SectionParam_HandleDownlink(u8Command, pPayload, payloadLen) == SUCCESS) {
                LOG(LOG_DEBUG, "downlink cmd=%u handled, body_len=%u", u8Command, payloadLen);
            } else {
                LOG(LOG_WARNING, "downlink cmd=%u not handled, body_len=%u", u8Command, payloadLen);
            }
            continue;
        }
    }
}
