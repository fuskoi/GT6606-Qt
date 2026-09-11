#include "device_sim.h"
#include "config.h"
#include "depth_sensor.h"
#include "dc_motor.h"
#include "relay_module.h"
#include "nishatask.h"
#include "log.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int s_simAll = 0;
static int s_simDepth = 0;
static int s_simMotor = 0;
static int s_simRelay = 0;

/* 模拟深度 mm；电机运行时按轮询步进 */
static int s_depthMm = 500;
static int s_motorDir = 0; /* 0停 1下降(LEFT) -1上升(RIGHT) */
static int s_stepMm = 80;  /* 每次读深度步进 */

static int conf_flag(const char *key)
{
    myhash_t *item = find_item((char *)key);
    if (item == NULL || item->value[0] == '\0')
        return 0;
    return atoi(item->value) != 0;
}

void DeviceSim_LoadConfig(void)
{
    s_simAll = conf_flag("SimMissingDevices");
    s_simDepth = s_simAll || conf_flag("SimDepth");
    s_simMotor = s_simAll || conf_flag("SimMotor");
    s_simRelay = s_simAll || conf_flag("SimRelay");

    if (s_simDepth || s_simMotor || s_simRelay) {
        LOG(LOG_INFO, "DeviceSim on: depth=%d motor=%d relay=%d (sediment=real)",
            s_simDepth, s_simMotor, s_simRelay);
    }
}

int DeviceSim_Enabled(void)
{
    return (s_simDepth || s_simMotor || s_simRelay) ? 1 : 0;
}

int DeviceSim_Depth(void)
{
    return s_simDepth;
}

int DeviceSim_Motor(void)
{
    return s_simMotor;
}

int DeviceSim_Relay(void)
{
    return s_simRelay;
}

void DeviceSim_ApplyOnline(void)
{
    int keep;

    if (!DeviceSim_Enabled())
        return;

    keep = get_default_mode_keep_depth();
    if (keep > 0)
        s_depthMm = keep;
    else
        s_depthMm = 500;
    s_motorDir = 0;

    if (s_simDepth) {
        set_depth_online(true);
        depth_sensor_info.depth = s_depthMm;
        LOG(LOG_INFO, "DeviceSim depth online, init=%d mm", s_depthMm);
    }
    if (s_simMotor) {
        set_dc_motor_online(true);
        set_dc_motor_working(false);
        set_dc_motor_state1(DM_STATE_STOP);
        LOG(LOG_INFO, "DeviceSim motor online");
    }
    if (s_simRelay) {
        SetRelayModuleOnline(true);
        BreakWorking();
        g_relay_module_volt = 12.0f;
        LOG(LOG_INFO, "DeviceSim relay online, volt=12.0");
    }
}

static void sim_tick_depth(void)
{
    if (s_motorDir > 0) {
        s_depthMm += s_stepMm;
        {
            int maxMm = Config_GetMaxWaterDepthMm();
            if (s_depthMm > maxMm)
                s_depthMm = maxMm;
        }
    } else if (s_motorDir < 0) {
        s_depthMm -= s_stepMm;
        {
            int minMm = Config_GetMinWaterDepthMm();
            if (s_depthMm < minMm)
                s_depthMm = minMm;
            if (s_depthMm < 0)
                s_depthMm = 0;
        }
    }
    depth_sensor_info.depth = s_depthMm;
}

int DeviceSim_SubmitDepth(int taskName)
{
    if (!s_simDepth)
        return 0;

    set_depth_online(true);
    sim_tick_depth();

    switch (taskName) {
    case IS_DEPTH_SENSOR_ONLINE:
        NS_sys_check_depth_sensor();
        break;
    case NS_DEPTH_GET_DEPTH:
        NiShaUD_depth_sensor_get_depth();
        break;
    case DEPTH_GET_DEPTH_WTEMP_WPRE:
    case SETOFFSET_DEPTH_GET_DEPTH_WTEMP_WPRE:
        depth_sensor_info.depth_task_status = DEPTH_TASK_SUCCESS;
        break;
    case DEPTH_GET_ZERO_POINT_OFFSET:
        depth_sensor_info.offset = 0;
        depth_sensor_info.depth_task_status = DEPTH_TASK_SUCCESS;
        break;
    case DEPTH_SET_ZERO_POINT_OFFSET:
        depth_sensor_info.depth_task_status = DEPTH_TASK_SUCCESS;
        break;
    default:
        depth_sensor_info.depth_task_status = DEPTH_TASK_SUCCESS;
        break;
    }
    LOG(LOG_DEBUG, "DeviceSim depth task=%d depth=%dmm", taskName, s_depthMm);
    return 1;
}

