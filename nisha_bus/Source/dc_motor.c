#include "dc_motor.h"
#include "device_sim.h"
dc_motor_info_t dc_motor_info = {
    .error_code = DC_MOTOR_ERROR_CODE_NONE,
    .is_running = false,
    .online = false,
    .state1 = DM_STATE_STOP,
};
dc_motor_config_t dc_motor_config = {
    .motor_speed = 100,
    .motor_direction = 1, // 1: clockwise, 2: counterclockwise
};

void set_dc_motor_speed(int speed)
{
    dc_motor_config.motor_speed = speed;
    SubmitDcMotorTask(DC_MOTOR_SET_SPEED);
}
void set_dc_motor_working(bool is_running)
{
    dc_motor_info.is_running = is_running;
}
void set_dc_motor_error_code(dc_motor_error_code_t error_code)
{
    dc_motor_info.error_code = error_code;
}
void set_dc_motor_state1(dc_motor_state1_t state1)
{
    dc_motor_info.state1 = state1;
}

void set_dc_motor_online(bool online)
{
    dc_motor_info.online = online;
}

bool is_dc_motor_online(void)
{
    return dc_motor_info.online;
}

int get_dc_motor_state1(void)
{
    return dc_motor_info.state1;
}

bool is_dc_motor_working(void)
{
    return dc_motor_info.is_running;
}
int send_dc_motor_stop_command(int fd)
{
    // 01 06 20 00 00 05 42 09
    uint8_t tempInfo[10] = {0x01, 0x06, 0x20, 0x00, 0x00, 0x05, 0x42, 0x09};
    write(fd, tempInfo, 8);
    return 0;
}

int send_dc_motor_turn_left_command(int fd)
{
    uint8_t tempInfo[10] = {0x01, 0x06, 0x20, 0x00, 0x00, 0x02, 0x03, 0xCB};
    write(fd, tempInfo, 8);
    return 0;
}

int send_dc_motor_turn_right_command(int fd)
{
    uint8_t tempInfo[10] = {0x01, 0x06, 0x20, 0x00, 0x00, 0x01, 0x43, 0xCA};
    write(fd, tempInfo, 8);
    return 0;
}

int send_dc_motor_jog_turn_left_command(int fd)
{
    uint8_t tempInfo[10] = {0x01, 0x06, 0x20, 0x00, 0x00, 0x04, 0x83, 0xC9};
    write(fd, tempInfo, 8);
    return 0;
}

int send_dc_motor_jog_turn_right_command(int fd)
{
    uint8_t tempInfo[10] = {0x01, 0x06, 0x20, 0x00, 0x00, 0x03, 0xC2, 0x0B};
    write(fd, tempInfo, 8);
    return 0;
}

int send_dc_motor_set_speed_command(int fd, int speed)
{
    uint8_t buffer[8] = {0};
    uint8_t tempInfo[8] = {0x01, 0x06, 0x20, 0x01};
    uint16_t speed_16bit = speed & 0xFFFF;     // 确保只取低16位
    tempInfo[4] = (uint8_t)(speed_16bit >> 8); // 高字节
    tempInfo[5] = (uint8_t)(speed_16bit);      // 低字节
    appendCRCtoBuffer(tempInfo, 6, buffer, 8);
    write(fd, buffer, 8);
    return 0;
}

int send_dc_motor_reset_errorcode_command(int fd)
{
    uint8_t tempInfo[10] = {0x01, 0x06, 0x20, 0x00, 0x00, 0x07, 0xC3, 0xC8};
    write(fd, tempInfo, 8);
    return 0;
}

int send_dc_motor_get_errorcode_command(int fd)
{
    uint8_t tempInfo[10] = {0x01, 0x03, 0x21, 0x02, 0x00, 0x01, 0x2F, 0xF6};
    write(fd, tempInfo, 8);
    return 0;
}

