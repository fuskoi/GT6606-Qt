#include "zigbee_com.h"

UpdateSoftTask_t g_updateSoftTask;
pthread_mutex_t g_upMutex;
pthread_mutex_t g_zigbeeRespondQueueMutex;
list_queue_t g_zigbeeRespondTaskQueue; // 任务队列 - 通过zigbee发送需要响应的报文
int g_sendSeqNum = -1;                 // 发送的需要响应的报文的序号
int g_recvSeqNum = -1;                 // 接收到的响应报文中响应的报文的序号
int g_zigbeePacketCount = 1;           // 报文的报文序号
int g_localState = 0;
short g_localDistance = 0;
int g_localCurPos = 0;
char g_appDir[100];
common_item_t g_curSendItem; // 当前要通过zigbee发送报文的任务(只需要包含数据部分)
velo_task_info g_veloTaskInfo;
int ZigbeeDataCheck(uint8_t *pData, uint16_t wLen)
{
    int i = 0;
    uint8_t nCheckCode[] = {0x12, 0x34, 0x56, 0x78};
    if (wLen < 4)
        return DATA_PROC_INCOMPLETE;
    for (i = 0; i < 4; i++)
    {
        if (pData[i] != nCheckCode[i])
            return DATA_PROC_ERROR;
    }
    if (wLen < sizeof(zigbee_head_t) + sizeof(zigbee_tail_t) + 1)
        return DATA_PROC_INCOMPLETE;
    pzigbee_head_t pHead = (pzigbee_head_t)pData;
    if (pHead->dwHeadFlag != ZIGBEE_HEAD_FLAG)
        return DATA_PROC_ERROR;
    if (pHead->wLength > 600)
        return -2;
    if (pHead->wLength > wLen)
        return DATA_PROC_INCOMPLETE;
    pzigbee_tail_t pTail = (pzigbee_tail_t)(pData + pHead->wLength - sizeof(zigbee_tail_t));
    if (pTail->wTailFlag != ZIGBEE_TAIL_FLAG)
        return DATA_PROC_ERROR;
    return DATA_PROC_OK;
}

