#ifndef SOCKET_LINK_H
#define SOCKET_LINK_H

#include "Common.h"

#define WEB_LINK_MODE_DTU    0
#define WEB_LINK_MODE_SOCKET 1

/* 网页/中心站链路：0=4G_DTU 串口，1=TCP Socket（GT6606 板载 4G usb0 / eth0） */
extern uint8_t g_u8WebLinkMode;
extern char g_szSocketServerIP[64];
extern uint16_t g_u16SocketServerPort;

void WebLink_LoadConfig(void);
int WebLink_IsSocketMode(void);
/* 按 WebLinkMode 发送；成功返回已发送长度，失败 -1 */
int WebLink_Send(const uint8_t *pData, int length);
/* Socket 已连接，或 DTU 串口 fd 可用 */
int WebLink_IsReady(void);

void SocketLink_Start(void);
int SocketLink_WaitConnected(uint32_t timeoutMs);
int SocketLink_Send(const uint8_t *pData, int length);
int SocketLink_IsConnected(void);
unsigned int SocketLink_RxQueued(void);

#endif
