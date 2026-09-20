#include "Tel15App.h"

#include <pthread.h>
#include <unistd.h>

#include "CRC.h"
#include "RS485_ICD.h"
#include "Timertask.h"
#include "WebSeriver.h"
#include "zigbee_dataproc.h"
#include "LinkList.h"
#include "Sensor.h"
#include "Relay.h"
#include "timer_manager.h"
#include "Encoder.h"
#include "log.h"
#include "SL651_2014.h"
#include "TaskQueue.h"
#include "ModbusQueue.h"
#include "AMPSTechMotor.h"
#include "OtaUpgrade.h"
#include "UplinkOffline.h"
#include "SocketLink.h"

#define SEND_INTERVEL_WORK   2000
#define SEND_INTERVEL_STANDBY (1000 * 120)

int g_EncoderTimer = 0;

static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t s_last_active_time;
static int s_exit_flag = 0;

time_t GetLastActiveTime(void)
{
    time_t t;
    pthread_mutex_lock(&s_mutex);
    t = s_last_active_time;
    pthread_mutex_unlock(&s_mutex);
    return t;
}

void updateActiveTime()
{
    pthread_mutex_lock(&s_mutex);
    if (s_exit_flag) {
        Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM0_ADDR);
        mSleep(500);
        Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM1_ADDR);
        mSleep(500);
    }
    s_exit_flag = 0;
    s_last_active_time = time(NULL);
    pthread_mutex_unlock(&s_mutex);
}

void CheckOperateState(void *arg)
{
    (void)arg;
    if (!s_exit_flag) {
        time_t now;
        time_t elapsed;

        pthread_mutex_lock(&s_mutex);
        now = time(NULL);
        elapsed = now - s_last_active_time;
        pthread_mutex_unlock(&s_mutex);

        if (elapsed >= (time_t)g_timeout_Intervel) {
            LOG(LOG_DEBUG, "into standby time is %ld", (long)now);
            s_exit_flag = 1;
            MotorDrivePowerOFF();
        }
    }
}

static void OtaKickTimer(void *arg)
{
    int kickRet;
    (void)arg;
    if (!OtaUpgrade_IsBusy()) {
        return;
    }
    kickRet = OtaUpgrade_KickIfStalled(4000U);
    if (kickRet < 0) {
        LOG(LOG_WARNING, "ota: no packet over 1 min, abort upgrade");
    }
}

static void *auto_measure_thread(void *arg)
{
    (void)arg;
    while (1) {
        int i = 0;
        if (g_AutoMeasureFlag) {
            ReadSectionPara_new(&g_stSection_new);
            ReadCablewayPara(&g_stSetCableway);
            ReadAutoMeasurePara(&g_stAutoMeasure);
            g_ChoosedMode = GetMaeasureMode(g_stAutoMeasure);
            for (i = 0; i < MAX_LINE_NUM; i++) {
                g_ChoosedMode.stLine[i].u32MeaElement = ROTATOR;
            }
            g_AutoMeasureFlag = 0;
        }
        sleep(1);
    }
    return NULL;
}

