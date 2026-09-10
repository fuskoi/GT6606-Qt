/**
 * OTA 产品适配 + F1/F2 帧收发（协议同 Doppler libota / cmd 102）
 * 打包校验 + 启动回滚在此开启（网页仍只传文件，无需改协议）。
 */
#include "OtaUpgrade.h"
#include "ota.h"
#include "ota_linux.h"
#include "ota_pkg.h"
#include "FirmwareVersion.h"
#include "CRC.h"
#include "UplinkOffline.h"
#include "SL651_2014.h"
#include "SocketLink.h"
#include "uart.h"
#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OTA_RCS_PATH "/etc/init.d/rcS"
#define OTA_RC_LOCAL_PATH "/etc/rc.local"
#define OTA_BIN_NAME "nisha_gt6606"

/* 供 ota_pack 脚本 strings 检查；勿改字符串格式 */
const char g_nisha_ota_product_tag[] = "NISHA_OTA_ID=NSHJ";
#define EXTEND_DATA_LEN_OVERHEAD  (sizeof(uint16_t) + sizeof(OTA_BCD_TIME) + sizeof(uint8_t))
#define MAX_EXTEND_FRAME_LEN      4200U
#define LIFT_ELEMENT_ID           0x1EU

void NiShaUD_Stop_Running_Task(void);
void get_bcd_time(uint8_t *buffer);

extern BYTE g_CenterStationID;
extern BYTE g_RTUID[5];
extern uint32_t g_u32CommansSeri;

static int g_otaReady = 0;

/* ---------------- DTU 收发（OTA 帧） ---------------- */

int OtaUpgrade_DtuWrite(const uint8_t *data, int len)
{
    int n;

    if (data == NULL || len <= 0) {
        return -1;
    }
    if (!WebLink_IsReady()) {
        return -1;
    }
    log_dtu_frame("UP", WebLink_IsSocketMode() ? "ota/uol-socket" : "ota/uol", data, len);
    if (WebLink_IsSocketMode()) {
        return WebLink_Send(data, len);
    }
    pthread_mutex_lock(&g_lock[1]);
    n = WebLink_Send(data, len);
    pthread_mutex_unlock(&g_lock[1]);
    return n;
}

uint16_t OtaUpgrade_SendReport(uint8_t report_cmd, uint8_t *pData, uint16_t datalen)
{
    uint8_t *buffer = NULL;
    OTA_UPFrameHead stUpframehead;
    uint8_t u8EOF = ETX;
    uint16_t u16CRC = 0;
    uint16_t u16Seri = 0;
    int bufferDataLen = 0;
    int sent = -1;

    if (pData == NULL || datalen == 0) {
        return 0xFFFFU;
    }
    if (!WebLink_IsReady()) {
        return 0xFFFFU;
    }

    buffer = (uint8_t *)malloc((size_t)datalen + sizeof(OTA_UPFrameHead) + sizeof(OTA_FrameTail));
    if (buffer == NULL) {
        return 0xFFFFU;
    }

    memset(&stUpframehead, 0, sizeof(stUpframehead));
    stUpframehead.u16SOH = 0x7E7E;
    stUpframehead.u8CentreStaID = g_CenterStationID;
    memcpy(stUpframehead.aRTUID, g_RTUID, sizeof(g_RTUID));
    stUpframehead.u16Password = swap_u16(g_stM2UpFramepara.password);
    stUpframehead.u8FunctionCode = UP_EXPAND_CODE;
    stUpframehead.u8Signature = report_cmd;
    get_bcd_time((uint8_t *)&stUpframehead.BCD_time);
    stUpframehead.u8DataStart = STX;
    u16Seri = (uint16_t)g_u32CommansSeri++;
    stUpframehead.u16FrameSeri = swap_u16(u16Seri);
    {
        uint16_t lenField = (uint16_t)((datalen + EXTEND_DATA_LEN_OVERHEAD) & 0x0FFF);
        stUpframehead.u16FlagAndLen = swap_u16(lenField);
    }

    memcpy(buffer, &stUpframehead, sizeof(OTA_UPFrameHead));
    memcpy(buffer + sizeof(OTA_UPFrameHead), pData, datalen);
    memcpy(buffer + sizeof(OTA_UPFrameHead) + datalen, &u8EOF, 1);
    bufferDataLen = (int)(sizeof(OTA_UPFrameHead) + datalen + 1);
    u16CRC = CRC16_Modbus(buffer, (uint16_t)bufferDataLen);
    memcpy(buffer + bufferDataLen, &u16CRC, sizeof(u16CRC));
    bufferDataLen += (int)sizeof(u16CRC);

    sent = OtaUpgrade_DtuWrite(buffer, bufferDataLen);
    if (!OtaUpgrade_IsBusy()) {
        LOG(LOG_DEBUG, "ota uplink cmd=%u len=%d", (unsigned)report_cmd, bufferDataLen);
    }
    free(buffer);

    if (sent != bufferDataLen) {
        return 0xFFFFU;
    }
    return u16Seri;
}

