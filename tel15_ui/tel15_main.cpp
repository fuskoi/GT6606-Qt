#include <QApplication>
#include <QTextCodec>
#include <QTimer>
#include "board_buzzer.h"
#include "button_beep_filter.h"
#include "tel15_mainwindow.h"

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
    BoardBuzzer::forceOff();
    ::_exit(0);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, onPosixSignal);
    signal(SIGTERM, onPosixSignal);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("TEL-15 GT6606"));
    app.setApplicationVersion(QStringLiteral("0.6"));
    {
        QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
        if (utf8)
            QTextCodec::setCodecForLocale(utf8);
    }

    BoardBuzzer::instance()->openDevice();
    app.installEventFilter(new ButtonBeepFilter(&app));

    Tel15MainWindow w;
    w.setWindowFlags(Qt::FramelessWindowHint);
    w.setFixedSize(BOARD_WIDTH, BOARD_HEIGHT);
    w.setGeometry(0, 0, BOARD_WIDTH, BOARD_HEIGHT);
    w.show();
    w.showFullScreen();
    w.setFixedSize(BOARD_WIDTH, BOARD_HEIGHT);
    w.move(0, 0);
    app.processEvents();

    QTimer::singleShot(50, &w, SLOT(onStartBusiness()));
    return app.exec();
}
