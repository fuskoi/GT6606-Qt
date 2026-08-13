#include "TaskList.h"
#include "RS485_ICD.h"
#include "log.h"

/*
*函数名称:InitListManager
*函数功能:初始化任务列表
*输    入: int nItemCount    任务个数
*          int nItemSize,    单个任务大小
*          void* pItemArray  任务数组
*          
*输   出:struct ListManagerInfo* pListManager  任务队列
*返   回:SUCCESS - 初始化成功  ERROR - 初始化失败
*/
int InitListManager(int nItemCount,int nItemSize,void* pItemArray,struct ListManagerInfo* pListManager)
{
    if (NULL == pItemArray)
    {
        PRINTERROR;
        return ERROR;
    }
    //pListManager->mutex = PTHREAD_MUTEX_INITIALIZER;
    //pthread_mutex_init(&pListManager->mutex,NULL);
    //printf("%d\n",&pListManager->mutex);
    pListManager->dataItemCount = 0;
    pListManager->itemCount = nItemCount;
    pListManager->newPos = 0;
    pListManager->nItemSize = nItemSize;
    pListManager->orgPos = 0;
    pListManager->pValueArray = pItemArray;
    return SUCCESS;
}


/*
*函数名称:AddItem
*函数功能:向任务队列中添加一个任务
*输    入: void* pItemArray  任务数组
*          
*输   出:struct ListManagerInfo* pListManager  任务队列
*返   回:SUCCESS - 初始化成功  ERROR - 初始化失败
*/
bool AddItem(void* pItemArray,struct ListManagerInfo* pListManager)
{
    //printf("list additem enter: %d %d %d %d\n",pListManager->newPos,
     //      pListManager->orgPos,pListManager->dataItemCount,pListManager->itemCount);
    bool bRet = false;

    pthread_mutex_lock(pListManager->mutex);

    if(pListManager->dataItemCount < pListManager->itemCount)
    {

        memcpy(pListManager->pValueArray + pListManager->newPos * pListManager->nItemSize,
               pItemArray,pListManager->nItemSize);

        pListManager->newPos++;
        pListManager->newPos %= pListManager->itemCount;
        pListManager->dataItemCount++;
        bRet = true;
    }

    pthread_mutex_unlock(pListManager->mutex);
    //printf("list additem leave: %d %d %d %d\n",pListManager->newPos,
    //       pListManager->orgPos,pListManager->dataItemCount,pListManager->itemCount);
    return bRet;
}


/*
*函数名称:PopupTaskList
*函数功能:总任务队列中获取一个任务结点
*输    入:void* pItemArray  任务数组
*          
*输   出:struct ListManagerInfo* pListManager  任务队列
*返   回:SUCCESS - 初始化成功  ERROR - 初始化失败
*/
bool PopupTaskList(void* pItemArray,struct ListManagerInfo* pListManager)
{
    bool bRet = false;
    /* printf("list GetListOldValueAndDel enter: %d %d %d %d\n",pListManager->newPos,
           pListManager->orgPos,pListManager->dataItemCount,pListManager->itemCount); */
    pthread_mutex_lock(pListManager->mutex);
    if(pListManager->dataItemCount > 0)
    {
        bRet = true;
        memcpy(pItemArray,pListManager->pValueArray + pListManager->nItemSize * pListManager->orgPos,
               pListManager->nItemSize);
        pListManager->orgPos++;
        pListManager->dataItemCount--;
        pListManager->orgPos %= pListManager->itemCount;
    }
    pthread_mutex_unlock(pListManager->mutex);
    /* printf("list GetListOldValueAndDel leave: %d %d %d %d\n",pListManager->newPos,
           pListManager->orgPos,pListManager->dataItemCount,pListManager->itemCount); */
    return bRet;
}


/*
*函数名称:GetListTotalValue
*函数功能:
*输    入:void* pItemArray  任务数组
*          
*输   出:struct ListManagerInfo* pListManager  任务队列
*返   回:SUCCESS - 初始化成功  ERROR - 初始化失败
*/
int GetListTotalValue(void* pItemArray,struct ListManagerInfo* pListManager)
{
    //printf("list GetListTotalValue enter: %d %d %d %d\n",pListManager->newPos,
     //      pListManager->orgPos,pListManager->dataItemCount,pListManager->itemCount);
    pthread_mutex_lock(pListManager->mutex);
    int nRet = pListManager->dataItemCount;
    int nTempItem = pListManager->orgPos;
    int i = 0;
    for(i = 0; i < nRet; i++)
    {
        memcpy(pItemArray + i * pListManager->nItemSize,pListManager->pValueArray + pListManager->nItemSize * nTempItem,
               pListManager->nItemSize);
        nTempItem++;
        nTempItem %= pListManager->itemCount;
    }
    pthread_mutex_unlock(pListManager->mutex);
    //printf("list GetListTotalValue leave: %d %d %d %d\n",pListManager->newPos,
     //      pListManager->orgPos,pListManager->dataItemCount,pListManager->itemCount);
    return nRet;
}


/*
*函数名称:GetItemCount
*函数功能:获取任务数据个数
*输    入: struct ListManagerInfo* pListManager  任务队列
*输   出:无
*返   回:任务数据个数
*/
int GetItemCount(struct ListManagerInfo* pListManager)
{
    return pListManager->dataItemCount;
}




/*
*函数名称:DealTaskWork
*函数功能:处理任务队列中的任务
*输    入: pParam  线程输入参数
*输   出:无
*返   回:无
*/
void DealTaskWork(void* pParam)
{
    struct Uart_Info * pUartinfo  = (struct Uart_Info*)pParam;
    uint32_t fifolen = 0;
    WORD pUsed; 
    
    LOG(LOG_DEBUG,"sub thread id is: %lu,uart fd is %d",pthread_self(),pUartinfo->i32fd);
    
    while(1)
    {
        fifolen = kfifo_len(pUartinfo->ringfifo);

        if(0 != fifolen ) //10ms
        {
            if(ERROR == UartReceiveDataProc(pUartinfo->staticUartType,pUartinfo->ringfifo,&pUsed))
            {
                /* 半包等待时务必让出 CPU，否则会忙等到丢后续大包 */
                usleep(1000);
            }
        }
        else
        {
            usleep(10);
        }
    }

}