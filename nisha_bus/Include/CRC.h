#ifndef _CRC_H
#define _CRC_H
#include "Common.h"

uint16_t CRC16_Modbus(uint8_t *_pBuf, uint16_t _usLen);
uint16_t swap_u16(uint16_t shortValue);
int16_t swap_i16(int16_t shortValue);
uint32_t swap_u32(uint32_t ResData);
int32_t swap_i32(int32_t ResData);
void mSleep(uint32_t ms);
//bool send_modbus_frame(uint8_t u8SlaveAddr, uint8_t u8FuncCode ,uint16_t u16regAddr, uint16_t u16data);
#endif