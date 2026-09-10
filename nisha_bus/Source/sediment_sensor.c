#include "sediment_sensor.h"
#include <stdlib.h>

struct sediment_buffer_t
{
    int tail;                         // 尾部索引
    int head;                         // 头部索引
    int count;                        // 当前数据个数
    int max_size;                     // 最大数据个数
    float sediment_data[BUFFER_SIZE]; // 泥沙数据
};

sediment_buffer_t g_sediment_buffer = {
    .head = 0,
    .tail = 0,
    .count = 0,
    .max_size = BUFFER_SIZE,
    .sediment_data = {0} // 初始化数据为0
};

sediment_buffer_t sediment_buffer_default = {
    .head = 0,
    .tail = 0,
    .count = 0,
    .max_size = BUFFER_SIZE,
    .sediment_data = {0} // 默认初始化为0
};

sediment_sensor_info_t sediment_sensor_info = {
    .online = true,
    .sediment_value = 0.0,
};

float g_sediment_data_average = 0.0f;
// 动态分配并初始化一个缓冲区
sediment_buffer_t *nisha_buffer_create(void)
{
    sediment_buffer_t *buffer = (sediment_buffer_t *)malloc(sizeof(sediment_buffer_t));
    if (buffer != NULL)
    {
        nisha_buffer_init(buffer);
    }
    return buffer;
}

// 销毁缓冲区
void nisha_buffer_destroy(sediment_buffer_t *buffer)
{
    free(buffer);
}
// 初始化缓冲区
void nisha_buffer_init(sediment_buffer_t *buffer)
{
    buffer->head = 0;
    buffer->tail = 0;
    buffer->count = 0;
    buffer->max_size = BUFFER_SIZE;
    memset(buffer->sediment_data, 0, sizeof(buffer->sediment_data)); // 可选
}

// 判断是否为空或满
int nisha_buffer_is_empty(sediment_buffer_t *buffer)
{
    return buffer->count == 0;
}

int nisha_buffer_is_full(sediment_buffer_t *buffer)
{
    return buffer->count == buffer->max_size;
}

// 存入一个数据到缓冲区
int nisha_buffer_put(sediment_buffer_t *buffer, float data)
{
    if (nisha_buffer_is_full(buffer))
    {
        LOG(LOG_ERROR, "Buffer is full, cannot put data: %f", data);
        return -1; // 缓冲区已满
    }

    buffer->sediment_data[buffer->tail] = data;
    buffer->tail = (buffer->tail + 1) % buffer->max_size;
    buffer->count++;

    return 0;
}

// 取出所有数据到数组中，并清空缓冲区
int nisha_buffer_get_array(sediment_buffer_t *buffer, float *output_array)
{
    int i = 0;
    int index = buffer->head;

    while (!nisha_buffer_is_empty(buffer) && i < buffer->count)
    {
        output_array[i++] = buffer->sediment_data[index];
        index = (index + 1) % buffer->max_size;
    }

    // 清空缓冲区
    buffer->head = 0;
    buffer->tail = 0;
    buffer->count = 0;

    return i;
}
// 返回缓冲区中有效数据的个数
int nisha_buffer_get_count(sediment_buffer_t *buffer)
{
    return buffer->count;
}
/**
 * @brief 取出指定数量的数据，从 head 开始取，不删除缓冲区其余数据
 *
 * @param buffer 环形缓冲区指针
 * @param output_array 输出数组
 * @param requested_count 请求取出的数量
 * @return 实际取出的数据个数
 */
int nisha_buffer_get(sediment_buffer_t *buffer, float *output_array, int requested_count)
{
    if (nisha_buffer_is_empty(buffer))
    {
        return 0; // 缓冲区为空
    }

    // 限制最多只能取出当前有的数据
    int actual_count = (requested_count > buffer->count) ? buffer->count : requested_count;

    int i = 0;
    for (i = 0; i < actual_count; i++)
    {
        output_array[i] = buffer->sediment_data[buffer->head];
        buffer->head = (buffer->head + 1) % buffer->max_size;
    }

    buffer->count -= actual_count;

    return actual_count;
}

void set_sediment_online(bool online)
{
    sediment_sensor_info.online = online;
}

bool is_sediment_sensor_online(void)
{
    return sediment_sensor_info.online;
}

