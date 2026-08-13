#include "ota.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "ota_pkg.h"

static uint32_t ota_now_ms(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }
    return (uint32_t)((unsigned long)tv.tv_sec * 1000UL +
                      (unsigned long)tv.tv_usec / 1000UL);
}

extern void sync(void);

typedef struct ota_session {
    uint8_t  target_slot;
    uint8_t  status;
    uint8_t  proto102;
    uint8_t  fw102_type;
    uint16_t chunk_size;
    uint16_t err_code;
    uint16_t pkt_expect;
    uint16_t pkt_total;
    uint32_t file_size;
    uint32_t next_offset;
    uint32_t recv_size;
    uint32_t last_progress_ms; /* 最近一次真实收包进度（不含 reack） */
    uint32_t last_kick_ms;     /* 最近一次 stall reack */
    FILE    *staging_fp;
    char     version[OTA_VERSION_MAX_LEN + 1];
    char     staging_path[OTA_PATH_MAX];
    char     target_path[OTA_PATH_MAX];
} ota_session_t;

static ota_config_t g_cfg;
static ota_ops_t g_ops;
static ota_session_t g_sess;
static uint8_t g_reboot_pending;
static uint8_t g_quiesced;
static uint8_t g_inited;
static uint8_t g_have_last_result;
static uint8_t g_last_ok;
static uint16_t g_last_err;
static char g_last_version[OTA_VERSION_MAX_LEN + 1];

static void ota_log(int level, const char *fmt, ...)
{
    va_list ap;

    if (g_ops.vlog == NULL) {
        return;
    }
    va_start(ap, fmt);
    g_ops.vlog(g_ops.user, level, fmt, ap);
    va_end(ap);
}

static uint16_t ota_htons16(uint16_t v)
{
    return (uint16_t)(((v & 0x00FFu) << 8) | ((v & 0xFF00u) >> 8));
}

void ota_config_init_defaults(ota_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->bin_name = "APP";
    cfg->staging_name = "staging.bin";
    cfg->version_name = "version";
    cfg->ota_dir = "/ota";
    cfg->slot_a = "/ota/slot_a";
    cfg->slot_b = "/ota/slot_b";
    cfg->active_name = "/ota/active";
    cfg->slot_size_max = 2U * 1024U * 1024U;
    cfg->fw102_chunk_max = 2048U;
    cfg->reboot_delay_ms = 5000U;
    cfg->require_elf = 1;
    cfg->require_pkg = 0;
    cfg->product_id = 0;
    cfg->fw102_ack_then_write = 1;
    cfg->fw_version = NULL;
    cfg->cmd_fw102 = (uint8_t)OTA_CMD_FW102_DEFAULT;
    cfg->rpt_result = (uint8_t)OTA_RPT_RESULT_DEFAULT;
    cfg->rpt_reply = (uint8_t)OTA_RPT_REPLY_DEFAULT;
}

static int ota_mkdir_p(const char *path)
{
    char tmp[OTA_PATH_MAX];
    size_t len = 0;
    size_t i = 0;

    if (path == NULL || path[0] == '\0') {
        return OTA_FAIL;
    }
    len = strlen(path);
    if (len >= sizeof(tmp)) {
        return OTA_FAIL;
    }
    memcpy(tmp, path, len + 1);
    for (i = 1; i < len; i++) {
        if (tmp[i] != '/') {
            continue;
        }
        tmp[i] = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            return OTA_FAIL;
        }
        tmp[i] = '/';
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return OTA_FAIL;
    }
    return OTA_OK;
}

static void ota_slot_dir(uint8_t slot, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s%s", g_cfg.root_dir,
             (slot == OTA_SLOT_B) ? g_cfg.slot_b : g_cfg.slot_a);
}

static void ota_slot_bin_path(uint8_t slot, char *out, size_t out_size)
{
    char dir[OTA_PATH_MAX];
    ota_slot_dir(slot, dir, sizeof(dir));
    snprintf(out, out_size, "%s/%s", dir, g_cfg.bin_name);
}

static void ota_slot_staging_path(uint8_t slot, char *out, size_t out_size)
{
    char dir[OTA_PATH_MAX];
    ota_slot_dir(slot, dir, sizeof(dir));
    snprintf(out, out_size, "%s/%s", dir, g_cfg.staging_name);
}

