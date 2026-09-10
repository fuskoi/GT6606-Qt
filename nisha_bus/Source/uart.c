/**
 * Copyright (C), 2023,
 * @file 	 【文件名称，不包含绝对路径】
 * @author 	 【作者】
 * @date 	 2023-08-08
 * @brief 	 【简介】串口的配置以及收发功能函数
 * @note 	 【重大修改可以放在这里】
 */

#include "uart.h"
#include "UplinkOffline.h"
#include "SocketLink.h"
#include "OtaUpgrade.h"
#include "SL651_2014.h"

#define CRTSCTS 020000000000
#define TASKCOUNT 30
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
typedef void (*different_uart_function)(uart_work_info_t *);
port_info_t g_portinfos;
uart_work_info_t g_uartWorkInfoArry[6] = {
    {.role = UART_DEFAULT},
    {.role = UART_DEFAULT},
    {.role = UART_DEFAULT},
    {.role = UART_DEFAULT},
    {.role = UART_DEFAULT},
    {.role = UART_DEFAULT},
};
pthread_mutex_t g_lock[MAX_QUEUE_LOCK];
Node *g_device_list = NULL;
DeviceIndex g_device_index = {
    .communicate = -1,
    .encoder = -1,
    .level_indicator = -1,
    .load_cell = -1,
    .meteorological_sensor = -1,
    .relay_module = -1,
    .soil_moisture_sensorA = -1,
    .soil_moisture_sensorB = -1,
    .wind_sensor = -1,
    .radar_velocity = -1,
    .coulomb_counter = -1,
    .step_motor_4aix = -1,
    .zigbee = -1,
    .sediment_sensor = -1,
    .dc_motor = -1,
    .depth_sensor = -1,
    .dtu = -1,
    .lora = -1,
};
// 串口设备列表 每个值代表设备连接的串口号，也代表在g_uartWorkInfoArry数组中的位置
// int g_level_indicator = -1;
// int g_load_cell = -1;
// int g_meteorological_sensor = -1;
// int g_soil_moisture_sensorA = -1;
// int g_soil_moisture_sensorB = -1;
// int g_wind_sensor = -1;
// int g_communicate = -1;
// int g_relay_module = -1;
// int g_encoder = -1;
// 初始化串口(打开串口 并获得串口默认属性)
int get_device_port_fd(int device_name)
{
    int index = get_device_index(device_name);
    // printf("index:%d\n", index);
    if (index < 0 || index >= 6)
    {
        fprintf(stderr, "Invalid device index: %d\n", index);
        return -1;
    }
    switch (device_name)
    {
    case LORA:
        /* code */
        return g_uartWorkInfoArry[index].comfd;
        break;
    case DTU:
        return g_uartWorkInfoArry[index].comfd;
        break;
    case DEPTH_SENSOR:
        return g_uartWorkInfoArry[index].comfd;
        break;
    case SEDIMENT_SENSOR:
        return g_uartWorkInfoArry[index].comfd;
        break;
    case DC_MOTOR:
        return g_uartWorkInfoArry[index].comfd;
        break;
    case RELAY_MODULE:
        return g_uartWorkInfoArry[index].comfd;
        break;
    default:
        break;
    }
    return -1;
}

int uart_init(const char *devpath, struct termios *old_cfg)
{
    int com_fd;
    if ((com_fd = open(devpath, O_RDWR | O_NOCTTY)) < 0) // | O_NDELAY | O_NOCTTY
    {
        perror("open com_fd failed");
        return -1;
    }
    // 获取串口当前的配置参数
    if ((tcgetattr(com_fd, old_cfg)) < 0)
    {
        fprintf(stderr, "tcgetattr error:%s\n", strerror(errno));
        close(com_fd);
        return -1;
    }
    return com_fd;
}
// 设置串口属性并使其生效
int uart_cfg_set(int com_fd, const pport_info_t cfg)
{
    struct termios new_cfg = {0};
    speed_t speed;
    // 设置为原始模式
    // cfmakeraw(&new_cfg);
    // new_cfg.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    new_cfg.c_oflag &= ~OPOST;
    // new_cfg.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    //   使能接收
    new_cfg.c_cflag |= CREAD;
    // 设置波特率
    switch (cfg->baudrate)
    {
    case 1200:
        speed = B1200;
        break;
    case 1800:
        speed = B1800;
        break;
    case 2400:
        speed = B2400;
        break;
    case 4800:
        speed = B4800;
        break;
    case 9600:
        speed = B9600;
        break;
    case 19200:
        speed = B19200;
        break;
    case 38400:
        speed = B38400;
        break;
    case 57600:
        speed = B57600;
        break;
    case 115200:
        speed = B115200;
        break;
    case 230400:
        speed = B230400;
        break;
    default: // 默认配置为 9600
        speed = B9600;
        ts_printf("default baud rate: 9600\n");
        break;
    }
    if (cfsetispeed(&new_cfg, speed) < 0)
    {
        fprintf(stderr, "cfsetspeed error:%s\n", strerror(errno));
        return -1;
    }
    cfsetospeed(&new_cfg, speed); // 填入串口输出端的波特率

    // 设置数据位大小
    new_cfg.c_cflag &= ~CSIZE;
    switch (cfg->databits)
    {
    case 5:
        new_cfg.c_cflag |= CS5;
        break;
    case 6:
        new_cfg.c_cflag |= CS6;
        break;
    case 7:
        new_cfg.c_cflag |= CS7;
        break;
    case 8:
        new_cfg.c_cflag |= CS8;
        break;
    default: // 默认数据位大小为 8
        new_cfg.c_cflag |= CS8;
        ts_printf("default data bit size: 8\n");
        break;
    }

    // 设置奇偶校验
    switch (cfg->parity)
    {
    case 'N': // 无校验
    case 'n':
        new_cfg.c_cflag &= ~PARENB;
        new_cfg.c_iflag &= ~INPCK;
        break;
    case 'O': // 奇校验
    case 'o':
        new_cfg.c_cflag |= (PARODD | PARENB);
        new_cfg.c_iflag |= INPCK;
        break;
    case 'E': // 偶校验
    case 'e':
        new_cfg.c_cflag |= PARENB;
        new_cfg.c_cflag &= ~PARODD; /* 清除 PARODD 标志，配置为偶校验 */
        new_cfg.c_iflag |= INPCK;
        break;
    default: // 默认配置为无校验
        new_cfg.c_cflag &= ~PARENB;
        new_cfg.c_iflag &= ~INPCK;
        ts_printf("default parity: N\n");
        break;
    }
    // 设置停止位
    switch (cfg->stopbits)
    {
    case 1: // 1 个停止位
        new_cfg.c_cflag &= ~CSTOPB;
        break;
    case 2: // 2 个停止位
        new_cfg.c_cflag |= CSTOPB;
        break;
    default: // 默认配置为 1 个停止位
        new_cfg.c_cflag &= ~CSTOPB;
        ts_printf("default stop bit size: 1\n");
        break;
    }
    // 将MIN和TIME设置为03
    new_cfg.c_cc[VTIME] = 50;
    new_cfg.c_cc[VMIN] = 0;
    // 不使用流控制
    new_cfg.c_cflag &= ~CRTSCTS; // 无流控
    new_cfg.c_iflag &= ~(ICRNL | IXON);
    // new_cfg.c_iflag |= IXON | IXOFF | IXANY; // 软件流控
    new_cfg.c_cflag |= CLOCAL; // 控制模式，保证程序不会成为端口的占有者
    // 清空缓冲区
    if (tcflush(com_fd, TCIOFLUSH) < 0)
    {
        fprintf(stderr, "tcflush error:%s\n", strerror(errno));
        return -1;
    }

    // 写入配置使配置生效
    if (tcsetattr(com_fd, TCSANOW, &new_cfg) < 0)
    {
        fprintf(stderr, "tcsetattr error:%s\n", strerror(errno));
        return -1;
    }
    return 0;
}
// 配置串口属性结构体
void serial_set(pport_info_t cfg, int baudrate, int databits, int stopbits, char parity)
{
    cfg->baudrate = baudrate;
    cfg->databits = databits;
    cfg->parity = parity;
    cfg->stopbits = stopbits;
}
// 响应指令输出
void print_data(int size, const BYTE *data)
{
    int i;
    // ts_printf("recv size = %d\n", size);
    if (size > 0)
    {
        for (i = 0; i < size; i++)
        {
            ts_printf("%02x ", data[i]);
            fflush(stdout);
        }
    }
    ts_printf("\n");
}

