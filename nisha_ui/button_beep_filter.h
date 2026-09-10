#ifndef BUTTON_BEEP_FILTER_H
#define BUTTON_BEEP_FILTER_H

#include <QObject>
#include <QtGlobal>

class QEvent;

/* 仅在 QAbstractButton（或其子控件）被按下时短蜂鸣。 */
class ButtonBeepFilter : public QObject
{
    Q_OBJECT
public:
    /* 安装到 QApplication 的全局按键蜂鸣过滤器。 */
    explicit ButtonBeepFilter(QObject *parent = 0);

protected:
    /* 拦截 MouseButtonPress/TouchBegin：若落在按钮上则短蜂鸣。 */
    bool eventFilter(QObject *watched, QEvent *event);

private:
    qint64 m_lastPulseMs;
};

#endif
