#ifndef __CRC_MODBUS_H__
#define __CRC_MODBUS_H__

// #include "data_type.h"
#include "general_head.h"
#include "data_proc.h"
typedef int (*CheckModbusResDataPtr)(BYTE *pData, int nLength, BYTE nDeviceSite);
/**
 * @brief 	 【简介】根据数组中的有效数据计算出CRC_MODBUS协议的校验码
 * @param 	 _pBuf [in], 储存数据的数组
 * @param 	 _usLen [in], 数组中有效数据的长度
 * @return 	 【解释返回值】 成功返回校验码
 */
uint16_t CRC16_Modbus(uint8_t *_pBuf, uint16_t _usLen);

void appendCRCtoBuffer(uint8_t *src, int src_len, uint8_t *dest, int dest_Len);

#endif