static void ota_slot_version_path(uint8_t slot, char *out, size_t out_size)
{
    char dir[OTA_PATH_MAX];
    ota_slot_dir(slot, dir, sizeof(dir));
    snprintf(out, out_size, "%s/%s", dir, g_cfg.version_name);
}

static void ota_active_path(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s%s", g_cfg.root_dir, g_cfg.active_name);
}

static void ota_boot_pending_path(char *out, size_t out_size)
{
    snprintf(out, out_size, "%s%s/boot_pending", g_cfg.root_dir, g_cfg.ota_dir);
}

/*
 * boot_pending 格式（文本）：
 *   行1 tries
 *   行2 prev_slot (A/B)
 *   行3 prev_bin 绝对路径
 * 升级成功切槽前写入 tries=0；ota_boot.sh 首次启动改为 1；
 * 若再次启动仍存在且 tries>=1 则回滚到 prev。
 */
static int ota_write_boot_pending(uint8_t prev_slot, const char *prev_bin)
{
    char path[OTA_PATH_MAX];
    FILE *fp = NULL;

    if (prev_bin == NULL || prev_bin[0] == '\0') {
        return OTA_FAIL;
    }
    ota_boot_pending_path(path, sizeof(path));
    fp = fopen(path, "wb");
    if (fp == NULL) {
        return OTA_FAIL;
    }
    fprintf(fp, "0\n%c\n%s\n",
            (prev_slot == OTA_SLOT_B) ? 'B' : 'A',
            prev_bin);
    fclose(fp);
    return OTA_OK;
}

void ota_confirm_boot(void)
{
    char path[OTA_PATH_MAX];

    ota_boot_pending_path(path, sizeof(path));
    if (path[0] != '\0') {
        remove(path);
    }
}

uint8_t ota_active_slot(void)
{
    char path[OTA_PATH_MAX];
    char buf[8] = {0};
    FILE *fp = NULL;

    ota_active_path(path, sizeof(path));
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return OTA_SLOT_A;
    }
    if (fread(buf, 1, sizeof(buf) - 1, fp) == 0) {
        fclose(fp);
        return OTA_SLOT_A;
    }
    fclose(fp);
    if (buf[0] == 'B' || buf[0] == 'b' || buf[0] == '1') {
        return OTA_SLOT_B;
    }
    return OTA_SLOT_A;
}

static int ota_write_active_slot(uint8_t slot)
{
    char path[OTA_PATH_MAX];
    FILE *fp = NULL;
    char ch = (slot == OTA_SLOT_B) ? 'B' : 'A';

    ota_active_path(path, sizeof(path));
    fp = fopen(path, "wb");
    if (fp == NULL) {
        return OTA_FAIL;
    }
    if (fwrite(&ch, 1, 1, fp) != 1) {
        fclose(fp);
        return OTA_FAIL;
    }
    fclose(fp);
    return OTA_OK;
}

static int ota_write_version_file(uint8_t slot, const char *version)
{
    char path[OTA_PATH_MAX];
    FILE *fp = NULL;

    ota_slot_version_path(slot, path, sizeof(path));
    fp = fopen(path, "wb");
    if (fp == NULL) {
        return OTA_FAIL;
    }
    fprintf(fp, "%s\n", version != NULL ? version : "");
    fclose(fp);
    return OTA_OK;
}

int ota_write_active_version(const char *version)
{
    return ota_write_version_file(ota_active_slot(), version);
}

static void ota_send_raw(uint8_t report_cmd, const void *body, uint16_t len)
{
    if (g_ops.send != NULL && body != NULL) {
        g_ops.send(g_ops.user, report_cmd, body, len);
    }
}

static void ota_send_result(uint8_t ok, uint16_t err)
{
    ota_result_wire_t res;

    memset(&res, 0, sizeof(res));
    res.ok = ok;
    res.active_slot = ota_active_slot();
    res.err_code = err;
    strncpy(res.version, g_sess.version, OTA_VERSION_MAX_LEN);
    ota_send_raw(g_cfg.rpt_result, &res, (uint16_t)sizeof(res));

    g_have_last_result = 1;
    g_last_ok = ok;
    g_last_err = err;
    memset(g_last_version, 0, sizeof(g_last_version));
    strncpy(g_last_version, g_sess.version, OTA_VERSION_MAX_LEN);
}

static void ota_mark_progress(void)
{
    g_sess.last_progress_ms = ota_now_ms();
}

