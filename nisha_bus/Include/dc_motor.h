#ifndef __DC_MOTOR_H__
#define __DC_MOTOR_H__
#include "uart.h"

typedef enum
{
    DC_MOTOR_STOP,                 // 停止
    DC_MOTOR_TURN_RIGHT,           // 顺时针转
    DC_MOTOR_TURN_LEFT,            // 逆时针转
    DC_MOTOR_SET_SPEED,            // 设置速度
    DC_MOTOR_JOG_TURN_RIGHT,       // 电机点动 顺时针
    DC_MOTOR_JOG_TURN_LEFT,        // 电机点动 逆时针
    DC_MOTOR_GET_ERROR_CODE,       // 获取故障码
    DC_MOTOR_RESET_ERROR_CODE,     // 重置故障码
    DC_MOTOR_GET_STATE,            // 读取当前状态
    IS_DC_MOTOR_ONLINE,            // 驱动器在线状态
    NS_DC_MOTOR_TURN_LEFT,         // [泥沙模式]下电机逆时针转
    NS_DC_MOTOR_TURN_RIGHT,        // [泥沙模式]下电机顺时针转
    NS_DC_MOTOR_GET_STATE,         // [泥沙模式]下读取当前状态
    NS_DC_MOTOR_STOP,              // [泥沙模式]下电机停机
    NS_DC_MOTOR_GET_ERROR_CODE,    // [泥沙模式]下获取故障码
    NS_DC_MOTOR_RESET_ERROR_CODE,  // [泥沙模式]下重置故障码
    DC_MOTOR_GET_DI1,              // 获取DI1输入口的寄存器状态
    DC_MOTOR_GET_DI2,              // 获取DI2输入口的寄存器状态
    dc_motor_running_online_check, // 运行过程中检测电机是否在线
} uart_dcmotor_task_name;

typedef enum
{
    DM_STATE_TURN_RIGHT = 1, // 正转运行中
    DM_STATE_TURN_LEFT,      // 反转运行中
    DM_STATE_STOP,           // 驱动器停机中
    DM_STATE_ERROR,          // 驱动器故障
    DM_STATE_OFF,            // 驱动器OFF状态
    DM_STATE_EBREAK,         // 电子刹车状态

} dc_motor_state1_t;

typedef enum
{
    HARDWARE_ACCELERATION_OVERCURRENT = 1, // 硬件加速过流
    HARDWARE_DECELERATION_OVERCURRENT,     // 硬件减速过流
    HARDWARE_CONSTANT_OVERCURRENT,         // 硬件恒速过流
    SOFTWARE_ACCELERATION_OVERCURRENT,     // 软件加速过流
    SOFTWARE_DECELERATION_OVERCURRENT,     // 软件减速过流
    SOFTWARE_CONSTANT_OVERCURRENT,         // 软件恒速过流
    OVERVOLTAGE_DURING_ACCELERATION,       // 加速时过压
    OVERVOLTAGE_DURING_DECELERATION,       // 减速时过压
    OVERVOLTAGE_DURING_CONSTANT,           // 恒速时过压
    BUS_UNDERVOLTAGE_FAULT,                // 母线欠压故障
    MOTOR_OVERLOAD,                        // 电机过载
    DRIVER_OVERLOAD,                       // 驱动器过载
    HALL_SENSOR_FAULT,                     // 霍尔故障
    STALL_FAULT,                           // 堵转故障
    MODULE_OVERHEAT_1,                     // 模块过热1
    MODULE_OVERHEAT_2,                     // 模块过热2
    EXTERNAL_INPUT_FAULT,                  // 外部输入故障
    CURRENT_DETECTION_FAULT,               // 电流检测故障
    INVERTER_PHASE_U_FAULT,                // 逆变U相故障
    INVERTER_PHASE_V_FAULT,                // 逆变V相故障
    INVERTER_PHASE_W_FAULT,                // 逆变W相故障
    ELECTRONIC_OVERLOAD_FAULT,             // 电子过载故障
    ELECTRONIC_UNDERLOAD_FAULT,            // 电子欠载故障
    SHORT_CIRCUIT_FAULT,                   // 短路故障
    KEYBOARD_COMMUNICATION_FAULT,          // 键盘通讯故障
    PARAMETER_STORAGE_FAULT,               // 参数存储故障
    CONDUCTION_FAULT,                      // 导通故障
    OVERCURRENT_FEEDBACK_FAULT,            // 过流反馈故障
    OUTPUT_PHASE_LOSS,                     // 输出缺相
    DC_MOTOR_ERROR_CODE_NONE,              //  无
} dc_motor_error_code_t;

