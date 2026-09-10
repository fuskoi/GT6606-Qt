#include "nishatask.h"

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
nisha_task_info_t nisha_task_info = {
    .task_mode = SedimentUD_Mode_Default,
    .task_state = SedimentUD_IDLE,
    .is_atmode1_position = true,
    // .default_mode_allowed_collect = false,
    .default_mode_keep_depth = 500, // 默认模式保持深度 500mm
    .tolerance = 50,
    .default_mode_start_time = 0,
    .mode_one_start_time = 0,
    .mode_two_start_time = 0,
    .stop_request = false,
    .default_mode_para = {
        .depth = 0,
    },
    .mode_one_para = {
        .depth_1 = 0,
        .depth_2 = 0,
        .depth_3 = 0,
        .depth_4 = 0,
    },
    .mode_two_para = {
        .depth_A = 0,
        .depth_B = 0,
    },
};

pthread_mutex_t nisha_task_state_mutex = PTHREAD_MUTEX_INITIALIZER;

/// @brief Sleep for a specified number of milliseconds
/// @param milliseconds The number of milliseconds to sleep
void sleep_ms(int milliseconds)
{
    usleep(milliseconds * 1000);
}

/// @brief Get the length of the opposite side of a right triangle
/// @param hypotenuse The length of the hypotenuse
/// @param angle_degrees The angle in degrees
double get_opposite_side(double hypotenuse, double angle_degrees)
{
    // 将角度转换为弧度
    double angle_radians = angle_degrees * M_PI / 180.0;

    // 计算对边
    double opposite = hypotenuse * sin(angle_radians);

    return opposite;
}

void set_default_mode_para_depth(int depth_mm)
{
    nisha_task_info.default_mode_para.depth = depth_mm;
}

void set_default_mode_keep_depth(int depth_mm)
{
    nisha_task_info.default_mode_keep_depth = depth_mm;
}

int get_default_mode_keep_depth(void)
{
    return nisha_task_info.default_mode_keep_depth;
}

int get_nisha_task_mode(void)
{
    return nisha_task_info.task_mode;
}

int get_default_mode_para_depth(void)
{
    return nisha_task_info.default_mode_para.depth;
}
void set_mode_default_start_time(void)
{
    nisha_task_info.default_mode_start_time = time(NULL);
}
void set_mode_one_start_time(void)
{
    nisha_task_info.mode_one_start_time = time(NULL);
}
void set_mode_two_start_time(void)
{
    nisha_task_info.mode_two_start_time = time(NULL);
}

void set_mode_default_work_end_time(void)
{
    nisha_task_info.default_mode_work_end_time = time(NULL);
}

void set_mode_one_work_end_time(void)
{
    nisha_task_info.mode_one_work_end_time = time(NULL);
}

void set_mode_two_work_end_time(void)
{
    nisha_task_info.mode_two_work_end_time = time(NULL);
}
time_t get_mode_default_start_time(void)
{
    return nisha_task_info.default_mode_start_time;
}
time_t get_mode_default_work_end_time(void)
{
    return nisha_task_info.default_mode_work_end_time;
}

time_t get_mode_one_start_time(void)
{
    return nisha_task_info.mode_one_start_time;
}
time_t get_mode_one_work_end_time(void)
{
    return nisha_task_info.mode_one_work_end_time;
}
time_t get_mode_two_start_time(void)
{
    return nisha_task_info.mode_two_start_time;
}
time_t get_mode_two_work_end_time(void)
{
    return nisha_task_info.mode_two_work_end_time;
}

void set_mode_one_para_depth(int depth_num, int depth_mm)
{
    switch (depth_num)
    {
    case 1:
        nisha_task_info.mode_one_para.depth_1 = depth_mm;
        break;
    case 2:
        nisha_task_info.mode_one_para.depth_2 = depth_mm;
        break;
    case 3:
        nisha_task_info.mode_one_para.depth_3 = depth_mm;
        break;
    case 4:
        nisha_task_info.mode_one_para.depth_4 = depth_mm;
        break;
    default:
        break;
    }
}

int get_mode_one_para_depth(int depth_num)
{
    switch (depth_num)
    {
    case 1:
        return nisha_task_info.mode_one_para.depth_1;
        break;
    case 2:
        return nisha_task_info.mode_one_para.depth_2;
        break;
    case 3:
        return nisha_task_info.mode_one_para.depth_3;
        break;
    case 4:
        return nisha_task_info.mode_one_para.depth_4;
        break;
    default:
        break;
    }
    return -1;
}
void set_mode_two_para_depth(int depth_num, int depth_mm)
{
    switch (depth_num)
    {
    case 1:
        /* code */
        nisha_task_info.mode_two_para.depth_A = depth_mm;
        break;
    case 2:
        nisha_task_info.mode_two_para.depth_B = depth_mm;
        break;
    default:
        break;
    }
}

int get_mode_two_para_depth(int depth_num)
{
    switch (depth_num)
    {
    case 1:
        return nisha_task_info.mode_two_para.depth_A;
        break;
    case 2:
        return nisha_task_info.mode_two_para.depth_B;
        break;
    default:
        break;
    }
    return -1;
}

void NiShaUD_System_Self_Check(void)
{
    nisha_task_info.task_state = SedimentUD_INITIALIZING;

    NiShaSystemCheckTask();

    while (isNiShaSystemCheckTaskRunning())
    {
        /* code */
        // ts_printf("[NISHA SYSTEM SELF CHECKING]");
        sleep_ms(1000);
    }

    check_device_online(&nisha_task_info.dev_online_status);
    {
        int ok = 1;
        /* 只检查配置里绑定了串口的设备，方便少设备联调 */
        if (get_device_index(SEDIMENT_SENSOR) != -1 &&
            !nisha_task_info.dev_online_status.sediment_sensor)
            ok = 0;
        if (get_device_index(DC_MOTOR) != -1 &&
            !nisha_task_info.dev_online_status.dc_motor)
            ok = 0;
        if (get_device_index(RELAY_MODULE) != -1 &&
            !nisha_task_info.dev_online_status.relay_module)
            ok = 0;
        if (get_device_index(DEPTH_SENSOR) != -1 &&
            !nisha_task_info.dev_online_status.depth_sensor)
            ok = 0;
        if (get_device_index(ENCODER) != -1 &&
            !nisha_task_info.dev_online_status.encoder)
            ok = 0;

        if (ok)
        {
            nisha_task_info.task_state = SedimentUD_IDLE;
            set_mode_default_work_end_time(); /* 避免 end_time=0 导致立刻进默认模式校准 */
            ts_printf("[NISHA SYSTEM SELF CHECK PASSED]\n");
        }
        else
        {
            nisha_task_info.task_state = SedimentUD_ERROR;
            LOG(LOG_ERROR, "NiShaUD_System_Self_Check: Device not online");
        }
    }
}

void NiShaUD_change_task_state(int state)
{
    pthread_mutex_lock(&nisha_task_state_mutex);
    nisha_task_info.sec_task_state = state;
    pthread_mutex_unlock(&nisha_task_state_mutex);
    // ts_printf("[NISHA TASK STATE CHANGED TO %d]\n", state);
}
static char *statestr[] = {
    "SEC_TASK_READY",
    "SEC_TASK_WAITING",
    "SEC_TASK_COMPLETED",
    "SEC_TASK_ERROR",
    "SEC_TASK_TIMEOUT"};
void NiShaUD_Get_task_state(int *state)
{
    pthread_mutex_lock(&nisha_task_state_mutex);
    *state = nisha_task_info.sec_task_state;
    // LOG(LOG_DEBUG, "NiShaUD_Get_task_state: %s", statestr[nisha_task_info.sec_task_state]);
    pthread_mutex_unlock(&nisha_task_state_mutex);
}

