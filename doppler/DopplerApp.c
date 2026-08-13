#include "DopplerApp.h"

#include "Common.h"
#include "CRC.h"
#include "log.h"
#include "timer_manager.h"
#include "ModbusQueue.h"
#include "Sensor.h"
#include "Dam5213.h"
#include "Timertask.h"
#include "SL651_2014.h"
#include "Command_proc.h"
#include "SectionParam.h"
#include "OtaUpgrade.h"
#include "ReportAck.h"
#include "UplinkOffline.h"
#include "FirmwareVersion.h"
#include "SocketLink.h"

int Doppler_BusinessStart(void)
{
    uint32_t collectMs = 10000;
    uint16_t basedelay = 1000;

    log_init();
    set_log_level(LOG_DEBUG);

    if (InitDevice() != SUCCESS) {
        LOG(LOG_ERROR, "InitDevice failed (check doppler.conf next to binary)");
        return ERROR;
    }
    InitWatchDog();
    timer_init();
    ModbusQueue_Init();
    Sensor_Init();
    Dam5213_Init();
    ReportAck_Init();
    OtaUpgrade_Init();
    OtaUpgrade_ConfirmBoot();
    if (UplinkOffline_Init() != SUCCESS) {
        LOG(LOG_ERROR, "UplinkOffline_Init failed");
    }

    g_stM2UpFramepara.center_addr = g_CenterStationID;
    memcpy(g_stM2UpFramepara.tele_addr, g_RTUID, sizeof(g_RTUID));
    g_stM2UpFramepara.password = g_u16Password;
    if (g_stDevicelist.HOST_COMPUTER >= 0) {
        g_stM2UpFramepara.fd = g_stDevicelist.UartList[g_stDevicelist.HOST_COMPUTER].i32fd;
    }

    collectMs = g_stMeasureConfig.u32CollectIntervalSec * 1000U;
    if (collectMs < 1000U) {
        collectMs = 1000U;
    }

    {
        uint8_t ver[4];
        uint32_t v = 0;
        FirmwareVersion_Get(ver);
        memcpy(&v, ver, sizeof(v));
        LOG(LOG_DEBUG,
            "Doppler(GT6606) fw=%u, sensor COM%d, dtu COM%d, dam5213 COM%d, simulate=%u, webLink=%s",
            (unsigned)v,
            g_stDevicelist.Sensor + 1,
            g_stDevicelist.HOST_COMPUTER + 1,
            g_stDevicelist.Dam5213 + 1,
            (unsigned)g_stMeasureConfig.u8SensorSimulate,
            WebLink_IsSocketMode() ? "socket" : "dtu");
    }

    SocketLink_Start();
    if (WebLink_IsSocketMode()) {
        if (SocketLink_WaitConnected(5000U)) {
            LOG(LOG_DEBUG, "SocketLink: ready before business uplink");
        } else {
            LOG(LOG_WARNING, "SocketLink: not connected in 5s, will keep retrying");
        }
    }

    SectionParam_Init();
    add_timer(basedelay + 800, 0, TIMER_ONCE, 0, FirmwareVersion_ReportBoot, NULL);
    if (g_WatchDogFd != 0) {
        add_timer(1000, 1000, TIMER_REPEAT, 0, FeedWatchDog, NULL);
    }
    add_timer(basedelay, collectMs, TIMER_REPEAT, 0, ReadFlowMeterData, NULL);
    if (g_stDevicelist.Dam5213 >= 0 && g_stDeviceAddr.u8Dam5213 > 0) {
        add_timer(basedelay + 500, collectMs, TIMER_REPEAT, 0, Dam5213_Poll, NULL);
    }
    add_timer(basedelay, 1000, TIMER_REPEAT, 0, TickPrintDateTime, NULL);
    add_timer(basedelay, 1000, TIMER_REPEAT, 0, Sensor_CheckTimeout, NULL);
    add_timer(basedelay + 300, 1000, TIMER_REPEAT, 0, ReportAck_CheckRetry, NULL);
    add_timer(basedelay + 100, 1000, TIMER_REPEAT, 0, UplinkOffline_FlushTick, NULL);

    if (g_stMeasureConfig.u8WebReportEnable) {
        LOG(LOG_DEBUG, "web report enabled, link=%s, check every 1s",
            WebLink_IsSocketMode() ? "socket" : "dtu");
        add_timer(basedelay + 200, 1000, TIMER_REPEAT, 0, CheckWebReportAtFiveMinute, NULL);
    }
    if (g_stMeasureConfig.u8Sl651Enable &&
        (WebLink_IsSocketMode() || g_stM2UpFramepara.fd > 0)) {
        add_timer(basedelay + 2000, 0, TIMER_ONCE, 0, build_test_report, &g_stM2UpFramepara);
        add_timer(basedelay + 1000, 1000, TIMER_REPEAT, 0, CheckFixtimeAndHourlyFrame,
                  &g_stM2UpFramepara);
    }

    return SUCCESS;
}
