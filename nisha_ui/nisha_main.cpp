#include <QApplication>
#include <QTextCodec>
#include <QTimer>
#include "board_buzzer.h"
#include "button_beep_filter.h"
#include "screen_idle_filter.h"
#include "nisha_mainwindow.h"

#include <signal.h>
#include <pthread.h>
#include <unistd.h>

extern "C" {
#include "config.h"
}

#ifndef BOARD_WIDTH
#define BOARD_WIDTH 1024
#endif
#ifndef BOARD_HEIGHT
#define BOARD_HEIGHT 600
#endif

static void onPosixSignal(int)
{
    BoardBuzzer::forceOff();
    ::_exit(0);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, onPosixSignal);
    signal(SIGTERM, onPosixSignal);

    /* 业务定时器使用 SIGALRM；主线程屏蔽，避免打断 Qt 事件循环导致触控“无响应后一起爆发” */
    {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGALRM);
        pthread_sigmask(SIG_BLOCK, &set, NULL);
    }

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Nisha GT6606"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));
    {
        QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
        if (utf8)
            QTextCodec::setCodecForLocale(utf8);
    }

    BoardBuzzer::instance()->openDevice();
    app.installEventFilter(new ButtonBeepFilter(&app));

    NishaMainWindow w;
    w.setWindowFlags(Qt::FramelessWindowHint);
    w.setFixedSize(BOARD_WIDTH, BOARD_HEIGHT);
    w.setGeometry(0, 0, BOARD_WIDTH, BOARD_HEIGHT);
    w.show();
    w.showFullScreen();
    w.setFixedSize(BOARD_WIDTH, BOARD_HEIGHT);
    w.move(0, 0);
    app.processEvents();

    ScreenIdleFilter *idle = new ScreenIdleFilter(&app);
    idle->setIdleMinutes(Config_GetInt("ScreenIdleMinutes", 0));
    app.installEventFilter(idle);

    QTimer::singleShot(50, &w, SLOT(onStartBusiness()));

    return app.exec();
}
