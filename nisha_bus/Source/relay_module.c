#include "relay_module.h"
#include "device_sim.h"
relay_module_info_t relay_module = {
    .online = false,
    .breakWorking = true,
    .voltage = 0.0,
    .getRelayModuleVoltage = GetRelayModuleVoltage,
    .isRelayModuleOnline = IsRelayModuleOnline,
};

void BreakWorking(void)
{
    relay_module.breakWorking = true;
}
void BreakDiswork(void)
{
    relay_module.breakWorking = false;
}

bool isBreakWorking(void)
{
    return relay_module.breakWorking;
}

bool isRelayModuleOnline(void)
{
    return relay_module.online;
}

void GetRelayModuleVoltage(float *voltage)
{
    *voltage = relay_module.voltage;
}

void IsRelayModuleOnline(bool *online)
{
    *online = relay_module.online;
}

void SetRelayModuleOnline(bool online)
{
    relay_module.online = online;
}

// NC -> 闭合 | NO -> 断开
int NO_CloseToOpenRelaySwitch_COM1(int fd)
{
    BYTE tempInfo[10] = {0xF1, 0x05, 0x00, 0x00, 0x00, 0x00, 0xD9, 0x3A};
    // 0x01, 0x05, 0x00, 0x00, 0x00, 0x00,0xCD, 0xCA
    // 0xF1, 0x05, 0x00, 0x00, 0x00, 0x00, 0xD9, 0x3A
    write(fd, tempInfo, 8);
    return 0;
}
// NC -> 断开 | NO -> 闭合
int NO_OpenToCloseRelaySwitch_COM1(int fd)
{
    BYTE tempInfo[10] = {0xF1, 0x05, 0x00, 0x00, 0xFF, 0x00, 0x98, 0xCA};
    // 0x01, 0x05, 0x00, 0x00, 0xFF, 0x00, 0x8C, 0x3A
    // 0xF1, 0x05, 0x00, 0x00, 0xFF, 0x00, 0x98, 0xCA
    write(fd, tempInfo, 8);
    return 0;
}

int NO_CloseToOpenRelaySwitch_COM2(int fd)
{
    BYTE tempInfo[10] = {0xF1, 0x05, 0x00, 0x01, 0x00, 0x00, 0x88, 0xFA};
    write(fd, tempInfo, 8);
    // f1 05 00 01 00 00 88 fa
    return 0;
}

int NO_OpenToCloseRelaySwitch_COM2(int fd)
{
    BYTE tempInfo[10] = {0xF1, 0x05, 0x00, 0x01, 0xFF, 0x00, 0xC9, 0x0A};
    write(fd, tempInfo, 8);
    // f1 05 00 01 ff 00 c9 0a
    return 0;
}

int NO_GetVoltage(int fd)
{
    BYTE tempInfo[10] = {0xF1, 0X03, 0X00, 0X01, 0X00, 0X01, 0XC1, 0X3A};
    write(fd, tempInfo, 8);
    // f1 03 00 01 00 01 C1 3A
    return 0;
}

int SubmitRelayModuleTask(uart_relay_module_task_name taskName)
{
    int index;

    if (DeviceSim_SubmitRelay((int)taskName))
        return 0;

    index = get_device_index(RELAY_MODULE);
    if (index < 0 || index >= MAX_UART_NUM)
        return -3;

    task_item_t tempTask;
    tempTask.priority = 1;
    tempTask.sendTime = time(NULL);
    tempTask.taskName.relay_module_task = taskName;
    tempTask.deviceName = RELAY_MODULE;
    EnTaskQueue_List(&g_uartWorkInfoArry[index].taskQueue, &tempTask);
    return 0;
}

