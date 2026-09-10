#ifndef __TASKQUEUE_H__
#define __TASKQUEUE_H__

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "general_head.h"
// #include "uart.h"
#include "data_proc.h"
#include "log.h"

// #define LIST_MAX_SIZE 10
typedef void *(*FUNC)(void *);

/* DEFINE TASK */
#define CARSTART 1

// 通用的队列(LIST)
typedef struct
{
    ////pthread_cond_t *condition;  //条件变量，控制LORA屏向任务队列里面随意添加指令，等上一条屏幕指令执行完后，才能添加下一条指令，如 上升 下降
    pthread_mutex_t *mutex;
    uint8_t front;    // 队头
    uint8_t rear;     // 队尾
    int itemSize;     // 队列中一个元素的大小
    int maxArrayLen;  // 队列最大长度
    int curItemCount; // 队列中元素个数
    void *pItemArray; // 数组队列指针
    // int capacity;      // 动态容量

} list_queue_t, *plist_queue_t;

/* 串口连接设备的类型来区分目标串口上应该执行的任务 任务队列的基本单位 */
typedef struct
{
    uart_task_name taskName;
    int deviceName;
    uint8_t priority; // 优先级，数字越低优先级越高
    time_t sendTime;  // 指令发送时间
    int retryCount;   // 当前重试次数
    int taskPort;     // 向任务链添加任务时，绑定任务和设备连接的串口
    void *value;      // 参数
} task_item_t, *ptask_item_t;

/* 函数指针ITEM */
typedef struct
{
    FUNC functionPtr;

} func_ptr_item_t, *pfunc_ptr_item_t;

/* 通用 buffer item */
typedef struct
{
    WORD wLength;
    BYTE tempBuffer[512];

} common_item_t, *pcommon_item_t;

/**
 * @brief 	 【简介】初始化队列结构
 * @param 	 itemSize [in], 队列中一个元素的大小
 * @param 	 arrayLen [in], 队列的最大长度
 * @param 	 pItemArray [in], 被作为队列管理的数组
 * @param 	 taskQueue [out], 获得以上参数的队列结构的指针
 * @return 	 【解释返回值】成功返回-1, 失败返回-1
 */
int InitTaskQueue_List(int itemSize, int arrayLen, void *pItemArray, plist_queue_t taskQueue);
/**
 * @brief 	 【简介】把Item加入到队列中
 * @param 	 taskQueue [out], 目标队列
 * @param 	 pItemSrc [in], 加入到队列中的item的地址
 * @return 	 【解释返回值】成功返回true 失败返回false
 */
bool EnTaskQueue_List(plist_queue_t taskQueue, void *pItemSrc);
/**
 * @brief 	 【简介】把队列中的一个Item出队
 * @param 	 taskQueue [in], 出队的源队列
 * @param 	 pItemDest [out], 获取出队的Item
 * @return 	 【解释返回值】成功返回true 失败返回false
 */
bool DeTaskQueue_List(plist_queue_t taskQueue, void *pItemDest);
/**
 * @brief 	 【简介】把任务强行加入队列中 如果队列满了 队头元素会被覆盖
 * @param 	 taskQueue [out], 目标队列
 * @param 	 pItemSrc [in], 想要加入到队列中的Item
 * @return 	 【解释返回值】 成功返回true
 */
bool ForceEnTaskQueue_List(plist_queue_t taskQueue, void *pItemSrc);
bool ClearTaskQueue_List(plist_queue_t taskQueue);
void PrintTaskQueue_List(plist_queue_t taskQueue);
/**
 * @brief 	 【简介】
 * @param 	 taskQueue [in/out], 参数意义
 * @return 	 【解释返回值】
 * @return 	 【解释返回值】
 */

bool TaskQueueEmpty(plist_queue_t taskQueue);
int compareTasks(ptask_item_t task1, ptask_item_t task2);
int findInsertPosition(plist_queue_t taskQueue, ptask_item_t new_task);

#endif
