#ifndef _MODBUS_QUEUE_H
#define _MODBUS_QUEUE_H

#include "Common.h"
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

/* 单帧最大长度（AMPS电机所有指令 ≤ 32字节） */
#define MODBUS_MAX_FRAME_LEN  32

/* Modbus命令参数 */
typedef struct _MODBUS_CMD_PARAM {
    int i32Fd;                    /* 文件描述符 */
    uint8_t u8SlaveAddr;          /* 从设备地址 */
    uint8_t u8FunctionCode;       /* 功能码 */
    uint8_t pu8RawFrame[MODBUS_MAX_FRAME_LEN];  /* 原始 Modbus 帧（内联，免malloc） */
    uint16_t u16RawFrameLen;      /* 原始帧长度 */
} MODBUS_CMD_PARAM;

/* 函数声明 */
int ModbusQueue_Init(void);
void ModbusQueue_Destroy(void);
int ModbusQueue_AddCommand(MODBUS_CMD_PARAM *pParam);

#endif /* _MODBUS_QUEUE_H */