void log_dtu_frame(const char *dir, const char *tag, const uint8_t *data, int len)
{
    if (data == NULL || len <= 0) {
        return;
    }
    if (dir == NULL) {
        dir = "?";
    }
    if (tag == NULL) {
        tag = "";
    }
    ts_printf("[DTU %s] %s len=%d: ", dir, tag, len);
    print_data(len, data);
    LOG(LOG_INFO, "[DTU %s] %s len=%d", dir, tag, len);
    LOG_ARRAY(LOG_INFO, data, (size_t)len, "%02x");
}
// 响应指令输出
void print_cdata(int size, const char *data)
{
    int i;
    // ts_printf("recv size = %d\n", size);
    if (size > 0)
    {
        for (i = 0; i < size; i++)
        {
            ts_printf("%02x ", data[i]);
            fflush(stdout);
        }
    }
}
// 串口发送
int uart_send(int com_fd, const void *data, int len)
{
    // 清空缓冲区
    if (tcflush(com_fd, TCIOFLUSH) < 0)
    {
        fprintf(stderr, "tcflush error:%s\n", strerror(errno));
        return -1;
    }
    if (NULL == data)
    {
        return -1;
    }
    ssize_t written = 0;
    written = write(com_fd, data, len);
    if (len == written)
    {
        return (len);
    }
    else
    {
        tcflush(com_fd, TCOFLUSH);
        return -1;
    }
}
// 读取串口
int uart_recv(int com_fd, void *buf, int len)
{
    int readlen, fs_sel;
    fd_set fs_read;
    // struct timeval tv_timeout;

    FD_ZERO(&fs_read);
    FD_SET(com_fd, &fs_read);
    // tv_timeout.tv_sec = 0;
    // tv_timeout.tv_usec = 20000;

    fs_sel = select(com_fd + 1, &fs_read, NULL, NULL, NULL);
    if (fs_sel)
    {
        readlen = read(com_fd, buf, len);
        return (readlen);
    }
    else
    {
        return (-1);
    }

    return (readlen);
}