int DeviceSim_SubmitMotor(int taskName)
{
    if (!s_simMotor)
        return 0;

    set_dc_motor_online(true);

    switch (taskName) {
    case IS_DC_MOTOR_ONLINE:
    case dc_motor_running_online_check:
        NS_sys_check_dc_motor();
        break;
    case NS_DC_MOTOR_TURN_LEFT:
    case DC_MOTOR_TURN_LEFT:
        s_motorDir = 1; /* 下降加深 */
        set_dc_motor_working(true);
        set_dc_motor_state1(DM_STATE_TURN_LEFT);
        NiShaUD_dc_motor_turn_left();
        break;
    case NS_DC_MOTOR_TURN_RIGHT:
    case DC_MOTOR_TURN_RIGHT:
        s_motorDir = -1; /* 上升变浅 */
        set_dc_motor_working(true);
        set_dc_motor_state1(DM_STATE_TURN_RIGHT);
        NiShaUD_dc_motor_turn_right();
        break;
    case NS_DC_MOTOR_STOP:
    case DC_MOTOR_STOP:
        s_motorDir = 0;
        set_dc_motor_working(false);
        set_dc_motor_state1(DM_STATE_STOP);
        NiShaUD_dc_motor_stop();
        break;
    case NS_DC_MOTOR_GET_STATE:
    case DC_MOTOR_GET_STATE:
        NiShaUD_dc_motor_get_state();
        break;
    case DC_MOTOR_SET_SPEED:
    case DC_MOTOR_JOG_TURN_LEFT:
    case DC_MOTOR_JOG_TURN_RIGHT:
    case DC_MOTOR_GET_ERROR_CODE:
    case DC_MOTOR_RESET_ERROR_CODE:
    case NS_DC_MOTOR_GET_ERROR_CODE:
    case NS_DC_MOTOR_RESET_ERROR_CODE:
    case DC_MOTOR_GET_DI1:
    case DC_MOTOR_GET_DI2:
        break;
    default:
        break;
    }
    LOG(LOG_DEBUG, "DeviceSim motor task=%d dir=%d depth=%d",
        taskName, s_motorDir, s_depthMm);
    return 1;
}

int DeviceSim_SubmitRelay(int taskName)
{
    if (!s_simRelay)
        return 0;

    SetRelayModuleOnline(true);

    switch (taskName) {
    case RELAY_MODULE_HEARTBEAT:
    case relay_module_runnig_online_check:
    case RELAY_MODULE_GETVOLTAGE:
        g_relay_module_volt = 12.0f;
        NS_sys_check_relay_module();
        break;
    case NS_RELAY_MODULE_DISABLE_BREAK:
    case RELAY_MODULE_OPENTOCLOSE_COM2:
    case RELAY_MODULE_OPENTOCLOSE_COM2_COMMON:
        BreakDiswork();
        NiShaUD_relay_module_disable_break();
        break;
    case NS_RELAY_MODULE_ENABLE_BREAK:
    case RELAY_MODULE_CLOSETOOPEN_COM2:
    case RELAY_MODULE_CLOSETOOPEN_COM2_COMMON:
        BreakWorking();
        NiShaUD_relay_module_enable_break();
        break;
    case RELAY_MODULE_OPENTOCLOSE_COM1:
    case RELAY_MODULE_CLOSETOOPEN_COM1:
        /* 驱动电源切换：台架无感，直接成功 */
        break;
    default:
        break;
    }
    LOG(LOG_DEBUG, "DeviceSim relay task=%d", taskName);
    return 1;
}
