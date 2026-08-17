#include "board_buzzer.h"

#include <QTimer>
#include <fcntl.h>
#include <unistd.h>

/* Shared with SIGINT/_exit: do not touch QObject from the signal path. */
static volatile int g_buzzerFd = -1;

/* 返回全局 BoardBuzzer 单例（首次调用时创建）。 */
BoardBuzzer *BoardBuzzer::instance()
{
    static BoardBuzzer *s = 0;
    if (!s)
        s = new BoardBuzzer();
    return s;
}

/* 信号安全关蜂鸣：写已持有 fd，并额外 open/write/close 兜底。 */
void BoardBuzzer::forceOff()
{
    char offc = '0';
    const int held = g_buzzerFd;
    if (held >= 0)
        (void)::write(held, &offc, 1);

    /* Extra open in case latch stuck after fd already closed. */
    const int fd = ::open("/dev/buzzer", O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
        (void)::write(fd, &offc, 1);
        ::close(fd);
    }
}

/* 构造：蜂鸣器 fd 置为未打开。 */
BoardBuzzer::BoardBuzzer(QObject *parent)
    : QObject(parent)
    , m_fd(-1)
{
}

/* 析构：关闭设备。 */
BoardBuzzer::~BoardBuzzer()
{
    closeDevice();
}

/* 打开 /dev/buzzer，同步到 g_buzzerFd，并先 off 一次。 */
bool BoardBuzzer::openDevice()
{
    if (m_fd >= 0)
        return true;

    m_fd = ::open("/dev/buzzer", O_RDWR | O_CLOEXEC);
    if (m_fd < 0)
        return false;

    g_buzzerFd = m_fd;
    off();
    return true;
}

/* 关蜂鸣后关闭 fd，并清空全局 g_buzzerFd。 */
void BoardBuzzer::closeDevice()
{
    if (m_fd < 0)
        return;
    off();
    g_buzzerFd = -1;
    ::close(m_fd);
    m_fd = -1;
}

/* 判断蜂鸣器设备是否已打开。 */
bool BoardBuzzer::isOpen() const
{
    return m_fd >= 0;
}

/* 写 '1' 响一声，定时器到期后调 off()；ms 限制在 10~200。 */
void BoardBuzzer::pulse(int ms)
{
    if (m_fd < 0)
        return;
    if (ms < 10)
        ms = 10;
    if (ms > 200)
        ms = 200;

    char on = '1';
    ::write(m_fd, &on, 1);
    QTimer::singleShot(ms, this, SLOT(off()));
}

/* 写 '0' 停止蜂鸣。 */
void BoardBuzzer::off()
{
    if (m_fd < 0)
        return;
    char offc = '0';
    ::write(m_fd, &offc, 1);
}
