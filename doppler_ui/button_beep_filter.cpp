#include "button_beep_filter.h"
#include "board_buzzer.h"

#include <QAbstractButton>
#include <QDateTime>
#include <QEvent>
#include <QWidget>

/* 构造：初始化上次蜂鸣时间戳。 */
ButtonBeepFilter::ButtonBeepFilter(QObject *parent)
    : QObject(parent)
    , m_lastPulseMs(0)
{
}

/* 事件过滤：仅在按钮按下时蜂鸣，并做 80ms 防抖（触摸会合成鼠标事件）。 */
bool ButtonBeepFilter::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type t = event->type();
    if (t != QEvent::MouseButtonPress && t != QEvent::TouchBegin)
        return false;

    QWidget *w = qobject_cast<QWidget *>(watched);
    while (w) {
        if (qobject_cast<QAbstractButton *>(w)) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            /* Touch often synthesizes a mouse press; debounce double fire. */
            if (now - m_lastPulseMs >= 80) {
                m_lastPulseMs = now;
                BoardBuzzer::instance()->pulse(40);
            }
            break;
        }
        w = w->parentWidget();
    }
    return false;
}
