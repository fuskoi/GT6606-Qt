#ifndef REPORT_ACK_H
#define REPORT_ACK_H

#include "Common.h"

/*
 * 设备主动 Web 上报（F2）需等服务器确认后才算完成：
 * - 链路短确认（F2 dataLen=8），或
 * - 下行 DATA_RESPONSE(150)：对「每一包」上报数据的回复（类似 OTA 每包 ACK）
 * 未确认则每 5 分钟重发，直到收到确认。
 * 断面请求(11)也可被下行 100/101 视为完成。
 */

#define REPORT_ACK_RETRY_SEC  300U
#define REPORT_ACK_BODY_MAX   256U

void ReportAck_Init(void);

/* 发送并登记待确认；cmd 为指令字（如 121/1/11） */
void ReportAck_Send(uint8_t u8Cmd, const uint8_t* pBody, uint16_t u16BodyLen);

/* 收到 F2 短确认时调用（流水号，大端解析后的主机序） */
void ReportAck_OnLinkConfirm(uint16_t u16Seri);

/* 下行业务应答：100/101 清除断面请求；150 按净荷确认对应上报包 */
void ReportAck_OnDownlinkCmd(uint8_t u8DownCmd);
/* DATA_RESPONSE(150) 带净荷：按被确认的上行命令字清待确认槽 */
void ReportAck_OnDataResponse(const uint8_t* pBody, uint16_t u16BodyLen);

/* 是否仍有该指令字未确认 */
int ReportAck_HasPending(uint8_t u8Cmd);

/* 定时检查：到期未确认则重发（建议 1s 调用） */
void ReportAck_CheckRetry(void* arg);

#endif