// 初始化设备串口
int SetUartProperties(const char *devpath, port_info_t portinfo)
{
    struct termios old_cfg;
    int com_fd;
    int fifoSize = RECV_FIFOSIZE;
    if (devpath == NULL)
        return -1;
    com_fd = uart_init(devpath, &old_cfg);
    if (com_fd < 0)
        return -1;
#ifndef BOARD_GT6606
    /* 旧板私有 ioctl；GT6606 标准 ttyAMA 上忽略即可 */
    ioctl(com_fd, HTSETRXFIFO, fifoSize);
#else
    (void)fifoSize;
#endif
    // 配置串口
    if (uart_cfg_set(com_fd, &portinfo) < 0)
        tcsetattr(com_fd, TCSANOW, &old_cfg);
    return com_fd;
}
//  从串口发送指令并读取数据 (超时重传 最多三次)
int GetDataFromUart(int com_fd, const BYTE *command, BYTE *response, int len)
{
    int retry_count = 3;
    int n;
    int size;
    fd_set read_fds;
    struct timeval timeout;
    while (retry_count > 0)
    {
        // 发送指令
        if (write(com_fd, command, 8) == -1)
        {
            perror("Failed to write to serial port");
            return -1;
        }
        // 设置读取操作的超时时间
        FD_ZERO(&read_fds);
        FD_SET(com_fd, &read_fds);
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        // 接收响应
        n = select(com_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (n == -1)
        {
            perror("Failed to select from serial port");
            return -1;
        }
        else if (n == 0)
        {
            // 超时未接收到响应，重新发送指令
            retry_count--;
            continue;
        }
        // 读取响应数据
        memset(response, 0, len);
        if ((size = read(com_fd, response, len)) == -1)
        {
            perror("Failed to read from serial port");
            return -1;
        }
        // ts_printf("Received response: %s\n", response);
        break;
    }
    // 接收失败
    if (retry_count == 0)
        return -1;
    return size;
}

int InitUartDeviceFromConfig(void)
{
    myhash_t *tmp_type = NULL;
    myhash_t *tmp_baudrate = NULL;
    char uartType[32];
    char uartBaudrate[32];
    int i;
    char *COM_NAME[] = {COM1_NAME, COM2_NAME, COM3_NAME, COM4_NAME, COM5_NAME, COM6_NAME};
    SetConfigParameters();
    for (i = 0; i < 6; i++)
    {
        sprintf(uartBaudrate, "UART%d_BAUDRATE", i + 1);
        sprintf(uartType, "UART%d_ROLE", i + 1);
        tmp_type = find_item(uartType);
        tmp_baudrate = find_item(uartBaudrate);

        if (tmp_type == NULL || tmp_baudrate == NULL)
            continue;
        ts_printf("%s,%s\n", tmp_type->key, tmp_type->value);
        ts_printf("%s,%s\n", tmp_baudrate->key, tmp_baudrate->value);
        g_portinfos.baudrate = atoi(tmp_baudrate->value);
        g_portinfos.databits = 8;
        g_portinfos.stopbits = 1;
        g_portinfos.parity = 'n';
        g_uartWorkInfoArry[i].comID = i + 1;
        g_uartWorkInfoArry[i].recvddatalen = 0;
        if (strncmp(tmp_type->value, "PASSIVE", 7) == 0)
        {
            g_uartWorkInfoArry[i].role = UART_PASSIVE_LISTEN;
        }
        else if (strncmp(tmp_type->value, "SERVER", 6) == 0)
        {
            g_uartWorkInfoArry[i].role = UART_SERVER_COMM;
        }
        else if (strncmp(tmp_type->value, "ACTIVE", 6) == 0)
        {
            g_uartWorkInfoArry[i].role = UART_ACTIVE_CONTROL;
            g_uartWorkInfoArry[i].bRunTask = false;
            pthread_mutex_init(&g_lock[i], NULL);
            // 创建任务数组
            ptask_item_t pTaskArray = (ptask_item_t)malloc(sizeof(task_item_t) * TASKCOUNT + 1);
            // 初始化锁资源
            g_uartWorkInfoArry[i].taskQueue.mutex = &g_lock[i];
            // 初始化任务队列
            InitTaskQueue_List(sizeof(task_item_t), TASKCOUNT + 1, pTaskArray, &g_uartWorkInfoArry[i].taskQueue);

            // 初始化DTU的均匀报队列，只针对串口3的泥沙传感器，读取的数据存入此队列
            if (i == 2)
            {
                queue_init(&g_hour_float);
            }

            ts_printf("pthread mutex lock[%d] init succeed\n", i + 1);
        }

        g_uartWorkInfoArry[i].comfd = SetUartProperties(COM_NAME[i], g_portinfos);
        if (i == 1)
        {
            g_stM2UpFramepara.fd = g_uartWorkInfoArry[i].comfd; // 把串口2的fd给DTU全局结构体
        }

        if (g_uartWorkInfoArry[i].comfd < 0)
        {
            ts_printf("InitDeviceUart_%d error (%s)\n", i + 1, COM_NAME[i]);
#ifdef BOARD_GT6606
            /* 板级：缺某路串口时跳过，便于无 LoRa 串口屏 / 未接线调试 */
            continue;
#else
            return -1;
#endif
        }

        // 开启串口工作函数
        StartUartWork(&g_uartWorkInfoArry[i]);
        ts_printf("COM%d OK (%s)\n", i + 1, COM_NAME[i]);
    }
    return 0;
}

int StartUartWork(puart_work_info_t pUartWorkInfo)
{
    pthread_t thread_id;
    int ret;
    ret = pthread_create(&thread_id, NULL, UartWorker, pUartWorkInfo); /* 传递参数直接确定接收数据线程去读的串口 */
    if (ret == EAGAIN)
    {
        return -11;
    }
    pthread_detach(thread_id);
    return 0;
}

//******************** 改版1.0 epoll ******************** */
#define MAX_EVENTS 10
// void *UartWorker(void *arg)
// {

//     puart_work_info_t pUartWorkInfo = (puart_work_info_t)arg;
//     different_uart_function func_ptr;
//     if (pUartWorkInfo->role == UART_PASSIVE_LISTEN || pUartWorkInfo->role == UART_SERVER_COMM)
//     {
//         func_ptr = PassiveReceiveSerialPort; // 被动接收数据
//         ts_printf("UART_PASSIVE_LISTEN OR UART_SERVER_COMM-----");
//     }
//     else if (pUartWorkInfo->role == UART_ACTIVE_CONTROL)
//     {
//         func_ptr = ActiveSendSerialPort; // 主动发送数据再接收数据
//         ts_printf("UART_ACTIVE_CONTROL-----");
//     }
//     // 设置串口为非阻塞模式
//     int flags = fcntl(pUartWorkInfo->comfd, F_GETFL, 0);
//     fcntl(pUartWorkInfo->comfd, F_SETFL, flags | O_NONBLOCK);

//     ts_printf("DevRole:%d comID:%d, comfd:%d \n", pUartWorkInfo->role, pUartWorkInfo->comID, pUartWorkInfo->comfd);

//     int epoll_fd = epoll_create1(0);
//     if (epoll_fd == -1)
//     {
//         perror("epoll_create1");
//         return NULL;
//     }

//     struct epoll_event event;
//     event.events = EPOLLIN;
//     event.data.fd = pUartWorkInfo->comfd;

//     if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pUartWorkInfo->comfd, &event) == -1)
//     {
//         perror("epoll_ctl: add");
//         close(epoll_fd);
//         return NULL;
//     }

//     struct epoll_event events[MAX_EVENTS];

//     while (1)
//     {
//         int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 20); // -1 表示无限阻塞等待 1000 表示阻塞等待一秒
//         if (n == -1)
//         {
//             if (errno == EINTR)
//             {
//                 // LOG(LOG_ERROR, "epoll_wait error %d, comid: %d", errno, pUartWorkInfo->comID);
// #ifdef DEBUG
//                 ts_printf("epoll_wait was interrupted by signal %d\n", interrupted_by_signal);
//                 interrupted_by_signal = 0; // 重置标志
// #endif                                     // DEBUG

//                 continue;
//             }
//             else
//             {
//                 perror("epoll_wait");
//                 break;
//             }
//         }

//         if (n >= 0)
//         {
//             func_ptr(pUartWorkInfo);
//         }
//     }

//     close(epoll_fd);
//     // ShowLog("recv complete");
//     return NULL;
// }
void *UartWorker(void *arg)
{
    puart_work_info_t pUartWorkInfo = (puart_work_info_t)arg;

    // 设置串口为非阻塞模式
    int flags = fcntl(pUartWorkInfo->comfd, F_GETFL, 0);
    fcntl(pUartWorkInfo->comfd, F_SETFL, flags | O_NONBLOCK);

    ts_printf("DevRole:%d comID:%d, comfd:%d \n", pUartWorkInfo->role, pUartWorkInfo->comID, pUartWorkInfo->comfd);

    // 创建 epoll 实例
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
    {
        perror("epoll_create1");
        return NULL;
    }

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET; // 边沿触发
    event.data.fd = pUartWorkInfo->comfd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, pUartWorkInfo->comfd, &event) == -1)
    {
        perror("epoll_ctl: add");
        close(epoll_fd);
        return NULL;
    }

    struct epoll_event events[MAX_EVENTS];

    if (pUartWorkInfo->role == UART_PASSIVE_LISTEN || pUartWorkInfo->role == UART_SERVER_COMM)
    {
        ts_printf("UART_PASSIVE_LISTEN OR UART_SERVER_COMM-----");

        while (1)
        {
            int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); // 阻塞等待
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                perror("epoll_wait");
                break;
            }

            for (int i = 0; i < n; ++i)
            {
                if (events[i].data.fd == pUartWorkInfo->comfd && (events[i].events & EPOLLIN))
                {
                    WORD wUsed;
                    int nRead = read(pUartWorkInfo->comfd,
                                     pUartWorkInfo->recvBuffer + pUartWorkInfo->recvddatalen,
                                     RECEIVE_BUFFER_LENGTH - pUartWorkInfo->recvddatalen);

                    if (nRead > 0)
                    {
                        /* Socket 模式下 DTU 串口与 TCP 互斥，丢弃串口下行防双通道 */
                        if (pUartWorkInfo->protocol == DTU_651_USR && WebLink_IsSocketMode()) {
                            pUartWorkInfo->recvddatalen = 0;
                            continue;
                        }
                        /* DTU 任意收数视为链路有下行，用于在线判定与补发 */
                        if (pUartWorkInfo->protocol == DTU_651_USR) {
                            UplinkOffline_OnDownlink();
                            log_dtu_frame("DOWN", "raw",
                                          pUartWorkInfo->recvBuffer + pUartWorkInfo->recvddatalen,
                                          nRead);
                        }
                        pUartWorkInfo->recvddatalen += nRead;
                        int nProcCode = ProcUartData_Passive(pUartWorkInfo->protocol,
                                                             pUartWorkInfo->recvBuffer,
                                                             pUartWorkInfo->recvddatalen,
                                                             &wUsed);

                        if (wUsed > 0)
                        {
                            memmove(pUartWorkInfo->recvBuffer,
                                    pUartWorkInfo->recvBuffer + wUsed,
                                    pUartWorkInfo->recvddatalen - wUsed);
                            pUartWorkInfo->recvddatalen -= wUsed;
                            memset(pUartWorkInfo->recvBuffer + pUartWorkInfo->recvddatalen, 0,
                                   RECEIVE_BUFFER_LENGTH - pUartWorkInfo->recvddatalen);
                        }
                    }
                    else if (nRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        perror("read error");
                    }
                }
            }
        }
    }
    else if (pUartWorkInfo->role == UART_ACTIVE_CONTROL)
    {
        ts_printf("UART_ACTIVE_CONTROL-----");
        uint8_t retry_count = 0;

        while (1)
        {
            if (!pUartWorkInfo->bRunTask)
            {
                task_item_t tempTask;
                bool bDeQueueFlag = DeTaskQueue_List(&pUartWorkInfo->taskQueue, &tempTask);

                if (bDeQueueFlag)
                {
                    pUartWorkInfo->recvddatalen = 0;
                    pUartWorkInfo->taskName = tempTask.taskName;
                    pUartWorkInfo->deviceName = tempTask.deviceName;
                    pUartWorkInfo->value = tempTask.value;

                    if (ExecTaskByUart(tempTask.deviceName, tempTask.taskName,
                                       pUartWorkInfo->comfd, tempTask.value))
                    {
                        pUartWorkInfo->tsend_time = time(NULL);
                        pUartWorkInfo->bRunTask = true;
                        retry_count = 0; // 清除重试计数器
                    }
                }
                else
                {
                    /* 队列空时勿空转，否则多路 ACTIVE 串口会打满 CPU */
                    usleep(10000);
                }
            }
            else
            {
                int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000); // 超时1秒
                if (n < 0)
                {
                    if (errno == EINTR)
                        continue;
                    perror("epoll_wait");
                    break;
                }

                if (n == 0)
                {
                    // 超时处理
                    time_t tNow = time(NULL);
                    time_t time_interval = (pUartWorkInfo->deviceName == RADARVELOCITY) ? 13 : 2;

                    if (tNow - pUartWorkInfo->tsend_time > time_interval)
                    {
                        if (ExecTaskByUart(pUartWorkInfo->deviceName, pUartWorkInfo->taskName,
                                           pUartWorkInfo->comfd, pUartWorkInfo->value))
                        {
                            pUartWorkInfo->tsend_time = time(NULL);
                        }
                        retry_count++;
                    }

                    if (retry_count >= 3)
                    {
                        LOG(LOG_ERROR, "task time out! COMID: %d, device: %s",
                            pUartWorkInfo->comID, get_device_name(pUartWorkInfo->deviceName));
                        pUartWorkInfo->recvddatalen = 0;
                        memset(pUartWorkInfo->recvBuffer, 0, RECEIVE_BUFFER_LENGTH);
                        pUartWorkInfo->bRunTask = false;
                        update_device_status(g_device_list, pUartWorkInfo->deviceName, false); // 更新设备状态为离线
                        DealWithTimeOutTask(pUartWorkInfo->deviceName, pUartWorkInfo->taskName);
                        retry_count = 0;
                    }

                    continue;
                }

                for (int i = 0; i < n; ++i)
                {
                    if (events[i].data.fd == pUartWorkInfo->comfd && (events[i].events & EPOLLIN))
                    {
                        int nRead = read(pUartWorkInfo->comfd,
                                         pUartWorkInfo->recvBuffer + pUartWorkInfo->recvddatalen,
                                         RECEIVE_BUFFER_LENGTH - pUartWorkInfo->recvddatalen);

                        if (nRead > 0)
                        {

                            // ts_printf("serial port:%d\n", pUartWorkInfo->comID);
                            pUartWorkInfo->recvddatalen += nRead;
                            int nProcCode = ProcUartData(pUartWorkInfo->taskName,
                                                         pUartWorkInfo->deviceName,
                                                         pUartWorkInfo->recvBuffer,
                                                         pUartWorkInfo->recvddatalen);

                            switch (nProcCode)
                            {
                            case DATA_PROC_OK:
                                pUartWorkInfo->recvddatalen = 0;
                                memset(pUartWorkInfo->recvBuffer, 0, RECEIVE_BUFFER_LENGTH);
                                pUartWorkInfo->bRunTask = false;
                                retry_count = 0;
                                break;

                            case DATA_PROC_ERROR:
                                pUartWorkInfo->recvddatalen = 0;
                                memset(pUartWorkInfo->recvBuffer, 0, RECEIVE_BUFFER_LENGTH);
                                pUartWorkInfo->bRunTask = false;
                                retry_count = 0;
                                break;

                            case DATA_PROC_INCOMPLETE:
                                continue;
                            }
                        }
                        else if (nRead < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                        {
                            perror("read error");
                            pUartWorkInfo->bRunTask = false;
                        }
                    }
                }
            }
        }
    }

    close(epoll_fd);
    return NULL;
}
void ActiveSendSerialPort(uart_work_info_t *pUartWorkInfo)
{
    int nProcCode = 0;
    int nRead = 0;
    bool bDeQueueFlag = false;
    task_item_t tempTask;
    time_t tNow = 0;
    uint8_t retry_count = 0;
    // 如果当前没有任务在运行
    if (!pUartWorkInfo->bRunTask)
    {
        // 从任务队列中取出一个任务
        bDeQueueFlag = DeTaskQueue_List(&pUartWorkInfo->taskQueue, &tempTask);

        if (bDeQueueFlag)
        {
            // 初始化接收数据长度和任务信息
            pUartWorkInfo->recvddatalen = 0;
            pUartWorkInfo->taskName = tempTask.taskName;
            pUartWorkInfo->deviceName = tempTask.deviceName;
            pUartWorkInfo->value = tempTask.value;

#ifdef DEBUG
            // printf("device:%d, taskName:%d, sendTime:%ld\n", pUartWorkInfo->deviceName, pUartWorkInfo->taskName, pUartWorkInfo->tsend_time);
#endif // DEBUG
       // 执行任务，发送指令
            if (ExecTaskByUart(pUartWorkInfo->deviceName, pUartWorkInfo->taskName, pUartWorkInfo->comfd, pUartWorkInfo->value))
            {
                pUartWorkInfo->tsend_time = time(NULL);
                // ts_printf("device:%d, taskName:%d, sendTime:%ld\n", pUartWorkInfo->deviceName, pUartWorkInfo->taskName, pUartWorkInfo->tsend_time);
                pUartWorkInfo->bRunTask = true;
            }
        }
    }
    else
    {
        // 任务正在运行，继续接收数据
        do
        {
            // 读取串口数据
            nRead = read(pUartWorkInfo->comfd, pUartWorkInfo->recvBuffer + pUartWorkInfo->recvddatalen,
                         RECEIVE_BUFFER_LENGTH - pUartWorkInfo->recvddatalen);

            // printf("nread = %d recvddatalen total = %d\n", nRead, pUartWorkInfo->recvddatalen);
            // print_data(pUartWorkInfo->recvddatalen, pUartWorkInfo->recvBuffer);
            if (nRead > 0)
            {
                pUartWorkInfo->recvddatalen += nRead;
                // 处理接收到的数据
                nProcCode = ProcUartData(pUartWorkInfo->taskName, pUartWorkInfo->deviceName, pUartWorkInfo->recvBuffer, pUartWorkInfo->recvddatalen);

                if (nProcCode == DATA_PROC_ERROR)
                {
                    // 数据错误处理
                    // 清空接收缓冲区
                    LOG(LOG_ERROR, "Data error, COMID:%d  Device:%s", pUartWorkInfo->comID, get_device_name(pUartWorkInfo->deviceName));
                    // update_device_status(g_device_list, pUartWorkInfo->deviceName, false);
                    pUartWorkInfo->recvddatalen = 0;
                    memset(pUartWorkInfo->recvBuffer, 0, RECEIVE_BUFFER_LENGTH);
                    pUartWorkInfo->bRunTask = false;
                }
                else if (nProcCode == DATA_PROC_OK)
                {
                    // 数据接收完成
                    // 清空接收缓冲区
                    pUartWorkInfo->recvddatalen = 0;
                    memset(pUartWorkInfo->recvBuffer, 0, RECEIVE_BUFFER_LENGTH);
                    pUartWorkInfo->bRunTask = false;
                    // printf("---------------------------------------------- Data received successfully! ----------------------------------------------\n");
                }
                // 如果nProcCode == 1，表示数据未接收完成，继续循环读取
                else if (nProcCode == DATA_PROC_INCOMPLETE)
                {
                    // ts_printf("Data not received completely!\n");
                    continue;
                }
            }
            else if (nRead == 0)
            {
                if (pUartWorkInfo->deviceName == RADARVELOCITY)
                {

                    DealUartTaskTimeOut(pUartWorkInfo, &retry_count, 13);
                }
                else
                {

                    DealUartTaskTimeOut(pUartWorkInfo, &retry_count, 2);
                }
            }
            else
            {
                // 读取错误处理

                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    if (pUartWorkInfo->deviceName == RADARVELOCITY)
                    {

                        DealUartTaskTimeOut(pUartWorkInfo, &retry_count, 13);
                    }
                    else
                    {

                        DealUartTaskTimeOut(pUartWorkInfo, &retry_count, 2);
                    }
                }
                else
                {
                    perror("read error");
                    // 清空接收缓冲区
                    pUartWorkInfo->recvddatalen = 0;
                    memset(pUartWorkInfo->recvBuffer, 0, RECEIVE_BUFFER_LENGTH);
                    pUartWorkInfo->bRunTask = false;
                    break;
                }
            }
        } while (pUartWorkInfo->bRunTask); // 继续读取直到数据接收完成
    }

    return;
}
void PassiveReceiveSerialPort(uart_work_info_t *pUartWorkInfo)
{
    WORD wUsed; // 已处理的字节数
    int nProcCode = 0;
    wUsed = 0;
    int nRead = 0;
    nRead = read(pUartWorkInfo->comfd, pUartWorkInfo->recvBuffer + pUartWorkInfo->recvddatalen, RECEIVE_BUFFER_LENGTH - pUartWorkInfo->recvddatalen);
    // ts_printf("nread = %d \n", nRead);
    if (nRead > 0)
    {
        if (pUartWorkInfo->protocol == DTU_651_USR && WebLink_IsSocketMode()) {
            pUartWorkInfo->recvddatalen = 0;
            return;
        }
        pUartWorkInfo->recvddatalen += nRead;
        // printf("recvddatalen total = %d\n", pUartWorkInfo->recvddatalen);
        // 处理数据
        nProcCode = ProcUartData_Passive(pUartWorkInfo->protocol, pUartWorkInfo->recvBuffer, pUartWorkInfo->recvddatalen, &wUsed);

        // 把没有处理的数据移动到BUFFER的起始位置，清0无效数据
        if (wUsed > 0)
        {
            memcpy(pUartWorkInfo->recvBuffer, pUartWorkInfo->recvBuffer + wUsed, pUartWorkInfo->recvddatalen - wUsed);
            pUartWorkInfo->recvddatalen -= wUsed;
            memset(pUartWorkInfo->recvBuffer + pUartWorkInfo->recvddatalen, 0, RECEIVE_BUFFER_LENGTH - pUartWorkInfo->recvddatalen);
        }

        // printf("recvddatalen Remain = %d\n", pUartWorkInfo->recvddatalen);
    }
    else if (nRead < 0)
    {
        // sleep(1);
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return;
        }
        else
        {
            perror("nread < 0");
            ts_printf("port %d read size:%d < 0 uartType:%d", pUartWorkInfo->comID, nRead, pUartWorkInfo->role);
        }
    }
    return;
}

