#ifndef __TIMERTASK_H
#define __TIMERTASK_H

#include "Common.h"

void CheckWebReportAtFiveMinute(void* arg);
void ReportFlowMeterAtFiveMinute(void* arg);
void ReportFlowMeterData(void* arg);
/* 采集/上报间隔真空期内：每秒打印一次当前日期时间 */
void TickPrintDateTime(void* arg);

#endif