int send_dc_motor_get_state_command(int fd)
{
    uint8_t tempInfo[10] = {0x01, 0x03, 0x21, 0x00, 0x00, 0x01, 0x8E, 0x36};
    write(fd, tempInfo, 8);
    return 0;
}

int send_dc_motor_DI1_command(int fd)
{
    // 向电机发送指令，查询是不是有旋转限制
    // 正转限制查询
    uint8_t buf[8] = {0x01, 0x03, 0x30, 0x0D, 0x00, 0x01, 0x1A, 0xC9};
    write(fd, buf, 8);
    return 0;
}

int send_dc_motor_DI2_command(int fd)
{
    // 反转限制查询
    uint8_t buf[8] = {0x01, 0x03, 0x30, 0x0D, 0x00, 0x01, 0x1A, 0xC9};
    write(fd, buf, 8);
    return 0;
}

void dc_motor_emergency_up_stop(int fd, uint8_t *recvBuffer)
{
    if ((recvBuffer[4] & 0x01) == 1)
    {
        ts_printf("stop up\n\n");
        // 关闭电机
        SubmitDcMotorTask(NS_DC_MOTOR_STOP);
        // sleep_ms(10);
        // 启动刹车
        // SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
        // sleep_ms(200);
        // 松开刹车
        SubmitRelayModuleTask(NS_RELAY_MODULE_DISABLE_BREAK);
        // sleep_ms(20);
        // 反向移动电机
        set_dc_motor_speed(600); // 600转/分
        SubmitDcMotorTask(NS_DC_MOTOR_TURN_LEFT);
        time_t current_time_sec = time(NULL);
        while ((time(NULL) - current_time_sec) < 1)
        ;
        // sleep_ms(1000);
        // 关闭电机
        SubmitDcMotorTask(NS_DC_MOTOR_STOP);
        // sleep_ms(20);
        // 启动刹车
        SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_clear_task();
    }
}

void dc_motor_emergency_down_stop(int fd, uint8_t *recvBuffer)
{
    if ((recvBuffer[4] & 0x02) == 1)
    {
        ts_printf("stop down\n\n");
        // 关闭电机
        SubmitDcMotorTask(NS_DC_MOTOR_STOP);
        // sleep_ms(10);
        // 启动刹车
        // SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
        // sleep_ms(200);
        // 松开刹车
        SubmitRelayModuleTask(NS_RELAY_MODULE_DISABLE_BREAK);
        // sleep_ms(20);
        // 反向移动电机
        set_dc_motor_speed(600); // 600转/分
        SubmitDcMotorTask(NS_DC_MOTOR_TURN_RIGHT);
        time_t current_time_sec = time(NULL);
        while ((time(NULL) - current_time_sec) < 1)
        ;
        // sleep_ms(1000);
        // 关闭电机
        SubmitDcMotorTask(NS_DC_MOTOR_STOP);
        // sleep_ms(20);
        // 启动刹车
        SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
        NiShaUD_clear_task();
    }
}

void dc_motor_emergency_stop(int fd)
{
    // 关闭电机
    SubmitDcMotorTask(NS_DC_MOTOR_STOP);
    sleep_ms(10);
    // 启动刹车
    SubmitRelayModuleTask(NS_RELAY_MODULE_ENABLE_BREAK);
    NiShaUD_clear_task();
}

int SubmitDcMotorTask(uart_dcmotor_task_name taskName)
{
    int index;

    if (DeviceSim_SubmitMotor((int)taskName))
        return 0;

    index = get_device_index(DC_MOTOR);
    if (index < 0 || index >= MAX_UART_NUM)
        return -3;
    task_item_t tempTask;
    tempTask.priority = 1;
    tempTask.sendTime = time(NULL);
    tempTask.taskName.dc_motor_task = taskName;
    tempTask.deviceName = DC_MOTOR;
    EnTaskQueue_List(&g_uartWorkInfoArry[index].taskQueue, &tempTask);
    return 0;
}

