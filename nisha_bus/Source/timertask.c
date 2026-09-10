#define _XOPEN_SOURCE 700
#include "timertask.h"
#include "OtaUpgrade.h"
#include "UplinkOffline.h"

time_t g_timeNow;              // 当前时间 每秒更新
time_t g_taskEndTime;          // 任务结束时间 (执行任务接收到数据为一次任务结束)
struct tm g_local_timeNow;     // 转换了时区的当前时间
bool g_voltage_update = false; // 上传电压的标志位
volatile uint32_t global_tick = 0;
int g_dest_waterweight = 0;
struct timespec current_time;
pthread_mutex_t g_time_lock = PTHREAD_MUTEX_INITIALIZER; // 互斥锁
timer_t timerid;
void StartRtcTimer(void)
{
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, TimerFunction, NULL);
    pthread_detach(thread_id);
}

void *TimerFunction(void *arg)
{
    SigactionInit();
    TimerInit();
    ts_printf("Timer Funciton Init Complete\n");
    ts_printf("\n");
    ts_printf("==========================================================================================\n");
    ts_printf("\n");
    while (1)
    {
        sleep(1);
    }
}

void SigactionInit(void)
{
    struct sigaction act;
    act.sa_handler = (void *)TimerTask;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGALRM, &act, NULL);
}

void TimerInit(void)
{
    struct itimerval value;
    value.it_value.tv_sec = 0;
    value.it_value.tv_usec = 100000; // 100ms - 100000

    value.it_interval = value.it_value;

    setitimer(ITIMER_REAL, &value, NULL);
}

// 判断当前时间是否为整点（分钟和秒均为0）
int is_hour_align()
{
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);
    return (timeinfo->tm_min == 0) && (timeinfo->tm_sec == 0);
}