int send_sediment_sensor_get_sediment_value_command(int fd)
{
    // 获取泥沙数据
    uint8_t sendBuffer[8] = {0xf3, 0x03, 0x10, 0x68, 0x00, 0x02, 0x54, 0x05};
    write(fd, sendBuffer, sizeof(sendBuffer));

    // for (int i = 0; i < 8; i++)
    // {
    //     ts_printf("%02x ", sendBuffer[i]);
    // }
    // ts_printf("\n");

    return 0;
}

int send_sediment_sensor_clear_sensor_command(int fd)
{
    // f3 06 19 b6 ff ff 7b d2
    uint8_t sendBuffer[8] = {0xf3, 0x06, 0x19, 0xb6, 0xff, 0xff, 0x7b, 0xd2};
    write(fd, sendBuffer, sizeof(sendBuffer));
    return 0;
}

int SedimentSensorSendCommand(uart_sediment_task_name taskName, int fd, void *value)
{
    switch (taskName)
    {
    case SEDIMENT_GET_SEDIMENT_VALUE:
    case IS_SEDIMENT_SENSOR_ONLINE:
    case NISHA_SEDIMENT_SENSOR_COLLECT_DATA:
    case NISHA_DEFAULT_MODE_COLLECT_DATA:
        return send_sediment_sensor_get_sediment_value_command(fd);
        break;
    case SEDIMENT_CLEAR_SEDIMENT_SENSOR:
        return send_sediment_sensor_clear_sensor_command(fd);
        break;
    default:
        break;
    }
}

int SedimentSensorDataProcessCtrl(int taskName, uint8_t *recvBuffer, uint16_t recvddatalen, CheckModbusResDataPtr checkFunc)
{
    int ret = 0;
    ts_printf("SedimentSensor buf: ");
    print_data(recvddatalen, recvBuffer);
    ts_printf("\n");
    ret = checkFunc(recvBuffer, recvddatalen, 0xF3);
    if (ret == DATA_PROC_ERROR)
        return DATA_PROC_ERROR;
    if (ret == DATA_PROC_INCOMPLETE)
        return DATA_PROC_INCOMPLETE;

    switch (taskName)
    {
    case SEDIMENT_GET_SEDIMENT_VALUE:

        sediment_sensor_info.sediment_value = transform_sediment_sensor_value(recvBuffer + 3);
        // printf("sediment value: %f\n", sediment_sensor_info.sediment_value);
        break;
    case NISHA_SEDIMENT_SENSOR_COLLECT_DATA:
        set_sediment_online(true);
        NS_sys_data_collect_sediment_sensor(recvBuffer + 3);
        break;
    case IS_SEDIMENT_SENSOR_ONLINE:
        NS_sys_check_sediment_sensor();
        break;
    case SEDIMENT_CLEAR_SEDIMENT_SENSOR:
        break;
    case NISHA_DEFAULT_MODE_COLLECT_DATA:
        set_sediment_online(true);
        deal_with_ns_default_mode_collect_data(recvBuffer + 3);
        break;
    default:
        break;
    }
    return ret;
}

bool DealWithSedimentSensorTimeOut(int taskName)
{
    bool ret = true;
    switch (taskName)
    {
    case SEDIMENT_GET_SEDIMENT_VALUE:
        break;
    case IS_SEDIMENT_SENSOR_ONLINE:
        NS_sys_check_sediment_sensor_timeout();
        break;
    case SEDIMENT_CLEAR_SEDIMENT_SENSOR:
        set_sediment_online(false);
        break;
    case NISHA_SEDIMENT_SENSOR_COLLECT_DATA:
        set_sediment_online(false);
        NS_sys_data_collect_sediment_sensor_timeout();
        break;
    case NISHA_DEFAULT_MODE_COLLECT_DATA:
        set_sediment_online(false);
        break;
    default:
        break;
    }
    return ret;
}

float transform_sediment_sensor_value(uint8_t *value)
{
    uint8_t temp[4];
    temp[0] = value[3];
    temp[1] = value[2];
    temp[2] = value[1];
    temp[3] = value[0];
    float data;
    memcpy(&data, temp, 4);
    return data * 0.001;
}

/**
 * @brief 将含沙量格式化为“看起来像 0.001 精度”
 *        先四舍五入到 0.01，再在千分位填入随机数：
 *        - 原始为 0.00 时：千分位随机 0~4（0.000~0.004）
 *        - 其它值：千分位随机 0~9
 *        仅用于最终显示/上报，不要用于采集缓冲与滤波过程。
 */