void ReStart(void)
{

    sync();
    reboot(0x01234567);
    return;
}

int CheckModbusResData(BYTE *pData, int nLength, BYTE nDeviceSite)
{
    if (nLength < 7)
    {
        return DATA_PROC_INCOMPLETE;
    }

    if (nLength > 30)
    {
        return DATA_PROC_ERROR;
    }

    int nPos = 0;

    while (nPos < nLength - 6)
    {
        if (pData[nPos] != nDeviceSite)
        {
            nPos++;
            continue;
        }

        if (pData[nPos + 1] == 0x03)
        {
            // 移除对数据长度的错误判断
            if (nPos + 5 + pData[nPos + 2] > nLength) // 数据不完整
            {
                return 1;
            }
            WORD wCrcCode = 0;
            wCrcCode = CRC16_Modbus(pData + nPos, 3 + pData[nPos + 2]);
            if (pData[3 + nPos + pData[nPos + 2]] == ((wCrcCode & 0xff00) >> 8) && pData[4 + nPos + pData[nPos + 2]] == (wCrcCode & 0x00ff))
            {
                return DATA_PROC_OK;
            }
            nPos++;
            continue;
        }
        else if (pData[nPos + 1] == 0x06 || pData[nPos + 1] == 0x10 || pData[nPos + 1] == 0x05)
        {
            if (nPos + 8 > nLength)
            {
                return 1;
            }
            WORD wCrcCode = 0;
            wCrcCode = CRC16_Modbus(pData + nPos, 6);
            if (pData[6 + nPos] == ((wCrcCode & 0xff00) >> 8) && pData[7 + nPos] == (wCrcCode & 0x00ff))
            {
                return DATA_PROC_OK;
            }
            nPos++;
            continue;
        }

        nPos++;
    }

    return DATA_PROC_INCOMPLETE;
}

