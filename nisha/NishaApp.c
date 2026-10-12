#include "NishaApp.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "Common.h"
#include "general_head.h"
#include "config.h"
#include "uart.h"
#include "timertask.h"
#include "log.h"
#include "task.h"
#include "watch_dog.h"
#include "OtaUpgrade.h"
#include "UplinkOffline.h"
#include "FirmwareVersion.h"
#include "SocketLink.h"
#include "nishatask.h"
#include "dc_motor.h"
#include "relay_module.h"

int Nisha_BusinessStart(void)
{
    log_init();
    set_log_level(LOG_INFO);
    LOG(LOG_INFO, "Nisha(GT6606) business start");
    srand((unsigned int)time(NULL));

    if (ReadConfig() != 0) {
        LOG(LOG_ERROR, "ReadConfig failed (check tyls.conf next to binary)");
        return -1;
    }
    init_nisha_default_keep_depth();

    if (InitUartDeviceFromConfig() != 0) {
        LOG(LOG_WARNING, "InitUartDeviceFromConfig returned error (partial UART OK on GT6606)");
    }

#ifdef BOARD_GT6606
    /* 板上串口稳定后再开业务；比原板 sleep(3) 略短 */
    sleep(1);
#else
    sleep(3);
#endif

    WatchDogControl();

    if (UplinkOffline_Init() != SUCCESS) {
        LOG(LOG_WARNING, "UplinkOffline_Init failed");
    }
    if (OtaUpgrade_Init() != SUCCESS) {
        LOG(LOG_WARNING, "OtaUpgrade_Init failed");
    }
    OtaUpgrade_ConfirmBoot();

    SocketLink_Start();
    if (WebLink_IsSocketMode()) {
        if (SocketLink_WaitConnected(5000)) {
            LOG(LOG_INFO, "SocketLink connected");
        } else {
            LOG(LOG_WARNING, "SocketLink not connected yet (will retry in background)");
        }
    }

    SetUartProtocol();
    InitRelayModule();
    StartRtcTimer();

    if (!StartTaskThread()) {
        LOG(LOG_ERROR, "StartTaskThread failed");
        return -1;
    }

    set_dc_motor_speed(600);
    NiShaUD_System_Self_Check();
    FirmwareVersion_ReportBoot(NULL);

    LOG(LOG_INFO, "Nisha(GT6606) business ready");
    return 0;
}
