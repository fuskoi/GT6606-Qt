/**
 * VERSION_ID(12)：开机上报固件版本
 * 协议：BYTE version[4]，小端；内容为编译时刻 Unix 时间戳（秒），每编一次即变。
 */
#ifndef _FIRMWARE_VERSION_H
#define _FIRMWARE_VERSION_H

#include <stddef.h>
#include <stdint.h>

#define REPORT_VERSION_ID 12

void FirmwareVersion_Get(uint8_t out[4]);
void FirmwareVersion_Format(char *buf, size_t bufSize);
void FirmwareVersion_ReportBoot(void *arg);

#endif
