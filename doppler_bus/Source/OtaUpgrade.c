/**
 * Doppler 产品适配层：把工程内的串口/日志/定时器接到 libota（网页 102）。
 * 打包校验 + 启动回滚在此开启（网页仍只传文件，无需改协议）。
 */
#include "OtaUpgrade.h"
#include "ota.h"
#include "ota_linux.h"
#include "ota_pkg.h"
#include "FirmwareVersion.h"
#include "Command_proc.h"
#include "SocketLink.h"
#include "WebSeriver.h"
#include "log.h"
#include "timer_manager.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OTA_RCS_PATH "/etc/init.d/rcS"
#define OTA_RC_LOCAL_PATH "/etc/rc.local"
#ifdef BOARD_GT6606
#define OTA_QT_DIR "/usr/local/htnice/Qt-5.8.0"
#define OTA_BIN_NAME "doppler_gt6606"
#else
#define OTA_QT_DIR NULL
#define OTA_BIN_NAME "DOPPLER"
#endif

/* 供 ota_pack 脚本 strings 检查；勿改字符串格式 */
const char g_doppler_ota_product_tag[] = "DOPPLER_OTA_ID=DPLR";

static void port_reboot_cb(void *arg)
{
    (void)arg;
    LOG(LOG_DEBUG, "ota reboot now");
    sync();
    (void)system("reboot");
}

static void port_vlog(void *user, int level, const char *fmt, va_list ap)
{
    char msg[512];
    LogLevel lv = LOG_DEBUG;

    (void)user;
    vsnprintf(msg, sizeof(msg), fmt, ap);
    if (level >= OTA_LOG_ERROR) {
        lv = LOG_ERROR;
    } else if (level >= OTA_LOG_WARN) {
        lv = LOG_WARNING;
    } else if (level >= OTA_LOG_INFO) {
        /* 工程 log_filter=DEBUG 时会丢掉 LOG_INFO，OTA 进度用 DEBUG 才能看见 */
        lv = LOG_DEBUG;
    }
    LOG(lv, "%s", msg);
}

static void port_send(void *user, uint8_t report_cmd, const void *body, uint16_t len)
{
    (void)user;
    if (body == NULL || len == 0) {
        return;
    }
    SendUpLinkToServer(UP_EXPAND_CODE, report_cmd, (uint8_t *)body, len, DTU_TYPE);
}

static void port_on_busy(void *user, int busy)
{
    (void)user;
    if (busy) {
        log_set_flash_quiet(1);
        (void)system("killall DC_MOTOR.0.6 >/dev/null 2>&1");
        (void)system("killall DC_MOTOR >/dev/null 2>&1");
        LOG(LOG_DEBUG, "ota quiesce: stopped DC_MOTOR, muted log.lg write");
    } else {
        log_set_flash_quiet(0);
    }
}

static void port_schedule_reboot(void *user, uint32_t delay_ms)
{
    (void)user;
    if (delay_ms == 0) {
        delay_ms = 5000U;
    }
    add_timer((uint64_t)delay_ms, 0, TIMER_ONCE, 0, port_reboot_cb, NULL);
}

static int port_activate_bin(void *user, const char *bin_path, uint8_t slot)
{
    static const char *const drops[] = {
        "DOPPLER",
        "doppler_gt6606",
        "run_doppler",
        "ota/slot_",
        "ota_boot.sh",
        "time-difference",
        NULL
    };
    char app_dir[MAX_PATH];

    (void)user;
    (void)bin_path;
    (void)slot;

    memset(app_dir, 0, sizeof(app_dir));
    if (!GetAppDir(app_dir)) {
        LOG(LOG_ERROR, "ota GetAppDir failed in activate");
        return OTA_FAIL;
    }
    /*
     * Ubuntu rcS 末尾是 exec，其后追加无效。
     * GT6606：挂 /etc/rc.local + ota_boot.sh 内带 Qt 环境；并清理 rcS 死行。
     */
#ifdef BOARD_GT6606
    if (ota_linux_install_boot_guard_ex(OTA_RCS_PATH, OTA_RC_LOCAL_PATH, app_dir, "/ota",
                                        OTA_BIN_NAME, OTA_QT_DIR, drops) != 0) {
        LOG(LOG_ERROR, "ota install boot guard / rc.local failed");
        return OTA_FAIL;
    }
    LOG(LOG_DEBUG, "ota boot guard installed, %s -> ota_boot.sh qt=%s (tag=%s)",
        OTA_RC_LOCAL_PATH, OTA_QT_DIR, g_doppler_ota_product_tag);
#else
    if (ota_linux_install_boot_guard(OTA_RCS_PATH, app_dir, "/ota", OTA_BIN_NAME, drops) != 0) {
        LOG(LOG_ERROR, "ota install boot guard / rewrite rcS failed");
        return OTA_FAIL;
    }
    LOG(LOG_DEBUG, "ota boot guard installed, %s -> ota_boot.sh (tag=%s)",
        OTA_RCS_PATH, g_doppler_ota_product_tag);
#endif
    return OTA_OK;
}