void NiShaUD_depth_sensor_get_depth(void)
{
    NiShaUD_change_task_state(SEC_TASK_COMPLETED);
}
void NiShaUD_depth_sensor_get_depth_timeout(void)
{
    NiShaUD_change_task_state(SEC_TASK_TIMEOUT);
    set_depth_online(false);
}

void NiShaUD_relay_module_disable_break(void)
{
    NiShaUD_change_task_state(SEC_TASK_COMPLETED);
}
void NiShaUD_relay_module_disable_break_timeout(void)
{
    NiShaUD_change_task_state(SEC_TASK_TIMEOUT);
    SetRelayModuleOnline(false);
}

void NiShaUD_relay_module_enable_break(void)
{
    NiShaUD_change_task_state(SEC_TASK_COMPLETED);
}

void NiShaUD_relay_module_enable_break_timeout(void)
{
    NiShaUD_change_task_state(SEC_TASK_TIMEOUT);
    SetRelayModuleOnline(false);
}

void NiShaUD_dc_motor_turn_right(void)
{
    NiShaUD_change_task_state(SEC_TASK_COMPLETED);
}

void NiShaUD_dc_motor_turn_right_timeout(void)
{
    NiShaUD_change_task_state(SEC_TASK_TIMEOUT);
    set_dc_motor_online(false);
}

void NiShaUD_dc_motor_turn_left(void)
{
    NiShaUD_change_task_state(SEC_TASK_COMPLETED);
}

void NiShaUD_dc_motor_turn_left_timeout(void)
{
    NiShaUD_change_task_state(SEC_TASK_TIMEOUT);
    set_dc_motor_online(false);
}

void NiShaUD_dc_motor_get_state(void)
{
    NiShaUD_change_task_state(SEC_TASK_COMPLETED);
}

void NiShaUD_dc_motor_get_state_timeout(void)
{
    NiShaUD_change_task_state(SEC_TASK_TIMEOUT);
    set_dc_motor_online(false);
}

void NiShaUD_dc_motor_stop(void)
{
    NiShaUD_change_task_state(SEC_TASK_COMPLETED);
}

void NiShaUD_dc_motor_stop_timeout(void)
{
    NiShaUD_change_task_state(SEC_TASK_TIMEOUT);
    set_dc_motor_online(false);
}

bool NiShaUD_default_mode_is_atposition(int cur_depth, int dst_depth)
{
    // return nisha_task_info.is_atmode1_position;
    if (cur_depth <= dst_depth + nisha_task_info.tolerance && cur_depth >= dst_depth - nisha_task_info.tolerance)
    {
        return true;
    }
    return false;
}

int submit_nishaUD_task(int (*SubmitTaskPtr)(int), int task_name)
{

    if (NULL == SubmitTaskPtr)
    {
        return -1;
    }
    nisha_task_info.sec_task_state = SEC_TASK_WAITING;
    int ret = SubmitTaskPtr(task_name);
    if (-1 == ret)
    {
        nisha_task_info.sec_task_state = SEC_TASK_TIMEOUT;
    }
    else if (ret == -2 || ret == -3)
    {
        nisha_task_info.sec_task_state = SEC_TASK_ERROR;
        LOG(LOG_ERROR, "Submit task error: %d", ret);
        return nisha_task_info.sec_task_state;
    }

    while (SEC_TASK_WAITING == nisha_task_info.sec_task_state)
    {
        // ts_printf("[NISHA TASK WAITING]\n");
        sleep_ms(200);
    }

    return nisha_task_info.sec_task_state;
}

// 固定位置查询数据(除了手动模式外,最后都要回到这个模式上,能自动切换,能手动切换)，手动模式可以通过发送深度并立即执行这个任务来实现

void NiShaUD_Task_Mode_Default_Stop_Task(void)
{
    // nisha_task_info.default_mode_allowed_collect = true;
    set_mode_default_work_end_time();
    set_default_mode_para_depth(0);
    NiShaUD_clear_task();
}
void *NiShaUD_Task_Mode_Default(void *arg)
{
    ts_printf("[NiShaUD_Task_Mode_Default] Start\n");
    nisha_task_info.stop_request = false;
    int dst_depth = get_default_mode_para_depth(); // 毫米
    if (0 == dst_depth)                            // 未设置深度，不运行任务
    {

        LOG(LOG_ERROR, "Default mode depth is 0, task not started");
        NiShaUD_Task_Mode_Default_Stop_Task();
        return NULL;
    }

    if (dst_depth < 50)
    {
        LOG(LOG_ERROR, "Depth is too small: %dmm", dst_depth);
        NiShaUD_Task_Mode_Default_Stop_Task();
        return NULL;
    }

    int cur_depth = 0; // 毫米
    // nisha_task_info.task_state = SedimentUD_RUNNING;
    // 移动设备到目标位置， 并开启采集模式
    // 一、 查询当前位置
    ts_printf("[NiShaUD_Task_Mode_Default] Get depth\n");
    submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
    NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
    if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
    {
        // 获取深度超时
        NiShaUD_Task_Mode_Default_Stop_Task();
        LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
        return NULL;
    }
    if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
    {
        // 获取深度错误
        NiShaUD_Task_Mode_Default_Stop_Task();
        LOG(LOG_ERROR, "GET DEPTH ERROR");
        return NULL;
    }
    cur_depth = get_depth_sensor_depth();
    ts_printf("[NiShaUD_Task_Mode_Default] Current depth: %dmm\n", cur_depth);
    default_mode_send_depth_to_screen(cur_depth * 0.001f); // 发送深度到屏幕
    nisha_task_info.is_atmode1_position = NiShaUD_default_mode_is_atposition(cur_depth, dst_depth);
    if (nisha_task_info.is_atmode1_position)
    {
        // nisha_task_info.default_mode_allowed_collect = true;
        // nisha_task_info.sampling_interval = 6;
        ts_printf("nisha_task_info.is_atmode1_position\n ");
        NiShaUD_Task_Mode_Default_Stop_Task();
        return NULL;
    }

    if (cur_depth < dst_depth - nisha_task_info.tolerance)
    {
        // 向下
        ts_printf("[NiShaUD_Task_Mode_Default] Down\n");
        // 1.关闭刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_DISABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 关闭刹车超时
            LOG(LOG_ERROR, "DISABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "DISABLE BREAK ERROR");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }
        sleep_ms(50);
        // 2.启动电机
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_TURN_LEFT);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 启动电机超时
            LOG(LOG_ERROR, "TURN LEFT TIMEOUT");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "TURN LEFT ERROR");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }

        while ((cur_depth) < dst_depth && !nisha_task_info.stop_request)
        {
            /* code */
            // 2. 查询深度
            submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 获取深度超时
                // 启动刹车
                LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);

                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "GET DEPTH ERROR");
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }
            cur_depth = get_depth_sensor_depth();
            ts_printf("Current Depth: %d mm\n", cur_depth);
            default_mode_send_depth_to_screen(cur_depth * 0.001f); // 发送深度到屏幕
            // 异常处理 - 深度长时间不变时 出现异常
            // 4.查询状态
            submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_GET_STATE);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 获取状态超时
                LOG(LOG_ERROR, "GET DC MOTOR STATE TIMEOUT");
                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "GET DC MOTOR STATE ERROR");
                // 获取状态错误
                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }
            if (DM_STATE_ERROR == get_dc_motor_state1())
            {
                // 电机异常处理
                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaSystemProcMotorErrorTask();
                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }
            sleep_ms(200);
        }
        ts_printf("[NiShaUD_Task_Mode_Default] Depth reached\n");
        // 5. 停止电机
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_STOP);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 停止电机超时
            LOG(LOG_ERROR, "STOP DC MOTOR TIMEOUT");
            // 关闭电机
            SubmitDcMotorTask(NS_DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_Default_Stop_Task();

            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "STOP DC MOTOR ERROR");
            // 停止电机错误
            // 关闭电机
            SubmitDcMotorTask(NS_DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_Default_Stop_Task();

            return NULL;
        }
        sleep_ms(50);
        // 3.启动刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 启动刹车超时的处理
            LOG(LOG_ERROR, "ENABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "ENABLE BREAK ERROR");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }
    }
    else if (cur_depth > dst_depth + nisha_task_info.tolerance)
    {
        // 向上
        ts_printf("[NiShaUD_Task_Mode_Default] Up\n");
        // 1.关闭刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_DISABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 关闭刹车超时
            LOG(LOG_ERROR, "DISABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "DISABLE BREAK ERROR");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }
        sleep_ms(50);
        // 2. 启动电机
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_TURN_RIGHT);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 启动电机超时
            LOG(LOG_ERROR, "TURN RIGHT TIMEOUT");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "TURN RIGHT ERROR");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }

        while ((cur_depth) > dst_depth && !nisha_task_info.stop_request)
        {
            /* code */
            // 3.查询深度
            submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 获取深度超时
                LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "GET DEPTH ERROR");
                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }
            cur_depth = get_depth_sensor_depth();
            ts_printf("Current Depth: %d mm\n", cur_depth);
            default_mode_send_depth_to_screen(cur_depth * 0.001f); // 发送深度到屏幕
            // 4.查询状态
            submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_GET_STATE);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 获取状态超时
                LOG(LOG_ERROR, "GET DC MOTOR STATE TIMEOUT");
                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "GET DC MOTOR STATE ERROR");
                // 获取状态错误
                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }
            if (DM_STATE_ERROR == get_dc_motor_state1())
            {
                // 电机异常处理
                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaSystemProcMotorErrorTask();
                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }

            sleep_ms(200);
        }
        ts_printf("[NiShaUD_Task_Mode_Default] Depth reached\n");
        // 5. 停止电机
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_STOP);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 停止电机超时
            LOG(LOG_ERROR, "STOP DC MOTOR TIMEOUT");
            // 关闭电机
            SubmitDcMotorTask(NS_DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_Default_Stop_Task();

            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "STOP DC MOTOR ERROR");
            // 停止电机错误
            // 关闭电机
            SubmitDcMotorTask(NS_DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_Default_Stop_Task();

            return NULL;
        }
        sleep_ms(50);
        // 6.启动刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 启动刹车超时
            LOG(LOG_ERROR, "ENABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "ENABLE BREAK ERROR");
            NiShaUD_Task_Mode_Default_Stop_Task();
            return NULL;
        }
    }

    // 任务完成
    // 1.标志位清理
    // 2.深度参数归0
    // 3.启动采集
    // nisha_task_info.default_mode_allowed_collect = true; // 允许采集数据
    // nisha_task_info.sampling_interval = 6;
    NiShaUD_Task_Mode_Default_Stop_Task();
    return NULL;
}

