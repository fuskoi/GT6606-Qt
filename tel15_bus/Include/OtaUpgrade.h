#ifndef _OTA_UPGRADE_H
#define _OTA_UPGRADE_H

#include "Common.h"
#include "WebSeriver.h"
#include "ota.h"

/*
 * 远程升级按《WEB服务器和接收服务器协议》：
 *   下行 UPDATE_SOFTWARE = 26（子设备+序号+文件大小+内容，≤8192/包）
 *   上行 REPORT_OTA_UPGRADE = 0xFFFFFFE4（4 字节命令字）
 *       站码12 + 升级类型1 + 升级状态1 + 升级序号2
 * libota 仅负责落盘/A-B 切槽。
 *
 * SL651 单帧 FlagAndLen 仅 12bit：一帧业务净荷约 ≤4KB。
 * 文档 8192 需网页走 ETB/ETX 多帧；设备侧 MultiFrameProc 拼包后再进本接口。
 */
#define SERVER_FIRMWARE_UPDATE  ((uint8_t)UPDATE_SOFTWARE)  /* 26 */

/* libota 内部回调标记（适配层会转成 REPORT_OTA_UPGRADE，不对外发这些命令字） */
#define REPORT_OTA_RESULT        13
#define REPORT_REPLY_MSG         120

/* 协议文档：每包传输数据不大于 8192（多帧拼包后） */
#define OTA_CHUNK_MAX         8192U
/* 单帧 STX 安全上限（含头尾余量，避免 FlagAndLen 溢出） */
#define OTA_CHUNK_SINGLE_MAX  4000U
#define OTA_FW102_CHUNK_MAX   OTA_CHUNK_MAX /* 兼容旧名 */
#define OTA_SLOT_SIZE_MAX     MAX_SOFTWARE_SIZE

/* 协议文档错误码（上报执行结果.错误码） */
#define PROTO_ERR_BUSY           8U   /* 接收指令，正在执行 */
#define PROTO_ERR_TIMEOUT        9U   /* 处理超时 */
#define PROTO_ERR_PARAM         10U   /* 参数错误 */
#define PROTO_ERR_FILE_OPEN     11U   /* 文件打开错误 */
#define PROTO_ERR_FILE_WRITE    12U   /* 文件写入错误 */
#define PROTO_ERR_FRAME_SERI    15U   /* 帧序号错误 */
#define PROTO_ERR_DATA_LEN      16U   /* 数据长度错误 */

int OtaUpgrade_Init(void);
/* 新固件跑起来后调用，清除 boot_pending，避免被当成失败回滚 */
void OtaUpgrade_ConfirmBoot(void);
int OtaUpgrade_HandleDownlink(uint8_t u8Command, const uint8_t *pBody, uint16_t u16BodyLen);
/* 文档格式包：子设备+序号+文件大小+内容（命令字 26 正文）
 * 返回 SUCCESS(0)；失败返回协议错误码(>0) */
int OtaUpgrade_HandleDocPacket(uint32_t u32CommandSeri, int deviceType,
                               const uint8_t *pData, uint16_t u16DataLen);
/* OTA 内部 err → 协议错误码（兼容） */
uint8_t OtaUpgrade_MapProtoErr(uint16_t ota_err);
/* 下一帧升级结果回显最近下行 SL651 流水号 */
void OtaUpgrade_PrepareResultEcho(void);
/* 最近一包序号（供早期失败上报升级序号） */
uint16_t OtaUpgrade_GetLastPkt(void);
int OtaUpgrade_IsBusy(void);
int OtaUpgrade_GetProgress(uint16_t *pkt_expect, uint16_t *pkt_total, uint32_t *recv_size);
int OtaUpgrade_KickIfStalled(uint32_t stall_ms);

#endif