void process_dc_motor_error_code(uint8_t *recvBuffer)
{
    uint16_t code = (uint16_t)(recvBuffer[0]) << 8 | (uint16_t)(recvBuffer[1]);

    // 错误码描述表（可根据需求扩展）
    typedef struct
    {
        uint16_t code;
        const char *description;
    } ErrorCodeInfo;

    static const ErrorCodeInfo errorTable[] = {
        {HARDWARE_ACCELERATION_OVERCURRENT, "Hardware acceleration overcurrent"},
        {HARDWARE_DECELERATION_OVERCURRENT, "Hardware deceleration overcurrent"},
        {HARDWARE_CONSTANT_OVERCURRENT, "Hardware constant overcurrent"},
        {SOFTWARE_ACCELERATION_OVERCURRENT, "Software acceleration overcurrent"},
        {SOFTWARE_DECELERATION_OVERCURRENT, "Software deceleration overcurrent"},
        {SOFTWARE_CONSTANT_OVERCURRENT, "Software constant overcurrent"},
        {OVERVOLTAGE_DURING_ACCELERATION, "Overvoltage during acceleration"},
        {OVERVOLTAGE_DURING_DECELERATION, "Overvoltage during deceleration"},
        {OVERVOLTAGE_DURING_CONSTANT, "Overvoltage during constant speed"},
        {BUS_UNDERVOLTAGE_FAULT, "Bus undervoltage fault"},
        {MOTOR_OVERLOAD, "Motor overload"},
        {DRIVER_OVERLOAD, "Driver overload"},
        {HALL_SENSOR_FAULT, "Hall sensor fault"},
        {STALL_FAULT, "Stall fault"},
        {MODULE_OVERHEAT_1, "Module overheat 1"},
        {MODULE_OVERHEAT_2, "Module overheat 2"},
        {EXTERNAL_INPUT_FAULT, "External input fault"},
        {CURRENT_DETECTION_FAULT, "Current detection fault"},
        {INVERTER_PHASE_U_FAULT, "Inverter phase U fault"},
        {INVERTER_PHASE_V_FAULT, "Inverter phase V fault"},
        {INVERTER_PHASE_W_FAULT, "Inverter phase W fault"},
        {ELECTRONIC_OVERLOAD_FAULT, "Electronic overload fault"},
        {ELECTRONIC_UNDERLOAD_FAULT, "Electronic underload fault"},
        {SHORT_CIRCUIT_FAULT, "Short circuit fault"},
        {KEYBOARD_COMMUNICATION_FAULT, "Keyboard communication fault"},
        {PARAMETER_STORAGE_FAULT, "Parameter storage fault"},
        {CONDUCTION_FAULT, "Conduction fault"},
        {OVERCURRENT_FEEDBACK_FAULT, "Overcurrent feedback fault"},
        {OUTPUT_PHASE_LOSS, "Output phase loss"},
        {DC_MOTOR_ERROR_CODE_NONE, "No error"},
    };

    const size_t tableSize = sizeof(errorTable) / sizeof(errorTable[0]);
    const char *errorMsg = NULL;

    for (size_t i = 0; i < tableSize; i++)
    {
        if (code == errorTable[i].code)
        {
            errorMsg = errorTable[i].description;
            break;
        }
    }

    if (errorMsg != NULL)
    {
        set_dc_motor_error_code(code);
        LOG(LOG_ERROR, "DC Motor Error: %s", errorMsg);
    }
    else
    {
        LOG(LOG_WARNING, "Unknown DC Motor Error Code: 0x%04X", code);
    }
}
void process_dc_motor_state_data(uint8_t *recvBuffer)
{
    uint16_t state = (uint16_t)(recvBuffer[0]) << 8 | (uint16_t)(recvBuffer[1]);
    switch (state)
    {
    case 1:
        /* code */
        set_dc_motor_state1(DM_STATE_TURN_RIGHT);
        set_dc_motor_working(true);
        break;
    case 2:
        set_dc_motor_state1(DM_STATE_TURN_LEFT);
        set_dc_motor_working(true);
        break;
    case 3:
        set_dc_motor_state1(DM_STATE_STOP);
        set_dc_motor_working(false);
        break;
    case 4:
        set_dc_motor_state1(DM_STATE_ERROR);
        set_dc_motor_working(false);
        break;
    case 5:
        set_dc_motor_state1(DM_STATE_OFF);
        set_dc_motor_working(false);
        break;
    case 6:
        set_dc_motor_state1(DM_STATE_EBREAK);
        set_dc_motor_working(false);
        break;
    default:
        break;
    }
}

