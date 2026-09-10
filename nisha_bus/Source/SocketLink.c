#include "SocketLink.h"
#include "UplinkOffline.h"
#include "uart.h"
#include "log.h"
#include "config.h"
#include "SL651_2014.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* 4G OTA：中心常连发，缓冲需大于单帧，避免 RX 积压丢字节 */
#define SOCKET_RX_BUF_SIZE   (128U * 1024U)
#define SOCKET_RECV_CHUNK    4096
#define SOCKET_RECONNECT_SEC 3
#define SOCKET_RCVBUF_BYTES  (256 * 1024)

uint8_t g_u8WebLinkMode = WEB_LINK_MODE_DTU;
char g_szSocketServerIP[64];
uint16_t g_u16SocketServerPort = 0;

static int s_i32SockFd = -1;
static pthread_mutex_t s_sockMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_tid;
static int s_i32Started = 0;
static uint8_t *s_rxBuf = NULL;
static unsigned int s_rxLen = 0;

void WebLink_LoadConfig(void)
{
    myhash_t *item;

    g_u8WebLinkMode = WEB_LINK_MODE_DTU;
    g_szSocketServerIP[0] = '\0';
    g_u16SocketServerPort = 0;

    item = find_item("WebLinkMode");
    if (item != NULL && item->value[0] != '\0') {
        g_u8WebLinkMode = (uint8_t)atoi(item->value);
    }
    item = find_item("SocketServerIP");
    if (item != NULL && item->value[0] != '\0') {
        strncpy(g_szSocketServerIP, item->value, sizeof(g_szSocketServerIP) - 1);
        g_szSocketServerIP[sizeof(g_szSocketServerIP) - 1] = '\0';
    }
    item = find_item("SocketServerPort");
    if (item != NULL && item->value[0] != '\0') {
        g_u16SocketServerPort = (uint16_t)atoi(item->value);
    }

    LOG(LOG_INFO, "WebLinkMode=%u IP=%s port=%u",
        (unsigned)g_u8WebLinkMode,
        g_szSocketServerIP[0] ? g_szSocketServerIP : "(none)",
        (unsigned)g_u16SocketServerPort);
}

int WebLink_IsSocketMode(void)
{
    return (g_u8WebLinkMode == WEB_LINK_MODE_SOCKET) ? 1 : 0;
}

int WebLink_IsReady(void)
{
    if (WebLink_IsSocketMode()) {
        return SocketLink_IsConnected();
    }
    return (g_stM2UpFramepara.fd > 0) ? 1 : 0;
}

int WebLink_Send(const uint8_t *pData, int length)
{
    int fd;
    ssize_t n;

    if (pData == NULL || length <= 0) {
        return -1;
    }

    if (WebLink_IsSocketMode()) {
        if (!SocketLink_IsConnected()) {
            return -1;
        }
        return SocketLink_Send(pData, length);
    }

    /* DTU：不加 g_lock[1]，由调用方（SL651 已持锁 / OtaUpgrade_DtuWrite）串行化 */
    fd = g_stM2UpFramepara.fd;
    if (fd <= 0) {
        return -1;
    }
    n = write(fd, pData, (size_t)length);
    return (int)n;
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

    if (!WebLink_IsSocketMode()) {
        return -1;
    }
    if (g_szSocketServerIP[0] == '\0' || g_u16SocketServerPort == 0) {
        LOG(LOG_WARNING, "SocketLink: IP/port not set");
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG(LOG_ERROR, "SocketLink: socket() failed: %s", strerror(errno));
        return -1;
    }

    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    {
        int rcv = SOCKET_RCVBUF_BYTES;
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv));
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_u16SocketServerPort);
    if (inet_pton(AF_INET, g_szSocketServerIP, &addr.sin_addr) != 1) {
        LOG(LOG_ERROR, "SocketLink: bad IP %s", g_szSocketServerIP);
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG(LOG_WARNING, "SocketLink: connect %s:%u failed: %s",
            g_szSocketServerIP, (unsigned)g_u16SocketServerPort, strerror(errno));
        close(fd);
        return -1;
    }

    LOG(LOG_INFO, "SocketLink: connected %s:%u",
        g_szSocketServerIP, (unsigned)g_u16SocketServerPort);
    return fd;
}

int SocketLink_Send(const uint8_t *pData, int length)
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

static void socket_process_rx(void)
{
    WORD wUsed = 0;

    while (s_rxLen > 0) {
        uint16_t passLen = (s_rxLen > 0xFFFFU) ? 0xFFFFU : (uint16_t)s_rxLen;
        int nProc = ProcUartData_Passive(DTU_651_USR, s_rxBuf, passLen, &wUsed);
        (void)nProc;
        if (wUsed == 0) {
            break;
        }
        if (wUsed > s_rxLen) {
            s_rxLen = 0;
            break;
        }
        memmove(s_rxBuf, s_rxBuf + wUsed, s_rxLen - wUsed);
        s_rxLen -= wUsed;
        memset(s_rxBuf + s_rxLen, 0, 1);
    }
}

static void *socket_thread(void *arg)
{
    uint8_t recvBuf[SOCKET_RECV_CHUNK];

    (void)arg;

    if (s_rxBuf == NULL) {
        s_rxBuf = (uint8_t *)malloc(SOCKET_RX_BUF_SIZE);
        if (s_rxBuf == NULL) {
            LOG(LOG_ERROR, "SocketLink: rx buf malloc failed");
            return NULL;
        }
        s_rxLen = 0;
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
            s_rxLen = 0;
            UplinkOffline_OnDownlink();
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

            UplinkOffline_OnDownlink();
            log_dtu_frame("DOWN", "socket", recvBuf, (int)n);

            if (s_rxLen + (unsigned int)n > SOCKET_RX_BUF_SIZE) {
                LOG(LOG_WARNING, "SocketLink: rx buf full, drop %zd (queued=%u)",
                    n, s_rxLen);
            } else {
                memcpy(s_rxBuf + s_rxLen, recvBuf, (size_t)n);
                s_rxLen += (unsigned int)n;
                socket_process_rx();
            }
        }
    }

    return NULL;
}

unsigned int SocketLink_RxQueued(void)
{
    return s_rxLen;
}

void SocketLink_Start(void)
{
    int err;

    if (!WebLink_IsSocketMode()) {
        LOG(LOG_INFO, "SocketLink: WebLinkMode=DTU, socket client not started");
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
    LOG(LOG_INFO, "SocketLink: start client %s:%u",
        g_szSocketServerIP, (unsigned)g_u16SocketServerPort);
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
        usleep(100000);
        waited += 100U;
    }
    return SocketLink_IsConnected() ? 1 : 0;
}