int OtaUpgrade_Init(void)
{
    ota_config_t cfg;
    ota_ops_t ops;
    char app_dir[MAX_PATH];
    static char s_fwVer[16];

    ota_config_init_defaults(&cfg);
    memset(app_dir, 0, sizeof(app_dir));
    if (!GetAppDir(app_dir)) {
        LOG(LOG_ERROR, "ota GetAppDir failed");
        return ERROR;
    }
    strncpy(cfg.root_dir, app_dir, sizeof(cfg.root_dir) - 1);
    cfg.bin_name = OTA_BIN_NAME;
    cfg.slot_size_max = MAX_SOFTWARE_SIZE;
    cfg.require_elf = 1;
    cfg.require_pkg = 1;
    cfg.product_id = OTA_PKG_PRODUCT_DPLR;
    /*
     * DTU：先写盘再 ACK，避免网页立刻再塞包把串口 RX FIFO 打爆。
     * Socket/4G：先 ACK 再写盘——蜂窝 RTT 大，若等 Flash 写完再 ACK，中心常直接停传
     *（表现：网线正常、4G 卡在第 1～十余包后 1 分钟 abort）。应用 FIFO 已加大可吸后续包。
     */
    cfg.fw102_ack_then_write = WebLink_IsSocketMode() ? 1 : 0;
    LOG(LOG_DEBUG, "ota ack_then_write=%d (%s)",
        cfg.fw102_ack_then_write,
        WebLink_IsSocketMode() ? "socket/4G" : "dtu");
    FirmwareVersion_Format(s_fwVer, sizeof(s_fwVer));
    cfg.fw_version = s_fwVer;
    cfg.cmd_fw102 = SERVER_FIRMWARE_UPDATE;
    cfg.rpt_result = REPORT_OTA_RESULT;
    cfg.rpt_reply = REPORT_REPLY_MSG;

    memset(&ops, 0, sizeof(ops));
    ops.vlog = port_vlog;
    ops.send = port_send;
    ops.on_busy = port_on_busy;
    ops.schedule_reboot = port_schedule_reboot;
    ops.activate_bin = port_activate_bin;

    if (ota_init(&cfg, &ops) != OTA_OK) {
        return ERROR;
    }
    return SUCCESS;
}

void OtaUpgrade_ConfirmBoot(void)
{
    char ver[16];

    ota_confirm_boot();
    FirmwareVersion_Format(ver, sizeof(ver));
    if (ota_write_active_version(ver) == OTA_OK) {
        LOG(LOG_DEBUG, "ota boot confirmed, slot version=%s", ver);
    } else {
        LOG(LOG_WARNING, "ota boot confirmed, but write slot version failed");
    }
}

int OtaUpgrade_HandleDownlink(uint8_t u8Command, const uint8_t *pBody, uint16_t u16BodyLen)
{
    if (ota_handle_downlink(u8Command, pBody, u16BodyLen) == OTA_OK) {
        return SUCCESS;
    }
    return ERROR;
}

int OtaUpgrade_IsBusy(void)
{
    return ota_is_busy();
}

int OtaUpgrade_GetProgress(uint16_t *pkt_expect, uint16_t *pkt_total, uint32_t *recv_size)
{
    return ota_get_progress(pkt_expect, pkt_total, recv_size);
}

int OtaUpgrade_KickIfStalled(uint32_t stall_ms)
{
    uint32_t abort_ms = WebLink_IsSocketMode() ? 180000U : 60000U;
    uint32_t use_stall = stall_ms;

    /* Socket/4G：更勤重发 lastOk，促中心续传 */
    if (use_stall == 0U) {
        use_stall = WebLink_IsSocketMode() ? 2000U : 4000U;
    }
    return ota_kick_if_stalled(use_stall, abort_ms);
}

void OtaUpgrade_QueryUiStatus(ota_ui_status_t *out)
{
    ota_query_ui_status(out);
}
