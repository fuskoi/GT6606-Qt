/**
 * libota — 通用 A/B 槽固件升级（网页 FIRMWARE_UPDATE=102 分包）
 *
 * 用法概要：
 *   1. ota_config_init_defaults(&cfg); 再填 root_dir / bin_name / slot_size_max
 *   2. 实现 ota_ops_t（send / vlog / on_busy / schedule_reboot / activate_bin）
 *   3. ota_init(&cfg, &ops);
 *   4. 下行解出 command+body 后调用 ota_handle_downlink(cmd, body, len);
 *   5. 业务侧用 ota_is_busy() 暂停抢 Flash / 串口的任务
 */
#ifndef LIBOTA_H
#define LIBOTA_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_OK    0
#define OTA_FAIL  (-1)

#define OTA_PATH_MAX          260
#define OTA_VERSION_MAX_LEN   32U

#define OTA_CMD_FW102_DEFAULT    102U
#define OTA_RPT_RESULT_DEFAULT   13U
#define OTA_RPT_REPLY_DEFAULT    120U

#define OTA_SLOT_A       0U
#define OTA_SLOT_B       1U

#define OTA_STATUS_IDLE       0U
#define OTA_STATUS_RECEIVING  1U
#define OTA_STATUS_APPLYING   3U
#define OTA_STATUS_OK         4U
#define OTA_STATUS_ERROR      5U

#define OTA_ERR_NONE     0U
#define OTA_ERR_PARAM    1U
#define OTA_ERR_BUSY     2U
#define OTA_ERR_SIZE     3U
#define OTA_ERR_IO       5U
#define OTA_ERR_APPLY    7U
#define OTA_ERR_STATE    8U
#define OTA_ERR_TIMEOUT  9U  /* 收包长时间无进度，放弃会话 */
#define OTA_ERR_VERIFY   10U /* 产品标识/打包哈希校验失败 */

typedef enum {
    OTA_LOG_DEBUG = 0,
    OTA_LOG_INFO,
    OTA_LOG_WARN,
    OTA_LOG_ERROR
} ota_log_level_t;

#pragma pack(push, 1)

typedef struct ota_result_wire {
    uint8_t  ok;
    uint8_t  active_slot;
    uint16_t err_code;
    char     version[OTA_VERSION_MAX_LEN];
} ota_result_wire_t; /* 36 */

/* 102 ACK：REPLY_MSG=120，body = replyCmd(102)+type+pktIndex(BE) */
typedef struct ota_fw102_ack_wire {
    uint8_t  reply_cmd;
    uint8_t  type;
    uint16_t pkt_index;
} ota_fw102_ack_wire_t; /* 4 */

#pragma pack(pop)

typedef struct ota_config {
    char     root_dir[OTA_PATH_MAX]; /* 应用根；"" 表示盘根 */
    const char *bin_name;            /* 槽内可执行名，如 "DOPPLER" */
    const char *staging_name;        /* 默认 "staging.bin" */
    const char *version_name;        /* 默认 "version" */
    const char *ota_dir;             /* 默认 "/ota" */
    const char *slot_a;              /* 默认 "/ota/slot_a" */
    const char *slot_b;              /* 默认 "/ota/slot_b" */
    const char *active_name;         /* 默认 "/ota/active" */

    uint32_t slot_size_max;
    uint16_t fw102_chunk_max;        /* 单包净荷上限，默认 2048 */
    uint32_t reboot_delay_ms;
    uint8_t  require_elf;            /* 1：首包/整包要求 ELF 魔数 */
    uint8_t  require_pkg;            /* 1：要求 DPLRPKG1 尾部+产品ID+SHA256 */
    uint32_t product_id;             /* require_pkg 时校验，0=不校产品 ID */
    uint8_t  fw102_ack_then_write;   /* 1：先 ACK 再写 Flash（慢 NAND 推荐） */
    const char *fw_version;          /* 会话/槽 version 字符串；NULL 则用 "web102" */

    uint8_t  cmd_fw102;              /* 下行命令字，默认 102 */
    uint8_t  rpt_result;             /* 切槽结果上行命令字，默认 13 */
    uint8_t  rpt_reply;              /* 102 ACK 上行命令字，默认 120 */
} ota_config_t;

/**
 * 产品适配回调。除 send 外均可为 NULL（会跳过对应能力）。
 * activate_bin：库完成 rename/写 active 后调用，用于改 rcS / boot 标志等。
 */
typedef struct ota_ops {
    void *user;

    void (*vlog)(void *user, int level, const char *fmt, va_list ap);

    /* 发一帧上行：report_cmd 为协议里的 F2 命令字 */
    void (*send)(void *user, uint8_t report_cmd, const void *body, uint16_t len);

    /* busy=1 升级开始；busy=0 会话失败回 idle（重启前不会回调 0） */
    void (*on_busy)(void *user, int busy);

    void (*schedule_reboot)(void *user, uint32_t delay_ms);

    /* 返回 OTA_OK / OTA_FAIL */
    int (*activate_bin)(void *user, const char *bin_path, uint8_t slot);
} ota_ops_t;

void ota_config_init_defaults(ota_config_t *cfg);
int ota_init(const ota_config_t *cfg, const ota_ops_t *ops);

/* cmd==cmd_fw102 返回 OTA_OK；其它命令返回 OTA_FAIL */
int ota_handle_downlink(uint8_t cmd, const uint8_t *body, uint16_t len);

int ota_is_busy(void);
/* 进行中会话进度：expect=下一包序号；非 busy 时返回 0 */
int ota_get_progress(uint16_t *pkt_expect, uint16_t *pkt_total, uint32_t *recv_size);

/* 触摸屏 / 本地 UI：当前会话 + 最近一次上报结果 */
typedef struct ota_ui_status {
    uint8_t  inited;
    uint8_t  busy;
    uint8_t  status;          /* OTA_STATUS_* */
    uint8_t  reboot_pending;
    uint16_t err_code;
    uint16_t pkt_expect;
    uint16_t pkt_total;
    uint32_t recv_size;
    uint32_t file_size;
    uint8_t  active_slot;
    uint8_t  have_last_result;
    uint8_t  last_ok;
    uint16_t last_err;
    char     version[OTA_VERSION_MAX_LEN + 1];
    char     last_version[OTA_VERSION_MAX_LEN + 1];
} ota_ui_status_t;

void ota_query_ui_status(ota_ui_status_t *out);
/* 收包停滞：stall_ms 无真实进度则重发 lastOk ACK；abort_ms 仍无进度则放弃并解除 busy。
 * 返回：1=已重发 ACK，-1=已中止，0=无动作。建议 1s 调一次。 */
int ota_kick_if_stalled(uint32_t stall_ms, uint32_t abort_ms);
void ota_quiesce(void);
uint8_t ota_active_slot(void);
/* 新固件启动成功后调用：清除 boot_pending，避免回滚 */
void ota_confirm_boot(void);
/* 把 version 字符串写入当前 active 槽的 version 文件（开机同步编译号） */
int ota_write_active_version(const char *version);

#ifdef __cplusplus
}
#endif

#endif /* LIBOTA_H */