static void ota_send_fw102_ack(uint8_t type, uint16_t pkt_index)
{
    ota_fw102_ack_wire_t ack;

    memset(&ack, 0, sizeof(ack));
    ack.reply_cmd = g_cfg.cmd_fw102;
    ack.type = type;
    ack.pkt_index = ota_htons16(pkt_index);
    ota_send_raw(g_cfg.rpt_reply, &ack, (uint16_t)sizeof(ack));
}

static void ota_close_staging_fp(void)
{
    if (g_sess.staging_fp != NULL) {
        fflush(g_sess.staging_fp);
        fclose(g_sess.staging_fp);
        g_sess.staging_fp = NULL;
    }
}

static void ota_set_quiesced(int on)
{
    if (on) {
        if (!g_quiesced) {
            g_quiesced = 1;
            if (g_ops.on_busy != NULL) {
                g_ops.on_busy(g_ops.user, 1);
            }
        }
    } else if (g_quiesced && !g_reboot_pending) {
        g_quiesced = 0;
        if (g_ops.on_busy != NULL) {
            g_ops.on_busy(g_ops.user, 0);
        }
    }
}

static void ota_reset_session(void)
{
    uint8_t active = ota_active_slot();

    ota_close_staging_fp();
    memset(&g_sess, 0, sizeof(g_sess));
    g_sess.status = OTA_STATUS_IDLE;
    g_sess.target_slot = (uint8_t)(1U - active);
    ota_set_quiesced(0);
}

/* 升级失败：上报结果、清 staging、解除 busy（不重启，便于立刻重试） */
static void ota_fail_and_idle(uint16_t err)
{
    ota_send_result(0, err);
    if (g_sess.staging_path[0] != '\0') {
        remove(g_sess.staging_path);
    }
    ota_reset_session();
}

void ota_quiesce(void)
{
    ota_set_quiesced(1);
}

static void ota_schedule_reboot(void)
{
    if (g_reboot_pending) {
        return;
    }
    g_reboot_pending = 1;
    if (g_ops.schedule_reboot != NULL) {
        g_ops.schedule_reboot(g_ops.user, g_cfg.reboot_delay_ms);
    }
    ota_log(OTA_LOG_DEBUG, "ota will reboot in %u ms", (unsigned)g_cfg.reboot_delay_ms);
}

static int ota_file_is_elf(const char *path)
{
    FILE *fp = NULL;
    uint8_t mag[4] = {0};

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    if (fread(mag, 1, 4, fp) != 4) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return (mag[0] == 0x7FU && mag[1] == 'E' && mag[2] == 'L' && mag[3] == 'F') ? 1 : 0;
}

static int ota_apply_bin(void)
{
    char old_path[OTA_PATH_MAX];
    char prev_bin[OTA_PATH_MAX];
    uint8_t prev_slot = ota_active_slot();

    if (g_cfg.require_pkg) {
        if (ota_pkg_verify_and_strip(g_sess.staging_path, g_cfg.product_id) != 0) {
            ota_log(OTA_LOG_ERROR, "ota pkg verify failed (magic/product/sha256)");
            return OTA_FAIL;
        }
        ota_log(OTA_LOG_DEBUG, "ota pkg verify ok, trailer stripped");
    }

    if (g_cfg.require_elf && !ota_file_is_elf(g_sess.staging_path)) {
        ota_log(OTA_LOG_ERROR, "ota staging not ELF after verify");
        return OTA_FAIL;
    }

    ota_slot_bin_path(prev_slot, prev_bin, sizeof(prev_bin));
    if (ota_write_boot_pending(prev_slot, prev_bin) != OTA_OK) {
        ota_log(OTA_LOG_ERROR, "ota write boot_pending failed");
        return OTA_FAIL;
    }

    snprintf(old_path, sizeof(old_path), "%s.bak", g_sess.target_path);
    remove(old_path);
    rename(g_sess.target_path, old_path);
    if (rename(g_sess.staging_path, g_sess.target_path) != 0) {
        rename(old_path, g_sess.target_path);
        {
            char pend[OTA_PATH_MAX];
            ota_boot_pending_path(pend, sizeof(pend));
            remove(pend);
        }
        return OTA_FAIL;
    }
    (void)chmod(g_sess.target_path, 0755);
    if (ota_write_version_file(g_sess.target_slot, g_sess.version) != OTA_OK) {
        return OTA_FAIL;
    }
    if (ota_write_active_slot(g_sess.target_slot) != OTA_OK) {
        return OTA_FAIL;
    }
    if (g_ops.activate_bin != NULL) {
        if (g_ops.activate_bin(g_ops.user, g_sess.target_path, g_sess.target_slot) != OTA_OK) {
            return OTA_FAIL;
        }
    }
    return OTA_OK;
}

