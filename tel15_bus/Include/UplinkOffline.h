/**
 * 自动转子测流（带层数）与 SL651 小时报：离线或发送失败时落盘，恢复后补发。
 * 在线判定：
 *   - 开机宽限 UPLINK_OFFLINE_IDLE_SEC：尚未收到下行前先不判离线
 *   - 收到网页确认/下行 → 在线；其后空闲超时 → 离线
 *   - TCP 断开 → 立即离线（取消宽限）
 * 落盘条件：当前离线（无网页应答），或本地发送失败。
 * SL651 定时报及其余网页上报不缓存。
 */
#ifndef UPLINK_OFFLINE_H
#define UPLINK_OFFLINE_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPLINK_OFFLINE_IDLE_SEC   60U    /* 开机宽限 / 无应答超时（秒） */
#define UPLINK_OFFLINE_KIND_WEB   0U     /* SendZigbeeData 业务体 */
#define UPLINK_OFFLINE_KIND_SL651 1U     /* SL651 小时报整帧 */

int UplinkOffline_Init(void);
int UplinkOffline_IsOnline(void);
int UplinkOffline_ShouldPersist(uint32_t cmd);
int UplinkOffline_Enqueue(uint8_t kind, uint32_t cmd,
                          const uint8_t *data, uint16_t len, int type);
/* 收到网页链路确认/有效下行时调用 */
void UplinkOffline_OnDownlink(void);
/* TCP/链路断开时立即离线 */
void UplinkOffline_OnLinkLost(void);
void UplinkOffline_FlushTick(void *arg);

/* main.c：待机活动时间，与在线判定无关 */
time_t GetLastActiveTime(void);

#ifdef __cplusplus
}
#endif

#endif /* UPLINK_OFFLINE_H */
