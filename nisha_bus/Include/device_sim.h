#ifndef DEVICE_SIM_H
#define DEVICE_SIM_H

#include <stdbool.h>

/* 台架联调：仅接泥沙时，模拟深度计 / 电机 / 继电器。
 * tyls.conf: SimMissingDevices=1 开启；接齐外设后改 0。
 */
void DeviceSim_LoadConfig(void);
int DeviceSim_Enabled(void);
int DeviceSim_Depth(void);
int DeviceSim_Motor(void);
int DeviceSim_Relay(void);

/* 自检前调用：把模拟设备标为在线，并初始化模拟深度 */
void DeviceSim_ApplyOnline(void);

/* Submit* 命中模拟时返回 1（已处理），否则返回 0 走真实串口 */
int DeviceSim_SubmitDepth(int taskName);
int DeviceSim_SubmitMotor(int taskName);
int DeviceSim_SubmitRelay(int taskName);

#endif
