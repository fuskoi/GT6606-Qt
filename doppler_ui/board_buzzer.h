#ifndef BOARD_BUZZER_H
#define BOARD_BUZZER_H

#include <QObject>

/* GT6606: /dev/buzzer — short beep on button; always force-off before hard exit. */
class BoardBuzzer : public QObject
{
    Q_OBJECT
public:
    static BoardBuzzer *instance();
    /* Async-signal-safe: open, write '0', close. Safe from SIGINT / _exit paths. */
    static void forceOff();

    bool openDevice();
    void closeDevice();
    bool isOpen() const;

public slots:
    void pulse(int ms = 40);
    void off();

private:
    explicit BoardBuzzer(QObject *parent = 0);
    ~BoardBuzzer();

    int m_fd;
};

#endif
