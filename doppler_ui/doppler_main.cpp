#include <QApplication>
#include <QTimer>
#include "board_buzzer.h"
#include "button_beep_filter.h"
#include "doppler_mainwindow.h"

#include <signal.h>
#include <unistd.h>

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

    /* Hold /dev/buzzer so only button presses (via filter) can beep. */
    BoardBuzzer::instance()->openDevice();
    app.installEventFilter(new ButtonBeepFilter(&app));

    /* 先全屏显示，再在事件循环里启动业务，避免黑屏干等 socket/串口初始化 */
    DopplerMainWindow w;
    w.resize(1024, 600);
    w.show();
    w.showFullScreen();
    app.processEvents();

    QTimer::singleShot(50, &w, SLOT(onStartBusiness()));

    return app.exec();
}
