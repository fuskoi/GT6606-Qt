#ifndef __TIMERTASK_H__
#define __TIMERTASK_H__
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>
#include <sys/reboot.h>
#include "src_uthash.h"
#include "config.h"
#include "taskqueue.h"
#include "uart.h"

// #include "task.h"
extern time_t g_timeNow;
extern time_t g_taskEndTime;      // 任务结束时间 (执行任务接收到数据为一次任务结束)
extern struct tm g_local_timeNow; // 转换了时区的当前时间
extern bool g_voltage_update;     // 上传电压的标志位
extern volatile sig_atomic_t interrupted_by_signal;
extern int g_dest_waterweight;

void StartRtcTimer(void);
void *TimerFunction(void *arg);
void SigactionInit(void);
void TimerInit(void);
void TimerTask(void);
void GetCurrentTime(char *times);
void GetTimeNow(void);
void Restart();
void signal_handler(int signum);
void setup_signal_handlers();
void calibration_handler(struct timespec new_time);
void init_current_time(void);
void create_timer();
void set_timer_interval();
void timer_callback();
void start_calibration_simulation();
bool is_current_minute(int minute);
bool is_current_hour_and_minute(int hour, int minute);
int is_time_for_five_minute_mark(void);
void get_current_time(struct timespec *time);

#endif
