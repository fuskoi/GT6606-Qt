#include "watch_dog.h"

int g_dog = 0;
int g_fdwd = -1;
int InitWatchDog(void)
{
    int fdw;
    if ((fdw = open("/dev/watchdog", O_RDWR)) == -1)
    {
        perror("watch dog");
        return -1;
    }
    printf("enable wdt!\n");
    return fdw;
}
int SetWatchDogOverTime(int fdw, int sec)
{
    int wdt_val;
    int ret;
    switch (sec)
    {
    case 1:
        wdt_val = 1;
        break;
    case 3:
        wdt_val = 3;
        break;
    case 14:
        wdt_val = 14;
        break;
    default:
        wdt_val = 14;
        break;
    }
    ret = ioctl(fdw, WDIOC_SETTIMEOUT, &wdt_val);
    if (ret < 0)
        return -1;
    return 0;
}
int FeedWatchDog(int fdw)
{
    char buf = '1';
    int ret;
    ret = write(fdw, &buf, 1);
    if (ret < 0)
        return -1;
    return 0;
}
int CloseWatchDog(int fdw)
{
    char buf = 'V';
    int ret;
    ret = write(fdw, &buf, 1);
    if (ret < 0)
        return -1;
    printf("disable watchdog!\n");
    return 0;
}

int WatchDogControl(void)
{
    if (g_dog == 0)
    {
        // 关闭看门狗
        if (g_fdwd != -1)
        {
            CloseWatchDog(g_fdwd);
            g_fdwd = -1;
        }
    }
    else if (g_dog == 1)
    {
        // 打开看门狗
        if (g_fdwd == -1)
        {
            g_fdwd = InitWatchDog();
            if (g_fdwd == -1)
            {
                return -1; // 初始化失败
            }
            // 设置看门狗超时时间，这里假设设置为14秒
            if (SetWatchDogOverTime(g_fdwd, 14) == -1)
            {
                CloseWatchDog(g_fdwd);
                g_fdwd = -1;
                return -1; // 设置超时时间失败
            }
        }
        // 喂狗
        if (FeedWatchDog(g_fdwd) == -1)
        {
            CloseWatchDog(g_fdwd);
            g_fdwd = -1;
            return -1; // 喂狗失败
        }
    }
    return 0;
}