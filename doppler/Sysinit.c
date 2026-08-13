#include "Common.h"
#include "TaskList.h"
#include "log.h"
#include "WebSeriver.h"
#include <ctype.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>

#define WATCHDOG_DEV "/dev/watchdog"

DEVICE_LIST g_stDevicelist;
DEVICE_ADDR g_stDeviceAddr;
MEASURE_CONFIG g_stMeasureConfig;
BYTE g_aStationID[12] = {0};
BYTE g_RTUID[5] = {0};
BYTE g_CenterStationID = 0;
uint16_t g_u16Password = 0x1234;
pthread_mutex_t g_lock[MAX_UART_NUM];
int g_WatchDogFd = 0;
/* GT6606: default off — opening /dev/watchdog arms it; closing without 'V' reboots. */
static int s_watchdogEnable = 0;

/* 运行在 ota/slot_x 下时，业务根仍是安装目录（如 /doppler） */
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

bool GetAppDir(char* appDir)
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

void TrimLeft(char* pString)
{
    int nLength = (int)strlen(pString);
    int i = 0;
    for (i = 0; i < nLength; i++) {
        if (pString[i] == ' ') continue;
        break;
    }
    if (i > 0) {
        memmove(pString, pString + i, (size_t)(nLength - i + 1));
    }
}

void TrimRight(char* pString)
{
    int nLength = (int)strlen(pString);
    int i = 0;
    for (i = nLength - 1; i >= 0; i--) {
        if (pString[i] == ' ' || pString[i] == '\r' || pString[i] == '\n') {
            pString[i] = 0;
        } else {
            break;
        }
    }
}

void TrimStr(char* pStr)
{
    TrimRight(pStr);
    TrimLeft(pStr);
}

int str_to_bcd(const char* str, uint8_t* buffer)
{
    size_t len = strlen(str);
    size_t i = 0;

    if (len % 2 != 0) {
        return ERROR;
    }
    for (i = 0; i < len / 2; i++) {
        char high = str[i * 2];
        char low = str[i * 2 + 1];
        uint8_t highVal = (uint8_t)((high >= '0' && high <= '9') ? (high - '0') : (10 + toupper(high) - 'A'));
        uint8_t lowVal = (uint8_t)((low >= '0' && low <= '9') ? (low - '0') : (10 + toupper(low) - 'A'));
        buffer[i] = (uint8_t)((highVal << 4) | lowVal);
    }
    return (int)(len / 2);
}

typedef struct ConfigParseOut {
    DEVICE_LIST stDev;
    DEVICE_ADDR stAddr;
    MEASURE_CONFIG stMeas;
    BYTE aStationID[12];
    BYTE aRTUID[5];
    BYTE u8CenterId;
    uint16_t u16Password;
    int iWatchdogEnable;
} ConfigParseOut;

static void config_init_defaults(ConfigParseOut *o)
{
    int i = 0;

    memset(o, 0, sizeof(*o));
    o->stMeas.u32CollectIntervalSec = 10;
    o->stMeas.u32ReportIntervalSec = 60;
    o->stMeas.u8Sl651Enable = 1;
    o->stMeas.u8WebReportEnable = 1;
    o->stMeas.u8SensorSimulate = 0;
    o->stMeas.u32SensorTimeoutSec = 5;
    o->stMeas.u8WebLinkMode = WEB_LINK_MODE_DTU;
    strncpy(o->stMeas.szSocketServerIP, "192.168.101.180",
            sizeof(o->stMeas.szSocketServerIP) - 1U);
    o->stMeas.u16SocketServerPort = 8911;
    o->stDev.HOST_COMPUTER = -1;
    o->stDev.Sensor = -1;
    o->stDev.Dam5213 = -1;
    o->u16Password = 0x1234;
    o->iWatchdogEnable = 0;
    for (i = 0; i < MAX_UART_NUM; i++) {
        o->stDev.UartList[i].uartpara.Baudrate = 9600;
        snprintf(o->stDev.UartList[i].uartpara.hName, PORT_NAME_LENGTH,
                 "/dev/ttyAMA%d", i);
    }
}

/* com1based: conf 里 1..MAX；越界则忽略并打日志 */
static void config_set_uart_role(DEVICE_LIST *dev, int *pSlot, int com1based,
                                 enum UartType type, const char *name)
{
    int idx = com1based - 1;

    if (com1based < 1 || com1based > MAX_UART_NUM) {
        LOG(LOG_ERROR, "%s invalid COM%d (need 1..%d), ignored",
            name, com1based, MAX_UART_NUM);
        return;
    }
    *pSlot = idx;
    dev->UartList[idx].staticUartType = type;
    LOG(LOG_INFO, "%s uart COM%d", name, com1based);
}