void NiShaUD_Default_Mode_Start_Task(void *arg)
{
    if (SedimentUD_RUNNING == nisha_task_info.task_state || SedimentUD_ERROR == nisha_task_info.task_state || SedimentUD_INITIALIZING == nisha_task_info.task_state)
    {
        ts_printf("[NISHAUD] Default Mode Task is already running or in error state %d, cannot start again.\n", nisha_task_info.task_state);
        return;
    }
    /* 台架仅接泥沙/DTU 时不做升降校准 */
    if (get_device_index(DEPTH_SENSOR) < 0 ||
        get_device_index(DC_MOTOR) < 0 ||
        get_device_index(RELAY_MODULE) < 0)
    {
        ts_printf("[NISHAUD] Default Mode skip: depth/motor/relay not configured\n");
        set_mode_default_work_end_time();
        return;
    }
    // nisha_task_info.default_mode_allowed_collect = false; // 禁止采集数据
    nisha_task_info.task_mode = SedimentUD_Mode_Default;
    nisha_task_info.task_state = SedimentUD_RUNNING;
    nisha_task_info.sec_task_state = SEC_TASK_READY;
    set_mode_default_start_time();
    init_mode_default_collect_data_buffer();
    if (-1 == submitTask(g_threadPool, NiShaUD_Task_Mode_Default, arg))
    {
        NiShaUD_Task_Mode_Default_Stop_Task();
    }
}

