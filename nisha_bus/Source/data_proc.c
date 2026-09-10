/**
 * Copyright (C), 2023,
 * @file 	 【文件名称，不包含绝对路径】
 * @author 	 【作者】
 * @date 	 2023-08-11
 * @brief 	 【简介】数据类型转换功能函数
 * @note 	 【重大修改可以放在这里】
 */
#include "data_proc.h"
#include "general_head.h"
// #define __STDC_WANT_LIB_EXT1__ 1
void BcdTime2String(BYTE bcd_buf[], BYTE tmp_buf[])
{
    /* 文件名格式为： “2018-09-15 12:25:26.mp4” */
    tmp_buf[0] = '2';                           // 2 /* 年份 */
    tmp_buf[1] = '0';                           // 0
    tmp_buf[2] = (bcd_buf[0] >> 4) + (0x30);    // 1    (BCD码转为ASCII码时要加0x30，还要注意运算符优先级)
    tmp_buf[3] = (bcd_buf[0] & 0x0f) + (0x30);  // 8
    tmp_buf[4] = 0x2D;                          // - 中间横杠的ASCII码
    tmp_buf[5] = (bcd_buf[1] >> 4) + (0x30);    // 0 /* 月份 */
    tmp_buf[6] = (bcd_buf[1] & 0x0f) + (0x30);  // 9
    tmp_buf[7] = 0x2D;                          // - ()
    tmp_buf[8] = (bcd_buf[2] >> 4) + (0x30);    // 1 /* 天数 */
    tmp_buf[9] = (bcd_buf[2] & 0x0f) + (0x30);  // 5
    tmp_buf[10] = 0x20;                         // 空格的ASCII码
    tmp_buf[11] = (bcd_buf[3] >> 4) + (0x30);   // 1 /* 小时 */
    tmp_buf[12] = (bcd_buf[3] & 0x0f) + (0x30); // 2
    tmp_buf[13] = 0x3A;                         // 冒号的ASCII码
    tmp_buf[14] = (bcd_buf[4] >> 4) + (0x30);   // 2 /* 分钟 */
    tmp_buf[15] = (bcd_buf[4] & 0x0f) + (0x30); // 5
    tmp_buf[16] = 0x3A;                         // 冒号的ASCII码
    tmp_buf[17] = (bcd_buf[5] >> 4) + (0x30);   // 2 /* 秒数 */
    tmp_buf[18] = (bcd_buf[5] & 0x0f) + (0x30); // 6
}

void String2BcdTime(char *name_buf, BYTE *bcd_buf)
{
    /* 文件名格式为： “2018-09-15 12:25:26.mp4” */
    /* BCD 时间格式为 “180915122526” */
    bcd_buf[0] = (char)((name_buf[2] - '0') << 4) + (name_buf[3] - '0');
    bcd_buf[1] = (char)((name_buf[5] - '0') << 4) + (name_buf[6] - '0');
    bcd_buf[2] = (char)((name_buf[8] - '0') << 4) + (name_buf[9] - '0');
    bcd_buf[3] = (char)((name_buf[11] - '0') << 4) + (name_buf[12] - '0');
    bcd_buf[4] = (char)((name_buf[14] - '0') << 4) + (name_buf[15] - '0');
    bcd_buf[5] = (char)((name_buf[17] - '0') << 4) + (name_buf[18] - '0');
}

void IntToBCD(int value, BYTE *bcdArray, int len)
{
    int i;
    for (i = len - 1; i >= 0; i--)
    {
        bcdArray[i] = ((value / 10 % 10) << 4) + (value % 10);
        value /= 100;
    }
}

BYTE DataDefinition(BYTE numberOfBytes, BYTE decimalDigits)
{
    BYTE mergedByte = 0;

    // 限制param1的范围在0-31
    numberOfBytes = numberOfBytes & 0x1F;

    // 将param1的高五位存储到mergedByte的高五位
    mergedByte = (numberOfBytes << 3) & 0xF8;

    // 限制param2的范围在0-7
    decimalDigits = decimalDigits & 0x07;

    // 将param2的低三位存储到mergedByte的低三位
    mergedByte |= decimalDigits;

    return mergedByte;
}

int ConvertDecimalToInteger(double num)
{
    double rounded_num = round(num * 10); // 将小数四舍五入到最接近的整数
    int converted_num = (int)rounded_num; // 将浮点数转换为整数
    return converted_num;
}