int ProcZigbeeData(uint8_t *pData, uint16_t wLen, WORD *pUsed)
{

    int32_t nRet = 0;
    // int32_t i = 0;

    ts_printf("recv data from zigbee:");
    print_data(wLen, pData);
    ts_printf("datalen = %d\n", wLen);

    nRet = ZigbeeDataCheck(pData, wLen); //! 数据检查
    if (DATA_PROC_INCOMPLETE == nRet)
    {
        ts_printf("The data is incomplete\n");
        return DATA_PROC_INCOMPLETE;
    } // 数据不完整

    else if (0 > nRet) // 数据有误
    {
        *pUsed = wLen;
        return DATA_PROC_ERROR;
    }
    //! pHead指针指向pData的起始位置 -- 定位到头部信息
    pzigbee_head_t pHead = (pzigbee_head_t)pData;
    *pUsed = pHead->wLength;

    //! pInfo指针以pData的头部数据结尾为起始位置 -- 定位到数据信息
    BYTE *pInfo = pData + sizeof(zigbee_head_t);

    int nSeqNum = 0; // 当前正在处理的报文的序号

    pzigbee_respond_info_t pRespondInfo = (pzigbee_respond_info_t)(pData + sizeof(zigbee_head_t));
    nSeqNum = pRespondInfo->wSeqNum;
    /**
     * ? 这条报文需要回复 并且 报文的序号和接收到的响应报文中的被响应的序号相同
     */
    if (pRespondInfo->nNeedRespond == true && pRespondInfo->wSeqNum == g_recvSeqNum)
    {
        BYTE respondInfo[8];
        int nInfoLen = 5;
        memset(respondInfo, 0, 8);
        // respondInfo[0] = ZIGBEE_SH_RESPONE;   // 这是一条响应报文的标记
        memcpy(&respondInfo[1], &nSeqNum, 4); // 接收到的报文的序号
        // 发送回复的报文
        SendDataPacketToZigbee(respondInfo, nInfoLen, g_zigbeePacketCount++, FALSE);
        return DATA_PROC_OK;
    }
    DWORD dwDataLength = pHead->wLength - sizeof(zigbee_head_t) - sizeof(zigbee_respond_info_t) - sizeof(zigbee_tail_t);

    //! 处理接收到的指令，数据段第一个字节是指令类型
    nRet = HandleRtuDataRequest(pInfo + sizeof(zigbee_respond_info_t), dwDataLength);

    /**
     * ? 当需要响应的报文被处理完成，发送响应报文
     */
    if (pRespondInfo->nNeedRespond == true && nRet == 0)
    {
        BYTE respondInfo[8];
        g_recvSeqNum = nSeqNum;
        int nInfoLen = 5;
        memset(respondInfo, 0, 8);
        // respondInfo[0] = ZIGBEE_SH_RESPONE;
        memcpy(&respondInfo[1], &nSeqNum, 4);
        SendDataPacketToZigbee(respondInfo, nInfoLen, g_zigbeePacketCount++, FALSE);
    }
    return DATA_PROC_OK;
}
/// @brief 组成报文的数据部分(添加是否需要回应的标记和报序号)，并根据这条报文需不需要响应选择不同的处理方式进行发送
/// @param pData 报文的数据部分, //!不需要携带respondinfo的信息，在这个函数中添加
/// @param nLength 数据的长度
/// @param nResNum 报文的序号 - 报文序号
/// @param nResponeFlag 这条报文需不需要回应的标记 - 0不需要响应 - 非0需要响应
/// @return
int SendDataPacketToZigbee(BYTE *pData, int nLength, int nResNum, uint8_t nResponeFlag)
{
    BYTE buffer[512];
    memset(buffer, 0, 512);
    pzigbee_respond_info_t pInfo = (pzigbee_respond_info_t)buffer;
    memcpy(buffer + sizeof(zigbee_respond_info_t), pData, nLength);
    pInfo->wSeqNum = nResNum;
    pInfo->nNeedRespond = nResponeFlag;

    if (nResponeFlag) //* 需要回复的报文
    {
        // bool bRet;
        SendNeedRespondZigbeeData(buffer, nLength + sizeof(zigbee_respond_info_t));
    }
    else //* 不需要回复的报文
    {
        //! 使用前要初始化锁
        SendZigbeeDataWithMutex(buffer, sizeof(zigbee_respond_info_t) + nLength);
    }
    return 0;
}

void SendZigbeeDataWithMutex(BYTE *pData, int nLength)
{
    ts_printf("data send to zigbee without head and tail:");
    print_data(nLength, pData); // 输出到终端

    pthread_mutex_lock(&g_upMutex);
    SendZigbeeData(pData, nLength);
    pthread_mutex_unlock(&g_upMutex);
}

void SendZigbeeData(BYTE *pData, int nLength)
{
    int bufferDataLen = 0;
    BYTE buffer[1024];
    memset(buffer, 0, 1024);
    pzigbee_head_t pHead = (pzigbee_head_t)buffer;
    /**
     * !! 设置报文头部
     */
    pHead->dwHeadFlag = ZIGBEE_HEAD_FLAG;
    pHead->nZigbeeAddr = ZIGBEE_CLIENT_ADDR;
    pHead->wLength = sizeof(zigbee_head_t) + nLength + sizeof(zigbee_tail_t);
    bufferDataLen = pHead->wLength;
    /**
     * !! 设置报文数据部分
     */
    memcpy(buffer + sizeof(zigbee_head_t), pData, nLength);
    pzigbee_tail_t pTail = (pzigbee_tail_t)(buffer + sizeof(zigbee_head_t) + nLength);
    /**
     * !! 设置报文尾部
     */
    pTail->wTailFlag = ZIGBEE_TAIL_FLAG;
    pTail->wAddValue = 0;
    int i = 0;
    for (i = 0; i < nLength; i++)
    {
        pTail->wAddValue += pData[i];
    }
    //! 发送报文
    write(g_uartWorkInfoArry[get_device_index(ZIGBEE_DEV)].comfd, buffer, bufferDataLen);
}

