/**
 * 离线上报补发队列实现。
 */
#include "UplinkOffline.h"
#include "Common.h"
#include "WebSeriver.h"
#include "zigbee_dataproc.h"
#include "OtaUpgrade.h"
#include "log.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UOL_MAGIC           0x554F4C31U  /* UOL1 */
/* 约 1 个月：小时报×24/天 + 自动测流 DEPTH（最多约 12 次/天）≈ 780~1080 条；留余量 */
#define UOL_MAX_RECORDS     1500U
/* 小时报约几十 KB/月 + DEPTH 测流常见数百 KB；取 2MB */
#define UOL_MAX_FILE_BYTES  (2U * 1024U * 1024U)
#define UOL_MAX_PAYLOAD     8192U
#define UOL_FLUSH_PER_TICK  2U
#define UOL_FILE_NAME       "uplink_offline.dat"

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t  kind;
    uint8_t  type;
    uint32_t cmd;
    uint32_t unix_ts;
    uint16_t len;
} UolRecHead;
#pragma pack(pop)

static pthread_mutex_t g_uolMutex = PTHREAD_MUTEX_INITIALIZER;
static char g_uolPath[MAX_PATH];
static int g_uolReady = 0;
static volatile int g_uolFlushPending = 0;
/* 最近一次网页下行；0=开机后尚未收到 */
static time_t g_uolLastDownlink = 0;
/* 开机时刻：宽限期内未收到下行也不判离线 */
static time_t g_uolBootTime = 0;
/* TCP 断开强制离线，直到再次收到下行 */
static volatile int g_uolLinkLost = 0;
/* 开机宽限期到期离线提示只打一次 */
static volatile int g_uolBootGraceOfflineLogged = 0;

static int uol_path_init(void)
{
    char dir[MAX_PATH];

    memset(dir, 0, sizeof(dir));
    memset(g_uolPath, 0, sizeof(g_uolPath));
    if (!GetAppDir(dir)) {
        return ERROR;
    }
    snprintf(g_uolPath, sizeof(g_uolPath), "%s/%s", dir, UOL_FILE_NAME);
    return SUCCESS;
}

static int uol_scan(uint32_t *pCount, uint32_t *pBytes)
{
    FILE *fp;
    uint32_t count = 0;
    uint32_t bytes = 0;
    UolRecHead head;
    uint8_t skip[UOL_MAX_PAYLOAD];

    if (pCount) {
        *pCount = 0;
    }
    if (pBytes) {
        *pBytes = 0;
    }
    fp = fopen(g_uolPath, "rb");
    if (fp == NULL) {
        return SUCCESS;
    }
    for (;;) {
        size_t n = fread(&head, 1, sizeof(head), fp);
        if (n == 0) {
            break;
        }
        if (n != sizeof(head) || head.magic != UOL_MAGIC ||
            head.len == 0 || head.len > UOL_MAX_PAYLOAD) {
            LOG(LOG_WARNING, "uol scan corrupt, truncate");
            fclose(fp);
            fp = fopen(g_uolPath, "wb");
            if (fp != NULL) {
                fclose(fp);
            }
            if (pCount) {
                *pCount = 0;
            }
            if (pBytes) {
                *pBytes = 0;
            }
            return SUCCESS;
        }
        if (fread(skip, 1, head.len, fp) != head.len) {
            LOG(LOG_WARNING, "uol scan truncated record");
            break;
        }
        count++;
        bytes += (uint32_t)sizeof(head) + head.len;
    }
    fclose(fp);
    if (pCount) {
        *pCount = count;
    }
    if (pBytes) {
        *pBytes = bytes;
    }
    return SUCCESS;
}

/* 丢掉最旧的 dropN 条，其余写回 */
static int uol_drop_oldest(uint32_t dropN)
{
    FILE *fp;
    FILE *tmp;
    char tmpPath[MAX_PATH + 8];
    UolRecHead head;
    uint8_t payload[UOL_MAX_PAYLOAD];
    uint32_t seen = 0;

    if (dropN == 0) {
        return SUCCESS;
    }
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", g_uolPath);
    fp = fopen(g_uolPath, "rb");
    if (fp == NULL) {
        return ERROR;
    }
    tmp = fopen(tmpPath, "wb");
    if (tmp == NULL) {
        fclose(fp);
        return ERROR;
    }
    while (fread(&head, 1, sizeof(head), fp) == sizeof(head)) {
        if (head.magic != UOL_MAGIC || head.len == 0 || head.len > UOL_MAX_PAYLOAD) {
            break;
        }
        if (fread(payload, 1, head.len, fp) != head.len) {
            break;
        }
        if (seen < dropN) {
            seen++;
            continue;
        }
        if (fwrite(&head, 1, sizeof(head), tmp) != sizeof(head) ||
            fwrite(payload, 1, head.len, tmp) != head.len) {
            fclose(fp);
            fclose(tmp);
            unlink(tmpPath);
            return ERROR;
        }
    }
    fclose(fp);
    fclose(tmp);
    if (rename(tmpPath, g_uolPath) != 0) {
        unlink(tmpPath);
        return ERROR;
    }
    return SUCCESS;
}