void clear_line()
{
    ts_printf("\033[2K"); // 清空当前行
    ts_printf("\r");      // 移动光标到行首
}

void move_cursor_to_first_line()
{
    ts_printf("\033[1;1H"); // 移动到第一行开头
}

void move_cursor_to_second_line()
{
    ts_printf("\033[2;1H"); // 移动到第二行开头
}

void clear_current_line()
{
    ts_printf("\033[2K"); // 清空当前行
}

// LEVEL_INDICATOR = 1 液位传感器
// LOAD_CELL = 2 重量传感器
// METEOROLOGICAL_SENSOR = 3 气象传感器
// SOIL_MOISTURE_SENSOR1 = 4 土壤墒情传感器A
// SOIL_MOISTURE_SENSOR2 = 4 土壤墒情传感器B
// WIND_SENSOR = 5 风速风向传感器

/**
 * 更新配置项的值
 *
 * 本函数通过查找哈希表中的配置项，并根据找到的值更新传入的整型变量
 * 特别地，函数可以根据 subtract_one 参数的指示，选择是否从查找到的值中减去 1
 *
 * @param key 配置项的键，用于在哈希表中查找对应的值
 * @param value_ptr 指向一个整型变量的指针，通过此指针更新实际的配置值
 * @param find 一个函数指针，指向用于查找哈希表中特定键的函数
 * @param subtract_one 标志是否需要从查找到的配置值中减去 1
 */
