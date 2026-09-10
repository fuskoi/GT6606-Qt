#ifndef __RELAY_MODULE_H__
#define __RELAY_MODULE_H__

#include "uart.h"

float g_relay_module_volt;

typedef enum
{
    RELAY_MODULE_OPENTOCLOSE_COM1,        // 继电器开关1 常开端口闭合
    RELAY_MODULE_CLOSETOOPEN_COM1,        // 继电器开关1 常开端口打开
    RELAY_MODULE_OPENTOCLOSE_COM2,        // 继电器开关2 常开端口闭合
    RELAY_MODULE_CLOSETOOPEN_COM2,        // 继电器开关2 常开端口打开
    RELAY_MODULE_CLOSETOOPEN_COM2_COMMON, // 紧刹车，单独使用
    RELAY_MODULE_OPENTOCLOSE_COM2_COMMON, // 松刹车, 单独使用
    RELAY_MODULE_GETVOLTAGE,
    RELAY_MODULE_HEARTBEAT,
    NS_RELAY_MODULE_DISABLE_BREAK, // 关闭刹车
    NS_RELAY_MODULE_ENABLE_BREAK,  // 启动刹车
    relay_module_runnig_online_check,   //运行过程中在线检查
} uart_relay_module_task_name;

typedef struct relay_module
{
    bool online;
    bool breakWorking;
    float voltage;
    void (*isRelayModuleOnline)(bool *online);
    void (*getRelayModuleVoltage)(float *voltage);
    /* data */
} relay_module_info_t;

void BreakWorking(void);

void BreakDiswork(void);

bool isBreakWorking(void);

bool isRelayModuleOnline(void);

void GetRelayModuleVoltage(float *voltage);

void IsRelayModuleOnline(bool *online);

void SetRelayModuleOnline(bool online);

int NO_CloseToOpenRelaySwitch_COM1(int fd);
int NO_OpenToCloseRelaySwitch_COM1(int fd);
int NO_CloseToOpenRelaySwitch_COM2(int fd);
int NO_OpenToCloseRelaySwitch_COM2(int fd);
int NO_GetVoltage(int fd);
int SubmitRelayModuleTask(uart_relay_module_task_name taskName);

int SubmitRelayModuleHeartTask(void);

int RelayModuleSendCommand(uart_relay_module_task_name taskName, int fd);

int RelayModuleDataProcessCtrl(int taskName, uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc);


bool DealWithRelayModuleTimeOut(int taskName);

void InitRelayModule(void);

void enable_break(void);

void disable_break(void);

#endif // !__RELAY_MODULE__H