typedef struct dc_motor_config
{
    /* data */
    int motor_speed;
    int motor_direction; // 1: clockwise - 上, 2: counterclockwise - 上
} dc_motor_config_t;

/**
 *
 * 软件减速过流	software_deceleration_overcurrent	SOFTWARE_DECELERATION_OVERCURRENT
 * 软件恒速过流	software_constant_overcurrent	SOFTWARE_CONSTANT_OVERCURRENT
 * 加速时过压	overvoltage_during_acceleration	OVERVOLTAGE_DURING_ACCELERATION
 * 减速时过压	overvoltage_during_deceleration	OVERVOLTAGE_DURING_DECELERATION
 * 恒速时过压	overvoltage_during_constant	OVERVOLTAGE_DURING_CONSTANT
 * 母线欠压故障	bus_undervoltage_fault	BUS_UNDERVOLTAGE_FAULT
 * 电机过载	motor_overload	MOTOR_OVERLOAD
 * 驱动器过载	driver_overload	DRIVER_OVERLOAD
 * 霍尔故障	hall_sensor_fault	HALL_SENSOR_FAULT
 * 堵转故障	stall_fault	STALL_FAULT
 * 模块过热1	module_overheat_1	MODULE_OVERHEAT_1
 * 模块过热2	module_overheat_2	MODULE_OVERHEAT_2
 * 外部输入故障	external_input_fault	EXTERNAL_INPUT_FAULT
 * 电流检测故障	current_detection_fault	CURRENT_DETECTION_FAULT
 * 逆变U相故障	inverter_phase_u_fault	INVERTER_PHASE_U_FAULT
 * 逆变V相故障	inverter_phase_v_fault	INVERTER_PHASE_V_FAULT
 * 逆变W相故障	inverter_phase_w_fault	INVERTER_PHASE_W_FAULT
 * 电子过载故障	electronic_overload_fault	ELECTRONIC_OVERLOAD_FAULT
 * 电子欠载故障	electronic_underload_fault	ELECTRONIC_UNDERLOAD_FAULT
 * 短路故障	short_circuit_fault	SHORT_CIRCUIT_FAULT
 * 键盘通讯故障	keyboard_communication_fault	KEYBOARD_COMMUNICATION_FAULT
 * 参数存储故障	parameter_storage_fault	PARAMETER_STORAGE_FAULT
 * 导通故障	conduction_fault	CONDUCTION_FAULT
 * 过流反馈故障	overcurrent_feedback_fault	OVERCURRENT_FEEDBACK_FAULT
 * 输出缺相	output_phase_loss	OUTPUT_PHASE_LOSS
 */