int DC_MotorSendCommand(uart_dcmotor_task_name taskName, int fd, void *value)
{
    switch (taskName)
    {
    case DC_MOTOR_STOP:
    case NS_DC_MOTOR_STOP:
        return send_dc_motor_stop_command(fd);
        break;
    case DC_MOTOR_TURN_LEFT:
    case NS_DC_MOTOR_TURN_LEFT:
        return send_dc_motor_turn_left_command(fd);
        break;
    case DC_MOTOR_TURN_RIGHT:
    case NS_DC_MOTOR_TURN_RIGHT:
        return send_dc_motor_turn_right_command(fd);
        break;
    case DC_MOTOR_SET_SPEED:
        return send_dc_motor_set_speed_command(fd, dc_motor_config.motor_speed);
        break;
    case DC_MOTOR_JOG_TURN_LEFT:
        return send_dc_motor_jog_turn_left_command(fd);
        break;
    case DC_MOTOR_JOG_TURN_RIGHT:
        return send_dc_motor_jog_turn_right_command(fd);
        break;
    case DC_MOTOR_RESET_ERROR_CODE:
    case NS_DC_MOTOR_RESET_ERROR_CODE:
        return send_dc_motor_reset_errorcode_command(fd);
        break;
    case DC_MOTOR_GET_STATE:
    case IS_DC_MOTOR_ONLINE:
    case dc_motor_running_online_check: // 运行过程中的在线检测
    case NS_DC_MOTOR_GET_STATE:
        return send_dc_motor_get_state_command(fd);
        break;
    case DC_MOTOR_GET_ERROR_CODE:
    case NS_DC_MOTOR_GET_ERROR_CODE:
        return send_dc_motor_get_errorcode_command(fd);
    case DC_MOTOR_GET_DI1:
        return send_dc_motor_DI1_command(fd);
        break;
    case DC_MOTOR_GET_DI2:
        return send_dc_motor_DI2_command(fd);
        break;
    default:
        break;
    }

    return 0;
}