static int uol_ensure_space(uint16_t addLen)
{
    uint32_t count = 0;
    uint32_t bytes = 0;
    uint32_t need = (uint32_t)sizeof(UolRecHead) + addLen;

    (void)uol_scan(&count, &bytes);
    while ((count >= UOL_MAX_RECORDS || bytes + need > UOL_MAX_FILE_BYTES) &&
           count > 0) {
        if (uol_drop_oldest(1) != SUCCESS) {
            return ERROR;
        }
        (void)uol_scan(&count, &bytes);
    }
    return SUCCESS;
}

int UplinkOffline_Init(void)
{
    FILE *fp;

    pthread_mutex_lock(&g_uolMutex);
    if (uol_path_init() != SUCCESS) {
        pthread_mutex_unlock(&g_uolMutex);
        LOG(LOG_ERROR, "uol GetAppDir failed");
        return ERROR;
    }
    fp = fopen(g_uolPath, "ab");
    if (fp == NULL) {
        pthread_mutex_unlock(&g_uolMutex);
        LOG(LOG_ERROR, "uol open %s failed: %s", g_uolPath, strerror(errno));
        return ERROR;
    }
    fclose(fp);
    (void)uol_scan(NULL, NULL);
    g_uolReady = 1;
    g_uolBootTime = time(NULL);
    g_uolLastDownlink = 0;
    g_uolLinkLost = 0;
    g_uolBootGraceOfflineLogged = 0;
    pthread_mutex_unlock(&g_uolMutex);
    LOG(LOG_DEBUG, "uol init ok path=%s boot_grace=%us",
        g_uolPath, (unsigned)UPLINK_OFFLINE_IDLE_SEC);
    return SUCCESS;
}

int UplinkOffline_IsOnline(void)
{
    time_t last = g_uolLastDownlink;
    time_t boot = g_uolBootTime;
    time_t now = time(NULL);

    /* 链路断开：立即离线 */
    if (g_uolLinkLost) {
        return 0;
    }
    /* 已收到过下行：按空闲超时 */
    if (last != 0) {
        if (now < last) {
            return 1;
        }
        return ((time_t)(now - last) < (time_t)UPLINK_OFFLINE_IDLE_SEC) ? 1 : 0;
    }
    /* 开机后尚未收到下行：宽限期内先当在线，到期仍无应答再离线 */
    if (boot == 0) {
        return 0;
    }
    if (now < boot) {
        return 1;
    }
    if ((time_t)(now - boot) < (time_t)UPLINK_OFFLINE_IDLE_SEC) {
        return 1;
    }
    if (!g_uolBootGraceOfflineLogged) {
        g_uolBootGraceOfflineLogged = 1;
        LOG(LOG_WARNING, "uol offline (boot grace expired, no downlink in %us)",
            (unsigned)UPLINK_OFFLINE_IDLE_SEC);
    }
    return 0;
}

int UplinkOffline_ShouldPersist(uint32_t cmd)
{
    return (cmd == REPORT_AUTO_ROTATOR_DATA_DEPTH) ? 1 : 0;
}

int UplinkOffline_Enqueue(uint8_t kind, uint32_t cmd,
                          const uint8_t *data, uint16_t len, int type)
{
    FILE *fp;
    UolRecHead head;

    if (data == NULL || len == 0 || len > UOL_MAX_PAYLOAD) {
        return ERROR;
    }
    if (kind != UPLINK_OFFLINE_KIND_WEB && kind != UPLINK_OFFLINE_KIND_SL651) {
        return ERROR;
    }

    pthread_mutex_lock(&g_uolMutex);
    if (!g_uolReady) {
        pthread_mutex_unlock(&g_uolMutex);
        return ERROR;
    }
    if (uol_ensure_space(len) != SUCCESS) {
        pthread_mutex_unlock(&g_uolMutex);
        LOG(LOG_ERROR, "uol ensure space fail");
        return ERROR;
    }

    memset(&head, 0, sizeof(head));
    head.magic = UOL_MAGIC;
    head.kind = kind;
    head.type = (uint8_t)type;
    head.cmd = cmd;
    head.unix_ts = (uint32_t)time(NULL);
    head.len = len;

    fp = fopen(g_uolPath, "ab");
    if (fp == NULL) {
        pthread_mutex_unlock(&g_uolMutex);
        LOG(LOG_ERROR, "uol enqueue open fail");
        return ERROR;
    }
    if (fwrite(&head, 1, sizeof(head), fp) != sizeof(head) ||
        fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        pthread_mutex_unlock(&g_uolMutex);
        LOG(LOG_ERROR, "uol enqueue write fail");
        return ERROR;
    }
    fflush(fp);
    fclose(fp);
    pthread_mutex_unlock(&g_uolMutex);

    LOG(LOG_DEBUG, "uol enqueue kind=%u cmd=0x%x len=%u type=%d",
        (unsigned)kind, (unsigned)cmd, (unsigned)len, type);
    return SUCCESS;
}

