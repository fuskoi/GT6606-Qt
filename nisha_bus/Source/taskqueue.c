#include "taskqueue.h"

int InitTaskQueue_List(int itemSize, int arrayLen, void *pItemArray, plist_queue_t taskQueue)
{
    if (NULL == pItemArray)
    {
        PRINTERROR;
        return -1;
    }
    // memset(taskQueue, 0, sizeof(list_queue_t)); // 确保队列结构被初始化为0
    taskQueue->front = 0;               // 队头
    taskQueue->rear = 0;                // 队尾
    taskQueue->curItemCount = 0;        // 当前队列元素个数
    taskQueue->itemSize = itemSize;     // 元素大小
    taskQueue->maxArrayLen = arrayLen;  // 数组长度
    taskQueue->pItemArray = pItemArray; // 元素数组

    return 0;
}
/// @brief 入队
/// @param taskQueue 队列指针
/// @param pItemSrc 源元素的指针
/// @return 成功true 失败false
// 比较两个任务的优先级
int compareTasks(ptask_item_t task1, ptask_item_t task2)
{
    return task1->priority - task2->priority;
}

// 使用二分查找找到插入位置
int findInsertPosition(plist_queue_t taskQueue, ptask_item_t new_task)
{
    if (taskQueue == NULL || new_task == NULL)
        return -1;

    int front = taskQueue->front;
    int maxLen = taskQueue->maxArrayLen;
    int count = taskQueue->curItemCount; // 使用实际元素数量

    // 空队列直接插入队头
    if (count == 0)
        return front;

    int low = 0;          // 逻辑索引起点
    int high = count - 1; // 逻辑索引终点

    while (low <= high)
    {
        int mid = low + (high - low) / 2;
        // 将逻辑索引转换为物理索引
        int phys_mid = (front + mid) % maxLen;
        ptask_item_t mid_task = (ptask_item_t)((char *)taskQueue->pItemArray + phys_mid * taskQueue->itemSize);

        int cmp = compareTasks(new_task, mid_task);
        if (cmp <= 0)
        {
            high = mid - 1; // 向左半部分查找
        }
        else
        {
            low = mid + 1; // 向右半部分查找
        }
    }

    // 插入位置为逻辑low对应的物理索引
    return (front + low) % maxLen;
}

bool EnTaskQueue_List(plist_queue_t taskQueue, void *pItemSrc)
{
    if (!taskQueue || !pItemSrc || !taskQueue->mutex || !taskQueue->pItemArray
        || taskQueue->maxArrayLen <= 0 || taskQueue->itemSize <= 0)
        return false;

    if (pthread_mutex_lock(taskQueue->mutex) != 0)
        return false;

    bool success = false;
    const int maxLen = taskQueue->maxArrayLen;

    /* 关键修改1：严格容量检查 */
    if ((taskQueue->rear + 1) % maxLen == taskQueue->front)
    {
        // 队列已满，直接返回（避免后续操作）
        goto cleanup;
    }
    ptask_item_t new_task = (ptask_item_t)pItemSrc;
    int insert_pos = findInsertPosition(taskQueue, new_task);

    /* 关键修改2：安全索引计算 */
    int current_rear = taskQueue->rear;
    int new_rear = (current_rear + 1) % maxLen; // 确保rear不越界

    // 逆向移动元素（从原队尾到插入位置）
    int move_pos = current_rear;
    while (move_pos != insert_pos)
    {
        int prev_pos = (move_pos - 1 + maxLen) % maxLen; // 安全取模
        memcpy((char *)taskQueue->pItemArray + move_pos * taskQueue->itemSize,
               (char *)taskQueue->pItemArray + prev_pos * taskQueue->itemSize,
               taskQueue->itemSize);
        move_pos = prev_pos;
    }

    // 插入新元素
    memcpy((char *)taskQueue->pItemArray + insert_pos * taskQueue->itemSize,
           new_task, taskQueue->itemSize);
    taskQueue->rear = new_rear; // 更新为安全计算后的rear
    taskQueue->curItemCount++;
    success = true;

cleanup:
    if (pthread_mutex_unlock(taskQueue->mutex) != 0)
    {
        LOG(LOG_ERROR, "Failed to unlock mutex");
        return false;
    }
    return success;
}

