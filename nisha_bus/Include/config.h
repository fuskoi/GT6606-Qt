#ifndef __CONFIG_H__
#define __CONFIG_H__
// #include "src_uthash.h"
#include <stdio.h>
#include <strings.h>

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
#endif // __CONFIG_H__