static int ota_begin_target_slot_session(uint32_t file_size, uint16_t chunk_size, const char *version)
{
    char slot_dir[OTA_PATH_MAX];
    FILE *fp = NULL;
    uint8_t active = ota_active_slot();

    if (file_size == 0 || file_size > g_cfg.slot_size_max) {
        return OTA_FAIL;
    }

    ota_reset_session();
    g_sess.target_slot = (uint8_t)(1U - active);
    g_sess.chunk_size = chunk_size;
    g_sess.file_size = file_size;
    g_sess.next_offset = 0;
    g_sess.recv_size = 0;
    strncpy(g_sess.version, version != NULL ? version : "web102", OTA_VERSION_MAX_LEN);
    g_sess.version[OTA_VERSION_MAX_LEN] = '\0';

    ota_slot_dir(g_sess.target_slot, slot_dir, sizeof(slot_dir));
    if (ota_mkdir_p(slot_dir) != OTA_OK) {
        return OTA_FAIL;
    }
    ota_slot_staging_path(g_sess.target_slot, g_sess.staging_path, sizeof(g_sess.staging_path));
    ota_slot_bin_path(g_sess.target_slot, g_sess.target_path, sizeof(g_sess.target_path));

    fp = fopen(g_sess.staging_path, "wb+");
    if (fp == NULL) {
        return OTA_FAIL;
    }
    setvbuf(fp, NULL, _IOFBF, 16384);
    g_sess.staging_fp = fp;
    g_sess.status = OTA_STATUS_RECEIVING;
    g_sess.err_code = OTA_ERR_NONE;
    ota_set_quiesced(1);
    return OTA_OK;
}

/*
 * 网页 FIRMWARE_UPDATE=102：
 *   type(u8) + pktIndex(u16 BE) + pktTotal(u16 BE) + fileSize(u32 BE) + payload
 * ACK：rpt_reply(默认 120)，body = replyCmd + type + pktIndex(BE)
 */