// 自动到设定的测点进行一定时间的数据采集
// 需要设定测点位置
void NiShaUD_VMM_Mode_Stop_Task(void)
{
    set_mode_one_para_depth(1, 0);
    set_mode_one_para_depth(2, 0);
    set_mode_one_para_depth(3, 0);
    set_mode_one_para_depth(4, 0);
    NiShaUD_clear_task();
    set_mode_one_work_end_time();
}
void *NiShaUD_Task_Mode_VMM_Measurement(void *arg)
{
    ts_printf("[NiShaUD_Task_Mode_VMM_Measurement] Start\n");
    // 1.单测点
    // 2.多测点
    nisha_task_info.stop_request = false;
    // uint8_t point = 0;
    uint8_t cur_point = 0;
    int cur_depth = 0;
    int dst_depth[4] = {0};

    dst_depth[0] = get_mode_one_para_depth(1);
    dst_depth[1] = get_mode_one_para_depth(2);
    dst_depth[2] = get_mode_one_para_depth(3);
    dst_depth[3] = get_mode_one_para_depth(4);
    // 把收到的深度从浅到深排序
    int point = filterAndSort(dst_depth, 4, 50, nisha_task_info.tolerance + 100);
    ts_printf("point=%d\n", point);
    if (point < 1)
    {
        LOG(LOG_ERROR, "No valid depth points for VMM mode");
        NiShaUD_VMM_Mode_Stop_Task();
        return NULL;
    }
    // 1.读取当前深度
    submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
    NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
    if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
    {
        // 读取超时
        LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
        NiShaUD_VMM_Mode_Stop_Task();
        return NULL;
    }
    if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
    {
        // 读取错误
        LOG(LOG_ERROR, "GET DEPTH ERROR");
        NiShaUD_VMM_Mode_Stop_Task();
        return NULL;
    }
    cur_depth = get_depth_sensor_depth();

    float sediment_data[20] = {0};
    int sediment_data_count = 0;
    float sediment_data_filtered[20] = {0};
    float sediment_data_average = 0;
    float sediment_data_all[64] = {0};
    int sediment_data_all_count = 0;
    while (cur_point < point && !nisha_task_info.stop_request)
    {
        ts_printf("cur_point=%d\n", cur_point);
        // 从浅到深
        if (cur_depth <= dst_depth[cur_point] + nisha_task_info.tolerance && cur_depth >= dst_depth[cur_point] - nisha_task_info.tolerance) // 说明当前位置在最浅点上
        {
            // 在点位
            // 开始采集
            nisha_buffer_init(&g_sediment_buffer);
            ts_printf("start1\n");
            NiShaSystemDataCollectTask(5000, CHAIN_MODE_FINITE, 12); // 采集60秒数据
            ts_printf("start11\n");
            while (isNiShaSystemDataCollectTaskRunning())
            {
                // 等待采集完成
                if (nisha_task_info.stop_request)
                {
                    // 如果任务被请求停止，退出等待
                    stop_NiShaSystemDataCollectTask();
                    NiShaUD_VMM_Mode_Stop_Task();
                    return NULL;
                }
                sleep_ms(1000);
            }
            if (TASK_TIMEOUT == get_Ns_sys_data_collect_sediment_sensor_taskStatus())
            {
                // 采集超时
                LOG(LOG_ERROR, "SEDIMENT SENSOR DATA COLLECT TIMEOUT");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }

            // 6. 数据处理
            memset(sediment_data, 0, sizeof(sediment_data));
            sediment_data_count = 0;
            memset(sediment_data_filtered, 0, sizeof(sediment_data_filtered));
            sediment_data_average = 0;
            sediment_data_count = nisha_buffer_get_array(&g_sediment_buffer, sediment_data);
            memcpy(sediment_data_all + cur_point * sediment_data_count, sediment_data, sediment_data_count * sizeof(float));
            sediment_data_all_count += sediment_data_count;
            filter_sediment_data(sediment_data, sediment_data_count, sediment_data_filtered, &sediment_data_average, 5, 3);
            sediment_data_average = sediment_fill_milli_look(sediment_data_average);
            int count_time = 3;
            while (count_time > 0)
            {
                VMM_mode_send_sediment_to_screen(sediment_data_average, cur_point + 8);
                count_time--;
            }
        }
        else if (cur_depth < dst_depth[cur_point] - nisha_task_info.tolerance) // 说明当前位置比目标点浅
        {
            // 向下
            // 2.关闭刹车
            submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_DISABLE_BREAK);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 关闭刹车超时
                LOG(LOG_ERROR, "DISABLE BREAK TIMEOUT");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "DISABLE BREAK ERROR");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            sleep_ms(50);
            // 2. 启动电机
            submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_TURN_LEFT);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 启动电机超时
                LOG(LOG_ERROR, "TURN RIGHT TIMEOUT");
                NiShaUD_Task_Mode_Default_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "TURN RIGHT ERROR");
                NiShaUD_Task_Mode_Default_Stop_Task();
                return NULL;
            }

            while (cur_depth < dst_depth[cur_point] && !nisha_task_info.stop_request)
            {
                // 3. 查询深度
                submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
                NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
                if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
                {
                    LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
                    NiShaUD_VMM_Mode_Stop_Task();
                    return NULL;
                }
                if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
                {
                    LOG(LOG_ERROR, "GET DEPTH ERROR");
                    NiShaUD_VMM_Mode_Stop_Task();
                    return NULL;
                }
                cur_depth = get_depth_sensor_depth();
                ts_printf("Current Depth: %d mm\n", cur_depth);
                sleep_ms(200);
            }
            // 5. 停止电机
            submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_STOP);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 停止电机超时
                LOG(LOG_ERROR, "STOP DC MOTOR TIMEOUT");
                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "STOP DC MOTOR ERROR");
                // 停止电机错误
                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_Default_Stop_Task();

                return NULL;
            }
            sleep_ms(50);
            // 4. 启动刹车
            submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "ENABLE BREAK TIMEOUT");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "ENABLE BREAK ERROR");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }

            // 5. 开始采集
            nisha_buffer_init(&g_sediment_buffer);
            ts_printf("start2\n");
            NiShaSystemDataCollectTask(5000, CHAIN_MODE_FINITE, 12); // 采集60秒数据
            ts_printf("start22\n");
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            while (isNiShaSystemDataCollectTaskRunning())
            {
                // 等待采集完成
                if (nisha_task_info.stop_request)
                {
                    // 如果任务被请求停止，退出等待
                    stop_NiShaSystemDataCollectTask();
                    NiShaUD_VMM_Mode_Stop_Task();
                    return NULL;
                }
                sleep_ms(1000);
            }
            if (TASK_TIMEOUT == get_Ns_sys_data_collect_sediment_sensor_taskStatus())
            {
                // 采集超时
                LOG(LOG_ERROR, "SEDIMENT SENSOR DATA COLLECT TIMEOUT");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            // 6. 数据处理
            memset(sediment_data, 0, sizeof(sediment_data));
            sediment_data_count = 0;
            memset(sediment_data_filtered, 0, sizeof(sediment_data_filtered));
            sediment_data_average = 0;
            sediment_data_count = nisha_buffer_get_array(&g_sediment_buffer, sediment_data);
            memcpy(sediment_data_all + cur_point * sediment_data_count, sediment_data, sediment_data_count * sizeof(float));
            sediment_data_all_count += sediment_data_count;
            filter_sediment_data(sediment_data, sediment_data_count, sediment_data_filtered, &sediment_data_average, 5, 3);
            sediment_data_average = sediment_fill_milli_look(sediment_data_average);
            int count_time = 3;
            while (count_time > 0)
            {
                VMM_mode_send_sediment_to_screen(sediment_data_average, cur_point + 8);
                count_time--;
            }
        }
        else if (cur_depth > dst_depth[cur_point] + nisha_task_info.tolerance) // 当前位置比目标点深
        {
            // 向上
            // 1. 关闭刹车
            submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_DISABLE_BREAK);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 关闭刹车超时
                LOG(LOG_ERROR, "DISABLE BREAK TIMEOUT");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "DISABLE BREAK ERROR");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            sleep_ms(50);
            // 2. 启动电机
            submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_TURN_RIGHT);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "DC MOTOR TURN RIGHT TIMEOUT");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "DC MOTOR TURN RIGHT ERROR");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            while (cur_depth > dst_depth[cur_point] && !nisha_task_info.stop_request)
            {
                // 3. 查询深度
                submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
                NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
                if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
                {
                    LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
                    // 关闭电机
                    SubmitDcMotorTask(DC_MOTOR_STOP);
                    // 启动刹车
                    SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                    NiShaUD_VMM_Mode_Stop_Task();
                    return NULL;
                }
                if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
                {
                    LOG(LOG_ERROR, "GET DEPTH ERROR");
                    // 关闭电机
                    SubmitDcMotorTask(DC_MOTOR_STOP);
                    // 启动刹车
                    SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                    NiShaUD_VMM_Mode_Stop_Task();
                    return NULL;
                }
                cur_depth = get_depth_sensor_depth();
                ts_printf("Current Depth: %d mm\n", cur_depth);
                //  4. 查询电机状态
                submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_GET_STATE);
                NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
                if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
                {
                    LOG(LOG_ERROR, "GET DC MOTOR STATE TIMEOUT");
                    // 关闭电机
                    SubmitDcMotorTask(DC_MOTOR_STOP);
                    // 启动刹车
                    SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                    NiShaUD_VMM_Mode_Stop_Task();
                    return NULL;
                }
                if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
                {
                    LOG(LOG_ERROR, "GET DC MOTOR STATE ERROR");
                    // 获取状态错误
                    // 关闭电机
                    SubmitDcMotorTask(DC_MOTOR_STOP);
                    // 启动刹车
                    SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                    NiShaUD_VMM_Mode_Stop_Task();
                    return NULL;
                }
                if (DM_STATE_ERROR == get_dc_motor_state1())
                {
                    // 电机异常处理
                    NiShaSystemProcMotorErrorTask();
                    // 关闭电机
                    SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                    // 启动刹车
                    SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                    NiShaUD_VMM_Mode_Stop_Task();
                    return NULL;
                }

                sleep_ms(200);
            }
            ts_printf("start3111\n");
            // 5. 电机停止
            submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_STOP);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "STOP DC MOTOR TIMEOUT");
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "STOP DC MOTOR ERROR");
                // 停止电机错误
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            sleep_ms(50);
            // 6. 启动刹车
            ts_printf("start311\n");
            submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "ENABLE BREAK TIMEOUT");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                LOG(LOG_ERROR, "ENABLE BREAK ERROR");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }
            // 7. 开始采集
            ts_printf("start31\n");
            nisha_buffer_init(&g_sediment_buffer);
            ts_printf("start3\n");
            NiShaSystemDataCollectTask(5000, CHAIN_MODE_FINITE, 12); // 采集60秒数据
            ts_printf("start33\n");
            while (isNiShaSystemDataCollectTaskRunning())
            {
                // 等待采集完成
                if (nisha_task_info.stop_request)
                {
                    // 如果任务被请求停止，退出等待
                    stop_NiShaSystemDataCollectTask();
                    NiShaUD_VMM_Mode_Stop_Task();
                    return NULL;
                }
                sleep_ms(1000);
            }
            if (TASK_TIMEOUT == get_Ns_sys_data_collect_sediment_sensor_taskStatus())
            {
                // 采集超时
                LOG(LOG_ERROR, "SEDIMENT SENSOR DATA COLLECT TIMEOUT");
                NiShaUD_VMM_Mode_Stop_Task();
                return NULL;
            }

            // 6. 数据处理
            memset(sediment_data, 0, sizeof(sediment_data));
            sediment_data_count = 0;
            memset(sediment_data_filtered, 0, sizeof(sediment_data_filtered));
            sediment_data_average = 0;
            sediment_data_count = nisha_buffer_get_array(&g_sediment_buffer, sediment_data);
            memcpy(sediment_data_all + cur_point * sediment_data_count, sediment_data, sediment_data_count * sizeof(float));
            sediment_data_all_count += sediment_data_count;
            filter_sediment_data(sediment_data, sediment_data_count, sediment_data_filtered, &sediment_data_average, 5, 3);
            sediment_data_average = sediment_fill_milli_look(sediment_data_average);
            int count_time = 3;
            while (count_time > 0)
            {
                VMM_mode_send_sediment_to_screen(sediment_data_average, cur_point + 8);
                count_time--;
            }
        }
        cur_point++;
    }
    float sediment_data_all_filtered[64] = {0};
    float sediment_data_all_average = 0;
    filter_sediment_data(sediment_data_all, sediment_data_all_count, sediment_data_all_filtered, &sediment_data_all_average, get_optimal_window_size(sediment_data_all_count), 3);
    sediment_data_all_average = sediment_fill_milli_look(sediment_data_all_average);
    int count_time = 3;
    while (count_time > 0)
    {
        VMM_mode_send_sediment_to_screen(sediment_data_all_average, 12);
        count_time--;
    }

    // 这里增加DTU的发送
    SubmitRelayModuleHeartTask();
    g_sediment_data_average = sediment_data_all_average;
    sl651_send_fixtime_report(&g_stM2UpFramepara);

    NiShaUD_VMM_Mode_Stop_Task();
    return NULL;
}