/// @brief 把需要响应的报文加入到任务队列中
/// @param pData 想要发送的报文的数据部分
/// @param wLength 数据长度
/// @return 加入成功返回true 加入失败返回false
bool SendNeedRespondZigbeeData(BYTE *pData, int wLength)
{
    common_item_t tempItem;
    tempItem.wLength = wLength;
    memcpy(tempItem.tempBuffer, pData, wLength);
    return EnTaskQueue_List(&g_zigbeeRespondTaskQueue, &tempItem);
}

void InitNeedRespondZigbeeDataTaskResources(int nQueLen)
{
    pthread_mutex_init(&g_upMutex, NULL);
    //! 初始化队列中的锁资源
    pthread_mutex_init(&g_zigbeeRespondQueueMutex, NULL);
    g_zigbeeRespondTaskQueue.mutex = &g_zigbeeRespondQueueMutex;
    //! 初始化指定的item数组
    pcommon_item_t pTemp = (pcommon_item_t)malloc(sizeof(common_item_t) * nQueLen);
    //! 初始化队列
    InitTaskQueue_List(sizeof(common_item_t), nQueLen, pTemp, &g_zigbeeRespondTaskQueue);
    //! 创建发送需要响应报文的工作线程
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, SendZigbeeDataAndWaitRespond, NULL);
    pthread_detach(thread_id);
}
/**
 * @brief 	 【简介】向zigbee中写入一条需要回复的报文，等待从zigbee中接收到这条报文的响应， 否则每隔200ms重新发送一次这条报文， 当接收到响应后，再处理下一条向zigbee发送的报文
 * @param 	 arg [in/out], 无意义
 * @return 	 【解释返回值】
 */
void *SendZigbeeDataAndWaitRespond(void *arg)
{
    pzigbee_respond_info_t pRespone = NULL;
    while (1)
    {
        if (g_sendSeqNum == g_recvSeqNum)
        {
            if (TaskQueueEmpty(&g_zigbeeRespondTaskQueue)) //! 队列为空
            {
                localSleep(200);
                continue;
            }
            DeTaskQueue_List(&g_zigbeeRespondTaskQueue, &g_curSendItem);
            pRespone = (pzigbee_respond_info_t)g_curSendItem.tempBuffer;
            g_sendSeqNum = pRespone->wSeqNum;
            ts_printf("send new message sequence number:%d\n", g_sendSeqNum);
        }
        if (g_sendSeqNum == -1)
        {
            g_recvSeqNum = -1;
            localSleep(2000);
            continue;
        }
        ts_printf("send message sequence number:%d\n", g_sendSeqNum);
        SendZigbeeDataWithMutex(g_curSendItem.tempBuffer, g_curSendItem.wLength);
        localSleep(200);
    }
}

/// @brief 把接收到的响应报文中，响应的目标报文序号赋值给g_recvSeqNum
/// @param nSeqNum 服务器发送过来的确认报文中目标报文的序号
void GetZigbeeRespondSeqNum(int nSeqNum)
{
    if (g_sendSeqNum == nSeqNum)
        ts_printf("%d message confirm: %d\n", g_sendSeqNum, nSeqNum);
    g_recvSeqNum = nSeqNum;
}
/// @brief 延时函数
/// @param ms 时间 单位毫秒
void localSleep(int ms)
{
    struct timeval delay;
    delay.tv_sec = 0;
    delay.tv_usec = ms * 1000; // 20 ms
    select(0, NULL, NULL, NULL, &delay);
}
/**
 * !! uncomplete
 */