/*
 * 解析 doppler.conf 到 out，不碰运行中的全局串口 fd。
 * 成功返回 true（Sensor 端口必须有效）。
 */
static bool config_parse_file(ConfigParseOut *out)
{
    char configFileName[256];
    char tempBuffer[1024];
    char keyStr[1024];
    char valueStr[1024];

    if (out == NULL) {
        return false;
    }
    config_init_defaults(out);

    memset(configFileName, 0, sizeof(configFileName));
    if (!GetAppDir(configFileName)) {
        return false;
    }
    strcat(configFileName, CONFIG_FILE_PATH);

    {
        FILE *pFile = fopen(configFileName, "rb");
        if (pFile == NULL) {
            LOG(LOG_ERROR, "config file open error");
            return false;
        }

        while (fgets(tempBuffer, (int)sizeof(tempBuffer), pFile) != NULL) {
            char *pValue = strchr(tempBuffer, '=');
            if (pValue == NULL) {
                continue;
            }

            memset(keyStr, 0, sizeof(keyStr));
            memset(valueStr, 0, sizeof(valueStr));
            memcpy(keyStr, tempBuffer, (size_t)(pValue - tempBuffer));
            strncpy(valueStr, pValue + 1, sizeof(valueStr) - 1U);
            TrimStr(keyStr);
            TrimStr(valueStr);
            if (keyStr[0] == '#' || keyStr[0] == '\0' || valueStr[0] == '\0') {
                continue;
            }

            if (strcasecmp(keyStr, "HOST_COMPUTER") == 0) {
                config_set_uart_role(&out->stDev, &out->stDev.HOST_COMPUTER,
                                     atoi(valueStr), DTU_TYPE, "HOST_COMPUTER");
            } else if (strcasecmp(keyStr, "Sensor") == 0) {
                config_set_uart_role(&out->stDev, &out->stDev.Sensor,
                                     atoi(valueStr), SENSOR_TYPE, "Sensor");
            } else if (strcasecmp(keyStr, "Dam5213") == 0) {
                config_set_uart_role(&out->stDev, &out->stDev.Dam5213,
                                     atoi(valueStr), DAM5213_TYPE, "Dam5213");
            } else if (strcasecmp(keyStr, "FlowMeter_Addr") == 0) {
                out->stAddr.u8FlowMeter = (uint8_t)atoi(valueStr);
            } else if (strcasecmp(keyStr, "FlowMeter_Addr2") == 0) {
                out->stAddr.u8FlowMeter2 = (uint8_t)atoi(valueStr);
            } else if (strcasecmp(keyStr, "Dam5213_Addr") == 0) {
                out->stAddr.u8Dam5213 = (uint8_t)atoi(valueStr);
            } else if (strcasecmp(keyStr, "CenterStationID") == 0) {
                out->u8CenterId = (BYTE)atoi(valueStr);
            } else if (strcasecmp(keyStr, "Password") == 0) {
                out->u16Password = (uint16_t)atoi(valueStr);
            } else if (strcasecmp(keyStr, "CollectInterval") == 0) {
                out->stMeas.u32CollectIntervalSec = (uint32_t)atoi(valueStr);
            } else if (strcasecmp(keyStr, "ReportInterval") == 0) {
                out->stMeas.u32ReportIntervalSec = (uint32_t)atoi(valueStr);
            } else if (strcasecmp(keyStr, "SensorSimulate") == 0) {
                out->stMeas.u8SensorSimulate = (uint8_t)atoi(valueStr);
            } else if (strcasecmp(keyStr, "SensorTimeoutSec") == 0) {
                out->stMeas.u32SensorTimeoutSec = (uint32_t)atoi(valueStr);
            } else if (strcasecmp(keyStr, "WebLinkMode") == 0) {
                out->stMeas.u8WebLinkMode =
                    (atoi(valueStr) != 0) ? WEB_LINK_MODE_SOCKET : WEB_LINK_MODE_DTU;
            } else if (strcasecmp(keyStr, "SocketLogTestEnable") == 0) {
                if (atoi(valueStr) != 0) {
                    out->stMeas.u8WebLinkMode = WEB_LINK_MODE_SOCKET;
                }
            } else if (strcasecmp(keyStr, "SocketServerIP") == 0) {
                memset(out->stMeas.szSocketServerIP, 0,
                       sizeof(out->stMeas.szSocketServerIP));
                strncpy(out->stMeas.szSocketServerIP, valueStr,
                        sizeof(out->stMeas.szSocketServerIP) - 1U);
            } else if (strcasecmp(keyStr, "SocketServerPort") == 0) {
                out->stMeas.u16SocketServerPort = (uint16_t)atoi(valueStr);
            } else if (strcasecmp(keyStr, "StationID") == 0) {
                strncpy((char *)out->aStationID, valueStr, sizeof(out->aStationID) - 1);
                memset(out->aRTUID, 0, sizeof(out->aRTUID));
                str_to_bcd(valueStr, out->aRTUID + 1);
            } else if (strcasecmp(keyStr, "baudrate1") == 0) {
                out->stDev.UartList[0].uartpara.Baudrate = atoi(valueStr);
            } else if (strcasecmp(keyStr, "baudrate2") == 0) {
                out->stDev.UartList[1].uartpara.Baudrate = atoi(valueStr);
            } else if (strcasecmp(keyStr, "baudrate3") == 0) {
                out->stDev.UartList[2].uartpara.Baudrate = atoi(valueStr);
            } else if (strcasecmp(keyStr, "baudrate4") == 0) {
                out->stDev.UartList[3].uartpara.Baudrate = atoi(valueStr);
            } else if (strcasecmp(keyStr, "baudrate5") == 0) {
                out->stDev.UartList[4].uartpara.Baudrate = atoi(valueStr);
            } else if (strcasecmp(keyStr, "baudrate6") == 0) {
                out->stDev.UartList[5].uartpara.Baudrate = atoi(valueStr);
            } else if (strncasecmp(keyStr, "UartDev", 7) == 0 && keyStr[7] >= '1' &&
                       keyStr[7] <= '6' && keyStr[8] == '\0') {
                int port = keyStr[7] - '1';
                memset(out->stDev.UartList[port].uartpara.hName, 0, PORT_NAME_LENGTH);
                strncpy(out->stDev.UartList[port].uartpara.hName, valueStr,
                        PORT_NAME_LENGTH - 1);
                LOG(LOG_INFO, "COM%d device override: %s", port + 1,
                    out->stDev.UartList[port].uartpara.hName);
            } else if (strcasecmp(keyStr, "WatchdogEnable") == 0) {
                out->iWatchdogEnable = (atoi(valueStr) != 0) ? 1 : 0;
            }
        }

        fclose(pFile);
    }

    if (out->stDev.Sensor < 0 || out->stDev.Sensor >= MAX_UART_NUM) {
        LOG(LOG_ERROR, "Sensor COM not configured");
        return false;
    }
    if (out->stAddr.u8FlowMeter == 0) {
        out->stAddr.u8FlowMeter = 1;
    }
    return true;
}