int SubmitRelayModuleHeartTask(void)
{
    int index;

    if (DeviceSim_SubmitRelay((int)RELAY_MODULE_HEARTBEAT))
        return 0;

    index = get_device_index(RELAY_MODULE);
    if (index < 0 || index >= MAX_UART_NUM)
        return -1;

    task_item_t tempTask;
    tempTask.priority = 1;
    tempTask.sendTime = time(NULL);
    tempTask.taskName.relay_module_task = RELAY_MODULE_HEARTBEAT;
    tempTask.deviceName = RELAY_MODULE;
    EnTaskQueue_List(&g_uartWorkInfoArry[index].taskQueue, &tempTask);
    return 0;
}
int RelayModuleSendCommand(uart_relay_module_task_name taskName, int fd)
{
    switch (taskName)
    {
    // 驱动 接常闭
    case RELAY_MODULE_CLOSETOOPEN_COM1: // 驱动通电
        /* code */
        return NO_CloseToOpenRelaySwitch_COM1(fd);
        break;
    case RELAY_MODULE_OPENTOCLOSE_COM1: // 驱动断电
        /* code */
        return NO_OpenToCloseRelaySwitch_COM1(fd);
        break;
    // 刹车 接常开
    case RELAY_MODULE_CLOSETOOPEN_COM2: // 刹车断电,紧刹车
    case RELAY_MODULE_CLOSETOOPEN_COM2_COMMON:
    case NS_RELAY_MODULE_ENABLE_BREAK:
        return NO_CloseToOpenRelaySwitch_COM2(fd);
        /* code */
        break;
    case RELAY_MODULE_OPENTOCLOSE_COM2: // 刹车通电,松刹车
    case RELAY_MODULE_OPENTOCLOSE_COM2_COMMON:
    case NS_RELAY_MODULE_DISABLE_BREAK:
        /* code */
        return NO_OpenToCloseRelaySwitch_COM2(fd);
        break;
    case RELAY_MODULE_GETVOLTAGE:
        /* code */
        return NO_GetVoltage(fd);
        break;
    case RELAY_MODULE_HEARTBEAT:
        /* code */
        return NO_GetVoltage(fd);
        break;
    case relay_module_runnig_online_check:
        // 运行过程中在线检测
        return NO_GetVoltage(fd);
        break;
    default:
        break;
    }
    return 0;
}

// 继电器电压检测
void calulate_volt(uint8_t *recvBuffer)
{
    uint16_t tep_volt = (recvBuffer[3] << 8) | recvBuffer[4];
    g_relay_module_volt = tep_volt * 0.001;
}

int RelayModuleDataProcessCtrl(int taskName, uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc)
{
    int ret = 0;
    ts_printf("Relay module: ");
    print_data(recvddatalen, recvBuffer);
    ts_printf("\n");
    ret = checkFunc(recvBuffer, recvddatalen, 0xf1);
    if (ret == DATA_PROC_ERROR)
        return DATA_PROC_ERROR;
    if (ret == DATA_PROC_INCOMPLETE)
        return DATA_PROC_INCOMPLETE;

    switch (taskName)
    {
    case RELAY_MODULE_OPENTOCLOSE_COM1: // 驱动断电
        /* code */

        break;
    case RELAY_MODULE_CLOSETOOPEN_COM1: // 驱动通电
        /* code */

        break;
    case RELAY_MODULE_OPENTOCLOSE_COM2: // 刹车通电,松刹车
        /* code */

        BreakDiswork();
        break;
    case RELAY_MODULE_CLOSETOOPEN_COM2: // 刹车断电,紧刹车
        /* code */

        BreakWorking();
        break;
    case RELAY_MODULE_GETVOLTAGE:
        /* code */

#ifdef DEBUG
        // PrintVoltage(recvBuffer, recvddatalen);

#endif // DEBUG
        break;
    case RELAY_MODULE_HEARTBEAT:
        /* code */
        update_device_status(g_device_list, RELAY_MODULE, true);
        NS_sys_check_relay_module();
        calulate_volt(recvBuffer); // 增加继电器电压检测
        break;
    case relay_module_runnig_online_check:
        // 运行过程中继电器在线检测
        if (relay_module.online == false)
        {
            SetRelayModuleOnline(true);
        }
        break;
    case RELAY_MODULE_CLOSETOOPEN_COM2_COMMON:
        BreakWorking();
#ifdef DEBUG
        ts_printf("enable break!\n");
#endif
        break;
    case RELAY_MODULE_OPENTOCLOSE_COM2_COMMON:
        BreakDiswork();
#ifdef DEBUG
        ts_printf("disable break!\n");
#endif
        break;
    case NS_RELAY_MODULE_ENABLE_BREAK:
        NiShaUD_relay_module_enable_break();
        BreakWorking();
        break;
    case NS_RELAY_MODULE_DISABLE_BREAK:
        NiShaUD_relay_module_disable_break();
        BreakDiswork();
        break;
    default:
        break;
    }
    return ret;
}

