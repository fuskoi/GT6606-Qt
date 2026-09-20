#include "SocketLink.h"
#include "UplinkOffline.h"
#include "zigbee_dataproc.h"
#include "log.h"
#include "ringfifo.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define SOCKET_RX_FIFO_SIZE  16384U
#define SOCKET_RECV_BUF      2048
#define SOCKET_RECONNECT_SEC 3
#define SOCKET_CONNECT_TIMEOUT_SEC 3

static int s_i32SockFd = -1;
static pthread_mutex_t s_sockMutex = PTHREAD_MUTEX_INITIALIZER;
static KFIFO_ID s_rxFifo = NULL;
static pthread_t s_tid;
static int s_i32Started = 0;

int WebLink_IsSocketMode(void)
{
    return (g_stWebLinkConfig.u8WebLinkMode == WEB_LINK_MODE_SOCKET) ? 1 : 0;
}

int WebLink_Send(const uint8_t* pData, int length)
{
    int hostPort;
    int fd;

    if (pData == NULL || length <= 0) {
        return -1;
    }

    if (WebLink_IsSocketMode()) {
        if (!SocketLink_IsConnected()) {
            return -1;
        }
        return SocketLink_Send(pData, length);
    }

    hostPort = g_stDevicelist.HOST_COMPUTER;
    if (hostPort < 0 || hostPort >= MAX_UART_NUM) {
        return -1;
    }
    fd = g_stDevicelist.UartList[hostPort].i32fd;
    if (fd <= 0) {
        return -1;
    }
    return UartWrite(fd, pData, length);
}

static void socket_close_locked(void)
{
    if (s_i32SockFd >= 0) {
        close(s_i32SockFd);
        s_i32SockFd = -1;
    }
}

static int socket_connect_once(void)
{
    int fd = -1;
    struct sockaddr_in addr;
    int one = 1;
    int flags = 0;
    int ret = 0;
    fd_set wfds;
    struct timeval tv;
    int soerr = 0;
    socklen_t solen = sizeof(soerr);

    if (!WebLink_IsSocketMode()) {
        return -1;
    }
    if (g_stWebLinkConfig.szSocketServerIP[0] == '\0' ||
        g_stWebLinkConfig.u16SocketServerPort == 0) {
        LOG(LOG_WARNING, "SocketLink: IP/port not set");
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG(LOG_ERROR, "SocketLink: socket() failed: %s", strerror(errno));
        return -1;
    }

    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG(LOG_ERROR, "SocketLink: fcntl nonblock failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_stWebLinkConfig.u16SocketServerPort);
    if (inet_pton(AF_INET, g_stWebLinkConfig.szSocketServerIP, &addr.sin_addr) != 1) {
        LOG(LOG_ERROR, "SocketLink: bad IP %s", g_stWebLinkConfig.szSocketServerIP);
        close(fd);
        return -1;
    }

    ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret == 0) {
        /* 立刻连上 */
    } else if (errno != EINPROGRESS) {
        LOG(LOG_WARNING, "SocketLink: connect %s:%u failed: %s",
            g_stWebLinkConfig.szSocketServerIP,
            (unsigned)g_stWebLinkConfig.u16SocketServerPort,
            strerror(errno));
        close(fd);
        return -1;
    } else {
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        tv.tv_sec = SOCKET_CONNECT_TIMEOUT_SEC;
        tv.tv_usec = 0;
        ret = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (ret <= 0) {
            LOG(LOG_WARNING, "SocketLink: connect %s:%u timeout (%ds)",
                g_stWebLinkConfig.szSocketServerIP,
                (unsigned)g_stWebLinkConfig.u16SocketServerPort,
                SOCKET_CONNECT_TIMEOUT_SEC);
            close(fd);
            return -1;
        }
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &solen) < 0 || soerr != 0) {
            LOG(LOG_WARNING, "SocketLink: connect %s:%u failed: %s",
                g_stWebLinkConfig.szSocketServerIP,
                (unsigned)g_stWebLinkConfig.u16SocketServerPort,
                strerror(soerr != 0 ? soerr : errno));
            close(fd);
            return -1;
        }
    }

    /* 恢复阻塞，后续 send/recv 沿用阻塞语义 */
    (void)fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    LOG(LOG_DEBUG, "SocketLink: connected %s:%u",
        g_stWebLinkConfig.szSocketServerIP,
        (unsigned)g_stWebLinkConfig.u16SocketServerPort);
    return fd;
}