void UpdateConfigValue(const char *key, int *value_ptr, myhash_t *(*find)(char *), int subtract_one)
{
    myhash_t *phash_tmp = find(key);
    if (phash_tmp != NULL && value_ptr != NULL)
    {
        // 根据 subtract_one 的值决定是否减去 1
        *value_ptr = atoi(phash_tmp->value) - (subtract_one ? 1 : 0);
    }
}

#define DEBUG_MODE 1
void SetConfigParameters(void)
{
    UpdateConfigValue("COMMUNICATE_PORT", &g_device_index.communicate, find_item, 1);
    UpdateConfigValue("ZIGBEE", &g_device_index.zigbee, find_item, 1);
    UpdateConfigValue("4G_DTU", &g_device_index.dtu, find_item, 1);
    UpdateConfigValue("LORA_SCREEN", &g_device_index.lora, find_item, 1);
    UpdateConfigValue("DOG", &g_dog, find_item, 0);
    // 1. 定义配置映射表（集中管理配置项）
    static const DeviceConfigMapping config_map[] = {
        {"LEVEL_INDICATOR", &g_device_index.level_indicator, LEVEL_INDICATOR},
        {"LOAD_CELL", &g_device_index.load_cell, LOAD_CELL},
        {"METEOROLOGICAL_SENSOR", &g_device_index.meteorological_sensor, METEOROLOGICAL_SENSOR},
        {"SOIL_MOISTURE_SENSOR1", &g_device_index.soil_moisture_sensorA, SOIL_MOISTURE_SENSOR1},
        {"SOIL_MOISTURE_SENSOR2", &g_device_index.soil_moisture_sensorB, SOIL_MOISTURE_SENSOR2},
        {"WIND_SENSOR", &g_device_index.wind_sensor, WIND_SENSOR},
        {"RELAY_MODULE", &g_device_index.relay_module, RELAY_MODULE},
        {"ENCODER", &g_device_index.encoder, ENCODER},
        {"RADARVELOCITY", &g_device_index.radar_velocity, RADARVELOCITY},
        {"COULOMB_COUNTER", &g_device_index.coulomb_counter, COULOMB_COUNTER},
        {"STEP_MOTOR", &g_device_index.step_motor_4aix, STEPMOTOR},
        {"DC_MOTOR", &g_device_index.dc_motor, DC_MOTOR},
        {"DEPTH_SENSOR", &g_device_index.depth_sensor, DEPTH_SENSOR},
        {"SEDIMENT_SENSOR", &g_device_index.sediment_sensor, SEDIMENT_SENSOR},
    };

    // 2. 批量更新配置项
    const size_t num_configs = sizeof(config_map) / sizeof(config_map[0]);
    for (size_t i = 0; i < num_configs; ++i)
    {
        // 添加返回值检查
        UpdateConfigValue(config_map[i].config_key,
                          config_map[i].config_var,
                          find_item,
                          1);
    }

    // 3. 批量初始化设备列表
    device_state_t device = {0};
    for (size_t i = 0; i < num_configs; ++i)
    {
        if (*(config_map[i].config_var) != -1)
        {
            device.deviceName = config_map[i].device_name;
            device.is_online = true;

            // 添加节点创建检查
            if (!append_node(&g_device_list, &device, sizeof(device)))
            {
                LOG(LOG_ERROR, "Failed to add device: %s",
                    get_device_name(config_map[i].device_name));
            }
        }
    }

// 4. 遍历调试输出
#ifdef DEBUG
    traverse_list(g_device_list, print_device);
#endif // !DEBUG
}

