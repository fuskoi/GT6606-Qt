/**
 * DAM/DMA-5213：COM 口周期 Modbus 读寄存器1电压。
 * - 本地：与传感器相同采集周期，收包后打 DEBUG 日志
 * - 上行：仅 SL651 电源电压要素 0x38，无独立网页定时上报
 */
#include "Dam5213.h"
#include "ModbusQueue.h"
#include "DopplerProtocol.h"
#include "CRC.h"
#include "OtaUpgrade.h"
#include "log.h"
#include <string.h>
#include <sys/time.h>

static float g_fVoltageV = 0.0f;
static uint8_t g_u8Valid = 0;
static uint8_t g_u8Pending = 0;
static struct timeval g_stPendingSince;

static uint32_t dam_ms_since(const struct timeval* pTv)
{
    struct timeval now;

    if (pTv == NULL) {
        return 0xFFFFFFFFu;
    }
    gettimeofday(&now, NULL);
    if (now.tv_sec < pTv->tv_sec) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)((now.tv_sec - pTv->tv_sec) * 1000L +
                      (now.tv_usec - pTv->tv_usec) / 1000L);
}

static int dam_frame_crc_ok(const uint8_t* pFrame, uint16_t frameLen)
{
    uint16_t crcCalc = 0;
    uint16_t crcRaw = 0;

    if (pFrame == NULL || frameLen < 2) {
        return 0;
    }
    crcCalc = CRC16_Modbus(pFrame, (uint16_t)(frameLen - 2));
    memcpy(&crcRaw, pFrame + frameLen - 2, sizeof(crcRaw));
    return (crcCalc == crcRaw || crcCalc == swap_u16(crcRaw)) ? 1 : 0;
}

static float dam_raw_to_volt(uint16_t u16Raw)
{
    /* 现场约定：原始值 ÷1000 = 电压(V)，如 12622 → 12.622V */
    return (float)u16Raw / 1000.0f;
}

static int dam_send_read_voltage(void)
{
    ModbusSendFrame stSend;
    MODBUS_CMD_PARAM stCmd;
    int port = g_stDevicelist.Dam5213;
    uint8_t slave = g_stDeviceAddr.u8Dam5213;

    if (port < 0 || port >= MAX_UART_NUM || slave == 0) {
        return ERROR;
    }
    if (g_stDevicelist.UartList[port].i32fd <= 0) {
        return ERROR;
    }
    if (g_u8Pending) {
        return SUCCESS;
    }

    memset(&stSend, 0, sizeof(stSend));
    stSend.u8SubAddr = slave;
    stSend.u8Funcode = MODBUS_READ_HOLDING;
    stSend.u16RegAddr = swap_u16(DAM5213_REG_VOLTAGE);
    stSend.u16RegData = swap_u16(1); /* 读 1 个寄存器 */
    stSend.u16CRC = CRC16_Modbus((uint8_t*)&stSend, sizeof(stSend) - 2);
    stSend.u16CRC = swap_u16(stSend.u16CRC);

    memset(&stCmd, 0, sizeof(stCmd));
    stCmd.i32Fd = g_stDevicelist.UartList[port].i32fd;
    stCmd.u8SlaveAddr = slave;
    stCmd.u8FunctionCode = MODBUS_READ_HOLDING;
    stCmd.u16RawFrameLen = (uint16_t)sizeof(stSend);
    memcpy(stCmd.pu8RawFrame, &stSend, sizeof(stSend));

    if (ModbusQueue_AddCommand(&stCmd) != SUCCESS) {
        LOG(LOG_WARNING, "Dam5213: queue send failed");
        return ERROR;
    }
    g_u8Pending = 1;
    gettimeofday(&g_stPendingSince, NULL);
    return SUCCESS;
}

int Dam5213_Init(void)
{
    g_fVoltageV = 0.0f;
    g_u8Valid = 0;
    g_u8Pending = 0;
    if (g_stDevicelist.Dam5213 >= 0 && g_stDeviceAddr.u8Dam5213 > 0) {
        LOG(LOG_DEBUG, "Dam5213 init: COM%d addr=%u (V=raw/1000)",
            g_stDevicelist.Dam5213 + 1,
            (unsigned)g_stDeviceAddr.u8Dam5213);
    }
    return SUCCESS;
}

void Dam5213_Poll(void* arg)
{
    uint32_t timeoutMs = 0;

    (void)arg;
    if (OtaUpgrade_IsBusy()) {
        return;
    }
    if (g_stDevicelist.Dam5213 < 0 || g_stDeviceAddr.u8Dam5213 == 0) {
        return;
    }

    timeoutMs = g_stMeasureConfig.u32SensorTimeoutSec * 1000U;
    if (timeoutMs < 1000U) {
        timeoutMs = 1000U;
    }
    if (g_u8Pending && dam_ms_since(&g_stPendingSince) >= timeoutMs) {
        LOG(LOG_WARNING, "Dam5213: read timeout, clear pending");
        g_u8Pending = 0;
    }
    (void)dam_send_read_voltage();
}

int Dam5213_RecDataProc(KFIFO_ID pData, WORD* pUsed)
{
    uint8_t frame[DAM5213_RSP_LEN];
    uint8_t slave = g_stDeviceAddr.u8Dam5213;
    uint16_t u16Raw = 0;

    (void)pUsed;
    if (pData == NULL || slave == 0) {
        return SUCCESS;
    }

    while (kfifo_len(pData) >= DAM5213_RSP_LEN) {
        kfifo_preview(pData, frame, DAM5213_RSP_LEN);
        if (!dam_frame_crc_ok(frame, DAM5213_RSP_LEN)) {
            kfifo_get(pData, frame, 1);
            continue;
        }
        /* 过滤本机读命令回显：请求 8 字节，应答 7 字节且 byteCnt=2 */
        if (frame[0] != slave || frame[1] != MODBUS_READ_HOLDING || frame[2] != 2) {
            kfifo_get(pData, frame, 1);
            continue;
        }

        kfifo_get(pData, frame, DAM5213_RSP_LEN);
        u16Raw = (uint16_t)(((uint16_t)frame[3] << 8) | frame[4]);
        g_fVoltageV = dam_raw_to_volt(u16Raw);
        g_u8Valid = 1;
        g_u8Pending = 0;
        /* 与传感器一样：采集周期到则本地打日志；上行只走 SL651 0x38 */
        LOG(LOG_DEBUG, "dam5213: voltage=%.3fV raw=%u",
            (double)g_fVoltageV,
            (unsigned)u16Raw);
    }
    return SUCCESS;
}

int Dam5213_GetVoltage(float* pfVolt)
{
    if (pfVolt == NULL || !g_u8Valid) {
        return ERROR;
    }
    *pfVolt = g_fVoltageV;
    return SUCCESS;
}

int Dam5213_IsValid(void)
{
    return g_u8Valid ? 1 : 0;
}
