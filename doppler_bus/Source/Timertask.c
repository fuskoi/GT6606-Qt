#include "Timertask.h"
#include "Sensor.h"
#include "Command_proc.h"
#include "ReportAck.h"
#include "WebSeriver.h"
#include "OtaUpgrade.h"
#include "SocketLink.h"
#include "UplinkOffline.h"
#include "log.h"
#include <stdio.h>
#include <time.h>

static int s_i32LastWebReportSlot = -1;

static int calc_five_minute_slot(const struct tm* pTm)
{
    return (pTm->tm_year * 366 + pTm->tm_yday) * 288 + pTm->tm_hour * 12 + pTm->tm_min / 5;
}

void CheckWebReportAtFiveMinute(void* arg)
{
    time_t now = 0;
    struct tm tmNow;
    int i32Slot = 0;

    (void)arg;
    if (!g_stMeasureConfig.u8WebReportEnable) {
        return;
    }

    now = time(NULL);
    localtime_r(&now, &tmNow);
    if ((tmNow.tm_min % 5) != 0) {
        return;
    }

    i32Slot = calc_five_minute_slot(&tmNow);
    if (i32Slot == s_i32LastWebReportSlot) {
        return;
    }
    s_i32LastWebReportSlot = i32Slot;

    /* 上一次 121 尚未确认：由 ReportAck 5 分钟重发，不另开新报 */
    if (ReportAck_HasPending((uint8_t)REPORT_FLOW_JET_LAG_CMD)) {
        LOG(LOG_DEBUG, "web report skip new: FLOW_JET_LAG still pending ack");
        return;
    }

    ReportFlowMeterAtFiveMinute(NULL);
}

void ReportFlowMeterAtFiveMinute(void* arg)
{
    time_t now = 0;
    struct tm tmNow;
    int linkReady = 1;

    (void)arg;
    if (!g_stMeasureConfig.u8WebReportEnable) {
        return;
    }
    if (OtaUpgrade_IsBusy()) {
        return;
    }
    if (WebLink_IsSocketMode()) {
        if (!SocketLink_IsConnected()) {
            linkReady = 0;
        }
    } else if (g_stDevicelist.HOST_COMPUTER < 0 ||
               g_stDevicelist.UartList[g_stDevicelist.HOST_COMPUTER].i32fd <= 0) {
        linkReady = 0;
    }

    /* 链路未就绪：离线仍组包落盘；在线则跳过（由 ReportAck 等机制处理） */
    if (!linkReady) {
        if (UplinkOffline_IsOnline()) {
            LOG(LOG_WARNING, "web report skipped: link not ready");
            return;
        }
        LOG(LOG_WARNING, "web report offline: snapshot will persist to disk");
    }

    now = time(NULL);
    localtime_r(&now, &tmNow);
    LOG(LOG_DEBUG, "web report at aligned time %04d-%02d-%02d %02d:%02d:%02d",
        tmNow.tm_year + 1900,
        tmNow.tm_mon + 1,
        tmNow.tm_mday,
        tmNow.tm_hour,
        tmNow.tm_min,
        tmNow.tm_sec);
    ReportFlowMeterData(NULL);
}

