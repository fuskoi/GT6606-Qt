#ifndef SCREEN_IDLE_FILTER_H
#define SCREEN_IDLE_FILTER_H

#include <QObject>
#include <QtGlobal>

class QEvent;
class QTimer;

/*
 * 空闲软件息屏 + 触摸唤醒（黑遮罩，不关背光）。
 * tyls.conf ScreenIdleMinutes：1/3/5/10；0=不息屏。OTA 忙时保持亮屏。
 */
class ScreenIdleFilter : public QObject
{
    Q_OBJECT
public:
    explicit ScreenIdleFilter(QObject *parent = 0);
    static ScreenIdleFilter *instance();

    void setIdleMinutes(int minutes);
    int idleMinutes() const { return m_idleMinutes; }
    void noteActivity();
    bool isBlanked() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event);

private slots:
    void onTick();

private:
    void setBlanked(bool on);

    static ScreenIdleFilter *s_instance;
    QTimer *m_timer;
    qint64 m_lastActivityMs;
    int m_idleMinutes;
    bool m_blanked;
};

#endif