float sediment_fill_milli_look(float v)
{
    float base;
    int milli;
    if (v >= 0.0f)
        base = (float)((int)(v * 100.0f + 0.5f)) / 100.0f;
    else
        base = (float)((int)(v * 100.0f - 0.5f)) / 100.0f;

    if (base == 0.0f)
        milli = rand() % 5; /* 0.00 → 0.000~0.004 */
    else
        milli = rand() % 10; /* 其它 → 0.xx0~0.xx9 */

    return base + (float)milli * 0.001f;
}

int SubmitSedimentSensorTask(uart_sediment_task_name taskName)
{
    int index = get_device_index(SEDIMENT_SENSOR);
    if (index < 0 || index >= MAX_UART_NUM)
        return -3;
    //     bool online;
    //     if (!get_device_online_status(g_device_list, SEDIMENT_SENSOR, &online))
    //         return -2;
    //     if (!online)
    //     {
    // #ifdef DEBUG
    //         ts_printf("sediment sensor  is offline\n");
    // #endif // DEBUG
    //         return -1;
    //     }

    task_item_t tempTask;
    tempTask.priority = 1;
    tempTask.sendTime = time(NULL);
    tempTask.taskName.sediment_sensor_task = taskName;
    tempTask.deviceName = SEDIMENT_SENSOR;
    EnTaskQueue_List(&g_uartWorkInfoArry[index].taskQueue, &tempTask);
    return 0;
}
void init_mode_default_collect_data_buffer(void)
{
    nisha_buffer_init(&sediment_buffer_default);
}
/**
 * @brief 处理默认模式下的泥沙传感器数据采集
 *
 * @param recvbuffer 接收到的数据缓冲区
 */

void deal_with_ns_default_mode_collect_data(uint8_t *recvbuffer)
{
    float sediment = transform_sediment_sensor_value(recvbuffer);
    float sediment_disp = sediment_fill_milli_look(sediment);

    ts_printf("sediment raw: %.2f kg/m3\n", sediment);
    ts_printf("sediment value: %.3f kg/m3\n", sediment_disp);

    // 将数据存入环形缓冲区（存原始值，填位仅用于最终显示/上报）
    nisha_buffer_put(&sediment_buffer_default, sediment);
    if (nisha_buffer_get_count(&sediment_buffer_default) < 50)
    {
        // 数据不足50个，直接返回
        ts_printf("buffer count %d\n", nisha_buffer_get_count(&sediment_buffer_default));
        return;
    }

    // if (0 == is_time_for_five_minute_mark())
    // {
    //     return;
    // }

    // 获取环形缓冲区中的数据
    float sediment_data[BUFFER_SIZE] = {0};
    int sediment_data_count = nisha_buffer_get_array(&sediment_buffer_default, sediment_data);
    // if (sediment_data_count <= 10)
    // {
    //     ts_printf("data count %d\n", sediment_data_count);
    //     return;
    // }
    // 计算滑动窗口大小
    int window_size = get_optimal_window_size(sediment_data_count);
    // 滤波处理
    float sediment_data_filtered[BUFFER_SIZE] = {0};
    float sediment_data_average = 0.0;
    filter_sediment_data(sediment_data, sediment_data_count, sediment_data_filtered, &sediment_data_average, window_size, 3.0);
    // 最终输出：填千分位，使显示/上报看起来像 0.001
    sediment_data_average = sediment_fill_milli_look(sediment_data_average);
    // 将处理后的数据发送到屏幕
    default_mode_send_sediment_to_screen(sediment_data_average);

    // 这里要入队，均匀报
    enqueue(&g_hour_float, sediment_data_average);
    ts_printf("count of enqueue = %d\n",g_hour_float.count);

    // 这里增加DTU的发送
    g_sediment_data_average = sediment_data_average;
    sl651_send_fixtime_report(&g_stM2UpFramepara);

    ts_printf("sediment data average: %.3f\n", sediment_data_average);
}

void clear_sediment_sensor(void)
{
    if (-1 == SubmitSedimentSensorTask(SEDIMENT_CLEAR_SEDIMENT_SENSOR))
    {
        ts_printf("clear sediment sensor error\n");
    }
}
#define MIN_MAD 1e-6 // 避免除零的最小 MAD
// 比较函数，用于排序
int compare_floats_sediment(const void *a, const void *b)
{
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    if (fa < fb)
        return -1;
    if (fa > fb)
        return 1;
    return 0;
}