#define BASE_INTERVAL 100 // 定义基础时间间隔，单位ms
void TimerTask(void)
{
    global_tick++;
    task_item_t tempTask;

    if (global_tick % (900 / BASE_INTERVAL) == 0) // 每900ms执行一次的任务
    {
        if ((time(NULL) - web_current_time_sec) > g_web_control_run_time && g_web_control_run_time > 0) // 用于网页停止电机倒计时
        {
            NiShaUD_Stop_Running_Task();
            NiShaUD_Task_Mode_Manual_Stop(NULL);
            g_web_control_run_time = 0;
        }
    }

    if (global_tick % (500 / BASE_INTERVAL) == 0)
    { // 每500ms执行一次的任务
        // 0x300D
        // 查询DI1口寄存器的值
        //SubmitDcMotorTask(DC_MOTOR_GET_DI1);
        // 查询DI2口寄存器的值
        //SubmitDcMotorTask(DC_MOTOR_GET_DI2);
    }

    if (global_tick % (1000 / BASE_INTERVAL) == 0)
    { // 每1000ms执行一次的任务

        (void)OtaUpgrade_KickIfStalled(4000U);
        UplinkOffline_FlushTick(NULL);

        // 1小时执行一次均匀报
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm *tm = localtime(&ts.tv_sec);
        // 500ms检查时间，均匀报应在整点发送
        if (tm->tm_min == 0x00 && tm->tm_sec == 0x00)
        {
            if (!OtaUpgrade_IsBusy()) {
                sl651_send_hour_report(&g_stM2UpFramepara);
            }
        }

        GetTimeNow();
        // ts_printf("1 second times up\n");

        if (GetNiShaUD_Task_State() == SedimentUD_IDLE && (get_nisha_task_mode() == SedimentUD_Mode_VDIM_Measurement || get_nisha_task_mode() == SedimentUD_Mode_VMM_Measurement))
        {
            time_t current_time_sec = time(NULL);
            if (current_time_sec >= get_mode_one_work_end_time() + 300 || current_time_sec >= get_mode_two_work_end_time() + 300)
            {
                // 如果当前时间超过了模式一或模式二的工作结束时间加上5分钟，切换为默认模式
                set_default_mode_para_depth(get_default_mode_keep_depth());
                NiShaUD_Default_Mode_Start_Task(NULL);
            }
        }

        if (GetNiShaUD_Task_State() == SedimentUD_IDLE && (get_nisha_task_mode() == SedimentUD_Mode_Default))
        {
            time_t current_time_sec = time(NULL);
            time_t end_time = get_mode_default_work_end_time();
            // ts_printf("next default mode calibration time: %d\n", current_time_sec - get_mode_default_work_end_time());
            if (end_time > 0 && current_time_sec >= end_time + 3600)
            {
                // 每小时校准一次设备所在深度
                ts_printf("[start default mode calibration]\n");
                set_default_mode_para_depth(get_default_mode_keep_depth());
                NiShaUD_Default_Mode_Start_Task(NULL);
            }
        }
    }
    if (global_tick % (3000 / BASE_INTERVAL) == 0)
    {
        // SubmitDepthSensorTask(DEPTH_GET_DEPTH_WTEMP_WPRE);
        // SubmitSedimentSensorTask(SEDIMENT_GET_SEDIMENT_VALUE);

        
    }
    if (global_tick % (6000 / BASE_INTERVAL) == 0)
    {
        if (GetNiShaUD_Task_State() == SedimentUD_IDLE && (get_nisha_task_mode() == SedimentUD_Mode_Default || get_nisha_task_mode() == SedimentUD_Mode_Manual))
        {
            if (!OtaUpgrade_IsBusy()) {
                SubmitRelayModuleHeartTask();
                SubmitSedimentSensorTask(NISHA_DEFAULT_MODE_COLLECT_DATA);
                ts_printf("6 sec\n");
            }
        }

        if (GetNiShaUD_Task_State() == SedimentUD_IDLE)
        {
            if (!OtaUpgrade_IsBusy() && get_device_index(DEPTH_SENSOR) != -1) {
                SubmitDepthSensorTask(DEPTH_GET_DEPTH_WTEMP_WPRE);
                int cur_depth = get_depth_sensor_depth();
                ts_printf("Current Depth: %d mm\n", cur_depth);
                default_mode_send_depth_to_screen(cur_depth * 0.001f);
                manual_mode_send_depth_to_screen(cur_depth * 0.001f);
            }
        }

        if (get_device_index(LORA) != -1) {
            send_sensor_state_to_screen();
        }
    }

    if (global_tick % (7000 / BASE_INTERVAL) == 0)
    {
        WatchDogControl();
    }
    if (global_tick % (30000 / BASE_INTERVAL) == 0)
    {
        // if (SubmitDcMotorTask(dc_motor_running_online_check) == 0)
        // {
        //     ts_printf("DC_MOTOR_online_OK\n");
        // } // 每隔6秒检测电机是否在线

        // // sleep_ms(80);

        // if (SubmitRelayModuleTask(relay_module_runnig_online_check) == 0)
        // {
        //     ts_printf("RELAY_MODULE_online_OK\n");
        // } // 每隔6秒检测继电器是否在线
        if (is_current_hour_and_minute(8, 2))
        {
        }
    }

    if (global_tick % (72000 / BASE_INTERVAL) == 0)
    {
        // sl651_send_hour_report(&g_stM2UpFramepara); // 72秒发一次均匀报，测试用，后面要注释掉
        // ts_printf("send hour frame finished\n");
    }

    if (global_tick % (600000 / BASE_INTERVAL) == 0)
    {
        if (get_depth_sensor_depth() >= 300)
        {
            SubmitSedimentSensorTask(SEDIMENT_CLEAR_SEDIMENT_SENSOR);
        }
    }
    if (global_tick % (3600000 / BASE_INTERVAL) == 0)
    {
    }
    if (global_tick >= 7200000) // 大于两个小时，计数器清0
        global_tick = 0;
}

void GetCurrentTime(char *times)
{
    time_t rawTime;
    struct tm *timeInfo;
    // 获取当前时间
    time(&rawTime);
    // 将当前时间转换为本地时间
    timeInfo = localtime(&rawTime);
    // 将时间格式化为字符串，保存到timeString
    strftime(times, 20, "%Y-%m-%d %H:%M:%S", timeInfo);
}

void GetTimeNow(void)
{
    struct tm *local_time;
    time(&g_timeNow);
    local_time = localtime(&g_timeNow);
    memcpy(&g_local_timeNow, local_time, sizeof(struct tm));
}
void Restart()
{
    ts_printf("run restart\r\n");
    sync();
    reboot(0x01234567);
    return;
}

volatile sig_atomic_t interrupted_by_signal = 0;

void signal_handler(int signum)
{
    interrupted_by_signal = signum;
}

// 测试信号中断时使用
void setup_signal_handlers()
{
    // 设置信号处理函数
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    // sigaction(SIGHUP, &sa, NULL);  // 1
    // sigaction(SIGINT, &sa, NULL);  // 2
    // sigaction(SIGQUIT, &sa, NULL); // 3
    // sigaction(SIGILL, &sa, NULL);  // 4
    // sigaction(SIGABRT, &sa, NULL); // 6
    // sigaction(SIGFPE, &sa, NULL);  // 8
    // 注意 SIGKILL (9) 和 SIGSTOP (19) 不能被捕获
    sigaction(SIGSEGV, &sa, NULL); // 11
    // 可以根据需要添加更多信号
}