bool ReadConfig(void)
{
    ConfigParseOut o;

    if (!config_parse_file(&o)) {
        return false;
    }

    memcpy(&g_stDevicelist, &o.stDev, sizeof(g_stDevicelist));
    memcpy(&g_stDeviceAddr, &o.stAddr, sizeof(g_stDeviceAddr));
    memcpy(&g_stMeasureConfig, &o.stMeas, sizeof(g_stMeasureConfig));
    memcpy(g_aStationID, o.aStationID, sizeof(g_aStationID));
    memcpy(g_RTUID, o.aRTUID, sizeof(g_RTUID));
    g_CenterStationID = o.u8CenterId;
    g_u16Password = o.u16Password;
    s_watchdogEnable = o.iWatchdogEnable;
    return true;
}

/*
 * 热加载：解析到临时结构再写回业务字段，绝不 memset 运行中的 UartList（否则采集线程会踩空 fd 段错误）。
 * 串口角色(HOST/Sensor/Dam)变更仅告警，继续用当前端口直到重启。
 */
bool ReloadConfigKeepUart(void)
{
    ConfigParseOut o;
    int i = 0;

    if (!config_parse_file(&o)) {
        LOG(LOG_WARNING, "ReloadConfigKeepUart: parse failed, keep running config");
        return false;
    }

    if (o.stDev.HOST_COMPUTER != g_stDevicelist.HOST_COMPUTER ||
        o.stDev.Sensor != g_stDevicelist.Sensor ||
        o.stDev.Dam5213 != g_stDevicelist.Dam5213) {
        LOG(LOG_WARNING,
            "ReloadConfigKeepUart: uart port in conf changed host %d->%d sensor %d->%d dam %d->%d, keep running until reboot",
            g_stDevicelist.HOST_COMPUTER + 1, o.stDev.HOST_COMPUTER + 1,
            g_stDevicelist.Sensor + 1, o.stDev.Sensor + 1,
            g_stDevicelist.Dam5213 + 1, o.stDev.Dam5213 + 1);
    }

    memcpy(&g_stMeasureConfig, &o.stMeas, sizeof(g_stMeasureConfig));
    memcpy(&g_stDeviceAddr, &o.stAddr, sizeof(g_stDeviceAddr));
    memcpy(g_aStationID, o.aStationID, sizeof(g_aStationID));
    memcpy(g_RTUID, o.aRTUID, sizeof(g_RTUID));
    g_CenterStationID = o.u8CenterId;
    g_u16Password = o.u16Password;
    s_watchdogEnable = o.iWatchdogEnable;

    /* 只更新名称/波特率，保留 fd、ringfifo、收发缓冲 */
    for (i = 0; i < MAX_UART_NUM; i++) {
        g_stDevicelist.UartList[i].uartpara.Baudrate =
            o.stDev.UartList[i].uartpara.Baudrate;
        memcpy(g_stDevicelist.UartList[i].uartpara.hName,
               o.stDev.UartList[i].uartpara.hName, PORT_NAME_LENGTH);
    }

    LOG(LOG_DEBUG, "ReloadConfigKeepUart: applied (uart runtime kept)");
    return true;
}