static int ota_handle_firmware102(const uint8_t *body, uint16_t body_len)
{
    uint8_t type = 0;
    uint16_t pkt = 0;
    uint16_t total = 0;
    uint32_t file_size = 0;
    const uint16_t hdr_len = 9;
    uint16_t payload_len = 0;
    const uint8_t *payload = NULL;
    uint32_t offset = 0;
    FILE *fp = NULL;

    if (body == NULL || body_len < hdr_len) {
        ota_log(OTA_LOG_DEBUG, "fw102 body too short len=%u", body_len);
        return OTA_OK;
    }

    type = body[0];
    pkt = (uint16_t)((body[1] << 8) | body[2]);
    total = (uint16_t)((body[3] << 8) | body[4]);
    file_size = ((uint32_t)body[5] << 24) | ((uint32_t)body[6] << 16) |
                ((uint32_t)body[7] << 8) | (uint32_t)body[8];
    payload = body + hdr_len;
    payload_len = (uint16_t)(body_len - hdr_len);

    if (type > 1 || pkt == 0 || total == 0 || pkt > total || payload_len == 0) {
        ota_log(OTA_LOG_DEBUG, "fw102 bad param type=%u pkt=%u/%u pay=%u",
                type, pkt, total, payload_len);
        ota_send_fw102_ack(type, pkt);
        return OTA_OK;
    }
    if (file_size == 0 || file_size > g_cfg.slot_size_max ||
        payload_len > g_cfg.fw102_chunk_max) {
        ota_log(OTA_LOG_DEBUG, "fw102 size/chunk invalid size=%u pay=%u",
                (unsigned)file_size, payload_len);
        ota_send_fw102_ack(type, pkt);
        return OTA_OK;
    }

    if (pkt == 1U) {
        if (ota_begin_target_slot_session(file_size, payload_len,
                (g_cfg.fw_version != NULL && g_cfg.fw_version[0] != '\0')
                    ? g_cfg.fw_version : "web102") != OTA_OK) {
            ota_log(OTA_LOG_ERROR, "fw102 begin session fail");
            ota_send_result(0, OTA_ERR_IO);
            ota_send_fw102_ack(type, pkt);
            return OTA_OK;
        }
        g_sess.proto102 = 1;
        g_sess.fw102_type = type;
        g_sess.pkt_expect = 1;
        g_sess.pkt_total = total;
        if (g_cfg.require_elf &&
            (payload_len < 4 || payload[0] != 0x7FU || payload[1] != 'E' ||
             payload[2] != 'L' || payload[3] != 'F')) {
            ota_log(OTA_LOG_ERROR, "fw102 reject non-ELF first packet");
            ota_fail_and_idle(OTA_ERR_PARAM);
            ota_send_fw102_ack(type, pkt);
            return OTA_OK;
        }
        ota_log(OTA_LOG_DEBUG,
                "fw102 START size=%u totalPkt=%u chunk=%u active=%c target=%c",
                (unsigned)file_size, total, payload_len,
                ota_active_slot() == OTA_SLOT_B ? 'B' : 'A',
                g_sess.target_slot == OTA_SLOT_B ? 'B' : 'A');
    }

    if (!g_sess.proto102 || g_sess.status != OTA_STATUS_RECEIVING) {
        ota_log(OTA_LOG_DEBUG, "fw102 not in session, pkt=%u", pkt);
        ota_send_fw102_ack(type, pkt);
        return OTA_OK;
    }
    if (file_size != g_sess.file_size || total != g_sess.pkt_total) {
        ota_log(OTA_LOG_DEBUG, "fw102 meta mismatch");
        ota_send_fw102_ack(type, pkt);
        return OTA_OK;
    }
    if (pkt != g_sess.pkt_expect) {
        uint16_t last_ok = (g_sess.pkt_expect > 1U)
                               ? (uint16_t)(g_sess.pkt_expect - 1U)
                               : 0;
        ota_log(OTA_LOG_DEBUG, "fw102 pkt expect=%u got=%u, ack lastOk=%u",
                g_sess.pkt_expect, pkt, last_ok);
        ota_send_fw102_ack(type, last_ok);
        return OTA_OK;
    }

    offset = (uint32_t)(pkt - 1U) * (uint32_t)g_sess.chunk_size;
    if (offset >= g_sess.file_size) {
        ota_log(OTA_LOG_DEBUG, "fw102 offset overflow off=%u", (unsigned)offset);
        ota_send_fw102_ack(type, pkt);
        return OTA_OK;
    }
    {
        uint32_t remain = g_sess.file_size - offset;
        uint32_t expect_pay = (pkt < total) ? (uint32_t)g_sess.chunk_size : remain;
        if ((uint32_t)payload_len != expect_pay) {
            ota_log(OTA_LOG_DEBUG, "fw102 pay len mismatch got=%u expect=%u",
                    payload_len, (unsigned)expect_pay);
            ota_send_fw102_ack(type, pkt);
            return OTA_OK;
        }
    }

    fp = g_sess.staging_fp;
    if (fp == NULL) {
        fp = fopen(g_sess.staging_path, "rb+");
        if (fp == NULL) {
            ota_log(OTA_LOG_ERROR, "fw102 reopen staging fail pkt=%u", pkt);
            ota_fail_and_idle(OTA_ERR_IO);
            return OTA_OK;
        }
        setvbuf(fp, NULL, _IOFBF, 16384);
        g_sess.staging_fp = fp;
    }

    g_sess.next_offset = offset + payload_len;
    g_sess.recv_size = g_sess.next_offset;
    g_sess.pkt_expect = (uint16_t)(pkt + 1U);
    ota_mark_progress();

    if (g_cfg.fw102_ack_then_write) {
        ota_send_fw102_ack(type, pkt);
    }

    {
        long cur = ftell(fp);
        uint32_t t0 = 0;
        uint32_t t1 = 0;

        if (cur < 0 || (uint32_t)cur != offset) {
            if (fseek(fp, (long)offset, SEEK_SET) != 0) {
                ota_log(OTA_LOG_ERROR, "fw102 seek fail pkt=%u off=%u", pkt, (unsigned)offset);
                ota_fail_and_idle(OTA_ERR_IO);
                return OTA_OK;
            }
        }
        /* 写盘前后各打一行：若只有 begin 没有 done，即卡在 fwrite/fflush */
        ota_log(OTA_LOG_DEBUG, "fw102 WRITE begin pkt=%u len=%u", pkt, payload_len);
        t0 = ota_now_ms();
        if (fwrite(payload, 1, payload_len, fp) != payload_len) {
            ota_log(OTA_LOG_ERROR, "fw102 write fail pkt=%u", pkt);
            ota_fail_and_idle(OTA_ERR_IO);
            return OTA_OK;
        }
        (void)fflush(fp);
        t1 = ota_now_ms();
        ota_log(OTA_LOG_DEBUG, "fw102 WRITE done pkt=%u ms=%u",
                pkt, (unsigned)(t1 - t0));
    }

    if (!g_cfg.fw102_ack_then_write) {
        ota_log(OTA_LOG_DEBUG, "fw102 ACK send pkt=%u", pkt);
        ota_send_fw102_ack(type, pkt);
    }

    /* 每包都打，避免“助手有完整帧、设备其实没进处理”时无法对照 */
    ota_log(OTA_LOG_DEBUG, "fw102 DATA pkt=%u/%u recv=%u",
            pkt, total, (unsigned)g_sess.recv_size);
    if (pkt != total) {
        return OTA_OK;
    }

    ota_close_staging_fp();
    if (g_sess.recv_size != g_sess.file_size) {
        ota_log(OTA_LOG_ERROR, "fw102 incomplete recv=%u expect=%u",
                (unsigned)g_sess.recv_size, (unsigned)g_sess.file_size);
        ota_fail_and_idle(OTA_ERR_SIZE);
        return OTA_OK;
    }
    /* ELF/打包校验放到 apply 内（剥尾后再查 ELF） */

    g_sess.status = OTA_STATUS_APPLYING;
    if (ota_apply_bin() != OTA_OK) {
        ota_log(OTA_LOG_ERROR, "fw102 apply failed");
        ota_fail_and_idle(g_cfg.require_pkg ? OTA_ERR_VERIFY : OTA_ERR_APPLY);
        return OTA_OK;
    }
    sync();
    g_sess.status = OTA_STATUS_OK;
    ota_send_result(1, OTA_ERR_NONE);
    ota_schedule_reboot();
    ota_log(OTA_LOG_DEBUG, "fw102 apply ok, reboot soon");
    return OTA_OK;
}

