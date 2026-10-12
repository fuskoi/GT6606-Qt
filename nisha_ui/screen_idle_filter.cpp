#include "screen_idle_filter.h"
#include "board_display.h"

#include <QDateTime>
#include <QEvent>
#include <QTimer>

extern "C" {
#include "OtaUpgrade.h"
}

ScreenIdleFilter *ScreenIdleFilter::s_instance = 0;

ScreenIdleFilter::ScreenIdleFilter(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_lastActivityMs(QDateTime::currentMSecsSinceEpoch())
    , m_idleMinutes(0)
    , m_blanked(false)
{
    s_instance = this;
    connect(m_timer, SIGNAL(timeout()), this, SLOT(onTick()));
    m_timer->start(1000);
}

ScreenIdleFilter *ScreenIdleFilter::instance()
{
    return s_instance;
}

void ScreenIdleFilter::setIdleMinutes(int minutes)
{
    if (minutes < 0)
        minutes = 0;
    m_idleMinutes = minutes;
    noteActivity();
}

void ScreenIdleFilter::noteActivity()
{
    m_lastActivityMs = QDateTime::currentMSecsSinceEpoch();
    if (m_blanked)
        setBlanked(false);
}

bool ScreenIdleFilter::isBlanked() const
{
    return m_blanked;
}

void ScreenIdleFilter::setBlanked(bool on)
{
    if (m_blanked == on)
        return;
    m_blanked = on;
    if (on)
        BoardDisplay::blank();
    else
        BoardDisplay::unblank();
}

bool ScreenIdleFilter::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    const QEvent::Type t = event->type();
    const bool input = (t == QEvent::MouseButtonPress
                        || t == QEvent::MouseButtonDblClick
                        || t == QEvent::TouchBegin
                        || t == QEvent::KeyPress);
    if (!input)
        return false;

    if (m_blanked) {
        noteActivity();
        return true; /* 仅唤醒；遮罩顶层，本次点击不往下传 */
    }
    noteActivity();
    return false;
}

void ScreenIdleFilter::onTick()
{
    if (m_idleMinutes <= 0) {
        if (m_blanked)
            setBlanked(false);
        return;
    }

    if (OtaUpgrade_IsBusy()) {
        noteActivity();
        return;
    }

    if (m_blanked)
        return;

    const qint64 idleMs = QDateTime::currentMSecsSinceEpoch() - m_lastActivityMs;
    if (idleMs >= (qint64)m_idleMinutes * 60 * 1000)
        setBlanked(true);
}
