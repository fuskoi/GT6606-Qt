#ifndef _OTA_UPGRADE_H
#define _OTA_UPGRADE_H

#include "Common.h"
#include "WebSeriver.h"
#include "ota.h"

/* 网页远程升级 FIRMWARE_UPDATE */
#define SERVER_FIRMWARE_UPDATE 102

/* 102 ACK 走 F2 REPLY_MSG；切槽结果报 UPDATE_SOFT_STATE */
#define REPORT_UPDATE_SOFT_STATE  13
#define REPORT_OTA_RESULT        REPORT_UPDATE_SOFT_STATE
#define REPORT_REPLY_MSG         120

#define OTA_FW102_CHUNK_MAX   2048U
#define OTA_SLOT_SIZE_MAX     MAX_SOFTWARE_SIZE

typedef ota_fw102_ack_wire_t Firmware102AckWire;

int OtaUpgrade_Init(void);
/* 新固件跑起来后调用，清除 boot_pending，避免被当成失败回滚 */
void OtaUpgrade_ConfirmBoot(void);
int OtaUpgrade_HandleDownlink(uint8_t u8Command, const uint8_t *pBody, uint16_t u16BodyLen);
int OtaUpgrade_IsBusy(void);
int OtaUpgrade_GetProgress(uint16_t *pkt_expect, uint16_t *pkt_total, uint32_t *recv_size);
/* 停滞：1=重发 lastOk ACK，-1=超时放弃并解除 busy，0=无动作 */
int OtaUpgrade_KickIfStalled(uint32_t stall_ms);
void OtaUpgrade_QueryUiStatus(ota_ui_status_t *out);

#endif
