#include "board_buzzer.h"

#include <QTimer>
#include <fcntl.h>
#include <unistd.h>

/* Shared with SIGINT/_exit: do not touch QObject from the signal path. */
static volatile int g_buzzerFd = -1;

BoardBuzzer *BoardBuzzer::instance()
{
    static BoardBuzzer *s = 0;
    if (!s)
        s = new BoardBuzzer();
    return s;
}

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

BoardBuzzer::BoardBuzzer(QObject *parent)
    : QObject(parent)
    , m_fd(-1)
{
}

BoardBuzzer::~BoardBuzzer()
{
    closeDevice();
}

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

void BoardBuzzer::closeDevice()
{
    if (m_fd < 0)
        return;
    off();
    g_buzzerFd = -1;
    ::close(m_fd);
    m_fd = -1;
}

bool BoardBuzzer::isOpen() const
{
    return m_fd >= 0;
}

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

void BoardBuzzer::off()
{
    if (m_fd < 0)
        return;
    char offc = '0';
    ::write(m_fd, &offc, 1);
}