int OpenUart(uint8_t u8PortSeri, int baudrate)
{
    const char *devName;
    int fd;

    (void)baudrate;
    if (u8PortSeri >= MAX_UART_NUM) {
        return ERROR;
    }
    devName = g_stDevicelist.UartList[u8PortSeri].uartpara.hName;
    if (devName == NULL || devName[0] == '\0') {
        LOG(LOG_ERROR, "open uart %d failed: empty device name", u8PortSeri + 1);
        return ERROR;
    }
    fd = open(devName, O_RDWR | O_NOCTTY);
    if (fd == -1) {
        LOG(LOG_ERROR, "open uart %d (%s) failed: %s",
            u8PortSeri + 1, devName, strerror(errno));
        return ERROR;
    }
    g_stDevicelist.UartList[u8PortSeri].i32fd = fd;
    LOG(LOG_INFO, "COM%d opened %s fd=%d", u8PortSeri + 1, devName, fd);
    return fd;
}

int SetUartProperties(int32_t fd, uint32_t baudrate)
{
    speed_t setSpeed = B9600;
    struct termios option;

    memset(&option, 0, sizeof(option));
    tcgetattr(fd, &option);
    switch (baudrate) {
    case 1200: setSpeed = B1200; break;
    case 2400: setSpeed = B2400; break;
    case 4800: setSpeed = B4800; break;
    case 9600: setSpeed = B9600; break;
    case 19200: setSpeed = B19200; break;
    case 38400: setSpeed = B38400; break;
    case 57600: setSpeed = B57600; break;
    case 115200: setSpeed = B115200; break;
    default: setSpeed = B9600; break;
    }
    cfsetispeed(&option, setSpeed);
    cfsetospeed(&option, setSpeed);
    /* 二进制透传：清掉可能改字节的输入处理（旧代码只关了 ICRNL|IXON） */
    option.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                        ICRNL | IXON | IXOFF | IXANY);
    option.c_oflag &= ~OPOST;
    option.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG | IEXTEN);
    option.c_cflag |= (CLOCAL | CREAD);
    option.c_cflag &= ~CRTSCTS;
    option.c_cflag &= ~PARENB;
    option.c_cflag &= ~CSTOPB;
    option.c_cflag &= ~CSIZE;
    option.c_cflag |= CS8;
    /* 非阻塞轮询（配合 uartRecvFun 的 usleep）；VTIME=1 会拖住口锁 */
    option.c_cc[VTIME] = 0;
    option.c_cc[VMIN] = 0;
    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &option) < 0) {
        return ERROR;
    }
    return SUCCESS;
}

static int open_used_uart(int portIndex)
{
    int baudrate = 0;
    int fd = 0;

    if (portIndex < 0 || portIndex >= MAX_UART_NUM) {
        return SUCCESS;
    }
    baudrate = g_stDevicelist.UartList[portIndex].uartpara.Baudrate;
    fd = OpenUart((uint8_t)portIndex, baudrate);
    if (fd == ERROR) {
        return ERROR;
    }
    SetUartProperties(fd, (uint32_t)baudrate);
    g_stDevicelist.UartList[portIndex].ringfifo = kfifo_init(RINGFIFO_SIZE);
    if (g_stDevicelist.UartList[portIndex].ringfifo == NULL) {
        return ERROR;
    }
    LOG(LOG_INFO, "COM%d open success, fd=%d, baud=%d", portIndex + 1, fd, baudrate);
    return SUCCESS;
}