/// @brief 出队
/// @param taskQueue 队列指针
/// @param pItemDest 获取目标元素的指针
/// @return 成功true 失败false
// 出队函数修正
bool DeTaskQueue_List(plist_queue_t taskQueue, void *pItemDest)
{
    pthread_mutex_lock(taskQueue->mutex);
    if (taskQueue->front == taskQueue->rear) // 队列为空
    {
        pthread_mutex_unlock(taskQueue->mutex);
        return false;
    }
    // 修正指针运算：转换为char*进行字节偏移
    memcpy(pItemDest, (char *)taskQueue->pItemArray + taskQueue->front * taskQueue->itemSize, taskQueue->itemSize);
    taskQueue->front = (taskQueue->front + 1) % taskQueue->maxArrayLen;
    taskQueue->curItemCount--;
    pthread_mutex_unlock(taskQueue->mutex);
    // printf("task out Name = %d\n", ((ptask_item_t)pItemDest)->taskName);
    // printf("--------------------------------\n");
    // PrintTaskQueue_List(taskQueue);
    return true;
}

/// @brief 判断队列是否为空
/// @param taskQueue 队列指针
/// @return 为空返回true 非空返回false
bool TaskQueueEmpty(plist_queue_t taskQueue)
{
    bool bflag = false; // 队列非空
    pthread_mutex_lock(taskQueue->mutex);
    if (taskQueue->front == taskQueue->rear)
        bflag = true; // 队列空
    pthread_mutex_unlock(taskQueue->mutex);
    return bflag;
}

/// @brief 把任务强行加入队列中 如果队列满了 队头元素会被覆盖
/// @param taskQueue 目标队列
/// @param pItemSrc 将要入队的任务
/// @return 成功返回true
// 强制入队函数修正
bool ForceEnTaskQueue_List(plist_queue_t taskQueue, void *pItemSrc)
{
    pthread_mutex_lock(taskQueue->mutex);
    // 修正指针运算：转换为char*进行字节偏移
    memcpy((char *)taskQueue->pItemArray + taskQueue->rear * taskQueue->itemSize, pItemSrc, taskQueue->itemSize);
    if ((taskQueue->rear + 1) % taskQueue->maxArrayLen == taskQueue->front)
    {
        taskQueue->front = (taskQueue->front + 1) % taskQueue->maxArrayLen;
    }
    else
    {
        taskQueue->curItemCount++;
    }
    taskQueue->rear = (taskQueue->rear + 1) % taskQueue->maxArrayLen;
    pthread_mutex_unlock(taskQueue->mutex);
    return true;
}

bool ClearTaskQueue_List(plist_queue_t taskQueue)
{
    pthread_mutex_lock(taskQueue->mutex);
    taskQueue->front = 0;        // 队头
    taskQueue->rear = 0;         // 队尾
    taskQueue->curItemCount = 0; // 当前队列元素个数
    pthread_mutex_unlock(taskQueue->mutex);
    return true;
}

/// @brief 打印队列中的所有元素
void PrintTaskQueue_List(plist_queue_t taskQueue)
{
    if (NULL == taskQueue || NULL == taskQueue->pItemArray)
    {
        printf("Queue is not initialized or empty.\n");
        return;
    }

    pthread_mutex_lock(taskQueue->mutex);

    if (taskQueue->curItemCount == 0)
    {
        printf("The queue is empty.\n");
        pthread_mutex_unlock(taskQueue->mutex);
        return;
    }

    printf("Queue contents:\n");

    int index = taskQueue->front;
    for (int i = 0; i < taskQueue->curItemCount; ++i)
    {
        ptask_item_t item = (ptask_item_t)((char *)taskQueue->pItemArray + index * taskQueue->itemSize);
        printf("device Name: %d, Priority: %d\n", item->deviceName, item->priority);

        // 计算下一个元素的位置
        index = (index + 1) % taskQueue->maxArrayLen;
    }

    pthread_mutex_unlock(taskQueue->mutex);
}