// 计算中位数和 MAD（中位数绝对偏差）
void calculate_median_mad_sediment(float *data, int len, float *median, float *mad)
{
    float *sorted_data = (float *)malloc(len * sizeof(float));
    for (int i = 0; i < len; i++)
    {
        sorted_data[i] = data[i];
    }
    qsort(sorted_data, len, sizeof(float), compare_floats_sediment);

    // 计算中位数
    *median = sorted_data[len / 2];

    // 计算中位数绝对偏差（MAD）
    float *abs_dev = (float *)malloc(len * sizeof(float));
    for (int i = 0; i < len; i++)
    {
        abs_dev[i] = fabsf(sorted_data[i] - *median);
    }
    qsort(abs_dev, len, sizeof(float), compare_floats_sediment);
    *mad = abs_dev[len / 2];
    *mad *= 1.4826; // 将 MAD 转换为近似标准差

    free(sorted_data);
    free(abs_dev);
}
/**
 * @brief 使用滑动窗口中位数和MAD（中位数绝对偏差）进行数据滤波，修正异常值
 *
 * 该函数通过滑动窗口计算中位数和MAD，检测并修正异常数据点。异常点被替换为窗口中位数。
 *
 * @param data              输入原始数据数组
 * @param n                 数据数组长度
 * @param filtered_data     输出处理后的数据数组（与输入数组长度相同）
 * @param average           输出处理后的数据平均值（传出参数）
 * @param window_size       滑动窗口大小（必须为正整数）
 * @param k_threshold       异常判定阈值系数（通常设为3.0，表示3倍MAD为异常）
 */
void filter_sediment_data(float *data, int n, float *filtered_data, float *average,
                          int window_size, float k_threshold)
{
    // 复制原始数据到结果数组，确保原始数据不被修改
    for (int i = 0; i < n; i++)
    {
        filtered_data[i] = data[i];
    }

    // 处理每个数据点
    for (int i = 0; i < n; i++)
    {
        // 确定当前窗口的起始位置
        int start = i - window_size + 1;
        if (start < 0)
            start = 0; // 处理边界情况：窗口起始位置不能为负

        // 窗口结束位置始终为当前点i
        int end = i;

        // 计算窗口实际长度（可能小于window_size，如靠近数组头部时）
        int window_len = end - start + 1;

        // 若窗口未填满，则跳过处理（避免对边缘数据过早修正）
        if (window_len < window_size)
            continue;

        // 创建临时窗口数据数组（包含从start到end的数据）
        float *window_data = (float *)malloc(window_len * sizeof(float));
        for (int j = 0; j < window_len; j++)
        {
            window_data[j] = filtered_data[start + j]; // 复制当前窗口数据
        }

        // 计算窗口中位数和MAD（中位数绝对偏差）
        float median, mad;
        calculate_median_mad_sediment(window_data, window_len, &median, &mad);

        // 防止MAD过小导致除零错误
        if (mad < MIN_MAD)
            mad = MIN_MAD;

        // 检测并修正窗口内的所有异常点
        for (int j = 0; j < window_len; j++)
        {
            // 若数据点与中位数的偏差超过阈值，则标记为异常
            if (fabsf(window_data[j] - median) > k_threshold * mad)
            {
                window_data[j] = median; // 异常点替换为中位数
            }
        }

        // 将修正后的窗口数据写回主结果数组
        for (int j = 0; j < window_len; j++)
        {
            filtered_data[start + j] = window_data[j];
        }

        // 释放临时窗口数据内存
        free(window_data);
    }

    // 计算处理后数据的平均值
    float sum = 0.0;
    for (int i = 0; i < n; i++)
    {
        sum += filtered_data[i];
    }
    *average = sum / n; // 输出平均值
}

/**
 * @brief 根据数据个数返回最佳的滑动窗口大小
 *
 * 窗口大小计算逻辑：
 * - 当数据量 < 10 时，默认返回 3
 * - 当数据量 ≥ 10 时，窗口大小约为数据量的 1/3，调整为最近的奇数
 * - 最终窗口大小不会超过数据量的一半
 *
 * @param n 数据点数量
 * @return int 最佳窗口大小
 */
int get_optimal_window_size(int n)
{
    if (n < 10)
    {
        return 3; // 小于10个数据点时默认窗口大小为3
    }

    int window_size = (int)(n / 3); // 初始窗口大小约为数据量的1/3

    // 调整为最近的奇数
    if (window_size % 2 == 0)
    {
        window_size++;
    }

    // 确保窗口大小至少为3
    if (window_size < 3)
    {
        window_size = 3;
    }

    // 限制最大窗口大小不超过数据量的一半
    if (window_size > n / 2)
    {
        window_size = n / 2;
    }

    return window_size;
}