int SetPacketTypeAndLength(BYTE *result, int flag, int length)
{
    if (NULL == result)
        return -1;
    memset(result, 0, 2);
    // 首先，根据高四位选择，设置高四位的值
    if (flag == 0)
    {
        *(result) &= 0x00;
    }
    else
    {
        *(result) |= 0x80;
    }
    if (length > 255)
    {
        *(result) |= ((length >> 8) & 0x0F);
        *(result + 1) |= length;
    }
    else
    {
        *(result + 1) |= length;
    }
    return 0;
}
// 将两个字节数组合并成一个新的字节数组
// 参数arr1: 第一个字节数组
// 参数size1: 第一个字节数组的大小
// 参数arr2: 第二个字节数组
// 参数size2: 第二个字节数组的大小
// 参数index: 第二个数组在新数组中的起始下标
// 返回值: 指向新分配的字节数组的指针，包含两个输入数组的合并内容
// 需要手动释放内存
BYTE *AppendArrays(BYTE arr1[], int size1, BYTE arr2[], int size2, int index)
{
    int i;
    // 计算新数组的大小rr
    BYTE newSize = size1 + size2;

    // 创建新数组
    BYTE *newArr = (BYTE *)malloc(newSize * sizeof(BYTE));

    // 复制第一个数组的元素到新数组中
    for (i = 0; i < size1; i++)
    {
        newArr[i] = arr1[i];
    }
    // 从指定下标开始，将第二个数组的元素逐个复制到新数组中
    for (i = 0; i < size2; i++)
    {
        newArr[index + i] = arr2[i];
    }

    return newArr;
}

int TimeIncrement(BYTE a, BYTE b, BYTE c, BYTE *dhm)
{
    // 检查参数是否合法
    if (a > 99 || b > 99 || c > 99)
    {
        return -1;
    }
    // 将每个参数转换为BCD码并存储到数组中
    dhm[0] = (a / 10) * 16 + (a % 10);
    dhm[1] = (b / 10) * 16 + (b % 10);
    dhm[2] = (c / 10) * 16 + (c % 10);

    return 0;
}
void uint16_to_hex_array(uint16_t num, uint8_t *arr)
{
    arr[0] = (num >> 8) & 0xFF;
    arr[1] = num & 0xFF;
}
int string_to_hex_array(const char *str, uint8_t *arr)
{
    size_t i;
    size_t len = strlen(str);
    if (len % 2 != 0)
    {
        return -1;
    }
    for (i = 0; i < len; i += 2)
    {
        if (!isxdigit(str[i]) || !isxdigit(str[i + 1]))
        {
            return -1;
        }
        sscanf(str + i, "%2hhx", &arr[i / 2]);
    }
    return 0;
}
long strhex_to_dec(const char *hex_str)
{
    char *endptr;
    long result = strtol(hex_str, &endptr, 16);
    if (*endptr != '\0')
    {
        PRINTERROR;
        return -1;
    }
    return result;
}
// /**
//  * 分析数组的趋势
//  * @param arr 输入的数组
//  * @param size 数组大小
//  * @param dataType 数据类型 (INTEGER 或 FLOAT)
//  * @return 趋势类型
//  */
// Trend analyzeTrend(void *arr, int size, DataType dataType)
// {
//     // 参数严格校验
//     if (arr == NULL || size <= 0)
//     {
//         return INVALID_INPUT;
//     }
//     if (size == 1)
//     {
//         return CONSTANT;
//     }

//     bool has_increased = false;
//     bool has_decreased = false;

//     for (int i = 1; i < size; ++i)
//     {
//         double current_diff = 0.0;

//         switch (dataType)
//         {
//         case INTEGER:
//         {
//             int *int_arr = (int *)arr;
//             current_diff = int_arr[i] - int_arr[i - 1];
//             break;
//         }
//         case FLOAT:
//         {
//             float *float_arr = (float *)arr;
//             current_diff = float_arr[i] - float_arr[i - 1];
//             break;
//         }
//         case UINT32:
//         {
//             uint32_t *uint32_arr = (uint32_t *)arr;
//             current_diff = uint32_arr[i] - uint32_arr[i - 1];
//             break;
//         }
//         default:
//             return INVALID_DATA_TYPE;
//         }

//         // 类型差异化处理
//         if (dataType == INTEGER)
//         {
//             if (current_diff > 0)
//             {
//                 has_increased = true;
//             }
//             else if (current_diff < 0)
//             {
//                 has_decreased = true;
//             }
//         }
//         else if (dataType == FLOAT)
//         { // FLOAT
//             if (current_diff > FLOAT_EPSILON)
//             {
//                 has_increased = true;
//             }
//             else if (current_diff < -FLOAT_EPSILON)
//             {
//                 has_decreased = true;
//             }
//         }
//         else if (dataType == UINT32)
//         {
//             // UINT32
//             if (current_diff > 0)
//             {
//                 has_increased = true;
//             }
//             else if (current_diff < 0)
//             {
//                 has_decreased = true;
//             }
//         }

//         // 发现非单调趋势立即终止
//         if (has_increased && has_decreased)
//         {
//             return NON_MONOTONIC;
//         }
//     }

//     // 判断最终趋势
//     if (has_increased)
//         return INCREASING;
//     if (has_decreased)
//         return DECREASING;
//     return CONSTANT;
// }

void write_data(const char *filename, char *data)
{
    // 获取当前时间
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);

    // 格式化时间字符串
    char time_str[80];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);

    // 打开文件（追加模式）
    FILE *file = fopen(filename, "a");
    if (file == NULL)
    {
        perror("Error opening file");
        return;
    }

    // 写入数据
    fprintf(file, "%s,%s\n", time_str, data);

    // 关闭文件
    fclose(file);
}

