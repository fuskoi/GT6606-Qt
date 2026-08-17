#ifndef BOARD_BUZZER_H
#define BOARD_BUZZER_H

#include <QObject>

/* GT6606: /dev/buzzer — 按键短蜂鸣；硬退出前务必 forceOff。 */
class BoardBuzzer : public QObject
{
    Q_OBJECT
public:
    /* 返回全局蜂鸣器单例（首次调用时创建）。 */
    static BoardBuzzer *instance();

    /* 异步信号安全：写 '0' 关蜂鸣；可供 SIGINT / _exit 路径调用。 */
    static void forceOff();

    /* 打开 /dev/buzzer 并保持 fd，初始强制关闭。 */
    bool openDevice();

    /* 关闭蜂鸣器设备并释放 fd。 */
    void closeDevice();

    /* 设备是否已打开。 */
    bool isOpen() const;

public slots:
    /* 响 ms 毫秒后自动关闭（默认约 40ms）。 */
    void pulse(int ms = 40);

    /* 立即写入 '0' 停止蜂鸣。 */
    void off();

private:
    /* 私有构造，仅通过 instance() 创建。 */
    explicit BoardBuzzer(QObject *parent = 0);

    /* 析构时确保关闭设备。 */
    ~BoardBuzzer();

    int m_fd;
};

#endif