static uint16_t extend_extract_body_len(uint16_t dataLen)
{
    if (dataLen <= EXTEND_DATA_LEN_OVERHEAD) {
        return 0;
    }
    return (uint16_t)(dataLen - EXTEND_DATA_LEN_OVERHEAD);
}

static uint16_t extend_frame_len(uint16_t dataLen)
{
    uint32_t total = (uint32_t)sizeof(OTA_DOWNFrameHead) +
                     (uint32_t)extend_extract_body_len(dataLen) +
                     (uint32_t)sizeof(OTA_FrameTail);
    if (total > MAX_EXTEND_FRAME_LEN) {
        return 0;
    }
    return (uint16_t)total;
}

static int downlink_crc_ok(const uint8_t *frame, uint16_t frameLen)
{
    uint16_t crcCalc;
    uint16_t crcRaw;

    if (frame == NULL || frameLen < 3U) {
        return ERROR;
    }
    crcCalc = CRC16_Modbus((uint8_t *)frame, (uint16_t)(frameLen - 2));
    memcpy(&crcRaw, frame + frameLen - 2, sizeof(crcRaw));
    if (crcCalc == crcRaw || crcCalc == swap_u16(crcRaw)) {
        return SUCCESS;
    }
    return ERROR;
}

static int find_head(const uint8_t *buf, uint16_t len, uint16_t *outIdx)
{
    uint16_t i;
    for (i = 0; i + 1 < len; i++) {
        if (buf[i] == HEAD1 && buf[i + 1] == HEAD2) {
            *outIdx = i;
            return 1;
        }
    }
    return 0;
}

static void handle_ota_f1(const uint8_t *frame, uint16_t frameLen,
                          const OTA_DOWNFrameHead *head, uint16_t dataLen)
{
    const uint8_t *pPayload = frame + sizeof(OTA_DOWNFrameHead);
    uint16_t payloadLen = extend_extract_body_len(dataLen);
    uint8_t u8Command = head->u8Signature;

    (void)frameLen;
    UplinkOffline_OnDownlink();
    log_dtu_frame("DOWN", "ota-f1", frame, (int)frameLen);

    if (OtaUpgrade_HandleDownlink(u8Command, pPayload, payloadLen) == SUCCESS) {
        if (u8Command == (uint8_t)SERVER_FIRMWARE_UPDATE && payloadLen >= 3U) {
            uint16_t pkt = (uint16_t)(((uint16_t)pPayload[1] << 8) | pPayload[2]);
            LOG(LOG_DEBUG, "ota fw102 frame accepted pkt=%u body_len=%u",
                (unsigned)pkt, (unsigned)payloadLen);
        }
        return;
    }

    LOG(LOG_DEBUG, "ota downlink cmd=%u not handled, body_len=%u",
        (unsigned)u8Command, (unsigned)payloadLen);
}