void NiShaUD_VMM_Mode_Start_Task(void *arg)
{
    if (SedimentUD_RUNNING == nisha_task_info.task_state || SedimentUD_ERROR == nisha_task_info.task_state || SedimentUD_INITIALIZING == nisha_task_info.task_state)
    {
        ts_printf("[NISHAUD] VMM Mode Task is already running or in error state, cannot start again.\n");
        return;
    }
    set_mode_one_start_time();
    nisha_task_info.task_mode = SedimentUD_Mode_VMM_Measurement;
    nisha_task_info.task_state = SedimentUD_RUNNING;
    nisha_task_info.sec_task_state = SEC_TASK_READY;
    if (-1 == submitTask(g_threadPool, NiShaUD_Task_Mode_VMM_Measurement, arg))
    {
        NiShaUD_VMM_Mode_Stop_Task();
    }
}

// 从深度A移动到深度B,在移动过程中采集数据
void NiShaUD_Task_Mode_VDIM_Stop_Task(void)
{
    set_mode_two_para_depth(1, 0);
    set_mode_two_para_depth(2, 0);
    set_mode_two_work_end_time();
    NiShaUD_clear_task();
}
void *NiShaUD_Task_Mode_VDIM_Measurement(void *arg)
{
    ts_printf("[NiShaUD_Task_Mode_VDIM_Measurement] Start\n");
    // 先到深的地方，再往上移动
    nisha_task_info.stop_request = false;
    float sediment_data[100] = {0};
    float sediment_data_filtered[100] = {0};
    float sediment_data_average = 0;
    int sediment_data_count = 0;
    int cur_depth = 0;
    int depth_1 = get_mode_two_para_depth(1);
    int depth_2 = get_mode_two_para_depth(2);
    int depth_A, depth_B;
    // 把深的(大的)设置为B，浅的(小的)设置为A
    if (depth_1 < 50 || depth_2 < 50 || abs(depth_1 - depth_2) < nisha_task_info.tolerance + 100)
    {
        // 深度小于300mm, 两点距离太近,不运行任务
        LOG(LOG_ERROR, "Depth is too small or points are too close: %dmm, %dmm", depth_1, depth_2);
        NiShaUD_Task_Mode_VDIM_Stop_Task();
        return NULL;
    }
    depth_A = min(depth_1, depth_2);
    depth_B = max(depth_1, depth_2);

    // 1. 查询当前深度
    submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
    NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
    if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
    {
        // 读取超时
        LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
        NiShaUD_Task_Mode_VDIM_Stop_Task();
        return NULL;
    }
    if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
    {
        // 读取错误
        LOG(LOG_ERROR, "GET DEPTH ERROR");
        NiShaUD_Task_Mode_VDIM_Stop_Task();
        return NULL;
    }
    cur_depth = get_depth_sensor_depth();

    // 2. 移动到指定深度B
    if (cur_depth <= depth_B + nisha_task_info.tolerance && cur_depth >= depth_B - nisha_task_info.tolerance) // 当前位置在B这个范围公差内
    {
        // 在目标深度范围内
        // 1.关闭刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_DISABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "DISABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 读取错误
            LOG(LOG_ERROR, "DISABLE BREAK ERROR");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        sleep_ms(50);
        // 2. 启动电机
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_TURN_RIGHT);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "TURN RIGHT TIMEOUT");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 读取错误
            LOG(LOG_ERROR, "TURN RIGHT ERROR");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        // 设置采集的参数，并开始采集泥沙数据
        nisha_buffer_init(&g_sediment_buffer);
        NiShaSystemDataCollectTask(3000, CHAIN_MODE_INFINITE, 0);
        // 3. 向A点移动
        while (cur_depth > depth_A && !nisha_task_info.stop_request)
        {
            // 4. 读取深度
            submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 读取超时
                LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                // 读取错误
                LOG(LOG_ERROR, "GET DEPTH ERROR");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            cur_depth = get_depth_sensor_depth();
            ts_printf("Current Depth: %d mm\n", cur_depth);

            // 5. 读取电机状态
            submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_GET_STATE);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 读取超时
                LOG(LOG_ERROR, "GET DC MOTOR STATE TIMEOUT");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                // 读取错误
                LOG(LOG_ERROR, "GET DC MOTOR STATE ERROR");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }

            if (DM_STATE_ERROR == get_dc_motor_state1())
            {
                // 电机异常处理
                NiShaSystemProcMotorErrorTask();

                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);

                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }

            sleep_ms(200);
        }

        // 停止采集泥沙数据
        stop_NiShaSystemDataCollectTask();
        // 计算结果
        memset(sediment_data, 0, sizeof(sediment_data));
        memset(sediment_data_filtered, 0, sizeof(sediment_data_filtered));
        sediment_data_count = nisha_buffer_get_array(&g_sediment_buffer, sediment_data);
        if (sediment_data_count < 4)
        {
            float favg = sediment_fill_milli_look(calculateAverage(sediment_data, sediment_data_count));
            VDIM_mode_send_sediment_to_screen(favg);
        }
        filter_sediment_data(sediment_data, sediment_data_count, sediment_data_filtered, &sediment_data_average, get_optimal_window_size(sediment_data_count), 3);
        sediment_data_average = sediment_fill_milli_look(sediment_data_average);

        int count_time = 3;
        while (count_time > 0)
        {
            VDIM_mode_send_sediment_to_screen(sediment_data_average);
            count_time--;
        }

        // 这里增加DTU的发送
        SubmitRelayModuleHeartTask();
        g_sediment_data_average = sediment_data_average;
        sl651_send_fixtime_report(&g_stM2UpFramepara);

        // 6. 停止电机
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_STOP);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "STOP DC MOTOR TIMEOUT");
            // 停止电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)

        {
            // 停止电机错误
            LOG(LOG_ERROR, "STOP DC MOTOR ERROR");
            // 停止电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        sleep_ms(50);
        // 7. 启动刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "ENABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "ENABLE BREAK ERROR");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
    }
    else if (cur_depth < depth_B - nisha_task_info.tolerance) // 当前位置比B浅
    {
        // 先向下移动到B点
        // 1. 关闭刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_DISABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "DISABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 读取错误
            LOG(LOG_ERROR, "DISABLE BREAK ERROR");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        sleep_ms(50);
        // 2. 先启动电机下降到B点
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_TURN_LEFT);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "TURN RIGHT TIMEOUT");
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);

            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 读取错误
            LOG(LOG_ERROR, "TURN RIGHT ERROR");
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }

        while (cur_depth < depth_B && !nisha_task_info.stop_request)
        {
            // 2.读取深度
            submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 读取超时
                LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                // 读取错误
                LOG(LOG_ERROR, "GET DEPTH ERROR");
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            cur_depth = get_depth_sensor_depth();
            ts_printf("Current Depth: %d mm\n", cur_depth);
            sleep_ms(200);
        }

        // 3. 停止电机（达到B点深度后）
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_STOP);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "STOP DC MOTOR TIMEOUT");
            // 关闭电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 停止电机错误
            LOG(LOG_ERROR, "STOP DC MOTOR ERROR");
            // 关闭电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        sleep_ms(50);
        // 4. 启动刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "ENABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "ENABLE BREAK ERROR");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        sleep_ms(50);
        // 5. 关闭刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_DISABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "DISABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 读取错误
            LOG(LOG_ERROR, "DISABLE BREAK ERROR");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        sleep_ms(50);
        // 6. 启动电机
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_TURN_RIGHT);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "TURN RIGHT TIMEOUT");
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);

            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 读取错误
            LOG(LOG_ERROR, "TURN RIGHT ERROR");
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        // 设置采集的参数，并开始采集泥沙数据
        nisha_buffer_init(&g_sediment_buffer);
        NiShaSystemDataCollectTask(3000, CHAIN_MODE_INFINITE, 0);
        // 7. 向A点移动
        while (cur_depth > depth_A && !nisha_task_info.stop_request)
        {
            // 1. 读取深度
            submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 读取超时
                LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                // 读取错误
                LOG(LOG_ERROR, "GET DEPTH ERROR");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            cur_depth = get_depth_sensor_depth();
            ts_printf("Current Depth: %d mm\n", cur_depth);

            // 2. 读取电机状态

            submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_GET_STATE);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 读取超时
                LOG(LOG_ERROR, "GET DC MOTOR STATE TIMEOUT");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                // 读取错误
                LOG(LOG_ERROR, "GET DC MOTOR STATE ERROR");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (DM_STATE_ERROR == get_dc_motor_state1())
            {
                // 电机异常处理
                NiShaSystemProcMotorErrorTask();

                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }

            sleep_ms(200);
        }

        // 8. 停止采集泥沙数据
        stop_NiShaSystemDataCollectTask();
        // 9. 计算结果
        memset(sediment_data, 0, sizeof(sediment_data));
        memset(sediment_data_filtered, 0, sizeof(sediment_data_filtered));
        sediment_data_count = nisha_buffer_get_array(&g_sediment_buffer, sediment_data);
        if (sediment_data_count < 4)
        {
            float favg = sediment_fill_milli_look(calculateAverage(sediment_data, sediment_data_count));
            VDIM_mode_send_sediment_to_screen(favg);
        }
        filter_sediment_data(sediment_data, sediment_data_count, sediment_data_filtered, &sediment_data_average, get_optimal_window_size(sediment_data_count), 3);
        sediment_data_average = sediment_fill_milli_look(sediment_data_average);

        int count_time = 3;
        while (count_time > 0)
        {
            VDIM_mode_send_sediment_to_screen(sediment_data_average);
            count_time--;
        }

        // 这里增加DTU的发送
        SubmitRelayModuleHeartTask();
        g_sediment_data_average = sediment_data_average;
        sl651_send_fixtime_report(&g_stM2UpFramepara);

        // 10. 停止电机
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_STOP);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "STOP DC MOTOR TIMEOUT");
            // 关闭电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 停止电机错误
            LOG(LOG_ERROR, "STOP DC MOTOR ERROR");
            // 关闭电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        sleep_ms(50);
        // 11. 启动刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "ENABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "ENABLE BREAK ERROR");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
    }
    else if (cur_depth > depth_B + nisha_task_info.tolerance) // 当前位置比B深
    {
        // 直接向上移动到达A

        // 1.关闭刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_DISABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "DISABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 读取错误
            LOG(LOG_ERROR, "DISABLE BREAK ERROR");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        sleep_ms(50);
        // 2.启动电机
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_TURN_RIGHT);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "TURN RIGHT TIMEOUT");
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 读取错误
            LOG(LOG_ERROR, "TURN RIGHT ERROR");
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        while (cur_depth > depth_B && !nisha_task_info.stop_request)
        {
            // 3. 读取深度
            submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 读取超时
                LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                // 读取错误
                LOG(LOG_ERROR, "GET DEPTH ERROR");
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            cur_depth = get_depth_sensor_depth();
            ts_printf("Current Depth: %d mm\n", cur_depth);

            // 4. 读取电机状态
            submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_GET_STATE);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 读取超时
                LOG(LOG_ERROR, "GET DC MOTOR STATE TIMEOUT");
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                // 读取错误
                LOG(LOG_ERROR, "GET DC MOTOR STATE ERROR");
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (DM_STATE_ERROR == get_dc_motor_state1())
            {
                // 电机异常处理
                NiShaSystemProcMotorErrorTask();

                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }

            sleep_ms(200);
        }
        // 设置参数，并开始采集泥沙数据
        nisha_buffer_init(&g_sediment_buffer);
        NiShaSystemDataCollectTask(3000, CHAIN_MODE_INFINITE, 0);
        while (cur_depth > depth_A && !nisha_task_info.stop_request)
        {
            // 5. 读取深度
            submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 读取超时
                LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                // 读取错误
                LOG(LOG_ERROR, "GET DEPTH ERROR");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            cur_depth = get_depth_sensor_depth();
            ts_printf("Current Depth: %d mm\n", cur_depth);

            // 6. 读取电机状态
            submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_GET_STATE);
            NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
            if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
            {
                // 读取超时
                LOG(LOG_ERROR, "GET DC MOTOR STATE TIMEOUT");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
            {
                // 读取错误
                LOG(LOG_ERROR, "GET DC MOTOR STATE ERROR");
                // 停止采集
                stop_NiShaSystemDataCollectTask();
                // 关闭电机
                SubmitDcMotorTask(DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }
            if (DM_STATE_ERROR == get_dc_motor_state1())
            {
                // 电机异常处理
                NiShaSystemProcMotorErrorTask();

                // 关闭电机
                SubmitDcMotorTask(NS_DC_MOTOR_STOP);
                // 启动刹车
                SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);

                NiShaUD_Task_Mode_VDIM_Stop_Task();
                return NULL;
            }

            sleep_ms(200);
        }
        // 停止采集泥沙数据
        stop_NiShaSystemDataCollectTask();
        // 计算结果
        memset(sediment_data, 0, sizeof(sediment_data));
        memset(sediment_data_filtered, 0, sizeof(sediment_data_filtered));
        sediment_data_count = nisha_buffer_get_array(&g_sediment_buffer, sediment_data);
        if (sediment_data_count < 4)
        {
            float favg = sediment_fill_milli_look(calculateAverage(sediment_data, sediment_data_count));
            VDIM_mode_send_sediment_to_screen(favg);
        }
        filter_sediment_data(sediment_data, sediment_data_count, sediment_data_filtered, &sediment_data_average, get_optimal_window_size(sediment_data_count), 3);
        sediment_data_average = sediment_fill_milli_look(sediment_data_average);

        int count_time = 3;
        while (count_time > 0)
        {
            VDIM_mode_send_sediment_to_screen(sediment_data_average);
            count_time--;
        }

        // 这里增加DTU的发送
        SubmitRelayModuleHeartTask();
        g_sediment_data_average = sediment_data_average;
        sl651_send_fixtime_report(&g_stM2UpFramepara);

        // 7. 停止电机
        submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_STOP);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "STOP DC MOTOR TIMEOUT");
            // 关闭电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 停止电机错误
            LOG(LOG_ERROR, "STOP DC MOTOR ERROR");
            // 关闭电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        sleep_ms(50);
        // 8. 启动刹车
        submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            LOG(LOG_ERROR, "ENABLE BREAK TIMEOUT");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "ENABLE BREAK ERROR");
            NiShaUD_Task_Mode_VDIM_Stop_Task();
            return NULL;
        }
    }

    NiShaUD_Task_Mode_VDIM_Stop_Task();
    return NULL;
}

