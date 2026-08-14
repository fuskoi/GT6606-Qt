#include <QApplication>
#include <QTextCodec>
#include <QTimer>
#include "board_buzzer.h"
#include "button_beep_filter.h"
#include "doppler_mainwindow.h"

#include <signal.h>
#include <unistd.h>

#ifndef BOARD_WIDTH
#define BOARD_WIDTH 1024
#endif
#ifndef BOARD_HEIGHT
#define BOARD_HEIGHT 600
#endif

static void onPosixSignal(int)
{
    /* Detached business threads ignore Qt shutdown; hard-exit on Ctrl+C too. */
    BoardBuzzer::forceOff();
    ::_exit(0);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, onPosixSignal);
    signal(SIGTERM, onPosixSignal);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Doppler GT6606"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));
    /* 配置文件按 UTF-8；避免 locale 非 UTF-8 时读写中文注释乱码 */
    {
        QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
        if (utf8)
            QTextCodec::setCodecForLocale(utf8);
    }

    /* Hold /dev/buzzer so only button presses (via filter) can beep. */
    BoardBuzzer::instance()->openDevice();
    app.installEventFilter(new ButtonBeepFilter(&app));

    /* 先全屏显示，再在事件循环里启动业务，避免黑屏干等 socket/串口初始化 */
    DopplerMainWindow w;
    /* linuxfb 上若不锁死尺寸，QStackedWidget 会按“最大子页”撑高，屏外裁切，首页按钮只露一条 */
    w.setWindowFlags(Qt::FramelessWindowHint);
    w.setFixedSize(BOARD_WIDTH, BOARD_HEIGHT);
    w.setGeometry(0, 0, BOARD_WIDTH, BOARD_HEIGHT);
    w.show();
    w.showFullScreen();
    /* 部分板子 showFullScreen 后尺寸会漂，再钉一次 */
    w.setFixedSize(BOARD_WIDTH, BOARD_HEIGHT);
    w.move(0, 0);
    app.processEvents();

    QTimer::singleShot(50, &w, SLOT(onStartBusiness()));

    return app.exec();
}
