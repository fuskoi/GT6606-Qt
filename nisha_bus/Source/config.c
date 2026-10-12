#include "config.h"
#include "SL651_2014.h"
#include "Common.h"
#include "SocketLink.h"
#include "log.h"

// #include "pro_head.h"

char g_confkey[40];
char g_confvalue[40];
myhash_t *g_config_list = NULL; // hash结构空指针

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int ReadConfig(void)
{
    char buf[128];
    char confPath[MAX_PATH];
    char appDir[MAX_PATH];
    FILE *fp;

    memset(confPath, 0, sizeof(confPath));
    memset(appDir, 0, sizeof(appDir));
    if (!GetAppDir(appDir)) {
        return -1;
    }
    snprintf(confPath, sizeof(confPath), "%s/tyls.conf", appDir);
    fp = fopen(confPath, "rb");
    if (NULL == fp) {
        /* 兼容：仍尝试 cwd 相对路径 */
        fp = fopen("tyls.conf", "rb");
    }
    if (NULL == fp)
        return -1;
    while (NULL != (fgets(buf, 128, fp)))
    {
        ExtractKeyAndValue(buf);
        add_item(g_confkey, g_confvalue);
    }
    fclose(fp);

    {
        myhash_t *st = find_item("StationID");
        if (st != NULL && st->value[0] != '\0') {
            int len = 0;
            size_t vlen = strlen(st->value);
            if (vlen == 8) {
                len = str_to_bcd(st->value, g_RTUID + 1);
                memcpy(g_aStationID, st->value, len * 2);
            } else if (vlen == 10) {
                len = str_to_bcd(st->value, g_RTUID);
                memcpy(g_aStationID, st->value, len * 2);
            }
        }
    }

    g_stM2UpFramepara.center_addr = g_CenterStationID;
    memcpy(g_stM2UpFramepara.tele_addr, g_RTUID, 5);

    g_stM2UpFramepara.password = 0x1234;

    WebLink_LoadConfig();

    return 0;
}

int ExtractKeyAndValue(char *buf)
{
    if (buf[0] == '#' || buf[0] == '\n')
        return -1;
    sscanf(buf, "%[^=]=%s", g_confkey, g_confvalue);

    char *src = g_confkey;
    char *dst = g_confkey;

    while (*src != '\0')
    {
        if (*src != ' ')
        {
            *dst = *src;
            dst++;
        }
        src++;
    }
    *dst = '\0';

    return 0;
}

myhash_t *find_item(char *strkey)
{
    myhash_t *mh;
    HASH_FIND_STR(g_config_list, strkey, mh);
    return mh;
}

int Config_GetInt(const char *key, int default_val)
{
    myhash_t *item;

    if (key == NULL || key[0] == '\0')
        return default_val;
    item = find_item((char *)key);
    if (item == NULL || item->value[0] == '\0')
        return default_val;
    return atoi(item->value);
}

/* 判断一行是否为 key = ...（忽略前导空白与行内空格） */
static int conf_line_is_key(const char *line, const char *key)
{
    const char *p = line;
    const char *k = key;

    if (p == NULL || key == NULL)
        return 0;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '#' || *p == '\0' || *p == '\r' || *p == '\n')
        return 0;
    while (*k != '\0') {
        if (*p == ' ' || *p == '\t') {
            p++;
            continue;
        }
        if (*p != *k)
            return 0;
        p++;
        k++;
    }
    while (*p == ' ' || *p == '\t')
        p++;
    return (*p == '=');
}

static int Config_SaveKey(const char *key, const char *value)
{
    char appDir[MAX_PATH];
    char confPath[MAX_PATH];
    char tmpPath[MAX_PATH];
    char line[256];
    FILE *fp_in = NULL;
    FILE *fp_out = NULL;
    int found = 0;
    int ok = -1;

    if (key == NULL || key[0] == '\0' || value == NULL)
        return -1;
    if (!GetAppDir(appDir))
        return -1;
    snprintf(confPath, sizeof(confPath), "%s/tyls.conf", appDir);
    snprintf(tmpPath, sizeof(tmpPath), "%s/tyls.conf.tmp", appDir);

    fp_in = fopen(confPath, "rb");
    if (fp_in == NULL)
        fp_in = fopen("tyls.conf", "rb");
    if (fp_in == NULL)
        return -1;

    fp_out = fopen(tmpPath, "wb");
    if (fp_out == NULL) {
        fclose(fp_in);
        return -1;
    }

    while (fgets(line, sizeof(line), fp_in) != NULL) {
        if (!found && conf_line_is_key(line, key)) {
            fprintf(fp_out, "%s = %s\n", key, value);
            found = 1;
        } else {
            fputs(line, fp_out);
        }
    }
    if (!found)
        fprintf(fp_out, "\n%s = %s\n", key, value);

    fclose(fp_in);
    if (fclose(fp_out) != 0) {
        remove(tmpPath);
        return -1;
    }

    if (rename(tmpPath, confPath) != 0) {
        /* cwd 回退打开时，目标也可能是 ./tyls.conf */
        if (rename(tmpPath, "tyls.conf") != 0) {
            remove(tmpPath);
            return -1;
        }
    }
    ok = 0;
    return ok;
}

