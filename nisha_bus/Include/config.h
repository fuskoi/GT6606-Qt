#ifndef __CONFIG_H__
#define __CONFIG_H__
// #include "src_uthash.h"
#include <stdio.h>
#include <strings.h>
#include <stddef.h>

#include "src_uthash.h"
#define MAX_KEY_LEN 40
#define MAX_VALUE_LEN 40
typedef struct
{
    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
    UT_hash_handle hh; /* makes this structure hashable */
} myhash_t;

extern char g_confkey[40];
extern char g_confvalue[40];
extern myhash_t *g_config_list;

/**
 * @brief 	 【简介】读取配置文件并把读取出来的数据按键值对的形式保存到hash表中
 * @return 	 【解释返回值】 成功返回0 失败返回-1
 */
int ReadConfig(void);
/**
 * @brief 	 【简介】从buf中提取key和value
 * @param 	 buf [in], 配置文件中的一行数据
 * @return 	 【解释返回值】成功返回0 失败返回-1
 */
int ExtractKeyAndValue(char *buf);
/**
 * @brief 	 【简介】输入key在hash表中查找value
 * @param 	 strkey [in], 目标key - 字符串
 * @return 	 【解释返回值】查找成功返回节点的地址 查找失败返回NULL
 */
myhash_t *find_item(char *strkey);
/**
 * @brief 	 【简介】添加元素到hash表中
 * @param 	 strkey [in], 想要添加到表中的key
 * @param 	 strvalue [in], 想要添加到表中的value
 */
void add_item(char *strkey, char *strvalue);
/**
 * @brief 	 【简介】删除hash表中的一个元素(节点)，需要先查找出要删除的节点的地址
 * @param 	 item [in], 要删除的节点的地址
 */
void delete_item(myhash_t *item);
/**
 * @brief 	 【简介】删除整个hash表
 */
void delete_all(void);
/**
 * @brief 	 【简介】
 * @param 	 filename [in/out], 参数意义
 * @param 	 line_num [in/out], 参数意义
 * @param 	 new_line [in/out], 参数意义
 */
void modify_line(char *filename, int line_num, char *new_line);

/**
 * 读整型配置；键不存在或空则返回 default_val。
 * 布尔开关可用：Config_GetInt("EnableManualLift", 1) != 0
 */
int Config_GetInt(const char *key, int default_val);

/**
 * 写整型配置：更新内存表并落盘 tyls.conf（改已有行或追加）。
 * 成功返回 0，失败返回 -1。
 */
int Config_SetInt(const char *key, int value);

/**
 * 读字符串配置；键不存在则写入 default_val 到 out（可为空串）。
 * out_size 含结尾 0；成功返回 0。
 */
int Config_GetString(const char *key, char *out, size_t out_size, const char *default_val);

/**
 * 写字符串配置：更新内存表并落盘 tyls.conf。
 * 成功返回 0，失败返回 -1。
 */
int Config_SetString(const char *key, const char *value);

/** 读浮点配置；键不存在或无法解析则返回 default_val。 */
double Config_GetDouble(const char *key, double default_val);

/** 写浮点配置（保留 3 位小数）并落盘。成功 0，失败 -1。 */
int Config_SetDouble(const char *key, double value);

/**
 * 最大水深（毫米），来自 MaxWaterDepth（米），默认 8.0 m。
 * 夹紧到 [0.5 m, 50 m]。
 */
int Config_GetMaxWaterDepthMm(void);

/**
 * 最小水深（毫米），来自 MinWaterDepth（米），默认 0.1 m。
 * 用于定深下限与手动上升软停；保证小于最大水深。
 */
int Config_GetMinWaterDepthMm(void);

#endif // __CONFIG_H__