int HandleRtuDataRequest(BYTE *pData, DWORD dwLength)
{
    int nRet = 0;
    int nSeqNum = 0;
    BYTE outinfo[128];
    /**
     * * 在终端输出接收到的数据
     */
    int nTmpLen = dwLength;
    if (dwLength > 100)
        nTmpLen = 100;
    print_data(nTmpLen, pData); //! 终端输出数据 - 16进制

    ts_printf("function type:%d data length:%d \n", (int)pData[0], dwLength); //! 输出数据的类型和数据的长度
    switch (pData[0])
    {
    // case ZIGBEE_SH_LOCALCONTRL:
    //     break;
    // case ZIGBEE_SH_HEAR:
    //     /* code */
    //     ts_printf("heart back\n");
    //     break;
    // case ZIGBEE_SH_STARTCELIU_COUNT:
    //     /* code */
        
    //     break;
    // case ZIGBEE_SH_STARTCELIU:
    //     /* code */
    //     ts_printf("exec task ZIGBEE_SH_STARTCELIU!!\n");

    //     StartCeliu(pData);
    //     break;
    // case ZIGBEE_SH_STARTCELIU_CHESHUIWEI:
    //     break;
    // case ZIGBEE_SH_GOTOORG:
    //     ts_printf("Back to the origin fun type: %d \n", ZIGBEE_SH_GOTOORG);
        
    //     break;
    // case ZIGBEE_SH_RESTART:
    //     ts_printf("%d\n", ZIGBEE_SH_RESTART);
        
    //     ReStart();
    //     break;
    // case ZIGBEE_SH_UPDATESOFT:
    //     /* code */
    //     ts_printf("sub update length: %d\r\n", dwLength);
    //     nRet = SubProcessUpdateSoft(pData + 1); //! 不完整
    //     break;
    // case ZIGBEE_SH_EXECLINUXCMD:
    //     /* code */
    //     ProcLinuxCommand(pData + 1);
    //     break;
    // case ZIGBEE_SH_PORTCOMMAND:
    //     ProcPortCommand(pData + 1);
    //     break;
    // case ZIGBEE_SH_RESETJIEJINDATA:
       
    //     // WriteJiejinKaiguanErro();
    //     break;
    // /**
    //  * * 当接收到的报文是一条响应功能的报文, 提取出这条报文中响应的报文序号
    //  */
    // case ZIGBEE_SH_RESPONE:
    //     memcpy(&nSeqNum, pData + 1, 4);
    //     GetZigbeeRespondSeqNum(nSeqNum);
    //     break;
    }
    ts_printf("HandleRtuDataRequest OK\n");
    return nRet;
}

void ProcPortCommand(BYTE *pData)
{
    pCommandFromPort_head_t pHead = (pCommandFromPort_head_t)pData;
    if (pHead->portID > 3)
    {
        return;
    }
    write(g_uartWorkInfoArry[pHead->portID].comfd, pData + sizeof(CommandFromPort_head_t), pHead->commandLength);
}

