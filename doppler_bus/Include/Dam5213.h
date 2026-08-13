#ifndef DAM5213_H
#define DAM5213_H

#include "Common.h"

/* DMA/DAM-5213：寄存器1=电压；原始 UINT16 ÷1000 = 伏特 */
#define DAM5213_REG_VOLTAGE     1U
#define DAM5213_RSP_LEN         7U  /* addr+fc+byteCnt+2data+crc */

int Dam5213_Init(void);
/* 周期采集入口（定时器） */
void Dam5213_Poll(void* arg);
int Dam5213_RecDataProc(KFIFO_ID pData, WORD* pUsed);
/* 最近一次有效电压（伏特）；无效返回 ERROR */
int Dam5213_GetVoltage(float* pfVolt);
int Dam5213_IsValid(void);

#endif
