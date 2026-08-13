/**
 * FLOW_JET_LAG(121) 与 SL651 小时报：离线或发送失败时落盘，恢复后补发。
 * 在线判定：
 *   - 开机宽限 / 无下行超时：均为 UPLINK_OFFLINE_IDLE_SEC
 *   - 收到网页确认/下行 → 在线；其后空闲超时 → 离线
 *   - TCP 断开 → 立即离线（取消宽限）
 * 落盘条件：当前离线（无网页应答），或本地发送失败。
 * SL651 定时报及其余网页上报不缓存。
 */
#ifndef UPLINK_OFFLINE_H
#define UPLINK_OFFLINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UPLINK_OFFLINE_IDLE_SEC   300U   /* 开机宽限 / 无下行判离线（秒），5 分钟 */
#define UPLINK_OFFLINE_KIND_WEB   0U     /* SendUpLinkToServer 业务体 */
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

#ifdef __cplusplus
}
#endif

#endif /* UPLINK_OFFLINE_H */
