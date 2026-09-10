#ifndef __WATCH_DOG_H__
#define __WATCH_DOG_H__
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <unistd.h>

#define REBOOTTIME 14 // 看门狗超时时间默认为14秒，可设为1秒，3秒和14秒
#define SET_WDT_TIMEOUT   \
    wdt_val = REBOOTTIME; \
    ioctl(fdw, WDIOC_SETTIMEOUT, &wdt_val) // 设置看门狗超时时间
#define FEED_WATCHDOG \
    buf = '1';        \
    write(fdw, &buf, 1) // 喂狗
#define CLOSE_WDT        \
    buf = 'V';           \
    write(fdw, &buf, 1); \
    close(fdw) // 关闭看门狗
extern int g_dog;
extern int g_fdwd;
/**
 * @brief 	 【简介】初始化看门狗
 * @return 	 【解释返回值】 成功返回看门狗的文件描述符 失败返回-1
 */
int InitWatchDog(void);
/**
 * @brief 	 【简介】设置看门狗超时的时间，不设置就是默认14秒超时，可以设置1秒 3秒 14秒超时
 * @param 	 fdw [in/out], 看门狗的文件描述符
 * @param 	 sec [in/out], 超时的时间
 * @return 	 【解释返回值】 成功返回0 失败返回-1
 */
int SetWatchDogOverTime(int fdw, int sec);
/**
 * @brief 	 【简介】喂狗
 * @param 	 fdw [in/out], 看门狗的文件描述符
 * @return 	 【解释返回值】 成功返回0 失败返回-1
 */
int FeedWatchDog(int fdw);
/**
 * @brief 	 【简介】关闭看门狗
 * @param 	 fdw [in/out], 看门狗的文件描述符
 * @return 	 【解释返回值】 成功返回0 失败返回-1
 */
int CloseWatchDog(int fdw);
int WatchDogControl(void);
#endif // !__WATCH_DOG_H__