void ReportFlowMeterData(void* arg)
{
    char frame[768];
    TrafficReportSnapshot stSnapshot;
    uint8_t i = 0;
    int offset = 0;

    (void)arg;
    if (Sensor_GetTrafficReportSnapshot(&stSnapshot) != SUCCESS) {
        LOG(LOG_WARNING, "no valid flow meter data to report");
        return;
    }

    offset = snprintf(frame, sizeof(frame),
                      "{\"station\":\"%s\",\"avg_water\":%d,\"mode\":%u,\"line_count\":%u",
                      g_aStationID,
                      (int)stSnapshot.i32AvgWaterScaled,
                      (unsigned)stSnapshot.u8WaterMode,
                      (unsigned)stSnapshot.u8LineCount);

    for (i = 0; i < SENSOR_LINE_MAX && offset > 0 && offset < (int)sizeof(frame); i++) {
        const SensorTrafficLine* pLine = &stSnapshot.astLines[i];
        if (!pLine->u8Valid) {
            continue;
        }
        offset += snprintf(frame + offset, sizeof(frame) - (size_t)offset,
                           ",\"line%u\":{\"level\":%.4f,\"velocity\":%.4f,"
                           "\"sec_flow\":%.4f,\"hour_flow\":%.4f,"
                           "\"time\":\"%04d-%02d-%02d %02d:%02d:%02d\"}",
                           (unsigned)(i + 1),
                           pLine->stRaw.fWaterLevel,
                           pLine->stRaw.fFlowVelocity,
                           pLine->stRaw.fSecFlow,
                           pLine->stRaw.fHourFlow,
                           pLine->stCaptureTime.tm_year + 1900,
                           pLine->stCaptureTime.tm_mon + 1,
                           pLine->stCaptureTime.tm_mday,
                           pLine->stCaptureTime.tm_hour,
                           pLine->stCaptureTime.tm_min,
                           pLine->stCaptureTime.tm_sec);
    }

    if (offset > 0 && offset < (int)sizeof(frame)) {
        snprintf(frame + offset, sizeof(frame) - (size_t)offset, "}");
    }

    LOG(LOG_DEBUG, "report: %s", frame);
    Sensor_LogLocalDischargeFromSnapshot(&stSnapshot);
    SendDopplerRealData(NULL);
}

void TickPrintDateTime(void* arg)
{
    time_t now = 0;
    struct tm tmNow;
    static int s_i32OtaHbSec = 0;

    (void)arg;
    if (OtaUpgrade_IsBusy()) {
        uint16_t u16Expect = 0;
        uint16_t u16Total = 0;
        uint32_t u32Recv = 0;
        int kickRet = 0;

        /* 约 4s 无新包：重发 lastOk；约 60s 仍无进度：放弃并 WARNING */
        kickRet = OtaUpgrade_KickIfStalled(4000U);
        if (kickRet < 0) {
            s_i32OtaHbSec = 0;
            LOG(LOG_WARNING,
                "ota: no packet over 1 min, abort upgrade and resume normal work");
            return;
        }
        if (kickRet > 0) {
            s_i32OtaHbSec = 0;
        }
        /* OTA 时定时器线程仍活：每 10s 打进度；fifo=0 表示 MCU 根本没收到下一包 */
        if (++s_i32OtaHbSec >= 10) {
            uint32_t u32Fifo = 0;
            int hostPort = g_stDevicelist.HOST_COMPUTER;

            s_i32OtaHbSec = 0;
            (void)OtaUpgrade_GetProgress(&u16Expect, &u16Total, &u32Recv);
            if (hostPort >= 0 && hostPort < MAX_UART_NUM &&
                g_stDevicelist.UartList[hostPort].ringfifo != NULL) {
                u32Fifo = kfifo_len(g_stDevicelist.UartList[hostPort].ringfifo);
            }
            LOG(LOG_DEBUG, "ota heartbeat expect_pkt=%u/%u recv=%u fifo=%u",
                (unsigned)u16Expect, (unsigned)u16Total, (unsigned)u32Recv,
                (unsigned)u32Fifo);
        }
        return;
    }
    s_i32OtaHbSec = 0;
    now = time(NULL);
    localtime_r(&now, &tmNow);
    /* 真空期心跳：只打日期时间，不要日志前缀 */
    printf("%04d-%02d-%02d %02d:%02d:%02d\n",
           tmNow.tm_year + 1900,
           tmNow.tm_mon + 1,
           tmNow.tm_mday,
           tmNow.tm_hour,
           tmNow.tm_min,
           tmNow.tm_sec);
    fflush(stdout);
}
