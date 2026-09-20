#include "Timertask.h"
#include "RS485_ICD.h"
#include "Encoder.h"
#include "log.h"
uint16_t g_flag_10ms = 0;//10ms标志
uint8_t g_AutoMeasureFlag = 0;//自动测量标志
uint8_t g_StartMeaFlag = 0; //开始测量标志

/*
*函数名称:StartRtcTimer
*函数功能:创建定时器线程
*输   入:无
*输   出:无
*返   回:无
*/
void StartRtcTimer(void)
{
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, TimerFunction, NULL);
    pthread_detach(thread_id);
}



/*
*函数名称:TimerFunction
*函数功能:定时器功能函数
*输   入:无
*输   出:无
*返   回:无
*/
void *TimerFunction(void *arg)
{
    SigactionInit();
    TimerInit();
    printf("Timer Funciton Init Complete\n");
    while (1)
    {
        sleep(2);
    }
}


/*
*函数名称:SigactionInit
*函数功能:闹钟信号初始化函数
*输   入:无
*输   出:无
*返   回:无
*/
void SigactionInit(void)
{
    struct sigaction act;
    act.sa_handler = (void *)TimerTask;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGALRM, &act, NULL);
}

/*
*函数名称:TimerInit
*函数功能:定时器初始化
*输   入:无
*输   出:无
*返   回:无
*/
void TimerInit(void)
{
    struct itimerval value;
    value.it_value.tv_sec = 0;  //定时间隔（秒）
    value.it_value.tv_usec = 10000;  //定时间隔（微秒）

    value.it_interval = value.it_value;

    setitimer(ITIMER_REAL, &value, NULL);
}

/*
*函数名称:TimerTask
*函数功能:定时器执行任务
*输   入:无
*输   出:无
*返   回:无
*/
void TimerTask(void * arg)
{
    
	struct tm t_tm;
	int i = 0;
    char timestr[512] = {0};
    memset(&t_tm,0,sizeof(t_tm)); 
    t_tm = GetCurTimeStr(timestr);
    printf("%s\n",timestr);
    g_ChoosedMode = GetMaeasureMode(g_stAutoMeasure);
    if(0 != t_tm.tm_year) // 获取到当前时间，年不为0，
    {
        for(i = 0;i< 12;i++)
        {
            if(g_ChoosedMode.stMeaModeHead.au16AutoMeaTime[i] == (t_tm.tm_hour << 8)  + t_tm.tm_min  && (g_ChoosedMode.stMeaModeHead.au16AutoMeaTime[i] & 0x00ff) != 0x18)//到了自动测量时间
            {
                LOG(LOG_DEBUG,"%s,Start AutoMeasure!",timestr);
                
                g_AutoMeasureFlag = 1;//置自动测量标志
                g_StartMeaFlag = 1;
            }
        }
    }


}



/*
*函数名称:GetSysTime
*函数功能:获取系统时间
*输   入:无
*输   出:无
*返   回:time_t 时间
*/
time_t GetSysTime()
{
    time_t timer; // long int
    time(&timer);
    return timer;
}


/*
*函数名称:GetCurTimeStr
*函数功能:获取系统时间字符串
*输   入:无
*输   出:char *pTimestr  时间字符串
*返   回:无
*/
struct tm GetCurTimeStr(char *pTimestr)
{
    struct tm t_tm;
    time_t sec;
    sec = GetSysTime();
    localtime_r(&sec, &t_tm);
    sprintf(pTimestr, "%d-%02d-%02d %02d:%02d:%02d", t_tm.tm_year + 1900, t_tm.tm_mon + 1, t_tm.tm_mday,
            t_tm.tm_hour, t_tm.tm_min, t_tm.tm_sec);
	return t_tm;
}

