#include "Common.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

/* 运行在 ota/slot_x 下时，业务根仍是安装目录（如 /sediment） */
static void strip_ota_slot_suffix(char *dir)
{
    char *p;

    if (dir == NULL || dir[0] == '\0') {
        return;
    }
    p = strstr(dir, "/ota/slot_");
    if (p == NULL) {
        return;
    }
    *p = '\0';
    if (dir[0] == '\0') {
        dir[0] = '/';
        dir[1] = '\0';
    }
}

bool GetAppDir(char *appDir)
{
    char path[MAX_PATH];
    char *slash;
    ssize_t n;

    if (appDir == NULL) {
        return false;
    }
    memset(appDir, 0, MAX_PATH);
    memset(path, 0, sizeof(path));

    /* 1) 优先可执行文件目录（与 cwd 无关） */
    n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0) {
        path[n] = '\0';
        slash = strrchr(path, '/');
        if (slash != NULL && slash != path) {
            *slash = '\0';
            strip_ota_slot_suffix(path);
            strncpy(appDir, path, MAX_PATH - 1);
            return true;
        }
    }

    /* 2) 回退 cwd（排除盘根 /） */
    memset(path, 0, sizeof(path));
    if (getcwd(path, sizeof(path)) && path[0] != '\0' &&
        !(path[0] == '/' && path[1] == '\0')) {
        strip_ota_slot_suffix(path);
        strncpy(appDir, path, MAX_PATH - 1);
        return true;
    }

    /* 3) 最后才用盘根 */
    appDir[0] = '/';
    appDir[1] = '\0';
    return true;
}
