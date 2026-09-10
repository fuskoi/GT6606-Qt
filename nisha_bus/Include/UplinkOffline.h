/**
 * 断网写盘白名单：仅 SL651 均匀报/小时报(0x31) 整帧。
 * 定时报 0x32、扩展上报等不落盘。
 * 在线判定：开机默认在线；UPLINK_OFFLINE_IDLE_SEC 内无下行则离线；
 * 收到下行后续命。无 TCP，靠下行空闲超时。
 */
#ifndef UPLINK_OFFLINE_H
#define UPLINK_OFFLINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 6 分钟：覆盖约 5 分钟定时报周期，略留余量 */
#define UPLINK_OFFLINE_IDLE_SEC   360U
#define UPLINK_OFFLINE_KIND_WEB   0U
#define UPLINK_OFFLINE_KIND_SL651 1U     /* SL651 0x31 整帧 */

int UplinkOffline_Init(void);
int UplinkOffline_IsOnline(void);
int UplinkOffline_ShouldPersist(uint32_t cmd);
int UplinkOffline_Enqueue(uint8_t kind, uint32_t cmd,
                          const uint8_t *data, uint16_t len, int type);
void UplinkOffline_OnDownlink(void);
void UplinkOffline_OnLinkLost(void);
void UplinkOffline_FlushTick(void *arg);

#ifdef __cplusplus
}
#endif

#endif
