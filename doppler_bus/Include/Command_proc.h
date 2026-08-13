#ifndef _COMMAND_PROC_H
#define _COMMAND_PROC_H

/*
 * DTU/网页服务器上下行通信（HOST_COMPUTER 串口）
 *
 * 扩展 651 规约：F2 上行 / F1 下行；BCD 时间后第 1 字节为指令字，后跟业务数据。
 * SendUpLinkToServer(u8FunctionCode, u8CommandCode, pData, datalen, type)
 *   - u8CommandCode 写入 u8Signature
 * 上行新格式：业务数据后直接 03 + CRC（无 0xFFFFFFFF 帧尾）。
 * 上行 CRC 小端写入（低字节在前）；下行 CRC 大端校验。
 */
#include "Common.h"
#include "WebSeriver.h"

extern uint32_t g_u32CommansSeri;
/* 分配上行流水号：从 1 起，跳过 0（平台常拒） */
uint16_t AllocFrameSeri(void);

int Proc_DTU_RecData(KFIFO_ID pData, WORD* pUsed, int type);
/* 返回本帧流水号；失败返回 0xFFFF */
uint16_t SendUpLinkToServer(uint8_t u8FunctionCode, uint8_t u8CommandCode,
                            uint8_t* pData, uint16_t datalen, int type);
void SendDopplerRealData(void* arg);
/* 收到 DATA_RESPONSE(150) 时按净荷校验并通知取日志停等上传线程 */
void RetrieveLogs_NotifyDataResponse(const uint8_t* pBody, uint16_t u16BodyLen);

#endif
