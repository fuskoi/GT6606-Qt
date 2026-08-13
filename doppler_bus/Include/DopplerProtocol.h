#ifndef DOPPLER_PROTOCOL_H
#define DOPPLER_PROTOCOL_H

#include <stdint.h>

/*
 * 多普勒/电磁/雷达/时差明渠 通用 Modbus 寄存器表
 * 文档中 10 进制地址 01~16 对应 Modbus 字地址 0x0000~0x000F
 */
#define DOPPLER_REG_START           0x0000
#define DOPPLER_REG_COUNT           16

#define DOPPLER_REG_FLOW_VELOCITY   0x0000  /* 流速 Float, 字 01~02 */
#define DOPPLER_REG_WATER_LEVEL     0x0002  /* 液位 Float, 字 03~04 */
#define DOPPLER_REG_SEC_FLOW        0x0004  /* 秒流量 Float, 字 05~06 */
#define DOPPLER_REG_HOUR_FLOW       0x0006  /* 小时流量 Float, 字 07~08 */
#define DOPPLER_REG_POS_ACCUM_HI    0x0008  /* 正累积高位 Ulong, 字 09~10 */
#define DOPPLER_REG_POS_ACCUM_LO    0x000A  /* 正累积低位 Ulong, 字 11~12 */
#define DOPPLER_REG_NEG_ACCUM_HI    0x000C  /* 负累积高位 Ulong, 字 13~14 */
#define DOPPLER_REG_NEG_ACCUM_LO    0x000E  /* 负累积低位 Ulong, 字 15~16 */

#define DOPPLER_ACCUM_SCALE         100000000ULL

#define MODBUS_READ_HOLDING         0x03
#define MODBUS_FLOAT_RSP_LEN        9   /* addr+func+len+4+crc */
#define MODBUS_BATCH_RSP_LEN        37  /* addr+func+32+crc */

#pragma pack(push, 1)
typedef struct ModbusSendFrame
{
    uint8_t u8SubAddr;
    uint8_t u8Funcode;
    uint16_t u16RegAddr;
    uint16_t u16RegData;
    uint16_t u16CRC;
} ModbusSendFrame;
#pragma pack(pop)

#endif
