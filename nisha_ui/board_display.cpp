#include "board_display.h"

#include <QApplication>
#include <QString>
#include <QWidget>

#ifndef BOARD_WIDTH
#define BOARD_WIDTH 1024
#endif
#ifndef BOARD_HEIGHT
#define BOARD_HEIGHT 600
#endif

namespace {

QWidget *g_cover = 0;
bool g_blanked = false;

QWidget *ensureCover()
{
    if (!g_cover) {
        g_cover = new QWidget(0, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        g_cover->setObjectName(QStringLiteral("screenIdleCover"));
        g_cover->setStyleSheet(QStringLiteral("background-color:#000000;"));
        g_cover->setGeometry(0, 0, BOARD_WIDTH, BOARD_HEIGHT);
        g_cover->setAttribute(Qt::WA_AcceptTouchEvents, true);
    }
    return g_cover;
}

} /* namespace */

namespace BoardDisplay {

bool isBlanked(void)
{
    return g_blanked;
}

void blank(void)
{
    QWidget *w = ensureCover();
    w->show();
    w->raise();
    g_blanked = true;
}

void unblank(void)
{
    if (g_cover)
        g_cover->hide();
    g_blanked = false;
}

}