int Tel15_BusinessStart(void)
{
    uint16_t basedelay = 1000;
    pthread_t th;

    log_init();
    set_log_level(LOG_DEBUG);
    if (InitDevice() != SUCCESS) {
        LOG(LOG_ERROR, "InitDevice failed (check tyls.conf next to binary)");
        return ERROR;
    }
    InitMeasureConfig();
    timer_init();
    TaskQueue_Init(&g_taskQueue);
    ModbusQueue_Init();
    InitWatchDog();
    if (OtaUpgrade_Init() != SUCCESS) {
        LOG(LOG_ERROR, "OtaUpgrade_Init failed");
    }
    OtaUpgrade_ConfirmBoot();
    if (UplinkOffline_Init() != SUCCESS) {
        LOG(LOG_ERROR, "UplinkOffline_Init failed");
    }

    SocketLink_Start();
    if (WebLink_IsSocketMode()) {
        if (SocketLink_WaitConnected(5000U)) {
            LOG(LOG_DEBUG, "SocketLink: ready before business uplink");
        } else {
            LOG(LOG_WARNING, "SocketLink: not connected in 5s, will keep retrying");
        }
    }

    if (g_WatchDogFd != 0) {
        add_timer(1000, 1000, TIMER_REPEAT, 0, FeedWatchDog, NULL);
        sleep(1);
    }

    pthread_mutex_lock(&s_mutex);
    s_exit_flag = 0;
    s_last_active_time = time(NULL);
    pthread_mutex_unlock(&s_mutex);

    LOG(LOG_DEBUG, "TEL-15(GT6606) version is %s create date 2026-06-10", "0.6");

    g_stM2UpFramepara.center_addr = g_CenterStationID;
    memcpy(g_stM2UpFramepara.tele_addr, g_aStationID, 5);
    g_stM2UpFramepara.fd = g_stDevicelist.ZigBee;
    g_stM2UpFramepara.password = 0x1234;

    Relay_MotorBreak(RELAY_COM0_ADDR);
    mSleep(500);
    Relay_MotorBreak(RELAY_COM1_ADDR);
    mSleep(500);

    Relay_MotorDrivePowerOFF(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM0_ADDR);
    mSleep(500);
    Relay_MotorDrivePowerOFF(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM1_ADDR);
    mSleep(500);

    Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM0_ADDR);
    mSleep(500);
    Relay_MotorDrivePowerON(g_stDeviceAddr.u8Relay_2_port_power, RELAY_COM1_ADDR);
    mSleep(500);

    Relay_LightCtrl(GREEN_LIGHT, TURN_ON);
    ReadWaterLevel(NULL);

    add_timer(basedelay, 0, TIMER_ONCE, 0, sendSpeedAndSectionSeir, (void *)DTU_TYPE);
    add_timer(basedelay + 100, 1000, TIMER_REPEAT, 0, TimerTask, NULL);
    add_timer(basedelay + 100, 10000, TIMER_REPEAT, 0, ReadBatteryState, NULL);
    add_timer(basedelay + 100, 10000, TIMER_REPEAT, 0, CheckOperateState, NULL);
    add_timer(basedelay + 100, 1000, TIMER_REPEAT, 0, SendStatusFrame, (void *)DTU_TYPE);
    add_timer(basedelay + 100, 1000, TIMER_REPEAT, 0, OtaKickTimer, NULL);
    add_timer(basedelay + 100, 1000, TIMER_REPEAT, 0, UplinkOffline_FlushTick, NULL);
    g_EncoderTimer = add_timer(basedelay + 100, 16, TIMER_REPEAT, 0, ReadEncoderDistencs, NULL);
    add_timer(basedelay + 100, 150, TIMER_REPEAT, 0, ReadRotatorValuePeriodic, NULL);
    add_timer(basedelay + 100, 1000, TIMER_REPEAT, 0, ReadWaterLevelper5min, NULL);
    add_timer(basedelay + 100, 1000, TIMER_REPEAT, 0, CheckFixtimeAndHourlyFrame, &g_stM2UpFramepara);
    add_timer(basedelay + 100, 10 * 60 * 1000, TIMER_REPEAT, 0, ErrorStateProc, NULL);
    add_timer(basedelay + 100, 100, TIMER_REPEAT, 0, ReadDIStatusTimerCallback, NULL);
#if ENABLE_ELECTRIC_DOOR
    StartDoorStatusThread();
#endif

    if (pthread_create(&th, NULL, auto_measure_thread, NULL) == 0) {
        pthread_detach(th);
    } else {
        LOG(LOG_ERROR, "auto_measure_thread create failed");
    }

    LOG(LOG_DEBUG, "TEL-15(GT6606) business ready, webLink=%s",
        WebLink_IsSocketMode() ? "socket" : "dtu");
    return SUCCESS;
}
