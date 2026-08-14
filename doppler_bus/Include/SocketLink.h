#ifndef SOCKET_LINK_H
#define SOCKET_LINK_H

#include "Common.h"

/*
 * 网页服务器链路：
 * - WebLinkMode=0：HOST_COMPUTER/DTU 串口（全功能）
 * - WebLinkMode=1：以太网 TCP 客户端（全功能，与 DTU 互斥）
 */
int WebLink_IsSocketMode(void);
/* 按当前 WebLinkMode 发送；成功返回已发送长度，失败返回 -1 */
int WebLink_Send(const uint8_t* pData, int length);

void SocketLink_Start(void);
/* 等待已连接或超时；已连接返回 1，超时返回 0 */
int SocketLink_WaitConnected(uint32_t timeoutMs);
int SocketLink_Send(const uint8_t* pData, int length);
int SocketLink_IsConnected(void);
/* 应用层 RX FIFO 已缓存字节数（OTA 心跳诊断用） */
unsigned int SocketLink_RxQueued(void);

#endif
