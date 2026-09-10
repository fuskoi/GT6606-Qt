#ifndef __NISHATASK_H__
#define __NISHATASK_H__

#include "uart.h"

#include <math.h>
#define NISHA_FILE_NAME "keep_depth.bin"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
typedef enum
{
    SedimentUD_IDLE,         // 待机
    SedimentUD_INITIALIZING, // 初始化中
    SedimentUD_RUNNING,      // 运行中
    SedimentUD_ERROR,        // 出错(影响系统运行)

} nisha_task_state;

typedef enum
{
    SedimentUD_Mode_Default,          // 默认模式
    SedimentUD_Mode_VMM_Measurement,  // 垂线混合法测量
    SedimentUD_Mode_VDIM_Measurement, // 垂线积深法测量
    SedimentUD_Mode_Manual,           // 手动模式 只控制升降

} nisha_task_mode;

typedef enum
{
    SEC_TASK_READY,
    SEC_TASK_WAITING,
    SEC_TASK_COMPLETED,
    SEC_TASK_ERROR,
    SEC_TASK_TIMEOUT,
} nisha_sec_task_state;

typedef struct device_online
{
    /* data */
    bool sediment_sensor;
    bool depth_sensor;
    bool relay_module;
    bool encoder;
    bool dc_motor;

} device_online_status_t;

typedef struct nishadefaultmode
{
    int depth; // mm
} nisha_default_mode_para_t;

typedef struct nishamodeone
{
    int depth_1; // mm
    int depth_2; // mm
    int depth_3; // mm
    int depth_4; // mm
} nisha_mode_one_para_t;

typedef struct nishamodetwo
{
    int depth_A; // mm
    int depth_B; // mm

} nisha_mode_two_para_t;

typedef struct nishataskinfo
{
    /* data */
    bool stop_request;  //  停止任务
    int sec_task_state; // 小任务状态
    int task_mode;      // 泥沙升降模式
    int task_state;     // 泥沙升降任务状态
    bool is_atmode1_position;
    int sampling_interval;       // 采样间隔 /s
    int tolerance;               // 深度宽容度 /mm
    int default_mode_keep_depth; // 默认模式保持深度 /mm - 记录设置的深度，每次设置后，默认模式会使用这个深度
    // bool default_mode_allowed_collect; // 默认模式允许采集数据
    time_t default_mode_start_time;
    time_t default_mode_work_end_time;
    time_t mode_one_start_time;
    time_t mode_one_work_end_time;
    time_t mode_two_start_time;
    time_t mode_two_work_end_time;

    nisha_default_mode_para_t default_mode_para; // 默认模式参数（一次性）
    nisha_mode_one_para_t mode_one_para;         // 模式一参数（一次性）
    nisha_mode_two_para_t mode_two_para;         // 模式二参数（一次性）
    device_online_status_t dev_online_status;
} nisha_task_info_t;

void sleep_ms(int milliseconds);

double get_opposite_side(double hypotenuse, double angle_degrees);

void set_default_mode_para_depth(int depth_mm);

void set_default_mode_keep_depth(int depth_mm);

int get_default_mode_keep_depth(void);

int get_nisha_task_mode(void);

int get_default_mode_para_depth(void);

void set_mode_default_start_time(void);

void set_mode_one_start_time(void);

void set_mode_two_start_time(void);

void set_mode_default_work_end_time(void);

void set_mode_one_work_end_time(void);

void set_mode_two_work_end_time(void);

time_t get_mode_default_start_time(void);

time_t get_mode_default_work_end_time(void);

time_t get_mode_one_start_time(void);

time_t get_mode_one_work_end_time(void);

time_t get_mode_two_start_time(void);

time_t get_mode_two_work_end_time(void);

void set_mode_one_para_depth(int depth_num, int depth_mm);

int get_mode_one_para_depth(int depth_num);

void set_mode_two_para_depth(int depth_num, int depth_mm);

int get_mode_two_para_depth(int depth_num);

void NiShaUD_System_Self_Check(void);

void NiShaUD_change_task_state(int state);

void NiShaUD_Get_task_state(int *state);

void NiShaUD_depth_sensor_get_depth(void);

void NiShaUD_depth_sensor_get_depth_timeout(void);

void NiShaUD_relay_module_disable_break(void);

void NiShaUD_relay_module_disable_break_timeout(void);

void NiShaUD_relay_module_enable_break(void);

void NiShaUD_relay_module_enable_break_timeout(void);

void NiShaUD_dc_motor_turn_right(void);

void NiShaUD_dc_motor_turn_right_timeout(void);

void NiShaUD_dc_motor_turn_left(void);

void NiShaUD_dc_motor_turn_left_timeout(void);

void NiShaUD_dc_motor_get_state(void);

void NiShaUD_dc_motor_get_state_timeout(void);

void NiShaUD_dc_motor_stop(void);

void NiShaUD_dc_motor_stop_timeout(void);

bool NiShaUD_default_mode_is_atposition(int cur_depth, int dst_depth);

int submit_nishaUD_task(int (*SubmitTaskPtr)(int), int task_name);

void NiShaUD_Task_Mode_Default_Stop_Task(void);

void *NiShaUD_Task_Mode_Default(void *arg);

void NiShaUD_Default_Mode_Start_Task(void *arg);

void NiShaUD_VMM_mode_get_depth(void);

void NiShaUD_VMM_mode_get_depth_timeout(void);

void NiShaUD_VMM_Mode_Stop_Task(void);

void *NiShaUD_Task_Mode_VMM_Measurement(void *arg);

void NiShaUD_VMM_Mode_Start_Task(void *arg);

void NiShaUD_Task_Mode_VDIM_Stop_Task(void);

void *NiShaUD_Task_Mode_VDIM_Measurement(void *arg);

void NiShaUD_VDIM_Mode_Start_Task(void *arg);

void *NiShaUD_Task_Mode_Manual_Up(void *arg);

void *NiShaUD_Task_Mode_Manual_Down(void *arg);

void *NiShaUD_Task_Mode_Manual_Stop(void *arg);

void NiShaUD_Stop_Running_Task(void);

void NiShaUD_clear_task(void);

void Submit_NiShaUD_Task_To_ThreadPool(void *(*task_function)(void *), void *arg);

int GetNiShaUD_Task_State(void);

void NiShaUD_Manual_Mode_Stop_Task(void *arg);

void NiShaUD_Manual_Mode_Up_Task(void *arg);

void NiShaUD_Manual_Mode_Down_Task(void *arg);

// void Submit_NiShaUD_ManualMode_StopTask(void *arg);

void check_device_online(device_online_status_t *dev_online_status);

int compare(const void *a, const void *b);

int filterAndSort(int *arr, int size, int threshold, int minSpacing);

float calculateAverage(float arr[], int size);

void save_keep_depth_in_file(int depth_mm);

void get_keep_depth_from_file(int *depth_mm);

void init_nisha_default_keep_depth(void);

#endif // !