int DcMotorDataProcessCtrl(int taskName, uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc)
{
    int ret = 0;
    // ts_printf("DC_motor buf: ");
    // print_data(recvddatalen, recvBuffer);
    // ts_printf("\n");
    ret = checkFunc(recvBuffer, recvddatalen, 0x01);
    if (ret == DATA_PROC_ERROR)
        return DATA_PROC_ERROR;
    if (ret == DATA_PROC_INCOMPLETE)
        return DATA_PROC_INCOMPLETE;

    switch (taskName)
    {
    case DC_MOTOR_STOP:
        set_dc_motor_working(false);
        break;
    case DC_MOTOR_TURN_LEFT:
        set_dc_motor_working(true);
        break;
    case DC_MOTOR_TURN_RIGHT:
        set_dc_motor_working(true);
        break;
    case DC_MOTOR_SET_SPEED:
        break;
    case DC_MOTOR_JOG_TURN_LEFT:
        break;
    case DC_MOTOR_JOG_TURN_RIGHT:
        break;
    case DC_MOTOR_GET_ERROR_CODE:
        break;
    case DC_MOTOR_RESET_ERROR_CODE:
        break;
    case DC_MOTOR_GET_STATE:
        break;
    case IS_DC_MOTOR_ONLINE:
        NS_sys_check_dc_motor();
        break;
    case dc_motor_running_online_check:
        if (dc_motor_info.online == false)
        {
            set_dc_motor_online(true);
        }
        break;
    case NS_DC_MOTOR_TURN_RIGHT:
        set_dc_motor_working(true);
        NiShaUD_dc_motor_turn_right();
        break;
    case NS_DC_MOTOR_TURN_LEFT:
        set_dc_motor_working(true);
        NiShaUD_dc_motor_turn_left();
        break;
    case NS_DC_MOTOR_GET_STATE:
        process_dc_motor_state_data(recvBuffer + 3);
        NiShaUD_dc_motor_get_state();
        break;
    case NS_DC_MOTOR_STOP:
        set_dc_motor_working(false);
        NiShaUD_dc_motor_stop();
        break;
    case NS_DC_MOTOR_GET_ERROR_CODE:
        NS_sys_proc_motor_error_dc_motor(recvBuffer + 3);
        break;
    case NS_DC_MOTOR_RESET_ERROR_CODE:
        NS_sys_reset_motor_error_dc_motor();
        break;
    case DC_MOTOR_GET_DI1:
        // ts_printf("DC_motor buf up: ");
        // print_data(recvddatalen, recvBuffer);
        // ts_printf("\n");
        dc_motor_emergency_up_stop(g_uartWorkInfoArry[4].comfd, recvBuffer);
        break;
    case DC_MOTOR_GET_DI2:
        // ts_printf("DC_motor buf down: ");
        // print_data(recvddatalen, recvBuffer);
        // ts_printf("\n");
        dc_motor_emergency_down_stop(g_uartWorkInfoArry[4].comfd, recvBuffer);
        break;
    default:
        break;
    }
    return ret;
}

bool DealWithDcMotorTimeOut(int taskName)
{
    bool ret = true;
    switch (taskName)
    {
    case DC_MOTOR_STOP:
        set_dc_motor_online(false);
        break;
    case DC_MOTOR_TURN_LEFT:
        break;
    case DC_MOTOR_TURN_RIGHT:
        break;
    case DC_MOTOR_SET_SPEED:
        break;
    case DC_MOTOR_JOG_TURN_LEFT:
        break;
    case DC_MOTOR_JOG_TURN_RIGHT:
        break;
    case DC_MOTOR_GET_ERROR_CODE:
        break;
    case DC_MOTOR_RESET_ERROR_CODE:
        break;
    case DC_MOTOR_GET_STATE:
        break;
    case IS_DC_MOTOR_ONLINE:
        NS_sys_check_dc_motor_timeout();
        break;
    case dc_motor_running_online_check:
        if (dc_motor_info.online == true)
        {
            set_dc_motor_online(false);
        }
        break;
    case NS_DC_MOTOR_TURN_LEFT:
        NiShaUD_dc_motor_turn_left_timeout();
        break;
    case NS_DC_MOTOR_TURN_RIGHT:
        NiShaUD_dc_motor_turn_right_timeout();
        break;
    case NS_DC_MOTOR_GET_STATE:
        NiShaUD_dc_motor_get_state_timeout();
        break;
    case NS_DC_MOTOR_STOP:
        NiShaUD_dc_motor_stop_timeout();
        break;
    case NS_DC_MOTOR_GET_ERROR_CODE:
        NS_sys_proc_motor_error_dc_motor_timeout();
        break;
    case NS_DC_MOTOR_RESET_ERROR_CODE:
        NS_sys_reset_motor_error_dc_motor_timeout();
        break;
    case DC_MOTOR_GET_DI1:
        dc_motor_emergency_stop(g_uartWorkInfoArry[4].comfd);
        set_dc_motor_online(false);
        break;
    case DC_MOTOR_GET_DI2:
        dc_motor_emergency_stop(g_uartWorkInfoArry[4].comfd);
        set_dc_motor_online(false);
        break;
    default:
        break;
    }
    return ret;
}