// 校准处理函数（由外部调用）
void calibration_handler(struct timespec new_time)
{
    pthread_mutex_lock(&g_time_lock);
    current_time = new_time; // 直接设置校准时间
    pthread_mutex_unlock(&g_time_lock);
}

// 创建并初始化定时器
void create_timer()
{
    struct sigevent sev = {0}; // 初始化所有成员为 0

    sev.sigev_notify = SIGEV_THREAD; // 使用线程回调
    sev.sigev_notify_function = timer_callback;
    sev.sigev_value.sival_ptr = &timerid; // 传递 timerid
    sev.sigev_notify_attributes = NULL;   // 显式设置线程属性为 NULL

    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1)
    {
        perror("timer_create");
        exit(EXIT_FAILURE);
    }
}

// 初始化当前时间为系统当前时间
void init_current_time()
{
    clock_gettime(CLOCK_REALTIME, &current_time);
    // 调试输出
    // struct tm *tm_info = localtime(&current_time.tv_sec);
    // char buffer[50];
    // strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", tm_info);
    // ts_printf("Initialized time: %s\n", buffer);
}

// 设置定时器为每秒触发
void set_timer_interval()
{
    struct itimerspec its;
    its.it_value.tv_sec = 1; // 第一次触发时间（1秒后）
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = 1; // 后续触发间隔（每秒）
    its.it_interval.tv_nsec = 0;

    if (timer_settime(timerid, 0, &its, NULL) == -1)
    {
        perror("timer_settime");
        exit(EXIT_FAILURE);
    }
}
// 定时器回调函数（每秒触发）
void timer_callback(union sigval sv)
{
    // ts_printf("Timer callback executed.\n");
    pthread_mutex_lock(&g_time_lock);
    // 更新逻辑时间：秒数+1，纳秒归零
    current_time.tv_sec += 1;
    current_time.tv_nsec = 0;

    // // 显示本地时间（根据系统时区）
    // struct tm *tm_info = localtime(&current_time.tv_sec); // 使用 localtime
    // char buffer[50];
    // strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S %Z", tm_info);
    // ts_printf("Current time: %s.%09ld\n", buffer, current_time.tv_nsec);

    pthread_mutex_unlock(&g_time_lock);
}

// 新增：获取当前逻辑时间的接口
void get_current_time(struct timespec *time)
{
    pthread_mutex_lock(&g_time_lock);
    *time = current_time; // 复制当前时间
    pthread_mutex_unlock(&g_time_lock);
}

// 模拟校准请求（主循环）
void start_calibration_simulation()
{
    while (1)
    {
        sleep(5); // 每5秒触发一次校准
        struct timespec new_time;
        clock_gettime(CLOCK_REALTIME, &new_time);
        new_time.tv_sec += 10; // 校准时间比当前系统时间多10秒
        calibration_handler(new_time);
    }
}

/**
 * 判断当前时间是否为指定的分钟
 * @param minute 指定的分钟数 (0-59)
 * @return 匹配返回 true, 否则返回 false
 */
bool is_current_minute(int minute)
{
    if (minute < 0 || minute > 59)
    {
        // 输入的分钟数无效
        return false;
    }

    struct timespec current_ts;
    get_current_time(&current_ts);

    struct tm time_info;
    localtime_r(&current_ts.tv_sec, &time_info); // 安全地转换时间

    return time_info.tm_min == minute;
}

/**
 * 判断当前时间是否为指定的小时和分钟
 * @param hour 指定的小时数 (0-23)
 * @param minute 指定的分钟数 (0-59)
 * @return 匹配返回 true, 否则返回 false
 */
bool is_current_hour_and_minute(int hour, int minute)
{
    // 验证输入的有效性
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
    {
        return false;
    }

    struct timespec current_ts;
    get_current_time(&current_ts);

    struct tm time_info;
    localtime_r(&current_ts.tv_sec, &time_info); // 安全地转换时间

    return (time_info.tm_hour == hour) && (time_info.tm_min == minute);
}

/**
 * @brief 判断当前时间是否是每小时的 0、5、10、15、...、55 分钟的整点时刻
 *
 * @return int 1 表示到了指定时间点；0 表示否
 */
int is_time_for_five_minute_mark(void)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    // 判断当前分钟是5的倍数，并且秒数为0
    if (tm_info->tm_sec == 0 && (tm_info->tm_min % 5) == 0)
    {
        return 1;
    }

    return 0;
}