/**
 * 将浮点数组转换为逗号分隔的字符串
 * @param array 浮点数组
 * @param length 数组长度
 * @return 包含逗号分隔浮点数的字符串（需调用者释放内存）
 */
char *float_array_to_string(const float *array, size_t length)
{
    if (array == NULL || length == 0)
    {
        return NULL; // 输入无效时返回NULL
    }

    // 估算最大字符串长度：每个浮点数最多20字符（包含小数点和符号），加上逗号和终止符
    size_t max_buffer_size = length * 20 + length; // 每个数20字符+逗号
    char *result = (char *)malloc(max_buffer_size);
    if (result == NULL)
    {
        perror("Failed to allocate memory");
        return NULL;
    }

    size_t offset = 0; // 当前字符串偏移量

    for (size_t i = 0; i < length; i++)
    {
        // 格式化浮点数并写入结果字符串
        int written = snprintf(result + offset, max_buffer_size - offset, "%.3f", array[i]);
        if (written < 0)
        {
            free(result); // 写入失败时释放内存
            return NULL;
        }
        offset += written;

        // 如果不是最后一个数，添加逗号
        if (i < length - 1)
        {
            result[offset++] = ',';
        }
    }

    // 确保字符串以空字符结尾
    result[offset] = '\0';

    return result;
}

/*
 * printf 函数中的格式控制符说明：
 *
 * %d 或 %i    - 有符号十进制整数 (示例: 123)
 * %u          - 无符号十进制整数 (示例: 4294967295)
 * %o          - 无符号八进制整数 (示例: 17)
 * %x          - 无符号十六进制整数（小写）(示例: ff)
 * %X          - 无符号十六进制整数（大写）(示例: FF)
 * %f          - 浮点数（小数）(示例: 3.141593)
 * %e          - 科学计数法表示的浮点数（小写）(示例: 3.141593e+00)
 * %E          - 科学计数法表示的浮点数（大写）(示例: 3.141593E+00)
 * %g          - 根据值自动选择 %f 或 %e（小写）(示例: 3.14159)
 * %G          - 根据值自动选择 %f 或 %E（大写）(示例: 3.14159)
 * %c          - 单个字符 (示例: A)
 * %s          - 字符串 (示例: Hello)
 * %p          - 指针地址 (示例: 0x7ffee4a3c9a0)
 * %%          - 输出百分号 % (示例: %)
 * %n          - 将已经打印的字符数写入到参数中（不常用）
 * %hd         - 短整型（short int）(示例: 123)
 * %ld         - 长整型（long int）(示例: 123456789)
 * %lld        - 超长整型（long long int）(示例: 123456789012)
 * %hhu        - 无符号短整型（unsigned short int）(示例: 65535)
 * %lu         - 无符号长整型（unsigned long int）(示例: 4294967295)
 * %llu        - 无符号超长整型（unsigned long long int）(示例: 18446744073709551615)
 * %Lf         - 长双精度浮点数（long double）(示例: 3.141593)
 *
 * 其他说明：
 * - 宽度和精度：可以在格式控制符中指定宽度和精度，例如：%10.2f 表示总宽度为 10，保留 2 位小数。
 * - 对齐方式：默认右对齐，加 '-' 可改为左对齐，例如：%-10s。
 * - 填充字符：默认空格填充，可以使用 '0' 填充，例如：%05d。
 */

/**
 * @brief 将一个 int 类型的数据写入文件
 *
 * @param filename 文件名
 * @param value 要写入的整数
 * @return int 成功返回 0，失败返回 -1
 */
int write_int_to_file(const char *filename, int value)
{
    FILE *file = fopen(filename, "wb");
    if (file == NULL)
    {
        fprintf(stderr, "Error: Unable to open file '%s' for writing.\n", filename);
        return -1;
    }

    size_t items_written = fwrite(&value, sizeof(int), 1, file);
    fclose(file);

    if (items_written != 1)
    {
        fprintf(stderr, "Error: Failed to write data to file.\n");
        return -1;
    }

    printf("Successfully wrote integer to file '%s': %d\n", filename, value);
    return 0;
}

/**
 * @brief 从文件中读取一个 int 类型的数据
 *
 * @param filename 文件名
 * @param out_value 用于存储读取结果的指针
 * @return int 成功返回 0，失败返回 -1
 */
int read_int_from_file(const char *filename, int *out_value)
{
    FILE *file = fopen(filename, "rb");
    if (file == NULL)
    {
        /* 首次运行无文件属正常，由调用方填默认值 */
        return -1;
    }

    size_t items_read = fread(out_value, sizeof(int), 1, file);
    fclose(file);

    if (items_read != 1)
    {
        fprintf(stderr, "Error: Failed to read data from file or file is incomplete.\n");
        return -1;
    }

    printf("Successfully read integer from file '%s': %d\n", filename, *out_value);
    return 0;
}