int OtaUpgrade_RecDataProc(uint8_t *recvBuffer, uint16_t recvLen, WORD *wUsed)
{
    uint16_t headIdx = 0;
    uint16_t dataLen;
    uint16_t frameLen;
    OTA_DOWNFrameHead stHead;
    uint8_t func;

    *wUsed = 0;
    /* 未 Init 时勿解析，交给原 SL651 */
    if (!g_otaReady) {
        return 1;
    }
    if (recvBuffer == NULL || recvLen < sizeof(OTA_DOWNFrameHead)) {
        return 1;
    }

    if (!find_head(recvBuffer, recvLen, &headIdx)) {
        *wUsed = recvLen;
        return 0;
    }
    if (headIdx > 0) {
        *wUsed = headIdx;
        return 0;
    }

    if (recvBuffer[13] != STX) {
        *wUsed = 1;
        return 0;
    }

    func = recvBuffer[10];

    /* F2：本机 OTA ACK 回显，按长度跳过 */
    if (func == UP_EXPAND_CODE) {
        dataLen = (uint16_t)(((recvBuffer[11] & 0x0F) << 8) | recvBuffer[12]);
        if (dataLen < EXTEND_DATA_LEN_OVERHEAD) {
            *wUsed = 1;
            return 0;
        }
        frameLen = extend_frame_len(dataLen);
        if (frameLen == 0) {
            *wUsed = 1;
            return 0;
        }
        if (recvLen < frameLen) {
            *wUsed = 0;
            return -1;
        }
        *wUsed = frameLen;
        return 0;
    }

    if (func != DOWN_EXPAND_CODE) {
        return 1;
    }

    /* 升降控制要素 0x1E：交给原 SL651 */
    if (recvBuffer[22] == LIFT_ELEMENT_ID) {
        return 1;
    }

    memcpy(&stHead, recvBuffer, sizeof(stHead));
    dataLen = swap_u16(stHead.u16FlagAndLen) & 0x0FFF;
    if (dataLen < EXTEND_DATA_LEN_OVERHEAD) {
        *wUsed = 1;
        return 0;
    }
    frameLen = extend_frame_len(dataLen);
    if (frameLen == 0) {
        *wUsed = 1;
        return 0;
    }
    if (recvLen < frameLen) {
        *wUsed = 0;
        return -1;
    }
    if (recvBuffer[frameLen - 3U] != ETX) {
        *wUsed = 1;
        return 0;
    }
    if (downlink_crc_ok(recvBuffer, frameLen) != SUCCESS) {
        LOG(LOG_WARNING, "ota downlink CRC fail len=%u cmd=%u",
            (unsigned)frameLen, (unsigned)recvBuffer[sizeof(OTA_DOWNFrameHead) - 1U]);
        *wUsed = OtaUpgrade_IsBusy() ? 1U : frameLen;
        return 0;
    }

    stHead.u16FlagAndLen = dataLen;
    handle_ota_f1(recvBuffer, frameLen, &stHead, dataLen);
    *wUsed = frameLen;
    return 0;
}

/* ---------------- libota 回调 ---------------- */

static void port_reboot_cb(void)
{
    LOG(LOG_DEBUG, "ota reboot now");
    sync();
    (void)system("reboot");
}

static void *port_reboot_thread(void *arg)
{
    uint32_t delay_ms = (uint32_t)(uintptr_t)arg;
    if (delay_ms == 0) {
        delay_ms = 5000U;
    }
    usleep((useconds_t)delay_ms * 1000U);
    port_reboot_cb();
    return NULL;
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
    OtaUpgrade_SendReport(report_cmd, (uint8_t *)body, len);
}

static void port_on_busy(void *user, int busy)
{
    (void)user;
    if (busy) {
        log_set_flash_quiet(1);
        NiShaUD_Stop_Running_Task();
        LOG(LOG_DEBUG, "ota quiesce: stop nisha task, muted log.lg write");
    } else {
        log_set_flash_quiet(0);
    }
}

static void port_schedule_reboot(void *user, uint32_t delay_ms)
{
    pthread_t tid;
    (void)user;
    if (delay_ms == 0) {
        delay_ms = 5000U;
    }
    if (pthread_create(&tid, NULL, port_reboot_thread,
                       (void *)(uintptr_t)delay_ms) == 0) {
        pthread_detach(tid);
    } else {
        port_reboot_cb();
    }
}

static int port_activate_bin(void *user, const char *bin_path, uint8_t slot)
{
    static const char *const drops[] = {
        "nisha_shengjiang",
        "nisha_gt6606",
        "run_nisha",
        "ota/slot_",
        "ota_boot.sh",
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
    /* 清掉误写在 rcS（exec 前过早启动）的旧行；真正自启挂 rc.local */
    (void)ota_linux_rewrite_rcs(OTA_RCS_PATH, NULL, drops);
    if (ota_linux_install_boot_guard(OTA_RC_LOCAL_PATH, app_dir, "/ota",
                                     OTA_BIN_NAME, drops) != 0) {
        LOG(LOG_ERROR, "ota install boot guard / rewrite rc.local failed");
        return OTA_FAIL;
    }
    LOG(LOG_DEBUG, "ota boot guard installed, %s -> ota_boot.sh (tag=%s)",
        OTA_RC_LOCAL_PATH, g_nisha_ota_product_tag);
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
    cfg.product_id = OTA_PKG_PRODUCT_NSHJ;
    /* Socket/4G：先 ACK 再写盘；DTU 串口仍先写再 ACK */
    cfg.fw102_ack_then_write = WebLink_IsSocketMode() ? 1 : 0;
    LOG(LOG_INFO, "ota ack_then_write=%d (%s)",
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
    g_otaReady = 1;
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
    if (use_stall == 0U) {
        use_stall = WebLink_IsSocketMode() ? 2000U : 4000U;
    }
    return ota_kick_if_stalled(use_stall, abort_ms);
}

void OtaUpgrade_QueryUiStatus(ota_ui_status_t *out)
{
    ota_query_ui_status(out);
}
