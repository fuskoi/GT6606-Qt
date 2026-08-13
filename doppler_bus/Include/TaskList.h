#ifndef _TASKLIST_H
#define _TASKLIST_H

#include "Common.h"

#define MAX_ITEM_NUM 12

int InitListManager(int nItemCount, int nItemSize, void* pItemArray, struct ListManagerInfo* pListManager);
void DealTaskWork(void* pParam);

#endif