void NiShaUD_VDIM_Mode_Start_Task(void *arg)
{
    if (SedimentUD_RUNNING == nisha_task_info.task_state || SedimentUD_ERROR == nisha_task_info.task_state || SedimentUD_INITIALIZING == nisha_task_info.task_state)
    {
        ts_printf("[NISHAUD] VDIM Mode Task is already running or in error state, cannot start again.\n");
        return;
    }
    set_mode_two_start_time();
    nisha_task_info.task_mode = SedimentUD_Mode_VDIM_Measurement;
    nisha_task_info.task_state = SedimentUD_RUNNING;
    nisha_task_info.sec_task_state = SEC_TASK_READY;
    if (-1 == submitTask(g_threadPool, NiShaUD_Task_Mode_VDIM_Measurement, arg))
    {
        NiShaUD_Task_Mode_VDIM_Stop_Task();
    }
}
// 手动模式向上移动
void *NiShaUD_Task_Mode_Manual_Up(void *arg)
{
    ts_printf("[NiShaUD_Task_Mode_Manual_Up] Start\n");
    nisha_task_info.stop_request = false;
    int depth = 0;
    // 1. 关闭刹车
    submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_DISABLE_BREAK);
    NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
    if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
    {
        // 关闭刹车超时
        LOG(LOG_ERROR, "DISABLE BREAK TIMEOUT");
        NiShaUD_clear_task();
        return NULL;
    }
    if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
    {
        // 关闭刹车错误
        LOG(LOG_ERROR, "DISABLE BREAK ERROR");
        NiShaUD_clear_task();
        return NULL;
    }
    sleep_ms(50);
    // 2.启动电机
    submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_TURN_RIGHT);
    NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
    if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
    {
        // 启动刹车
        LOG(LOG_ERROR, "START MOTOR TIMEOUT");
        SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_clear_task();
        return NULL;
    }
    if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
    {
        // 启动电机错误
        LOG(LOG_ERROR, "START MOTOR ERROR");
        SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_clear_task();
        return NULL;
    }

    while (is_dc_motor_working() && !nisha_task_info.stop_request)
    {
        /* code */

        submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "GET DEPTH TIMEOUT");
            // 读取超时
            // 停止电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_clear_task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            LOG(LOG_ERROR, "GET DEPTH ERROR");
            // 读取深度错误
            // 停止电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_clear_task();
            return NULL;
        }

        depth = get_depth_sensor_depth();
        ts_printf("Current Depth: %d mm\n", depth);
        default_mode_send_depth_to_screen(depth * 0.001f); // 发送深度到屏幕
        manual_mode_send_depth_to_screen(depth * 0.001f);  // 显示深度，单位m
        sleep_ms(200);
    }
    ts_printf("[NiShaUD_Task_Mode_Manual_Up] End\n");
    NiShaUD_clear_task();

    return NULL;
}