// 升级为配置文件设置
void SetUartProtocol(void)
{
    if (g_device_index.communicate > -1)
    {
        if (g_uartWorkInfoArry[g_device_index.communicate].role == UART_SERVER_COMM)
            g_uartWorkInfoArry[g_device_index.communicate].protocol = MODBUS_RTU;
    }
    if (g_device_index.zigbee > -1)
    {

        if (g_uartWorkInfoArry[g_device_index.zigbee].role == UART_SERVER_COMM)
            g_uartWorkInfoArry[g_device_index.zigbee].protocol = ZIGBEE_USR;
    }
    if (g_device_index.dtu > -1)
    {
        if (g_uartWorkInfoArry[g_device_index.dtu].role == UART_SERVER_COMM)
            g_uartWorkInfoArry[g_device_index.dtu].protocol = DTU_651_USR;
    }
    if (g_device_index.lora > -1)
    {
        if (g_uartWorkInfoArry[g_device_index.lora].role == UART_SERVER_COMM)
            g_uartWorkInfoArry[g_device_index.lora].protocol = LORA_SCREEN;
    }
}

void print_device(void *data)
{
    if (!data)
        return;
    device_state_t *dev = (device_state_t *)data;
    ts_printf("Device: %s, Status: %s\n",
              get_device_name(dev->deviceName),
              dev->is_online ? "Online" : "Offline");
}

void change_device_status(void *data, device_name_t deviceName)
{
    if (!data)
        return;
    device_state_t *dev = (device_state_t *)data;
    if (dev->deviceName == deviceName)
        dev->is_online = false;
}
const char *get_device_name(device_name_t name)
{
    static const char *names[] = {
        [LEVEL_INDICATOR] = "Level Indicator",
        [LOAD_CELL] = "Load Cell",
        [METEOROLOGICAL_SENSOR] = "Meteorological Sensor",
        [SOIL_MOISTURE_SENSOR1] = "Soil Moisture Sensor PH",
        [SOIL_MOISTURE_SENSOR2] = "Soil Moisture Sensor TMP",
        [WIND_SENSOR] = "Wind Sensor",
        [RELAY_MODULE] = "Relay Module",
        [ENCODER] = "encoder",
        [RADARVELOCITY] = "Radar Velocity",
        [COULOMB_COUNTER] = "Coulomb Counter",
        [STEPMOTOR] = "Step Motor",
        [DC_MOTOR] = "dc motor",
        [SEDIMENT_SENSOR] = "Sediment Sensor",
        [DEPTH_SENSOR] = "Depth Sensor",
        // ...其他设备名称
    };
    return (name < MAX_DEVICES && names[name] != NULL) ? names[name] : "Unknown";
}

bool update_device_status(Node *head, device_name_t target_name, bool new_status)
{
    if (!head)
    {
        fprintf(stderr, "Error: Null linked list head\n");
        return false;
    }

    Node *current = head;
    while (current != NULL)
    {
        // 类型安全检查
        if (current->data_size != sizeof(device_state_t))
        {
            fprintf(stderr, "Error: Invalid node data type\n");
            return false;
        }

        device_state_t *device = (device_state_t *)current->data;
        if (device->deviceName == target_name)
        {
            device->is_online = new_status;

            // 可选：添加日志记录
            // LOG(LOG_INFO, "Device [%s] status updated to %s",
            //     get_device_name(target_name),
            //     new_status ? "Online" : "Offline");
            return true;
        }
        current = current->next;
    }

    fprintf(stderr, "Warning: Device [%s] not found\n",
            get_device_name(target_name));
    LOG(LOG_ERROR, "Device [%s] not found", get_device_name(target_name));
    return false;
}

/**
 * @brief 获取指定设备的在线状态
 * @param head 链表头节点指针
 * @param target 目标设备枚举值
 * @param out_online 输出参数，存储获取到的在线状态
 * @return true : 成功找到设备并返回状态
 * @return false : 设备未找到或发生错误
 */
bool get_device_online_status(const Node *head, device_name_t target, bool *out_online)
{
    // 参数合法性检查
    if (!head || !out_online)
    {
        fprintf(stderr, "[ERROR] Invalid input parameters\n");
        return false;
    }

    const Node *current = head;
    while (current != NULL)
    {
        // 类型安全检查
        if (current->data_size != sizeof(device_state_t))
        {
            fprintf(stderr, "[CRITICAL] Node data corruption at device %p\n", (void *)current);
            return false;
        }

        const device_state_t *device = (const device_state_t *)current->data;
        if (device->deviceName == target)
        {
            *out_online = device->is_online;
            return true;
        }
        current = current->next;
    }

    // 设备未找到日志
    fprintf(stderr, "[WARNING] Device %s not found in list\n",
            get_device_name(target));
    LOG(LOG_ERROR, "[WARNING] Device %s not found in list", get_device_name(target));
    return false;
}

int get_device_index(device_name_t deviceName)
{
    switch (deviceName)
    {
    case LEVEL_INDICATOR:
        /* code */
        return g_device_index.level_indicator;
        break;
    case LOAD_CELL:
        /* code */
        return g_device_index.load_cell;
        break;
    case METEOROLOGICAL_SENSOR:
        /* code */
        return g_device_index.meteorological_sensor;
        break;
    case SOIL_MOISTURE_SENSOR1:
        /* code */
        return g_device_index.soil_moisture_sensorA;
        break;
    case SOIL_MOISTURE_SENSOR2:
        /* code */
        return g_device_index.soil_moisture_sensorB;
        break;
    case WIND_SENSOR:
        /* code */
        return g_device_index.wind_sensor;
        break;
    case ENCODER:
        /* code */
        return g_device_index.encoder;
        break;
    case RELAY_MODULE:
        /* code */
        return g_device_index.relay_module;
        break;
    case RADARVELOCITY:
        /* code */
        return g_device_index.radar_velocity;
        break;
    case COULOMB_COUNTER:
        /* code */
        return g_device_index.coulomb_counter;
        break;
    case STEPMOTOR:
        /* code */
        return g_device_index.step_motor_4aix;
        break;
    case ZIGBEE:
        return g_device_index.zigbee;
        break;
    case DC_MOTOR:
        return g_device_index.dc_motor;
    case DEPTH_SENSOR:
        return g_device_index.depth_sensor;
    case SEDIMENT_SENSOR:
        return g_device_index.sediment_sensor;
    case DTU:
        return g_device_index.dtu;
    case LORA:
        return g_device_index.lora;
    default:
        break;
    }
    return -1;
}

