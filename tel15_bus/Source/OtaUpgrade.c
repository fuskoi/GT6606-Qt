/**
 * TEL-15 / tel_15_qudao 产品适配层：libota 落盘/切槽；
 * 命令字按《WEB服务器和接收服务器协议》远程升级=26，
 * 结果上报 REPORT_OTA_UPGRADE(0xFFFFFFE4，4 字节)。
 * 打包校验 + 启动回滚在此开启（网页仍只传文件，无需改协议）。
 */
#include "OtaUpgrade.h"
#include "ota.h"
#include "ota_linux.h"
#include "ota_pkg.h"
#include "WebSeriver.h"
#include "zigbee_dataproc.h"
#include "log.h"
#include "timer_manager.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OTA_RCS_PATH "/etc/init.d/rcS"
#define OTA_RC_LOCAL_PATH "/etc/rc.local"
#define OTA_BIN_NAME "tel15_gt6606"

/* 供 ota_pack 脚本 strings 检查；勿改字符串格式 */
const char g_tel15_ota_product_tag[] = "TEL15_OTA_ID=T15Q";

#pragma pack(push, 1)
/* 协议文档：远程升级正文（命令字 26 之后） */
typedef struct {
    uint8_t  u8SubDevice;   /* 1 RTU / 2 控制主机 / 3 行车 / 4 铅鱼 */
    uint16_t u16Seri;       /* 包序号，文档小端；设备按从 0 起 */
    uint32_t u32FileSize;   /* 文件总长，小端 */
} DocUpgradeHead;
#pragma pack(pop)

static uint32_t g_otaWebCmdSeri = 0;
static uint16_t g_otaDocChunk = 0;
static uint8_t g_otaDocZeroBased = 0; /* 首包序号为 0 则后续均 +1 */
static int g_otaResultDeviceType = DTU_TYPE;
static uint8_t g_otaUpgradeType = 0;  /* 最近一包子设备类型 */
static uint16_t g_otaLastPkt = 0;     /* 最近处理/确认的包序号 */

uint8_t OtaUpgrade_MapProtoErr(uint16_t ota_err)
{
    switch (ota_err) {
    case OTA_ERR_NONE:
        return (uint8_t)SUCCESS;
    case OTA_ERR_SEQ:
        return PROTO_ERR_FRAME_SERI;
    case OTA_ERR_SIZE:
        return PROTO_ERR_DATA_LEN;
    case OTA_ERR_PARAM:
    case OTA_ERR_VERIFY:
        return PROTO_ERR_PARAM;
    case OTA_ERR_IO:
    case OTA_ERR_APPLY:
        return PROTO_ERR_FILE_WRITE;
    case OTA_ERR_TIMEOUT:
        return PROTO_ERR_TIMEOUT;
    case OTA_ERR_BUSY:
        return PROTO_ERR_BUSY;
    case OTA_ERR_STATE:
    default:
        return PROTO_ERR_PARAM;
    }
}

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
    /* 库内历史前缀 fw102（Doppler 命令字）统一显示为本项目 ota */
    {
        char *p = msg;
        while ((p = strstr(p, "fw102")) != NULL) {
            p[0] = 'o'; p[1] = 't'; p[2] = 'a'; p[3] = ' '; p[4] = ' ';
            p += 5;
        }
    }
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

    /* 切槽结果 → 0xFFFFFFE4；升级序号用包序号 */
    if (report_cmd == REPORT_OTA_RESULT) {
        const ota_result_wire_t *res = (const ota_result_wire_t *)body;
        uint8_t status = (res->ok != 0)
                             ? (uint8_t)OTA_UPGRADE_STATUS_BUSY
                             : (uint8_t)OTA_UPGRADE_STATUS_ERROR;
        OtaUpgrade_PrepareResultEcho();
        (void)SendOtaUpgradeResultFrame(g_otaUpgradeType, status,
                                        g_otaWebCmdSeri, g_otaLastPkt,
                                        g_otaResultDeviceType);
        /*
         * 会话收尾日志必须进 log.lg：
         * - 失败：此时仍可能 quiet，先解除再写
         * - 成功：即将重启，on_busy(0) 不会来，更要在此落盘
         */
        log_set_flash_quiet(0);
        LOG(LOG_INFO,
            "ota finished %s: active_slot=%u err=%u status=%u cmdSeri=%u pkt=%u",
            (res->ok != 0) ? "SUCCESS" : "FAIL",
            (unsigned)res->active_slot,
            (unsigned)res->err_code,
            (unsigned)status,
            (unsigned)g_otaWebCmdSeri,
            (unsigned)g_otaLastPkt);
        return;
    }

    /* 每包确认 / 拒包 / 停滞重发 → 0xFFFFFFE4；升级序号=包序号 */
    if (report_cmd == REPORT_REPLY_MSG) {
        uint8_t status = (uint8_t)OTA_UPGRADE_STATUS_BUSY;
        uint16_t pkt = 0;
        uint8_t accepted = 1U;
        uint16_t errCode = 0;

        if (len >= sizeof(ota_fw102_ack_wire_t)) {
            const ota_fw102_ack_wire_t *ack = (const ota_fw102_ack_wire_t *)body;
            uint16_t pktBe = ack->pkt_index;
            pkt = (uint16_t)(((pktBe & 0x00FFu) << 8) | ((pktBe & 0xFF00u) >> 8));
            accepted = ack->accepted;
            errCode = ack->err_code;
            if (accepted == 0U) {
                status = (uint8_t)OTA_UPGRADE_STATUS_ERROR;
            }
        }
        if (pkt != 0) {
            g_otaLastPkt = pkt;
        }
        LOG(LOG_DEBUG,
            "ota ack uplink: accepted=%u pkt=%u err=%u status=%u cmdSeri=%u",
            (unsigned)accepted, (unsigned)pkt,
            (unsigned)errCode, (unsigned)status,
            (unsigned)g_otaWebCmdSeri);
        OtaUpgrade_PrepareResultEcho();
        (void)SendOtaUpgradeResultFrame(g_otaUpgradeType, status,
                                        g_otaWebCmdSeri, pkt,
                                        g_otaResultDeviceType);
        return;
    }

    (void)SendUpLinkToServer(UP_EXPAND_CODE, report_cmd, (uint8_t *)body, len, DTU_TYPE);
}