// 手动模式向下移动
void *NiShaUD_Task_Mode_Manual_Down(void *arg)
{
    ts_printf("[NiShaUD_Task_Mode_Manual_Down] Start\n");
    nisha_task_info.stop_request = false;
    int depth = 0;
    // 1. 关闭刹车
    submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_DISABLE_BREAK);
    NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
    if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
    {
        // 关闭刹车超时
        NiShaUD_clear_task();
        return NULL;
    }
    if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
    {
        // 关闭刹车错误
        NiShaUD_clear_task();
        return NULL;
    }
    sleep_ms(50);
    // 2.启动电机
    submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_TURN_LEFT);
    NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
    if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
    {
        // 启动刹车
        SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_clear_task();
        return NULL;
    }
    if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
    {
        // 启动电机错误
        SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_clear_task();
        return NULL;
    }
    while (is_dc_motor_working() && !nisha_task_info.stop_request)
    {

        /* code */
        submit_nishaUD_task(SubmitDepthSensorTask, NS_DEPTH_GET_DEPTH);
        NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
        if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
        {
            // 读取超时
            // 停止电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_clear_task();
            return NULL;
        }
        if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
        {
            // 读取深度错误
            // 停止电机
            SubmitDcMotorTask(DC_MOTOR_STOP);
            // 启动刹车
            SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
            NiShaUD_clear_task();
            return NULL;
        }
        depth = get_depth_sensor_depth();
        ts_printf("Current Depth: %d mm\n", depth);
        default_mode_send_depth_to_screen(depth * 0.001f); // 发送深度到屏幕
        manual_mode_send_depth_to_screen(depth * 0.001f);  // 显示深度，单位m
        sleep_ms(200);
    }
    NiShaUD_clear_task();
    return NULL;
}

// 停止
void *NiShaUD_Task_Mode_Manual_Stop(void *arg)
{

    // 1. 停止电机
    submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_STOP);
    NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
    if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
    {
        LOG(LOG_ERROR, "NiShaUD_Task_Mode_Manual_Stop: Stop motor timeout");
    }
    if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
    {
        LOG(LOG_ERROR, "NiShaUD_Task_Mode_Manual_Stop: Stop motor error");
    }
    sleep_ms(50);
    // 2.启动刹车
    submit_nishaUD_task(SubmitRelayModuleTask, NS_RELAY_MODULE_ENABLE_BREAK);
    NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
    if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
    {
        LOG(LOG_ERROR, "NiShaUD_Task_Mode_Manual_Stop: Enable brake timeout");
    }
    if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
    {
        LOG(LOG_ERROR, "NiShaUD_Task_Mode_Manual_Stop: Enable brake error");
    }
    // 3. 查询电机状态
    submit_nishaUD_task(SubmitDcMotorTask, NS_DC_MOTOR_GET_STATE);
    NiShaUD_Get_task_state(&nisha_task_info.sec_task_state);
    if (SEC_TASK_TIMEOUT == nisha_task_info.sec_task_state)
    {
        LOG(LOG_ERROR, "NiShaUD_Task_Mode_Manual_Stop: Get motor state timeout");
    }
    if (SEC_TASK_ERROR == nisha_task_info.sec_task_state)
    {
        LOG(LOG_ERROR, "NiShaUD_Task_Mode_Manual_Stop: Get motor state error");
    }
    if (DM_STATE_ERROR == get_dc_motor_state1())
    {
        // 电机异常处理
        NiShaSystemProcMotorErrorTask();
        NiShaUD_Task_Mode_Default_Stop_Task();
    }
    ts_printf("1\n");
    NiShaUD_clear_task();
    ts_printf("2\n");
    return NULL;
}
// 停止正在运行的任务
void NiShaUD_Stop_Running_Task(void)
{
    if (SedimentUD_RUNNING != nisha_task_info.task_state)
    {
        ts_printf("NiShaUD_Stop_Running_Task error return! state is %d,stop move\n", nisha_task_info.task_state);
        return;
    }
    nisha_task_info.stop_request = true;
}