int InitAllUart(void)
{
    if (!g_stMeasureConfig.u8SensorSimulate) {
        if (open_used_uart(g_stDevicelist.Sensor) != SUCCESS) {
            return ERROR;
        }
    } else {
        LOG(LOG_INFO, "SensorSimulate=1, skip sensor COM open");
    }
    if (g_stDevicelist.HOST_COMPUTER >= 0 &&
        g_stDevicelist.HOST_COMPUTER < MAX_UART_NUM &&
        g_stDevicelist.HOST_COMPUTER != g_stDevicelist.Sensor) {
        if (open_used_uart(g_stDevicelist.HOST_COMPUTER) != SUCCESS) {
            LOG(LOG_WARNING, "HOST_COMPUTER open failed, uplink uart unavailable");
            g_stMeasureConfig.u8Sl651Enable = 0;
        }
    }
    if (g_stDevicelist.Dam5213 >= 0 &&
        g_stDevicelist.Dam5213 < MAX_UART_NUM &&
        g_stDevicelist.Dam5213 != g_stDevicelist.Sensor &&
        g_stDevicelist.Dam5213 != g_stDevicelist.HOST_COMPUTER) {
        if (open_used_uart(g_stDevicelist.Dam5213) != SUCCESS) {
            LOG(LOG_WARNING, "Dam5213 COM open failed, voltage unavailable");
            g_stDevicelist.Dam5213 = -1;
        }
    }
    startUartReceiveThread(&g_stDevicelist);
    return SUCCESS;
}

int InitUartTask(void)
{
    int i = 0;
    for (i = 0; i < MAX_UART_NUM; i++) {
        if (g_stDevicelist.UartList[i].i32fd <= 0) {
            continue;
        }
        struct TaskItem* pTask = (struct TaskItem*)malloc(sizeof(struct TaskItem) * MAX_ITEM_NUM);
        g_stDevicelist.UartList[i].taskManager.mutex = &g_lock[i];
        InitListManager(MAX_ITEM_NUM, sizeof(struct TaskItem), pTask, &g_stDevicelist.UartList[i].taskManager);
        StartDealTaskThread(&g_stDevicelist.UartList[i]);
    }
    return SUCCESS;
}

int InitDevice(void)
{
    int i = 0;

    /* 收线程启动前先建口锁，避免 UartWritePort/uartRecvFun 抢未初始化 mutex */
    for (i = 0; i < MAX_UART_NUM; i++) {
        pthread_mutex_init(&g_lock[i], NULL);
    }
    if (!ReadConfig()) {
        return ERROR;
    }
    if (InitAllUart() != SUCCESS) {
        return ERROR;
    }
    if (InitUartTask() != SUCCESS) {
        return ERROR;
    }
    return SUCCESS;
}

int InitWatchDog(void)
{
    int timeout = 0;
    char kick = '1';

    g_WatchDogFd = 0;
    if (!s_watchdogEnable) {
        LOG(LOG_INFO, "Watchdog disabled (set WatchdogEnable=1 in doppler.conf to arm it)");
        return 0;
    }

    if ((g_WatchDogFd = open(WATCHDOG_DEV, O_RDWR)) < 0) {
        LOG(LOG_WARNING, "Failed to open watchdog (continue without it): %s",
            strerror(errno));
        g_WatchDogFd = 0;
        return 0;
    }

    /*
     * GT6606 NXP Watchdog：固定约 10s，不支持 SETTIMEOUT / MAGICCLOSE。
     * 打开即开始计时；不要 ioctl 设超时，也不要写 'V' 再 close（无效且危险）。
     * 依赖 DopplerApp 里 1s 周期 FeedWatchDog。
     */
    (void)write(g_WatchDogFd, &kick, 1);
    if (ioctl(g_WatchDogFd, WDIOC_GETTIMEOUT, &timeout) == 0 && timeout > 0) {
        LOG(LOG_DEBUG, "Watch dog armed, fd=%d hw_timeout=%ds (feed every 1s)",
            g_WatchDogFd, timeout);
    } else {
        LOG(LOG_DEBUG, "Watch dog armed, fd=%d (NXP fixed ~10s, feed every 1s)",
            g_WatchDogFd);
    }
    return g_WatchDogFd;
}

void FeedWatchDog(void* arg)
{
    char buf = '1';
    ssize_t ret;

    (void)arg;
    if (g_WatchDogFd <= 0) {
        return;
    }
    ret = write(g_WatchDogFd, &buf, 1);
    if (ret != 1) {
        LOG(LOG_ERROR, "Failed to feed WatchDog: %s", strerror(errno));
    }
}