int ota_init(const ota_config_t *cfg, const ota_ops_t *ops)
{
    char path[OTA_PATH_MAX];

    if (cfg == NULL || ops == NULL || cfg->bin_name == NULL || cfg->bin_name[0] == '\0') {
        return OTA_FAIL;
    }

    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg = *cfg;
    if (g_cfg.staging_name == NULL) {
        g_cfg.staging_name = "staging.bin";
    }
    if (g_cfg.version_name == NULL) {
        g_cfg.version_name = "version";
    }
    if (g_cfg.ota_dir == NULL) {
        g_cfg.ota_dir = "/ota";
    }
    if (g_cfg.slot_a == NULL) {
        g_cfg.slot_a = "/ota/slot_a";
    }
    if (g_cfg.slot_b == NULL) {
        g_cfg.slot_b = "/ota/slot_b";
    }
    if (g_cfg.active_name == NULL) {
        g_cfg.active_name = "/ota/active";
    }
    {
        size_t n = strlen(g_cfg.root_dir);
        while (n > 1 && g_cfg.root_dir[n - 1] == '/') {
            g_cfg.root_dir[n - 1] = '\0';
            n--;
        }
        if (n == 1 && g_cfg.root_dir[0] == '/') {
            g_cfg.root_dir[0] = '\0';
        }
    }

    memset(&g_ops, 0, sizeof(g_ops));
    g_ops = *ops;
    memset(&g_sess, 0, sizeof(g_sess));
    g_reboot_pending = 0;
    g_quiesced = 0;

    snprintf(path, sizeof(path), "%s%s", g_cfg.root_dir, g_cfg.ota_dir);
    (void)ota_mkdir_p(path);
    ota_slot_dir(OTA_SLOT_A, path, sizeof(path));
    (void)ota_mkdir_p(path);
    ota_slot_dir(OTA_SLOT_B, path, sizeof(path));
    (void)ota_mkdir_p(path);

    ota_active_path(path, sizeof(path));
    if (access(path, F_OK) != 0) {
        (void)ota_write_active_slot(OTA_SLOT_A);
    }

    ota_reset_session();
    g_inited = 1;
    ota_log(OTA_LOG_DEBUG, "ota init ok, root=%s active=%c",
            g_cfg.root_dir, ota_active_slot() == OTA_SLOT_B ? 'B' : 'A');
    return OTA_OK;
}