// Fault codes definition
// #define SOFTWARE_DECELERATION_OVERCURRENT "software_deceleration_overcurrent" // 软件减速过流
// #define SOFTWARE_CONSTANT_OVERCURRENT "software_constant_overcurrent"         // 软件恒速过流
// #define OVERVOLTAGE_DURING_ACCELERATION "overvoltage_during_acceleration"     // 加速时过压
// #define OVERVOLTAGE_DURING_DECELERATION "overvoltage_during_deceleration"     // 减速时过压
// #define OVERVOLTAGE_DURING_CONSTANT "overvoltage_during_constant"             // 恒速时过压
// #define BUS_UNDERVOLTAGE_FAULT "bus_undervoltage_fault"                       // 母线欠压故障
// #define MOTOR_OVERLOAD "motor_overload"                                       // 电机过载
// #define DRIVER_OVERLOAD "driver_overload"                                     // 驱动器过载
// #define HALL_SENSOR_FAULT "hall_sensor_fault"                                 // 霍尔故障
// #define STALL_FAULT "stall_fault"                                             // 堵转故障
// #define MODULE_OVERHEAT_1 "module_overheat_1"                                 // 模块过热1
// #define MODULE_OVERHEAT_2 "module_overheat_2"                                 // 模块过热2
// #define EXTERNAL_INPUT_FAULT "external_input_fault"                           // 外部输入故障
// #define CURRENT_DETECTION_FAULT "current_detection_fault"                     // 电流检测故障
// #define INVERTER_PHASE_U_FAULT "inverter_phase_u_fault"                       // 逆变U相故障
// #define INVERTER_PHASE_V_FAULT "inverter_phase_v_fault"                       // 逆变V相故障
// #define INVERTER_PHASE_W_FAULT "inverter_phase_w_fault"                       // 逆变W相故障
// #define ELECTRONIC_OVERLOAD_FAULT "electronic_overload_fault"                 // 电子过载故障
// #define ELECTRONIC_UNDERLOAD_FAULT "electronic_underload_fault"               // 电子欠载故障
// #define SHORT_CIRCUIT_FAULT "short_circuit_fault"                             // 短路故障
// #define KEYBOARD_COMMUNICATION_FAULT "keyboard_communication_fault"           // 键盘通讯故障
// #define PARAMETER_STORAGE_FAULT "parameter_storage_fault"                     // 参数存储故障
// #define CONDUCTION_FAULT "conduction_fault"                                   // 导通故障
// #define OVERCURRENT_FEEDBACK_FAULT "overcurrent_feedback_fault"               // 过流反馈故障
// #define OUTPUT_PHASE_LOSS "output_phase_loss"                                 // 输出缺相

typedef struct dc_motor
{
    bool online;
    bool is_running;
    dc_motor_error_code_t error_code;
    dc_motor_state1_t state1;
} dc_motor_info_t;

void set_dc_motor_speed(int speed);

void set_dc_motor_working(bool is_running);

void set_dc_motor_error_code(dc_motor_error_code_t error_code);

void set_dc_motor_state1(dc_motor_state1_t state1);

void set_dc_motor_online(bool online);

bool is_dc_motor_online(void);

int get_dc_motor_state1(void);

bool is_dc_motor_working(void);

int send_dc_motor_stop_command(int fd);

int send_dc_motor_turn_left_command(int fd);

int send_dc_motor_turn_right_command(int fd);

int send_dc_motor_jog_turn_left_command(int fd);

int send_dc_motor_jog_turn_right_command(int fd);

int send_dc_motor_set_speed_command(int fd, int speed);

int send_dc_motor_reset_errorcode_command(int fd);

int send_dc_motor_get_errorcode_command(int fd);

int send_dc_motor_get_state_command(int fd);

int send_dc_motor_DI1_command(int fd);

int send_dc_motor_DI1_command(int fd);

void dc_motor_emergency_up_stop(int fd, uint8_t *recvBuffer);

void dc_motor_emergency_down_stop(int fd, uint8_t *recvBuffer);

void dc_motor_emergency_stop(int fd);

int SubmitDcMotorTask(uart_dcmotor_task_name taskName);

void process_dc_motor_error_code(uint8_t *recvBuffer);

void process_dc_motor_state_data(uint8_t *recvBuffer);

int DC_MotorSendCommand(uart_dcmotor_task_name taskName, int fd, void *value);

int DcMotorDataProcessCtrl(int taskName, uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc);

bool DealWithDcMotorTimeOut(int taskName);

#endif // !
