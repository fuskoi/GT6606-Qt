#ifndef __TIMERTASK_H
#define __TIMERTASK_H

#include "Common.h"
#include "WebSeriver.h"
//#include "mul_wheel_timer.h"
void StartRtcTimer(void);
void *TimerFunction(void *arg);
void SigactionInit(void);
void TimerInit(void);
void TimerTask(void * arg);
time_t GetSysTime();
struct tm GetCurTimeStr(char *pTimestr);
extern uint16_t g_flag_10ms;
extern uint8_t g_u8DealFlag;
extern uint8_t g_AutoMeasureFlag ;
extern uint8_t g_StartMeaFlag;

#endif