int ota_is_busy(void)
{
    if (!g_inited) {
        return 0;
    }
    if (g_quiesced) {
        return 1;
    }
    return (g_sess.status == OTA_STATUS_RECEIVING ||
            g_sess.status == OTA_STATUS_APPLYING) ? 1 : 0;
}

int ota_get_progress(uint16_t *pkt_expect, uint16_t *pkt_total, uint32_t *recv_size)
{
    if (!g_inited || !ota_is_busy()) {
        if (pkt_expect) {
            *pkt_expect = 0;
        }
        if (pkt_total) {
            *pkt_total = 0;
        }
        if (recv_size) {
            *recv_size = 0;
        }
        return 0;
    }
    if (pkt_expect) {
        *pkt_expect = g_sess.pkt_expect;
    }
    if (pkt_total) {
        *pkt_total = g_sess.pkt_total;
    }
    if (recv_size) {
        *recv_size = g_sess.recv_size;
    }
    return 1;
}

void ota_query_ui_status(ota_ui_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->inited = g_inited;
    if (!g_inited) {
        return;
    }
    out->busy = (uint8_t)(ota_is_busy() ? 1 : 0);
    out->status = g_sess.status;
    out->reboot_pending = g_reboot_pending;
    out->err_code = g_sess.err_code;
    out->pkt_expect = g_sess.pkt_expect;
    out->pkt_total = g_sess.pkt_total;
    out->recv_size = g_sess.recv_size;
    out->file_size = g_sess.file_size;
    out->active_slot = ota_active_slot();
    strncpy(out->version, g_sess.version, OTA_VERSION_MAX_LEN);
    out->have_last_result = g_have_last_result;
    out->last_ok = g_last_ok;
    out->last_err = g_last_err;
    strncpy(out->last_version, g_last_version, OTA_VERSION_MAX_LEN);
}

/*
 * DTU/网页常丢 ACK：无真实收包达 stall_ms 则重发 lastOk，促使对端续传下一包；
 * 仍无进度达 abort_ms 则放弃会话、解除 busy（不重启）。
 * 返回 1=reack，-1=已中止，0=无动作。
 */
int ota_kick_if_stalled(uint32_t stall_ms, uint32_t abort_ms)
{
    uint32_t now = 0;
    uint32_t idle_ms = 0;
    uint16_t last_ok = 0;

    if (!g_inited || !g_sess.proto102 || g_sess.status != OTA_STATUS_RECEIVING) {
        return 0;
    }
    if (g_sess.pkt_expect <= 1U) {
        return 0;
    }
    if (stall_ms == 0) {
        stall_ms = 4000U;
    }
    if (abort_ms == 0) {
        abort_ms = 60000U;
    }
    if (abort_ms < stall_ms) {
        abort_ms = stall_ms;
    }

    now = ota_now_ms();
    if (g_sess.last_progress_ms == 0U) {
        g_sess.last_progress_ms = now;
        return 0;
    }
    idle_ms = now - g_sess.last_progress_ms;

    if (idle_ms >= abort_ms) {
        ota_log(OTA_LOG_WARN,
                "fw102 abort: no packet over %u ms, expect=%u lastOk=%u, clear busy",
                (unsigned)abort_ms,
                g_sess.pkt_expect,
                (uint16_t)(g_sess.pkt_expect - 1U));
        ota_fail_and_idle(OTA_ERR_TIMEOUT);
        return -1;
    }

    if (idle_ms < stall_ms) {
        return 0;
    }
    if (g_sess.last_kick_ms != 0U && (now - g_sess.last_kick_ms) < stall_ms) {
        return 0;
    }

    last_ok = (uint16_t)(g_sess.pkt_expect - 1U);
    ota_log(OTA_LOG_WARN, "fw102 stall kick: reack lastOk=%u expect=%u idle_ms=%u",
            last_ok, g_sess.pkt_expect, (unsigned)idle_ms);
    ota_send_fw102_ack(g_sess.fw102_type, last_ok);
    g_sess.last_kick_ms = now;
    return 1;
}

int ota_handle_downlink(uint8_t cmd, const uint8_t *body, uint16_t len)
{
    if (!g_inited) {
        return OTA_FAIL;
    }
    if (cmd == g_cfg.cmd_fw102) {
        return ota_handle_firmware102(body, len);
    }
    return OTA_FAIL;
}
