#ifndef __DATAPROC_H__
#define __DATAPROC_H__
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#define __IO volatile
#define FLOAT_EPSILON 1e-6
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;

typedef enum
{
    STABLE,
    INCREASING,
    DECREASING
} Trend;

// 定义数据类型枚举
typedef enum
{
    INTEGER,
    FLOAT,
    UINT32,

} DataType;


/**
 * @brief 	 【简介】int类型数据转换为BCD编码，保存到数组中以16进制数来显示，低位数据在右、高位数据在左(例：123 -> 0x0123)
 * @param 	 value [in/out],  想要转换的int类型的数据
 * @param 	 bcdArray [in/out],  用来保存转换后的数据的数组
 * @param 	 len [in/out],  数组长度
 */
void IntToBCD(int value, BYTE *bcdArray, int len);
/**
 * @brief 	 【简介】将一个字节的高5位用来保存第一个参数，低3位保存第二个参数，生成标识符中的数据定义字节
 * @param 	 numberOfBytes [in/out],  不包含小数点数据所占的字节数
 * @param 	 decimalDigits [in/out],  小数点后数据的位数
 * @return 	 【解释返回值】 一个字节被处理后的结果
 */
BYTE DataDefinition(BYTE numberOfBytes, BYTE decimalDigits);
/**
 * @brief 	 【简介】把浮点数保留一位小数放大为一个整数
 * @param 	 num [in/out],  想要放大的浮点数
 * @return 	 【解释返回值】 放大后的结果
 */
int ConvertDecimalToInteger(double num);
/**
 * @brief 	 【简介】将规定格式的字符串类型的时间转换成BCD编码的时间，保存到数组中以16进制数来显示
 * @param 	 name_buf [in/out],  字符串格式的时间
 * @param 	 bcd_buf [in/out],  保存转换成BCD编码的时间
 */
void String2BcdTime(char *name_buf, BYTE *bcd_buf);
/**
 * @brief 	 【简介】设置报文上下行标识以及报文正文的长度，2字节的高四位是上下行标识，其他12位标识正文的长度
 * @param 	 result [in/out],  保存设置好的上下行标识以及正文长度
 * @param 	 flag [in/out],  选择上下行标识
 * @param 	 length [in/out],  正文的长度
 * @return 	 【解释返回值】 成功返回0 失败返回-1
 */
int SetPacketTypeAndLength(BYTE *result, int flag, int length);
/**
 * @brief 	 【简介】将arr2拼接到arr1下标为index的位置上
 * @param 	 arr1 [in/out],  目的数组
 * @param 	 size1 [in/out],  目的数组的长度
 * @param 	 arr2 [in/out],  源数组
 * @param 	 size2 [in/out],  源数组的长度
 * @param 	 index [in/out],  拼接到目的数组的位置
 * @return 	 【解释返回值】 成功返回新数组的首地址，失败返回NULL
 */
BYTE *AppendArrays(BYTE arr1[], int size1, BYTE arr2[], int size2, int index);
/**
 * @brief 	 【简介】设置步长 6字节 BCD编码，其中一个参数不为0时，其他两个参数必须是0
 * @param 	 a [in/out],  天
 * @param 	 b [in/out],  小时
 * @param 	 c [in/out],  分钟
 * @param 	 dhm [in/out],  保存设置好的BCD编码
 * @return 	 【解释返回值】
 */
int TimeIncrement(BYTE a, BYTE b, BYTE c, BYTE *dhm);

/**
 * @brief 	 【简介】把16位的无符号整数保存到8位无符号整数数组中，大端模式
 * @param 	 num [in], 要保存到数组中的数
 * @param 	 arr [out], 数组
 */
void uint16_to_hex_array(uint16_t num, uint8_t *arr);
/**
 * @brief 	 【简介】把字符串转换为16进制数保存到数组中
 * @param 	 str [in], 源字符串
 * @param 	 arr [out], 目标数组
 * @return 	 【解释返回值】 成功返回0 失败返回-1
 */
int string_to_hex_array(const char *str, uint8_t *arr);
/**
 * @brief 	 【简介】将一个十六进制字符串转换为一个长整型数
 * @param 	 hex_str [in], 源字符串
 * @return 	 【解释返回值】返回转换成功的数字 失败返回-1
 */
long strhex_to_dec(const char *hex_str);

void write_data(const char *filename, char *data);

char *float_array_to_string(const float *array, size_t length);

int write_int_to_file(const char *filename, int value);

int read_int_from_file(const char *filename, int *out_value);

Trend analyzeTrend(void *arr, int size, DataType dataType);



#endif