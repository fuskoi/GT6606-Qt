#include "ReportAck.h"
#include "Command_proc.h"
#include "WebSeriver.h"
#include "SectionParam.h"
#include "OtaUpgrade.h"
#include "log.h"
#include <string.h>
#include <time.h>

#define REPORT_ACK_SLOT_MAX 4

typedef struct ReportAckSlot
{
    uint8_t u8Used;
    uint8_t u8Cmd;
    uint16_t u16Seri;
    uint16_t u16BodyLen;
    uint8_t au8Body[REPORT_ACK_BODY_MAX];
    time_t tLastSend;
} ReportAckSlot;

static ReportAckSlot g_astSlots[REPORT_ACK_SLOT_MAX];
static pthread_mutex_t g_stAckMutex = PTHREAD_MUTEX_INITIALIZER;

static int report_ack_need_confirm(uint8_t u8Cmd)
{
    switch (u8Cmd) {
    case REPORT_FLOW_JET_LAG_CMD:
    case REPORT_DEVICE_TIMEOUT_CMD:
    case REPORT_DUANMIAN_ID:
        return 1;
    default:
        return 0;
    }
}

static ReportAckSlot* report_ack_find_cmd(uint8_t u8Cmd)
{
    uint8_t i = 0;
    for (i = 0; i < REPORT_ACK_SLOT_MAX; i++) {
        if (g_astSlots[i].u8Used && g_astSlots[i].u8Cmd == u8Cmd) {
            return &g_astSlots[i];
        }
    }
    return NULL;
}

static ReportAckSlot* report_ack_alloc(uint8_t u8Cmd)
{
    ReportAckSlot* p = report_ack_find_cmd(u8Cmd);
    uint8_t i = 0;

    if (p != NULL) {
        return p;
    }
    for (i = 0; i < REPORT_ACK_SLOT_MAX; i++) {
        if (!g_astSlots[i].u8Used) {
            return &g_astSlots[i];
        }
    }
    /* 槽满：覆盖最旧 */
    {
        time_t tOldest = g_astSlots[0].tLastSend;
        ReportAckSlot* pOldest = &g_astSlots[0];
        for (i = 1; i < REPORT_ACK_SLOT_MAX; i++) {
            if (g_astSlots[i].tLastSend < tOldest) {
                tOldest = g_astSlots[i].tLastSend;
                pOldest = &g_astSlots[i];
            }
        }
        return pOldest;
    }
}

static void report_ack_clear_slot(ReportAckSlot* p)
{
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
}

static int report_ack_do_send(ReportAckSlot* p)
{
    uint16_t u16Seri = 0;

    if (p == NULL || !p->u8Used) {
        return ERROR;
    }
    if (OtaUpgrade_IsBusy()) {
        return ERROR;
    }

    u16Seri = SendUpLinkToServer(UP_EXPAND_CODE, p->u8Cmd, p->au8Body, p->u16BodyLen, DTU_TYPE);
    if (u16Seri == 0xFFFFU) {
        return ERROR;
    }
    p->u16Seri = u16Seri;
    p->tLastSend = time(NULL);
    LOG(LOG_DEBUG, "report ack pending: cmd=%u seri=%u bodyLen=%u",
        (unsigned)p->u8Cmd, (unsigned)p->u16Seri, (unsigned)p->u16BodyLen);
    return SUCCESS;
}

void ReportAck_Init(void)
{
    memset(g_astSlots, 0, sizeof(g_astSlots));
}

void ReportAck_Send(uint8_t u8Cmd, const uint8_t* pBody, uint16_t u16BodyLen)
{
    ReportAckSlot* p = NULL;

    if (pBody == NULL || u16BodyLen == 0 || u16BodyLen > REPORT_ACK_BODY_MAX) {
        return;
    }
    if (!report_ack_need_confirm(u8Cmd)) {
        (void)SendUpLinkToServer(UP_EXPAND_CODE, u8Cmd, (uint8_t*)pBody, u16BodyLen, DTU_TYPE);
        return;
    }

    pthread_mutex_lock(&g_stAckMutex);
    p = report_ack_alloc(u8Cmd);
    memset(p, 0, sizeof(*p));
    p->u8Used = 1;
    p->u8Cmd = u8Cmd;
    p->u16BodyLen = u16BodyLen;
    memcpy(p->au8Body, pBody, u16BodyLen);
    (void)report_ack_do_send(p);
    pthread_mutex_unlock(&g_stAckMutex);
}

void ReportAck_OnLinkConfirm(uint16_t u16Seri)
{
    uint8_t i = 0;
    int matched = 0;
    ReportAckSlot* pLatest = NULL;

    pthread_mutex_lock(&g_stAckMutex);
    for (i = 0; i < REPORT_ACK_SLOT_MAX; i++) {
        if (!g_astSlots[i].u8Used) {
            continue;
        }
        if (g_astSlots[i].u16Seri == u16Seri) {
            LOG(LOG_DEBUG, "report ack confirmed: cmd=%u seri=%u",
                (unsigned)g_astSlots[i].u8Cmd, (unsigned)u16Seri);
            report_ack_clear_slot(&g_astSlots[i]);
            matched = 1;
            break;
        }
        if (pLatest == NULL || g_astSlots[i].tLastSend >= pLatest->tLastSend) {
            pLatest = &g_astSlots[i];
        }
    }
    /* 流水号对不上时：仅有一条待确认则仍视为确认（兼容平台短确认） */
    if (!matched && pLatest != NULL) {
        uint8_t u8Pending = 0;
        for (i = 0; i < REPORT_ACK_SLOT_MAX; i++) {
            if (g_astSlots[i].u8Used) {
                u8Pending++;
            }
        }
        if (u8Pending == 1U) {
            LOG(LOG_DEBUG, "report ack confirmed(any): cmd=%u expectSeri=%u gotSeri=%u",
                (unsigned)pLatest->u8Cmd, (unsigned)pLatest->u16Seri, (unsigned)u16Seri);
            report_ack_clear_slot(pLatest);
        }
    }
    pthread_mutex_unlock(&g_stAckMutex);
}