static void port_on_busy(void *user, int busy)
{
    (void)user;
    if (busy) {
        log_set_flash_quiet(1);
        (void)system("killall DOPPLER.1.0 >/dev/null 2>&1");
        (void)system("killall DOPPLER >/dev/null 2>&1");
        LOG(LOG_DEBUG, "ota quiesce: muted log.lg write, pause other apps if present");
    } else {
        /* 失败回 idle：确保 flash 可写（成功路径已在 REPORT_OTA_RESULT 解除） */
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
        "tel_15_qudao",
        "tel15_gt6606",
        "run_tel15",
        "nisha_shengjiang",
        "nisha_gt6606",
        "run_nisha",
        "doppler_gt6606",
        "run_doppler",
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
    /* rcS 的 exec 之后不会跑；真正自启挂 rc.local，并清掉泥沙/多普勒旧行 */
    (void)ota_linux_rewrite_rcs(OTA_RCS_PATH, NULL, drops);
    if (ota_linux_install_boot_guard(OTA_RC_LOCAL_PATH, app_dir, "/ota",
                                     OTA_BIN_NAME, drops) != 0) {
        LOG(LOG_ERROR, "ota install boot guard / rewrite rc.local failed");
        return OTA_FAIL;
    }
    LOG(LOG_DEBUG, "ota boot guard installed, %s -> ota_boot.sh (tag=%s)",
        OTA_RC_LOCAL_PATH, g_tel15_ota_product_tag);
    return OTA_OK;
}

int OtaUpgrade_Init(void)
{
    ota_config_t cfg;
    ota_ops_t ops;
    char app_dir[MAX_PATH];

    ota_config_init_defaults(&cfg);
    memset(app_dir, 0, sizeof(app_dir));
    if (!GetAppDir(app_dir)) {
        LOG(LOG_ERROR, "ota GetAppDir failed");
        return ERROR;
    }
    strncpy(cfg.root_dir, app_dir, sizeof(cfg.root_dir) - 1);
    cfg.bin_name = OTA_BIN_NAME;
    cfg.slot_size_max = MAX_SOFTWARE_SIZE;
    cfg.fw102_chunk_max = (uint16_t)OTA_FW102_CHUNK_MAX;
    cfg.require_elf = 1;
    cfg.require_pkg = 1;
    cfg.product_id = OTA_PKG_PRODUCT_T15Q;
    cfg.fw102_ack_then_write = 0;
    /* 下行命令字按协议文档「远程升级」= 26 */
    cfg.cmd_fw102 = (uint8_t)SERVER_FIRMWARE_UPDATE;
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
    ota_confirm_boot();
    LOG(LOG_DEBUG, "ota boot confirmed (cleared boot_pending)");
}

int OtaUpgrade_HandleDownlink(uint8_t u8Command, const uint8_t *pBody, uint16_t u16BodyLen)
{
    if (ota_handle_downlink(u8Command, pBody, u16BodyLen) == OTA_OK) {
        return SUCCESS;
    }
    return ERROR;
}

/**
 * 协议文档远程升级包 → libota 分包格式后处理。
 * pData：SeriverCommandHead 之后；可含尾部 0xFFFFFFFF+ETX+CRC（7 字节），自动剥除。
 * 返回 SUCCESS(0) 或协议错误码(>0)。
 */
int OtaUpgrade_HandleDocPacket(uint32_t u32CommandSeri, int deviceType,
                               const uint8_t *pData, uint16_t u16DataLen)
{
    DocUpgradeHead head;
    const uint8_t *payload;
    uint16_t payLen;
    uint16_t pktIndex;
    uint16_t pktTotal;
    uint8_t *body = NULL;
    uint16_t bodyLen;
    int ret = PROTO_ERR_PARAM;

    if (pData == NULL || u16DataLen < sizeof(DocUpgradeHead) + 1U) {
        LOG(LOG_ERROR, "ota doc packet too short len=%u", (unsigned)u16DataLen);
        return PROTO_ERR_DATA_LEN;
    }

    g_otaLastPkt = 0;

    /* 去掉协议尾 4×0xFF + ETX + CRC */
    if (u16DataLen >= 7U) {
        u16DataLen = (uint16_t)(u16DataLen - 7U);
    }
    if (u16DataLen < sizeof(DocUpgradeHead) + 1U) {
        return PROTO_ERR_DATA_LEN;
    }

    memcpy(&head, pData, sizeof(head));
    payload = pData + sizeof(DocUpgradeHead);
    payLen = (uint16_t)(u16DataLen - sizeof(DocUpgradeHead));
    if (payLen == 0 || payLen > OTA_FW102_CHUNK_MAX) {
        LOG(LOG_ERROR, "ota doc payload len=%u invalid (max=%u)",
            (unsigned)payLen, (unsigned)OTA_FW102_CHUNK_MAX);
        return PROTO_ERR_DATA_LEN;
    }

    g_otaWebCmdSeri = u32CommandSeri;
    g_otaResultDeviceType = deviceType;
    g_otaUpgradeType = head.u8SubDevice;

    /*
     * 网页实抓首包序号为 1；旧实现曾用 0 起。
     * 未在会话中时看首包：0→按 0 基(+1)，否则按 1 基原样映射到 libota。
     */
    if (!ota_is_busy()) {
        g_otaDocZeroBased = (head.u16Seri == 0) ? 1U : 0U;
        g_otaDocChunk = 0;
    }
    pktIndex = g_otaDocZeroBased ? (uint16_t)(head.u16Seri + 1U) : head.u16Seri;
    g_otaLastPkt = pktIndex;
    if (pktIndex == 0) {
        LOG(LOG_ERROR, "ota doc seri=%u maps to pkt 0", (unsigned)head.u16Seri);
        return PROTO_ERR_FRAME_SERI;
    }
    if (pktIndex == 1U || g_otaDocChunk == 0) {
        g_otaDocChunk = payLen;
    }
    if (g_otaDocChunk == 0) {
        return PROTO_ERR_DATA_LEN;
    }
    pktTotal = (uint16_t)((head.u32FileSize + g_otaDocChunk - 1U) / g_otaDocChunk);
    if (pktTotal == 0) {
        pktTotal = 1;
    }

    /* libota: type 仅允许 0/1；文档子设备 1~4，映射为 0/1 奇偶即可 */
    bodyLen = (uint16_t)(9U + payLen);
    body = (uint8_t *)malloc(bodyLen);
    if (body == NULL) {
        return PROTO_ERR_FILE_WRITE;
    }
    body[0] = (uint8_t)(head.u8SubDevice > 1U ? 1U : head.u8SubDevice);
    body[1] = (uint8_t)((pktIndex >> 8) & 0xFF);
    body[2] = (uint8_t)(pktIndex & 0xFF);
    body[3] = (uint8_t)((pktTotal >> 8) & 0xFF);
    body[4] = (uint8_t)(pktTotal & 0xFF);
    body[5] = (uint8_t)((head.u32FileSize >> 24) & 0xFF);
    body[6] = (uint8_t)((head.u32FileSize >> 16) & 0xFF);
    body[7] = (uint8_t)((head.u32FileSize >> 8) & 0xFF);
    body[8] = (uint8_t)(head.u32FileSize & 0xFF);
    memcpy(body + 9, payload, payLen);

    LOG(LOG_DEBUG,
        "ota cmd26 recv sub=%u seri=%u->pkt=%u/%u size=%u pay=%u",
        (unsigned)head.u8SubDevice, (unsigned)head.u16Seri,
        (unsigned)pktIndex, (unsigned)pktTotal,
        (unsigned)head.u32FileSize, (unsigned)payLen);

    if (ota_handle_downlink((uint8_t)SERVER_FIRMWARE_UPDATE, body, bodyLen) == OTA_OK) {
        ret = SUCCESS;
    } else {
        ret = PROTO_ERR_PARAM;
    }
    free(body);
    return ret;
}

int OtaUpgrade_IsBusy(void)
{
    return ota_is_busy();
}

uint16_t OtaUpgrade_GetLastPkt(void)
{
    return g_otaLastPkt;
}

int OtaUpgrade_GetProgress(uint16_t *pkt_expect, uint16_t *pkt_total, uint32_t *recv_size)
{
    return ota_get_progress(pkt_expect, pkt_total, recv_size);
}

int OtaUpgrade_KickIfStalled(uint32_t stall_ms)
{
    return ota_kick_if_stalled(stall_ms == 0U ? 4000U : stall_ms, 60000U);
}