void NiShaUD_clear_task(void)
{
    nisha_task_info.sec_task_state = SEC_TASK_READY;
    nisha_task_info.task_state = SedimentUD_IDLE;
}

int GetNiShaUD_Task_State(void)
{
    return nisha_task_info.task_state;
}
void NiShaUD_Manual_Mode_Stop_Task(void *arg)
{
    // if (SedimentUD_Mode_Manual != nisha_task_info.task_mode)
    // {
    //     ts_printf("not manual control mode\n");
    //     return;
    // }
    nisha_task_info.sec_task_state = SEC_TASK_READY;
    nisha_task_info.task_state = SedimentUD_RUNNING;
    nisha_task_info.task_mode = SedimentUD_Mode_Manual;

    if (-1 == submitTask(g_threadPool, NiShaUD_Task_Mode_Manual_Stop, arg))
    {
        NiShaUD_clear_task();
    }
}

void NiShaUD_Manual_Mode_Up_Task(void *arg)
{
    if (SedimentUD_RUNNING == nisha_task_info.task_state /*|| SedimentUD_ERROR == nisha_task_info.task_state */ || SedimentUD_INITIALIZING == nisha_task_info.task_state)
    {
        ts_printf("[NISHAUD] Manual Mode Up Task is already running or in error state, cannot start again.\n");
        // 如果任务正在运行或处于错误状态，直接返回
        return;
    }

    nisha_task_info.sec_task_state = SEC_TASK_READY;
    nisha_task_info.task_state = SedimentUD_RUNNING;
    nisha_task_info.task_mode = SedimentUD_Mode_Manual;

    if (-1 == submitTask(g_threadPool, NiShaUD_Task_Mode_Manual_Up, arg))
    {
        NiShaUD_clear_task();
    }
}

void NiShaUD_Manual_Mode_Down_Task(void *arg)
{
    if (SedimentUD_RUNNING == nisha_task_info.task_state /*|| SedimentUD_ERROR == nisha_task_info.task_state */ || SedimentUD_INITIALIZING == nisha_task_info.task_state)
    {

        ts_printf("[NISHAUD] Manual Mode Down Task is already running or in error state state is %d, cannot start again.\n", nisha_task_info.task_state);
        return;
    }
    nisha_task_info.sec_task_state = SEC_TASK_READY;
    nisha_task_info.task_state = SedimentUD_RUNNING;
    nisha_task_info.task_mode = SedimentUD_Mode_Manual;

    if (-1 == submitTask(g_threadPool, NiShaUD_Task_Mode_Manual_Down, arg))
    {
        NiShaUD_clear_task();
    }
}

void check_device_online(device_online_status_t *dev_online_status)
{
    dev_online_status->dc_motor = is_dc_motor_online();
    dev_online_status->depth_sensor = is_depth_sensor_online();
    dev_online_status->sediment_sensor = is_sediment_sensor_online();
    dev_online_status->relay_module = isRelayModuleOnline();
    dev_online_status->encoder = isEncoderOnline();

    ts_printf("Device Online Status1:\n");
    ts_printf("DC Motor: %s\n",
              get_device_index(DC_MOTOR) < 0 ? "N/A"
              : (dev_online_status->dc_motor ? "Online" : "Offline"));
    ts_printf("Depth Sensor: %s\n",
              get_device_index(DEPTH_SENSOR) < 0 ? "N/A"
              : (dev_online_status->depth_sensor ? "Online" : "Offline"));
    ts_printf("Sediment Sensor: %s\n",
              get_device_index(SEDIMENT_SENSOR) < 0 ? "N/A"
              : (dev_online_status->sediment_sensor ? "Online" : "Offline"));
    ts_printf("Relay Module: %s\n",
              get_device_index(RELAY_MODULE) < 0 ? "N/A"
              : (dev_online_status->relay_module ? "Online" : "Offline"));
    ts_printf("Encoder: %s\n",
              get_device_index(ENCODER) < 0 ? "N/A"
              : (dev_online_status->encoder ? "Online" : "Offline"));
}

// 比较函数，用于 qsort 排序
int compare(const void *a, const void *b)
{
    return (*(int *)a - *(int *)b);
}

// 筛选、排序有效数据，并将无效数据放后面
int filterAndSort(int *arr, int size, int threshold, int minSpacing)
{
    if (arr == NULL || size <= 0)
        return 0;

    int *temp = (int *)malloc(size * sizeof(int));
    int *filtered = (int *)malloc(size * sizeof(int));

    if (temp == NULL || filtered == NULL)
    {
        printf("Memory allocation failed.\n");
        free(temp);
        free(filtered);
        return 0;
    }

    int validCount = 0;

    // 第一步：复制所有 > threshold 的数据到 temp
    for (int i = 0; i < size; i++)
    {
        if (arr[i] > threshold)
        {
            temp[validCount++] = arr[i];
        }
    }

    // 第二步：对有效数据排序
    qsort(temp, validCount, sizeof(int), compare);

    // 第三步：过滤掉不符合最小间隔要求的数据
    int filteredCount = 0;
    for (int i = 0; i < validCount; i++)
    {
        if (i == 0 || (temp[i] - temp[i - 1]) >= minSpacing)
        {
            filtered[filteredCount++] = temp[i];
        }
    }

    // 第四步：将合法的有效数据写回原数组前部
    for (int i = 0; i < filteredCount; i++)
    {
        arr[i] = filtered[i];
    }

    // 第五步：把原始数组中无效数据填入剩余位置（<= threshold）
    int idx = filteredCount;
    for (int i = 0; i < size; i++)
    {
        if (arr[i] <= threshold)
        {
            arr[idx++] = arr[i];
        }
    }

    // 注意：这里不会丢弃那些虽然 > threshold 但因间距不足而被剔除的数据
    // 它们仍然会保留在数组末尾（因为它们原本就是数组的一部分）

    free(temp);
    free(filtered);
    return filteredCount;
}

// 函数定义：计算平均值
float calculateAverage(float arr[], int size)
{
    if (size == 0)
    {
        // printf("错误：数组大小为0，无法计算平均值。\n");
        return 0.0f;
    }

    float sum = 0.0f;
    for (int i = 0; i < size; i++)
    {
        sum += arr[i];
    }

    return sum / size;
}

void save_keep_depth_in_file(int depth_mm)
{
    char path[MAX_PATH];
    char appDir[MAX_PATH];

    memset(path, 0, sizeof(path));
    memset(appDir, 0, sizeof(appDir));
    if (GetAppDir(appDir)) {
        snprintf(path, sizeof(path), "%s/%s", appDir, NISHA_FILE_NAME);
        write_int_to_file(path, depth_mm);
    } else {
        write_int_to_file(NISHA_FILE_NAME, depth_mm);
    }
}

void get_keep_depth_from_file(int *depth_mm)
{
    char path[MAX_PATH];
    char appDir[MAX_PATH];
    int ret;

    if (depth_mm == NULL)
    {
        return;
    }
    memset(path, 0, sizeof(path));
    memset(appDir, 0, sizeof(appDir));
    if (GetAppDir(appDir)) {
        snprintf(path, sizeof(path), "%s/%s", appDir, NISHA_FILE_NAME);
        ret = read_int_from_file(path, depth_mm);
    } else {
        ret = read_int_from_file(NISHA_FILE_NAME, depth_mm);
    }
    if (ret == -1)
    {
        // 如果读取失败，设置为默认值
        *depth_mm = 500; // 默认深度500mm
    }
}

void init_nisha_default_keep_depth(void)
{
    int depth_mm = 0;
    // 从文件中读取深度
    get_keep_depth_from_file(&depth_mm);
    if (depth_mm < 300)
    {
        // 如果深度不在合理范围内，设置为默认值
        depth_mm = 500; // 默认深度500mm
    }
    set_default_mode_keep_depth(depth_mm);
}