void ReportAck_OnDataResponse(const uint8_t* pBody, uint16_t u16BodyLen)
{
    uint8_t u8AckCmd = 0;
    ReportAckSlot* p = NULL;

    /*
     * 150：服务器对设备「每一包」上行数据的回复（对称于 OTA 每包 ACK）。
     * 净荷常见第 1 字节为被确认的上行命令字（121/1/11/122…）。
     * 122 取日志分包 ACK：净荷为 122+设备类型+日志级别+包序号，由
     * RetrieveLogs_NotifyDataResponse 校验；此处不占 ReportAck 槽。
     */
    if (pBody != NULL && u16BodyLen >= 1U) {
        u8AckCmd = pBody[0];
    }

    if (u8AckCmd == (uint8_t)REPORT_RETRIEVE_LOGS_RESULT) {
        return;
    }

    if (u8AckCmd == 0) {
        /* 无命令字时：兼容旧行为，清 121/1 */
        u8AckCmd = (uint8_t)REPORT_FLOW_JET_LAG_CMD;
        pthread_mutex_lock(&g_stAckMutex);
        p = report_ack_find_cmd((uint8_t)REPORT_FLOW_JET_LAG_CMD);
        if (p != NULL) {
            LOG(LOG_DEBUG, "DATA_RESPONSE(150): confirm cmd=121 seri=%u",
                (unsigned)p->u16Seri);
            report_ack_clear_slot(p);
        }
        p = report_ack_find_cmd((uint8_t)REPORT_DEVICE_TIMEOUT_CMD);
        if (p != NULL) {
            LOG(LOG_DEBUG, "DATA_RESPONSE(150): confirm cmd=1 seri=%u",
                (unsigned)p->u16Seri);
            report_ack_clear_slot(p);
        }
        pthread_mutex_unlock(&g_stAckMutex);
        return;
    }

    if (!report_ack_need_confirm(u8AckCmd)) {
        return;
    }

    pthread_mutex_lock(&g_stAckMutex);
    p = report_ack_find_cmd(u8AckCmd);
    if (p != NULL) {
        LOG(LOG_DEBUG, "DATA_RESPONSE(150): confirm cmd=%u seri=%u body_len=%u",
            (unsigned)u8AckCmd, (unsigned)p->u16Seri, (unsigned)u16BodyLen);
        report_ack_clear_slot(p);
    }
    pthread_mutex_unlock(&g_stAckMutex);
}

void ReportAck_OnDownlinkCmd(uint8_t u8DownCmd)
{
    ReportAckSlot* p = NULL;

    if (u8DownCmd != SERVER_CROSS_SECTION_NEW && u8DownCmd != SERVER_RUN_SECTION_NEW) {
        return;
    }

    pthread_mutex_lock(&g_stAckMutex);
    p = report_ack_find_cmd((uint8_t)REPORT_DUANMIAN_ID);
    if (p != NULL) {
        LOG(LOG_DEBUG, "report ack confirmed by downlink cmd=%u (section)",
            (unsigned)u8DownCmd);
        report_ack_clear_slot(p);
    }
    pthread_mutex_unlock(&g_stAckMutex);
}

int ReportAck_HasPending(uint8_t u8Cmd)
{
    int has = 0;

    pthread_mutex_lock(&g_stAckMutex);
    has = (report_ack_find_cmd(u8Cmd) != NULL) ? 1 : 0;
    pthread_mutex_unlock(&g_stAckMutex);
    return has;
}

void ReportAck_CheckRetry(void* arg)
{
    uint8_t i = 0;
    time_t now = time(NULL);

    (void)arg;
    if (OtaUpgrade_IsBusy()) {
        return;
    }

    pthread_mutex_lock(&g_stAckMutex);
    for (i = 0; i < REPORT_ACK_SLOT_MAX; i++) {
        ReportAckSlot* p = &g_astSlots[i];
        if (!p->u8Used) {
            continue;
        }
        if (p->tLastSend == 0 || (now < p->tLastSend)) {
            continue;
        }
        if ((uint32_t)(now - p->tLastSend) < REPORT_ACK_RETRY_SEC) {
            continue;
        }
        LOG(LOG_WARNING, "report ack timeout, retry cmd=%u lastSeri=%u",
            (unsigned)p->u8Cmd, (unsigned)p->u16Seri);

        /* 121：用最新快照重发；其它：原包重发 */
        if (p->u8Cmd == REPORT_FLOW_JET_LAG_CMD) {
            pthread_mutex_unlock(&g_stAckMutex);
            {
                static int s_force = 1;
                SendDopplerRealData(&s_force);
            }
            pthread_mutex_lock(&g_stAckMutex);
        } else if (p->u8Cmd == REPORT_DUANMIAN_ID) {
            pthread_mutex_unlock(&g_stAckMutex);
            SectionParam_ReportSectionId();
            pthread_mutex_lock(&g_stAckMutex);
        } else {
            (void)report_ack_do_send(p);
        }
    }
    pthread_mutex_unlock(&g_stAckMutex);
}