void DealUartTaskTimeOut(uart_work_info_t *pUartWorkInfo, uint8_t *retry_count, time_t time_interval)
{

    // 串口超时，继续循环
    time_t tNow = time(NULL);
    // ts_printf(" nread = -1 tsendtime = %d tNow = %d ; tNow-tsend = %d\n", tsend_time, tNow, tNow - tsend_time);
    if (tNow - pUartWorkInfo->tsend_time > time_interval)
    {
        if (ExecTaskByUart(pUartWorkInfo->deviceName, pUartWorkInfo->taskName, pUartWorkInfo->comfd, pUartWorkInfo->value))
        {
            pUartWorkInfo->tsend_time = time(NULL);
        }
        (*retry_count)++;
    }

    if ((*retry_count) >= 3)
    {
        LOG(LOG_ERROR, "task time out! COMID: %d, device: %s", pUartWorkInfo->comID, get_device_name(pUartWorkInfo->deviceName));
        pUartWorkInfo->recvddatalen = 0;
        memset(pUartWorkInfo->recvBuffer, 0, RECEIVE_BUFFER_LENGTH);
        pUartWorkInfo->bRunTask = false;
        update_device_status(g_device_list, pUartWorkInfo->deviceName, false); // 更新设备状态为离线
        DealWithTimeOutTask(pUartWorkInfo->deviceName, pUartWorkInfo->taskName);
        // traverse_list(g_device_list, print_device);
    }
}

// 线程安全的printf函数
void ts_printf(const char *format, ...)
{
    // 先加锁
    pthread_mutex_lock(&print_mutex);

    // 使用va_list处理可变参数
    va_list args;
    va_start(args, format);
    vprintf(format, args); // 注意：这里使用vprintf代替printf以支持可变参数列表
    va_end(args);

    // 解锁
    pthread_mutex_unlock(&print_mutex);
}

bool ExecTaskByUart(device_name_t deviceName, uart_task_name taskName, uint32_t comfd, void *value)
{
    int ret = 0;
    switch (deviceName)
    {
    case LEVEL_INDICATOR:
        break;
    case LOAD_CELL:
        break;
    case METEOROLOGICAL_SENSOR:
        break;
    case SOIL_MOISTURE_SENSOR1:
        break;
    case SOIL_MOISTURE_SENSOR2:
        break;
    case WIND_SENSOR:
        break;
    case ENCODER:
        ret = EncoderSendCmd(taskName.encoder_task, comfd);
        break;
    case RELAY_MODULE:
        ret = RelayModuleSendCommand(taskName.relay_module_task, comfd);
        break;
    case DC_MOTOR:
        ret = DC_MotorSendCommand(taskName.dc_motor_task, comfd, value);
        break;
    case DEPTH_SENSOR:
        // printf("[ExecTaskByUart] value = %d\n", *(int *)value);
        ret = DepthSensorSendCommand(taskName.depth_sensor_task, comfd, value);
        break;
    case SEDIMENT_SENSOR:
        ret = SedimentSensorSendCommand(taskName.sediment_sensor_task, comfd, value);
        break;
    default:
        break;
    }
    if (ret < 0)
        return false;
    return true;
}

// 处理接收的所有数据
int ProcUartData(uart_task_name taskName, int deviceName, uint8_t *recvBuffer, uint16_t recvddatalen)
{
    int nRet = 0;
    // print_data(recvddatalen, recvBuffer);
    switch (deviceName)
    {
    case RELAY_MODULE:
        SetRelayModuleOnline(true);
        nRet = RelayModuleDataProcessCtrl(taskName.relay_module_task, recvBuffer, recvddatalen, CheckModbusResData);
        break;
    case DC_MOTOR:
        set_dc_motor_online(true);
        nRet = DcMotorDataProcessCtrl(taskName.dc_motor_task, recvBuffer, recvddatalen, CheckModbusResData);
        break;
    case SEDIMENT_SENSOR:
        set_sediment_online(true);
        nRet = SedimentSensorDataProcessCtrl(taskName.sediment_sensor_task, recvBuffer, recvddatalen, CheckModbusResData);
        break;
    case DEPTH_SENSOR:
        set_depth_online(true);
        nRet = DepthSensorDataProcessCtrl(taskName.depth_sensor_task, recvBuffer, recvddatalen, CheckModbusResData);
        break;
    case ENCODER:
        set_encoder_online(true);
        nRet = ActiveEncoderDataProcess(taskName.encoder_task, recvBuffer, recvddatalen, CheckModbusResData);
        break;
    default:
        break;
    }
    return nRet; //* 0正常处理结束 -1接收到的是错误码
}

void DealWithTimeOutTask(int deviceName, uart_task_name taskName)
{
    switch (deviceName)
    {
    case RELAY_MODULE:
        DealWithRelayModuleTimeOut(taskName.relay_module_task);
        break;
    case DC_MOTOR:
        DealWithDcMotorTimeOut(taskName.dc_motor_task);
        break;
    case DEPTH_SENSOR:
        DealWithDepthSensorTimeOut(taskName.depth_sensor_task);
        break;
    case SEDIMENT_SENSOR:
        DealWithSedimentSensorTimeOut(taskName.sediment_sensor_task);
        break;
    case ENCODER:
        DealWithEncoderTimeOut(taskName.encoder_task);
        break;
    default:
        break;
    }
    return;
}

int ProcUartData_Passive(int protocol, uint8_t *recvBuffer, uint16_t recvddatalen, WORD *wUsed)
{
    // printf("uart_protocol: %d\n", protocol);
    // print_data(recvddatalen, recvBuffer);
    int nRet = -1;
    ModbusFuncPtr modbus = NULL;
    switch (protocol)
    {
    case UART_PASSIVE_LISTEN:

        break;
    case MODBUS_RTU:
        nRet = modbus(recvBuffer, recvddatalen, wUsed);
        break;
    case ZIGBEE_USR:
        nRet = ProcZigbeeData(recvBuffer, recvddatalen, wUsed);
        break;
    case DTU_651_USR:
    {
        int otaRet = OtaUpgrade_RecDataProc(recvBuffer, recvddatalen, wUsed);
        if (otaRet == -1) {
            /* OTA 大包未收齐，保留缓冲 */
            return 0;
        }
        if (otaRet == 0) {
            return 0;
        }
        nRet = SL651_RecDataProc(recvBuffer, recvddatalen, wUsed, deal_with_DTU_data);
        break;
    }
    case LORA_SCREEN:
        // print_data(recvddatalen, recvBuffer);
        nRet = ProcLoraData(recvBuffer, recvddatalen, wUsed, deal_with_screen_data);
        // print_data("nret = %d\n", nRet);
        break;
    default:
        break;
    }
    return nRet; //* 0正常处理结束 -1接收到的是错误码
}