int Config_SetInt(const char *key, int value)
{
    char buf[32];

    if (key == NULL || key[0] == '\0')
        return -1;
    snprintf(buf, sizeof(buf), "%d", value);
    add_item((char *)key, buf);
    return Config_SaveKey(key, buf);
}

int Config_GetString(const char *key, char *out, size_t out_size, const char *default_val)
{
    myhash_t *item;
    const char *src;

    if (out == NULL || out_size == 0)
        return -1;
    out[0] = '\0';
    if (key == NULL || key[0] == '\0') {
        if (default_val != NULL)
            snprintf(out, out_size, "%s", default_val);
        return 0;
    }
    item = find_item((char *)key);
    src = (item != NULL && item->value[0] != '\0') ? item->value
          : (default_val != NULL ? default_val : "");
    snprintf(out, out_size, "%s", src);
    return 0;
}

int Config_SetString(const char *key, const char *value)
{
    if (key == NULL || key[0] == '\0' || value == NULL)
        return -1;
    add_item((char *)key, (char *)value);
    return Config_SaveKey(key, value);
}

double Config_GetDouble(const char *key, double default_val)
{
    char buf[40];
    char *end = NULL;
    double v;

    if (Config_GetString(key, buf, sizeof(buf), "") != 0 || buf[0] == '\0')
        return default_val;
    v = strtod(buf, &end);
    if (end == buf)
        return default_val;
    return v;
}

int Config_SetDouble(const char *key, double value)
{
    char buf[40];

    if (key == NULL || key[0] == '\0')
        return -1;
    snprintf(buf, sizeof(buf), "%.3f", value);
    return Config_SetString(key, buf);
}

int Config_GetMaxWaterDepthMm(void)
{
    double m = Config_GetDouble("MaxWaterDepth", 8.0);
    if (m < 0.5)
        m = 0.5;
    if (m > 50.0)
        m = 50.0;
    return (int)(m * 1000.0 + 0.5);
}

int Config_GetMinWaterDepthMm(void)
{
    double m = Config_GetDouble("MinWaterDepth", 0.1);
    int maxMm = Config_GetMaxWaterDepthMm();
    int mm;

    if (m < 0.05)
        m = 0.05;
    mm = (int)(m * 1000.0 + 0.5);
    if (mm < 50)
        mm = 50;
    if (mm >= maxMm)
        mm = maxMm / 10;
    if (mm < 50)
        mm = 50;
    return mm;
}

void add_item(char *strkey, char *strvalue)
{
    myhash_t *mh;
    HASH_FIND_STR(g_config_list, strkey, mh); /* 插入前先查看key值是否已经在hash表config_list里面 */
    if (NULL == mh)                           /* 如果key不存在 */
    {
        mh = (myhash_t *)malloc(sizeof(myhash_t));
        strncpy(mh->key, strkey, MAX_KEY_LEN - 1);
        mh->key[MAX_KEY_LEN - 1] = '\0';
        strncpy(mh->value, strvalue, MAX_VALUE_LEN - 1);
        mh->value[MAX_VALUE_LEN - 1] = '\0';
        HASH_ADD_STR(g_config_list, key, mh); /* 添加到hash表中的字符串必须要用\0结尾 */
    }
    else /* 如果key存在 */
    {
        strncpy(mh->value, strvalue, MAX_VALUE_LEN - 1);
        mh->value[MAX_VALUE_LEN - 1] = '\0';
    }
}
/* 需要指定要删除的节点的结构的指针，如果没有需要用HASH_FIND先得到要删除节点的结构的指针 */
void delete_item(myhash_t *item)
{
    HASH_DEL(g_config_list, item); /* user: pointer to deletee */
    free(item);                    /* optional; it's up to you! */
}
/* 删除所有节点 */
void delete_all(void)
{
    myhash_t *current_user, *tmp;

    HASH_ITER(hh, g_config_list, current_user, tmp)
    {
        HASH_DEL(g_config_list, current_user); /* delete; users advances to next */
        free(current_user);                    /* optional- if you want to free  */
    }
}

void modify_line(char *filename, int line_num, char *new_line)
{
    int i = 0;
    FILE *fp = fopen(filename, "r");
    if (fp == NULL)
    {
        printf("无法打开文件\n");
        return;
    }
    // 计算文件中的行数
    int count = 0;
    char ch;
    while ((ch = fgetc(fp)) != '@')
    {
        putchar(ch);
        if (ch == '\n')
        {
            count++;
        }
    }
    rewind(fp);
    // 将文件内容读入内存
    char **lines = (char **)malloc(sizeof(char *) * (count + 1));
    for (i = 0; i <= count; i++)
    {
        lines[i] = (char *)malloc(sizeof(char) * 1024);
        fgets(lines[i], 1024, fp);
    }
    fclose(fp);

    // 修改指定行
    strncpy(lines[line_num - 1], new_line, 1023);
    lines[line_num - 1][1023] = '\0'; // 确保字符串以空字符结束

    // 将修改后的内容写回文件
    fp = fopen(filename, "w");
    for (i = 0; i <= count; i++)
    {
        fputs(lines[i], fp);
    }
    fclose(fp);

    // 释放内存
    for (i = 0; i <= count; i++)
    {
        free(lines[i]);
    }
    free(lines);

    return;
}