int SubProcessUpdateSoft(BYTE *pData)
{
    ts_printf("SubProcessUpdateSoft \r\n");
    int nError = 0;
    int nState = 0;
    int nMaxPacketSize = 512;
    pUpdateSoftPacket_t pPacket = (pUpdateSoftPacket_t)pData;
    if (g_updateSoftTask.bRunFlag != 0 && pPacket->dwCurPos == 0)
    {
        g_updateSoftTask.bRunFlag = 0;
        g_updateSoftTask.dwCurPos = 0;
        g_updateSoftTask.dwRecvSize = 0;
        fclose(g_updateSoftTask.pNew1);
        g_updateSoftTask.pNew1 = NULL;
        if (NULL != g_updateSoftTask.pNew2)
        {
            fclose(g_updateSoftTask.pNew2);
            g_updateSoftTask.pNew2 = NULL;
        }
    }
    if (g_updateSoftTask.bRunFlag == 0)
    {
        ts_printf("g_updatesoftTask.bRunFlag == 0\n");
        if (pPacket->dwCurPos != 0)
        {
            ts_printf("g_updatesoftTask nError = 1\n");
            nError = 1;
        }
    }
    else
    {
        if (strcmp(pPacket->name, g_updateSoftTask.name) != 0)
        {
            ts_printf("g_updatesoftTask nError = 2\n");
            nError = 2;
        }
        else if (pPacket->dwCurPos != g_updateSoftTask.dwCurPos + 1)
        {
            ts_printf("g_updatesoftTask nError = 3\n");
            nError = 3;
        }
    }

    if (nError == 0)
    {
        if (pPacket->wDataLength > nMaxPacketSize)
        {
            ts_printf("g_updatesoftTask nError = 4\n");
            nError = 4;
        }
    }
    if (nError == 0)
    {
        WORD wTempChek = 0;
        int i = 0;
        for (i = 0; i < pPacket->wDataLength; i++)
        {
            wTempChek += pPacket->dataBuffer[i];
        }
        if (wTempChek != pPacket->addValue)
        {
            ts_printf("g_updatesoftTask nError = 5\n");
            nError = 5;
        }
    }

    if (nError == 0)
    {
        if (g_updateSoftTask.bRunFlag == 0)
        {
            g_updateSoftTask.dwRecvSize = 0;
            memset(g_updateSoftTask.name, 0, 32);
            sprintf(g_updateSoftTask.name, "%s", pPacket->name);
            char fileUrl1[256];
            char fileUrl2[256];
            memset(fileUrl1, 0, 256);
            memset(fileUrl2, 0, 256);
            if (pPacket->filetype == 1)
            {
                sprintf(fileUrl1, "%s/%s_new1_back", g_appDir, pPacket->name);
                sprintf(fileUrl2, "%s/%s_new2_back", g_appDir, pPacket->name);
                g_updateSoftTask.pNew1 = fopen(fileUrl1, "wb");
                g_updateSoftTask.pNew2 = fopen(fileUrl2, "wb");
            }
            else
            {
                sprintf(fileUrl1, "%s/%s_back", g_appDir, pPacket->name);
                g_updateSoftTask.pNew1 = fopen(fileUrl1, "wb");
                g_updateSoftTask.pNew2 = NULL;
            }
            if (g_updateSoftTask.pNew1 == NULL)
            {
                nError = 6;
                ts_printf("g_updatesoftTask nError = 6\n");
            }
            else
            {
                g_updateSoftTask.bRunFlag = 1;
            }
        }
        else
        {
            if (g_updateSoftTask.dwCurPos + 1 != pPacket->dwCurPos)
            {
                nError = 7;
                ts_printf("g_updatesoftTask nError = 7\n");
            }
        }

        if (nError == 0)
        {
            g_updateSoftTask.dwRecvSize += pPacket->wDataLength;
            if (g_updateSoftTask.pNew1 != NULL)
            {
                fwrite(pPacket->dataBuffer, pPacket->wDataLength, 1, g_updateSoftTask.pNew1);
            }
            if (g_updateSoftTask.pNew2 != NULL)
            {
                fwrite(pPacket->dataBuffer, pPacket->wDataLength, 1, g_updateSoftTask.pNew2);
            }
            if (g_updateSoftTask.dwRecvSize == pPacket->dwFileSize)
            {
                nState = 0xffffffff;
                g_updateSoftTask.bRunFlag = 0;
                fclose(g_updateSoftTask.pNew1);
                if (NULL != g_updateSoftTask.pNew2)
                {
                    fclose(g_updateSoftTask.pNew2);
                }
                g_updateSoftTask.pNew1 = NULL;
                g_updateSoftTask.pNew2 = NULL;
                char fileUrl1[256];
                char fileUrl2[256];
                char fileDst[256];
                memset(fileDst, 0, 256);
                memset(fileUrl1, 0, 256);
                memset(fileUrl2, 0, 256);
                if (pPacket->filetype == 1)
                {
                    sprintf(fileUrl1, "%s/%s_new1_back", g_appDir, pPacket->name);
                    sprintf(fileUrl2, "%s/%s_new2_back", g_appDir, pPacket->name);
                    sprintf(fileDst, "%s/%s_new1", g_appDir, pPacket->name);
                    DelFile(fileDst);
                    int nRet1 = rename(fileUrl1, fileDst);
                    memset(fileDst, 0, 256);
                    sprintf(fileDst, "%s/%s_new2", g_appDir, pPacket->name);
                    DelFile(fileDst);
                    int nRet2 = rename(fileUrl2, fileDst);
                    if (nRet1 == 0 && nRet2 == 0)
                    {
                        // ts_printf("g_updatesoftTask fiiiiinish 1\n");
                        // BYTE tempInfoRestart[200];
                        // int nLengthRestart = 200;
                        // BYTE restart[20];
                        // restart[0] = RTU_RESTART;
                        // restart[1] = 5;
                        // PacketShuiWenXieYiData(tempInfoRestart, &nLengthRestart, restart,
                        //                        2, DEVICE_HLWSREPORT_FUNCODE, EMPTY_NOID, 0);
                        // SendToServerData(tempInfoRestart, nLengthRestart);
                        // sleep(5);
                        // Restart();
                        // exit(0);
                    }
                }
                else
                {
                    // sprintf(fileUrl1, "%s/%s_back", g_appDir, pPacket->name);
                    // sprintf(fileDst, "%s/%s", g_appDir, pPacket->name);
                    // DelFile(fileDst);
                    // int nRet = rename(fileUrl1, fileDst);
                    // ts_printf("pftTask fiiiiinish 1\n");
                    // if (nRet == 0)
                    // {
                    //     Restart();
                    //     exit(0);
                    // }
                }
            }
            else
            {
                g_updateSoftTask.dwCurPos = pPacket->dwCurPos;
            }
        }
    }
    // struct UpdatePacketInfo info;
    // info.dwCode = nError;
    // info.dwPos = pPacket->dwCurPos;
    // ReportUpdateSoftState(info);
    return 0;
}

