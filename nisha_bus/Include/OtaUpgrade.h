#ifndef _OTA_UPGRADE_H
#define _OTA_UPGRADE_H

/**
 * OTA 远程升级（与 Doppler 同协议：F1 下行 102 / F2 上行 120、13）
 * 帧头定义仅服务于 OTA 收发，不是独立业务模块。
 */
#include "Common.h"
#include "ota.h"

#ifndef HEAD1
#define HEAD1 0x7E
#endif
#ifndef HEAD2
#define HEAD2 0x7E
#endif
#ifndef STX
#define STX 0x02
#endif
#ifndef ETX
#define ETX 0x03
#endif

#define UP_EXPAND_CODE   0xF2
#define DOWN_EXPAND_CODE 0xF1
#define MAX_SOFTWARE_SIZE (2 * 1024 * 1024)

#define SERVER_FIRMWARE_UPDATE 102
#define REPORT_UPDATE_SOFT_STATE  13
#define REPORT_OTA_RESULT        REPORT_UPDATE_SOFT_STATE
#define REPORT_REPLY_MSG         120

#define OTA_FW102_CHUNK_MAX   2048U
#define OTA_SLOT_SIZE_MAX     MAX_SOFTWARE_SIZE

#pragma pack(push, 1)
typedef struct _OTA_BCD_TIME {
    uint8_t u8Year;
    uint8_t u8Month;
    uint8_t u8Day;
    uint8_t u8Hour;
    uint8_t u8Minutes;
    uint8_t u8Second;
} OTA_BCD_TIME;

typedef struct _OTA_UPFrameHead {
    uint16_t u16SOH;
    uint8_t u8CentreStaID;
    uint8_t aRTUID[5];
    uint16_t u16Password;
    uint8_t u8FunctionCode;
    uint16_t u16FlagAndLen;
    uint8_t u8DataStart;
    uint16_t u16FrameSeri;
    OTA_BCD_TIME BCD_time;
    uint8_t u8Signature;
} OTA_UPFrameHead;

typedef struct _OTA_DOWNFrameHead {
    uint16_t u16SOH;
    uint8_t aRTUID[5];
    uint8_t u8CentreStaID;
    uint16_t u16Password;
    uint8_t u8FunctionCode;
    uint16_t u16FlagAndLen;
    uint8_t u8DataStart;
    uint16_t u16FrameSeri;
    OTA_BCD_TIME BCD_time;
    uint8_t u8Signature;
} OTA_DOWNFrameHead;

typedef struct {
    uint8_t u8EOF;
    uint16_t u16CRC;
} OTA_FrameTail;
#pragma pack(pop)

typedef ota_fw102_ack_wire_t Firmware102AckWire;

int OtaUpgrade_Init(void);
/* 新固件跑起来后调用，清除 boot_pending，避免被当成失败回滚 */
void OtaUpgrade_ConfirmBoot(void);
int OtaUpgrade_HandleDownlink(uint8_t u8Command, const uint8_t *pBody, uint16_t u16BodyLen);
int OtaUpgrade_IsBusy(void);
int OtaUpgrade_GetProgress(uint16_t *pkt_expect, uint16_t *pkt_total, uint32_t *recv_size);
int OtaUpgrade_KickIfStalled(uint32_t stall_ms);

/* 供触摸屏刷新 OTA 全屏页 */
void OtaUpgrade_QueryUiStatus(ota_ui_status_t *out);

/* DTU 上解析 OTA(F1/F2) 帧；升降控制帧返回未消费交给 SL651 */
int OtaUpgrade_RecDataProc(uint8_t *recvBuffer, uint16_t recvLen, WORD *wUsed);

/* OTA ACK/结果上行；补发模块也可复用写 DTU */
uint16_t OtaUpgrade_SendReport(uint8_t report_cmd, uint8_t *pData, uint16_t datalen);
int OtaUpgrade_DtuWrite(const uint8_t *data, int len);

#endif