void PrintVoltage(uint8_t *recvBuffer, uint16_t recvddatalen)
{
    uint16_t voltage = 0;
    voltage = recvBuffer[3] << 8 | recvBuffer[4];
    ts_printf("voltage:     %.2f\n", voltage * 0.001);
}

bool DealWithRelayModuleTimeOut(int taskName)
{
    bool ret = true;
    switch (taskName)
    {
    case RELAY_MODULE_OPENTOCLOSE_COM1:
        /* code */

        break;
    case RELAY_MODULE_CLOSETOOPEN_COM1:

        break;
    case RELAY_MODULE_OPENTOCLOSE_COM2:
        /* code */

        break;
    case RELAY_MODULE_CLOSETOOPEN_COM2:
        /* code */

        break;
    case RELAY_MODULE_HEARTBEAT:
        NS_sys_check_relay_module_timeout();
        break;
    case NS_RELAY_MODULE_ENABLE_BREAK:
        NiShaUD_relay_module_enable_break_timeout();

        break;
    case NS_RELAY_MODULE_DISABLE_BREAK:
        NiShaUD_relay_module_disable_break_timeout();

        break;
    case relay_module_runnig_online_check:
        // 运行过程中掉线
        if (relay_module.online == true)
        {
            SetRelayModuleOnline(false);
        }
        break;
    default:
        break;
    }
    return ret;
}

void InitRelayModule(void)
{
    if (get_device_index(RELAY_MODULE) < 0)
        return;
    SubmitRelayModuleTask(RELAY_MODULE_CLOSETOOPEN_COM1);
    SubmitRelayModuleTask(RELAY_MODULE_CLOSETOOPEN_COM2);
}

void enable_break(void)
{
    SubmitRelayModuleTask(RELAY_MODULE_CLOSETOOPEN_COM2_COMMON);
}

void disable_break(void)
{
    SubmitRelayModuleTask(RELAY_MODULE_OPENTOCLOSE_COM2_COMMON);
}

// ------------------------ 0.INIT ---------------------------- //
// void carsystem_init_relaymodulecheck(uint8_t *recvBuffer)
// {
// #ifdef DEBUG_0
//     ts_printf("relay module check [INIT]\n");
// #endif //
//     chain_task_complete(CARSYSTEMINIT_TASK_CHAIN);
//     relay_module.online = true;
// }

// void carsystem_init_relaymodulecheck_timeout(void)
// {
// #ifdef DEBUG_0
//     ts_printf("relay module check [INIT] [TIMEOUT]\n");
// #endif //
//     chain_task_timeout(CARSYSTEMINIT_TASK_CHAIN);
//     relay_module.online = false;
// }

// void carsystem_init_disablebreak(uint8_t *recvBuffer)
// {
// #ifdef DEBUG_0
//     ts_printf("relay module disable break[INIT]\n");
// #endif //
//     chain_task_complete(CARSYSTEMINIT_TASK_CHAIN);
//     relay_module.online = true;
// }

// // 未成功松开刹车 - 结束INIT任务
// void carsystem_init_disablebreak_timeout(void)
// {
// #ifdef DEBUG_0
//     ts_printf("relay module disable break[INIT] [TIMEOUT]\n");
// #endif //
//     chain_task_timeout(CARSYSTEMINIT_TASK_CHAIN);
//     chain_task_stop(CARSYSTEMINIT_TASK_CHAIN);
//     relay_module.online = false;
// }

// void carsystem_init_enablebreak(uint8_t *recvBuffer)
// {
// #ifdef DEBUG_0
//     ts_printf("relay module enable break[INIT]\n");
// #endif //
//     chain_task_complete(CARSYSTEMINIT_TASK_CHAIN);
// }

// void carsystem_init_enablebreak_timeout(void)
// {
// #ifdef DEBUG_0
//     ts_printf("relay module enable break [INIT] [TIMEOUT]\n");
// #endif //
//     chain_task_timeout(CARSYSTEMINIT_TASK_CHAIN);
//     relay_module.online = false;
// }