int SocketLink_Send(const uint8_t* pData, int length)
{
    int fd;
    int sent = 0;

    if (pData == NULL || length <= 0) {
        return -1;
    }

    pthread_mutex_lock(&s_sockMutex);
    fd = s_i32SockFd;
    if (fd < 0) {
        pthread_mutex_unlock(&s_sockMutex);
        return -1;
    }

    while (sent < length) {
        ssize_t n = send(fd, pData + sent, (size_t)(length - sent), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG(LOG_WARNING, "SocketLink: send failed: %s", strerror(errno));
            socket_close_locked();
            pthread_mutex_unlock(&s_sockMutex);
            UplinkOffline_OnLinkLost();
            return -1;
        }
        if (n == 0) {
            socket_close_locked();
            pthread_mutex_unlock(&s_sockMutex);
            UplinkOffline_OnLinkLost();
            return -1;
        }
        sent += (int)n;
    }
    pthread_mutex_unlock(&s_sockMutex);
    return sent;
}

int SocketLink_IsConnected(void)
{
    int ok;
    pthread_mutex_lock(&s_sockMutex);
    ok = (s_i32SockFd >= 0) ? 1 : 0;
    pthread_mutex_unlock(&s_sockMutex);
    return ok;
}

static void* socket_thread(void* arg)
{
    uint8_t recvBuf[SOCKET_RECV_BUF];
    WORD used = 0;

    (void)arg;

    if (s_rxFifo == NULL) {
        s_rxFifo = kfifo_init(SOCKET_RX_FIFO_SIZE);
        if (s_rxFifo == NULL) {
            LOG(LOG_ERROR, "SocketLink: kfifo_init failed");
            return NULL;
        }
    }

    while (1) {
        int fd;

        if (!WebLink_IsSocketMode()) {
            pthread_mutex_lock(&s_sockMutex);
            socket_close_locked();
            pthread_mutex_unlock(&s_sockMutex);
            sleep(SOCKET_RECONNECT_SEC);
            continue;
        }

        pthread_mutex_lock(&s_sockMutex);
        if (s_i32SockFd < 0) {
            pthread_mutex_unlock(&s_sockMutex);
            fd = socket_connect_once();
            if (fd < 0) {
                sleep(SOCKET_RECONNECT_SEC);
                continue;
            }
            pthread_mutex_lock(&s_sockMutex);
            s_i32SockFd = fd;
            pthread_mutex_unlock(&s_sockMutex);
            kfifo_clear(s_rxFifo);
            used = 0;
        } else {
            fd = s_i32SockFd;
            pthread_mutex_unlock(&s_sockMutex);
        }

        {
            ssize_t n = recv(fd, recvBuf, sizeof(recvBuf), 0);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) {
                    continue;
                }
                LOG(LOG_WARNING, "SocketLink: disconnected (%s)",
                    (n < 0) ? strerror(errno) : "peer closed");
                pthread_mutex_lock(&s_sockMutex);
                if (s_i32SockFd == fd) {
                    socket_close_locked();
                }
                pthread_mutex_unlock(&s_sockMutex);
                UplinkOffline_OnLinkLost();
                sleep(SOCKET_RECONNECT_SEC);
                continue;
            }

            if (kfifo_put(s_rxFifo, recvBuf, (unsigned int)n) != (unsigned int)n) {
                LOG(LOG_WARNING, "SocketLink: rx fifo full, drop %zd bytes", n);
            }

            /* 全功能下行解析（与 DTU 同一套 Proc_DTU_RecData） */
            (void)Proc_DTU_RecData(s_rxFifo, &used, DTU_TYPE);
        }
    }

    return NULL;
}

void SocketLink_Start(void)
{
    int err;

    if (!WebLink_IsSocketMode()) {
        LOG(LOG_DEBUG, "SocketLink: WebLinkMode=DTU, socket client not started");
        return;
    }
    if (s_i32Started) {
        return;
    }

    err = pthread_create(&s_tid, NULL, socket_thread, NULL);
    if (err != 0) {
        LOG(LOG_ERROR, "SocketLink: pthread_create failed: %d", err);
        return;
    }
    (void)pthread_detach(s_tid);
    s_i32Started = 1;
    LOG(LOG_DEBUG, "SocketLink: start client %s:%u (full web link)",
        g_stWebLinkConfig.szSocketServerIP,
        (unsigned)g_stWebLinkConfig.u16SocketServerPort);
}

int SocketLink_WaitConnected(uint32_t timeoutMs)
{
    uint32_t waited = 0;

    if (!WebLink_IsSocketMode()) {
        return 0;
    }
    while (waited <= timeoutMs) {
        if (SocketLink_IsConnected()) {
            return 1;
        }
        usleep(100000); /* 100ms */
        waited += 100U;
    }
    return SocketLink_IsConnected() ? 1 : 0;
}