int ProcLinuxCommand(BYTE *pData)
{
    char tempContent[2048];
    char tempOut[4096];
    memset(tempContent, 0, 2048);
    WORD wCommandLength = 0;
    memcpy(&wCommandLength, pData, 2);

    if (wCommandLength > 2047)
    {
        return -1;
    }
    memcpy(tempContent, pData + 2, wCommandLength);
    ts_printf("shell %s\r\n", tempContent);
    short resultLength = 0;
    int nRet = ExecShell(tempContent, tempOut, &resultLength);
    if (nRet == 0)
    {
        ts_printf("shell null\n");
    }
    return 0;
}

void DelFile(char *pDir)
{
    if (access(pDir, F_OK) == 0)
    {
        remove(pDir);
    }
}
int ExecShell(char *pShell, char *pResult, short *pResultLength)
{
    *pResultLength = 0;
    FILE *fstream = NULL;
    char buff[1024];

    memset(buff, 0, sizeof(buff));
    // execute command get all video
    if (NULL == (fstream = popen(pShell, "r")))
    {
        // log_error("execute command %s failed", FIND_VIDEO_CMD);
        return 0;
    }
    // get video name
    int nPos = 0;
    memset(buff, 0x00, sizeof(buff));
    while (NULL != fgets(buff, sizeof(buff), fstream))
    {
        memcpy(pResult + nPos, buff, strlen(buff));
        nPos += strlen(buff);
        memset(buff, 0x00, sizeof(buff));
    }
    *pResultLength = nPos;
    pclose(fstream);
    return 1;
}

// void StartCeliu(uint8_t *pData)
// {

//     g_veloTaskInfo.curPosNum = 0;       // 当前所在位置
//     g_veloTaskInfo.curNumOfMeas = 0;    // 在点位上的当前测流次数
//     g_veloTaskInfo.measCount = 1;       // 目标点位 测流次数
//     g_veloTaskInfo.posCount = pData[1]; // 点位总数
//     // memset(g_veloTaskInfo.distanceArray, 0, sizeof(g_veloTaskInfo.distanceArray) / sizeof(g_veloTaskInfo.distanceArray[0]));
//     memcpy(g_veloTaskInfo.distanceArray, pData + 2, g_veloTaskInfo.posCount * sizeof(uint32_t));
//     CalcPulseArray();

//     CAR_RADARFLOWDET(&g_veloTaskInfo);
// }

// void CalcPulseArray(void)
// {
//     int i = 0;
//     g_veloTaskInfo.pluseArray[g_veloTaskInfo.posCount] = 0;
//     for (i = 0; i < g_veloTaskInfo.posCount; i++)
//     {
//         g_veloTaskInfo.pluseArray[i] = CalcPulse(g_veloTaskInfo.distanceArray[i]);
//     }
// }