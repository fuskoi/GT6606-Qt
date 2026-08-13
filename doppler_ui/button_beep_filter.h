#ifndef BUTTON_BEEP_FILTER_H
#define BUTTON_BEEP_FILTER_H

#include <QObject>
#include <QtGlobal>

class QEvent;

/* Beep only when a QAbstractButton (or its child) is pressed. */
class ButtonBeepFilter : public QObject
{
    Q_OBJECT
public:
    explicit ButtonBeepFilter(QObject *parent = 0);

protected:
    bool eventFilter(QObject *watched, QEvent *event);

private:
    qint64 m_lastPulseMs;
};

#endif
