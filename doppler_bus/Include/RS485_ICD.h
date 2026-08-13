#ifndef _RS485_ICD_H
#define _RS485_ICD_H

#include "Common.h"
#include "DopplerProtocol.h"

int UartReceiveDataProc(enum UartType StaticType, KFIFO_ID pData, WORD* pUsed);

#endif