void UplinkOffline_OnDownlink(void)
{
    time_t now = time(NULL);
    int wasOffline;

    /* 不经 IsOnline，避免宽限刚到期时先打 offline 再打 online */
    if (g_uolLinkLost) {
        wasOffline = 1;
    } else if (g_uolLastDownlink != 0) {
        wasOffline = (now >= g_uolLastDownlink) &&
            ((time_t)(now - g_uolLastDownlink) >= (time_t)UPLINK_OFFLINE_IDLE_SEC);
    } else if (g_uolBootTime != 0) {
        wasOffline = (now >= g_uolBootTime) &&
            ((time_t)(now - g_uolBootTime) >= (time_t)UPLINK_OFFLINE_IDLE_SEC);
    } else {
        wasOffline = 1;
    }

    g_uolLinkLost = 0;
    g_uolLastDownlink = now;
    g_uolBootGraceOfflineLogged = 0;
    g_uolFlushPending = 1;
    if (wasOffline) {
        LOG(LOG_WARNING, "uol online (web ack/downlink)");
    }
}

void UplinkOffline_OnLinkLost(void)
{
    if (!g_uolLinkLost) {
        LOG(LOG_WARNING, "uol offline (link lost)");
    }
    g_uolLinkLost = 1;
    g_uolLastDownlink = 0;
    g_uolFlushPending = 0;
}

/* 读出队首到 out，并从文件删除队首；返回 1 有数据，0 空 */
static int uol_pop_head(UolRecHead *outHead, uint8_t *outPayload)
{
    FILE *fp;
    FILE *tmp;
    char tmpPath[MAX_PATH + 8];
    UolRecHead head;
    uint8_t payload[UOL_MAX_PAYLOAD];
    int first = 1;
    int got = 0;

    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", g_uolPath);
    fp = fopen(g_uolPath, "rb");
    if (fp == NULL) {
        return 0;
    }
    tmp = fopen(tmpPath, "wb");
    if (tmp == NULL) {
        fclose(fp);
        return 0;
    }

    while (fread(&head, 1, sizeof(head), fp) == sizeof(head)) {
        if (head.magic != UOL_MAGIC || head.len == 0 || head.len > UOL_MAX_PAYLOAD) {
            break;
        }
        if (fread(payload, 1, head.len, fp) != head.len) {
            break;
        }
        if (first) {
            first = 0;
            *outHead = head;
            memcpy(outPayload, payload, head.len);
            got = 1;
            continue;
        }
        if (fwrite(&head, 1, sizeof(head), tmp) != sizeof(head) ||
            fwrite(payload, 1, head.len, tmp) != head.len) {
            fclose(fp);
            fclose(tmp);
            unlink(tmpPath);
            return got;
        }
    }
    fclose(fp);
    fclose(tmp);
    if (rename(tmpPath, g_uolPath) != 0) {
        unlink(tmpPath);
        return 0;
    }
    return got;
}

static int uol_send_one(const UolRecHead *head, const uint8_t *payload)
{
    int fd = -1;

    if (head->kind == UPLINK_OFFLINE_KIND_WEB) {
        SendZigbeeData(UP_EXPAND_CODE, (uint8_t *)payload, head->len, (int)head->type);
        return SUCCESS;
    }

    if ((int)head->type == DTU_TYPE) {
        fd = g_stDevicelist.UartList[g_stDevicelist.HOST_COMPUTER].i32fd;
    } else {
        fd = g_stDevicelist.UartList[g_stDevicelist.ZigBee].i32fd;
    }
    if (fd <= 0) {
        LOG(LOG_ERROR, "uol sl651 flush bad fd");
        return ERROR;
    }
    if (write(fd, payload, head->len) != (ssize_t)head->len) {
        LOG(LOG_WARNING, "uol sl651 flush write short");
        return ERROR;
    }
    return SUCCESS;
}

void UplinkOffline_FlushTick(void *arg)
{
    uint32_t i;
    uint32_t count = 0;
    UolRecHead head;
    uint8_t payload[UOL_MAX_PAYLOAD];

    (void)arg;
    /* 每秒走一次在线判定，确保开机宽限期到期有日志 */
    if (!UplinkOffline_IsOnline()) {
        return;
    }
    if (!g_uolFlushPending) {
        return;
    }
    if (OtaUpgrade_IsBusy()) {
        return;
    }

    for (i = 0; i < UOL_FLUSH_PER_TICK; i++) {
        pthread_mutex_lock(&g_uolMutex);
        if (!g_uolReady) {
            pthread_mutex_unlock(&g_uolMutex);
            return;
        }
        (void)uol_scan(&count, NULL);
        if (count == 0) {
            g_uolFlushPending = 0;
            pthread_mutex_unlock(&g_uolMutex);
            return;
        }
        if (!uol_pop_head(&head, payload)) {
            g_uolFlushPending = 0;
            pthread_mutex_unlock(&g_uolMutex);
            return;
        }
        pthread_mutex_unlock(&g_uolMutex);

        (void)uol_send_one(&head, payload);
        LOG(LOG_DEBUG, "uol flush kind=%u cmd=0x%x len=%u",
            (unsigned)head.kind, (unsigned)head.cmd, (unsigned)head.len);
    }
}
