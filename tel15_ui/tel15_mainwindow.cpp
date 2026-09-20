#include "tel15_mainwindow.h"
#include "board_buzzer.h"
#include "board_display.h"
#include "section_view.h"

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/reboot.h>

extern "C" {
#include "Tel15App.h"
#include "Common.h"
#include "Encoder.h"
#include "WebSeriver.h"
#include "SocketLink.h"
#include "zigbee_dataproc.h"
#include "CRC.h"
#include "OtaUpgrade.h"
#include "FirmwareVersion.h"
#include "log.h"
}

#ifndef BOARD_WIDTH
#define BOARD_WIDTH 1024
#endif
#ifndef BOARD_HEIGHT
#define BOARD_HEIGHT 600
#endif

namespace {

void setLabel(QLabel *lab, const QString &text)
{
    if (!lab || lab->text() == text)
        return;
    lab->setText(text);
}

void *tel15BusinessStartThread(void *arg)
{
    Tel15MainWindow *w = static_cast<Tel15MainWindow *>(arg);
    const int rc = Tel15_BusinessStart();
    QMetaObject::invokeMethod(w, "onBusinessReady", Qt::QueuedConnection,
                              Q_ARG(int, rc));
    return NULL;
}

void setStatusChip(QLabel *lab, const QString &name, bool ok)
{
    if (!lab)
        return;
    const QString color = ok ? QStringLiteral("#1a7f37") : QStringLiteral("#b42318");
    const QString html = QStringLiteral(
        "<span style=\"color:%1;font-size:20px;\">●</span>&nbsp;%2")
            .arg(color, name);
    if (lab->text() == html)
        return;
    lab->setText(html);
}

QWidget *makeTile(QWidget *parent, const QString &caption, QLabel **value, bool compact)
{
    QFrame *box = new QFrame(parent);
    box->setObjectName(compact ? QStringLiteral("metricTileCompact")
                               : QStringLiteral("metricTile"));
    box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QLabel *cap = new QLabel(caption, box);
    cap->setObjectName(compact ? QStringLiteral("metricCapCompact")
                               : QStringLiteral("metricCap"));
    cap->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    cap->setWordWrap(true);

    QLabel *val = new QLabel(QStringLiteral("--"), box);
    val->setObjectName(compact ? QStringLiteral("metricValCompact")
                               : QStringLiteral("metricVal"));
    val->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    val->setWordWrap(true);

    QVBoxLayout *v = new QVBoxLayout(box);
    v->setContentsMargins(compact ? 6 : 8, compact ? 4 : 10,
                          compact ? 6 : 8, compact ? 4 : 10);
    v->setSpacing(compact ? 2 : 4);
    v->addWidget(cap);
    v->addWidget(val, 1);
    if (value)
        *value = val;
    return box;
}

QFrame *makePageTop(QWidget *page, const QString &title)
{
    QFrame *bar = new QFrame(page);
    bar->setObjectName(QStringLiteral("topBar"));
    QLabel *lab = new QLabel(title, bar);
    lab->setObjectName(QStringLiteral("brand"));
    QHBoxLayout *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(4, 8, 4, 8);
    lay->addWidget(lab);
    lay->addStretch(1);
    return bar;
}

} // namespace

Tel15MainWindow::Tel15MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_root(0),
      m_stack(0),
      m_pageHome(0),
      m_pageMenu(0),
      m_pageLog(0),
      m_pageBlank(0),
      m_pageConf(0),
      m_pageConfEdit(0),
      m_pageOta(0),
      m_sideStack(0),
      m_metricCol(0),
      m_manualPanel(0),
      m_homeBtnBar(0),
      m_blankCover(0),
      m_lblClock(0),
      m_lblUptime(0),
      m_lblBiz(0),
      m_lblLink(0),
      m_lblMeta(0),
      m_valWater(0),
      m_valFlow(0),
      m_valSpeed(0),
      m_valCar(0),
      m_valFish(0),
      m_valRun(0),
      m_lblManualStatus(0),
      m_lblBlankHint(0),
      m_sectionView(0),
      m_log(0),
      m_confView(0),
      m_editServerIp(0),
      m_editServerPort(0),
      m_lblPrefer(0),
      m_lblWebLink(0),
      m_lblWatchdog(0),
      m_confFocusEdit(0),
      m_btnMenu(0),
      m_btnManual(0),
      m_btnExit(0),
      m_btnBackLog(0),
      m_btnBackManual(0),
      m_btnBackOta(0),
      m_lblOtaPercent(0),
      m_lblOtaState(0),
      m_lblOtaDetail(0),
      m_otaProgress(0),
      m_timer(0),
      m_bizOk(false),
      m_otaWasBusy(false),
      m_manualMode(false),
      m_screenBlanked(false),
      m_blankMinutes(5),
      m_preferUplink(QStringLiteral("auto")),
      m_webLinkMode(1),
      m_watchdogEnable(0)
{
    buildUi();
    m_blankMinutes = loadBlankMinutesFromConf();
    refreshBlankButtons();
    m_uptime.start();
    m_idleSince.start();
    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(onRefresh()));
    m_timer->start(1000);
    QTimer::singleShot(0, this, SLOT(onInstallFilters()));
}

Tel15MainWindow::~Tel15MainWindow()
{
}

QPushButton *Tel15MainWindow::makeMenuBtn(const QString &text, QWidget *parent)
{
    QPushButton *btn = new QPushButton(text, parent);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    btn->setMinimumHeight(72);
    return btn;
}

void Tel15MainWindow::buildUi()
{
    m_root = new QWidget(this);
    m_root->setObjectName(QStringLiteral("root"));
    setCentralWidget(m_root);
    m_root->setStyleSheet(QStringLiteral(
        "QWidget#root { background-color:#f3f6f8; color:#1a2330; }"
        "QFrame#topBar { background-color:#ffffff; border:none; border-bottom:3px solid #0b7a7a; }"
        "QLabel#brand { color:#102033; font-size:26px; font-weight:bold; background:transparent; }"
        "QLabel#clock { color:#334155; font-size:20px; font-weight:bold; background:transparent; }"
        "QLabel#statusChip { color:#243447; font-size:18px; font-weight:bold; background:transparent; }"
        "QLabel#uptimeChip { color:#334155; font-size:16px; font-weight:bold; background:transparent; }"
        "QLabel#metaChip { color:#475569; font-size:14px; font-weight:bold; background:transparent; }"
        "QFrame#metricTile { background-color:#ffffff; border:2px solid #c9d4df; border-radius:10px; }"
        "QLabel#metricCap { color:#3b4d61; font-size:18px; font-weight:bold; background:transparent; }"
        "QLabel#metricVal { color:#0b1c2c; font-size:36px; font-weight:bold; background:transparent; }"
        "QFrame#metricTileCompact { background-color:#ffffff; border:2px solid #c9d4df; border-radius:8px; }"
        "QLabel#metricCapCompact { color:#3b4d61; font-size:14px; font-weight:bold; background:transparent; }"
        "QLabel#metricValCompact { color:#0b1c2c; font-size:20px; font-weight:bold; background:transparent; }"
        "QPushButton {"
        "  background-color:#0b7a7a; color:#ffffff; border:none; border-radius:8px;"
        "  padding:8px 16px; font-size:22px; font-weight:bold; min-height:64px;"
        "}"
        "QPushButton:pressed { background-color:#096666; }"
        "QPushButton#dangerBtn { background-color:#b42318; }"
        "QPushButton#dangerBtn:pressed { background-color:#912018; }"
        "QPushButton#backBtn { background-color:#475569; }"
        "QPushButton#backBtn:pressed { background-color:#334155; }"
        "QPushButton#idleOn { background-color:#0b7a7a; }"
        "QPushButton#idleOff { background-color:#94a3b8; }"
        "QWidget#confEditPage QPushButton {"
        "  min-height:40px; max-height:56px; font-size:18px; padding:4px 8px;"
        "}"
        "QWidget#confEditPage QPushButton#confPadKey {"
        "  min-height:0; max-height:16777215; font-size:22px; padding:0;"
        "}"
        "QWidget#confEditPage QPushButton#confCycleBtn {"
        "  min-height:52px; max-height:64px; font-size:16px; padding:4px;"
        "}"
        "QWidget#confEditPage QLineEdit#confEdit {"
        "  min-height:36px; font-size:20px; padding:4px 8px;"
        "}"
        "QWidget#manualPanel { background:transparent; }"
        "QWidget#manualPanel QPushButton {"
        "  min-height:0; max-height:16777215; padding:0; margin:0;"
        "  border:none; border-radius:10px; color:#ffffff;"
        "  font-size:18px; font-weight:bold;"
        "}"
        "QWidget#manualPanel QPushButton#manMove { background-color:#0b7a7a; }"
        "QWidget#manualPanel QPushButton#manMove:pressed { background-color:#096666; }"
        "QWidget#manualPanel QPushButton#manMove:disabled { background-color:#9db5b5; }"
        "QWidget#manualPanel QPushButton#manStop { background-color:#5b6b7c; }"
        "QWidget#manualPanel QPushButton#manStop:pressed { background-color:#455363; }"
        "QWidget#manualPanel QPushButton#manStop:disabled { background-color:#a8b0b8; }"
        "QWidget#manualPanel QPushButton#manExtra { background-color:#1f6f8f; }"
        "QWidget#manualPanel QPushButton#manExtra:pressed { background-color:#185a74; }"
        "QWidget#manualPanel QPushButton#manExtra:disabled { background-color:#9bb4c0; }"
        "QWidget#manualPanel QPushButton#manEstop { background-color:#c0352b; }"
        "QWidget#manualPanel QPushButton#manEstop:pressed { background-color:#9a2a22; }"
        "QWidget#manualPanel QPushButton#manEstop:disabled { background-color:#c49a96; }"
        "QWidget#manualPanel QPushButton#manBack { background-color:#475569; }"
        "QWidget#manualPanel QPushButton#manBack:pressed { background-color:#334155; }"
        "QLabel#manualStatus {"
        "  color:#102033; font-size:15px; font-weight:bold;"
        "  background-color:#ffffff; border:2px solid #c9d4df; border-radius:8px;"
        "  padding:6px 8px; min-height:58px; max-height:64px;"
        "}"
        "QLabel#confFieldLab { color:#3b4d61; font-size:18px; font-weight:bold; }"
        "QLineEdit#confEdit {"
        "  background:#ffffff; border:2px solid #c9d4df; border-radius:8px;"
        "  font-size:22px; font-weight:bold; padding:8px; min-height:44px;"
        "}"
        "QTextEdit#logView, QTextEdit#confView {"
        "  background-color:#ffffff; color:#102033; border:2px solid #c9d4df;"
        "  border-radius:8px; font-size:18px; padding:8px;"
        "}"
        "QLabel#otaPercent { color:#0b7a7a; font-size:72px; font-weight:bold; background:transparent; }"
        "QLabel#otaState { color:#102033; font-size:28px; font-weight:bold; background:transparent; }"
        "QLabel#otaDetail { color:#3b4d61; font-size:20px; background:transparent; }"
        "QProgressBar#otaBar {"
        "  border:2px solid #9db0c0; border-radius:10px; background:#dce5ec;"
        "  text-align:center; color:#102033; font-size:18px; font-weight:bold; min-height:32px;"
        "}"
        "QProgressBar#otaBar::chunk { background-color:#0b7a7a; border-radius:8px; margin:2px; }"
        "QWidget#blankCover { background-color:#000000; }"
    ));

    QVBoxLayout *outer = new QVBoxLayout(m_root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_stack = new QStackedWidget(m_root);
    outer->addWidget(m_stack, 1);

    /* ---------- Home ---------- */
    m_pageHome = new QWidget(m_stack);
    QVBoxLayout *home = new QVBoxLayout(m_pageHome);
    home->setContentsMargins(12, 0, 12, 10);
    home->setSpacing(10);

    QFrame *topBar = new QFrame(m_pageHome);
    topBar->setObjectName(QStringLiteral("topBar"));
    topBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QLabel *brand = new QLabel(QStringLiteral("渠道测流"), topBar);
    brand->setObjectName(QStringLiteral("brand"));
    m_lblBiz = new QLabel(topBar);
    m_lblBiz->setObjectName(QStringLiteral("statusChip"));
    m_lblBiz->setTextFormat(Qt::RichText);
    m_lblLink = new QLabel(topBar);
    m_lblLink->setObjectName(QStringLiteral("statusChip"));
    m_lblLink->setTextFormat(Qt::RichText);
    setStatusChip(m_lblBiz, QStringLiteral("业务"), false);
    setStatusChip(m_lblLink, QStringLiteral("链路"), false);
    m_lblMeta = new QLabel(QStringLiteral("断面 --  Ver --"), topBar);
    m_lblMeta->setObjectName(QStringLiteral("metaChip"));
    m_lblUptime = new QLabel(QStringLiteral("运行 00:00:00"), topBar);
    m_lblUptime->setObjectName(QStringLiteral("uptimeChip"));
    m_lblClock = new QLabel(topBar);
    m_lblClock->setObjectName(QStringLiteral("clock"));

    QHBoxLayout *topLay = new QHBoxLayout(topBar);
    topLay->setContentsMargins(4, 8, 4, 8);
    topLay->setSpacing(12);
    topLay->addWidget(brand);
    topLay->addWidget(m_lblBiz);
    topLay->addWidget(m_lblLink);
    topLay->addWidget(m_lblMeta);
    topLay->addStretch(1);
    topLay->addWidget(m_lblUptime);
    topLay->addWidget(m_lblClock);
    home->addWidget(topBar);

    m_sectionView = new SectionView(m_pageHome);
    m_sideStack = new QStackedWidget(m_pageHome);
    m_sideStack->setMinimumWidth(300);
    m_sideStack->setMaximumWidth(320);

    m_metricCol = new QWidget(m_sideStack);
    QVBoxLayout *metricLay = new QVBoxLayout(m_metricCol);
    metricLay->setContentsMargins(0, 0, 0, 0);
    metricLay->setSpacing(6);
    metricLay->addWidget(makeTile(m_metricCol, QStringLiteral("水位 (m)"), &m_valWater, true), 1);
    metricLay->addWidget(makeTile(m_metricCol, QStringLiteral("流量 (m³/s)"), &m_valFlow, true), 1);
    metricLay->addWidget(makeTile(m_metricCol, QStringLiteral("流速 (m/s)"), &m_valSpeed, true), 1);
    metricLay->addWidget(makeTile(m_metricCol, QStringLiteral("小车 (m)"), &m_valCar, true), 1);
    metricLay->addWidget(makeTile(m_metricCol, QStringLiteral("铅鱼 (m)"), &m_valFish, true), 1);
    metricLay->addWidget(makeTile(m_metricCol, QStringLiteral("运行状态"), &m_valRun, true), 1);

    m_manualPanel = new QWidget(m_sideStack);
    m_manualPanel->setObjectName(QStringLiteral("manualPanel"));
    QVBoxLayout *manLay = new QVBoxLayout(m_manualPanel);
    manLay->setContentsMargins(0, 0, 0, 0);
    manLay->setSpacing(8);
    m_lblManualStatus = new QLabel(QStringLiteral("水位 --\n小车 --  铅鱼 --"), m_manualPanel);
    m_lblManualStatus->setObjectName(QStringLiteral("manualStatus"));
    m_lblManualStatus->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lblManualStatus->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    manLay->addWidget(m_lblManualStatus);

    QGridLayout *manGrid = new QGridLayout;
    manGrid->setContentsMargins(0, 0, 0, 0);
    manGrid->setHorizontalSpacing(8);
    manGrid->setVerticalSpacing(8);
    struct ManBtn {
        const char *text;
        const char *obj;
        int addr;
        int row;
        int col;
    };
    const ManBtn manBtns[] = {
        { "前进", "manMove", TOUCH_FORWARD, 0, 0 },
        { "停小车", "manStop", TOUCH_CAR_STOP, 0, 1 },
        { "后退", "manMove", TOUCH_BACKWARD, 0, 2 },
        { "上升", "manMove", TOUCH_UP, 1, 0 },
        { "停铅鱼", "manStop", TOUCH_FISH_STOP, 1, 1 },
        { "下降", "manMove", TOUCH_DOWN, 1, 2 },
        { "加测", "manExtra", TOUCH_ADDMEARESE, 2, 0 },
        { "急停", "manEstop", TOUCH_RESTART, 2, 1 },
        { "返回", "manBack", -1, 2, 2 },
    };
    for (unsigned i = 0; i < sizeof(manBtns) / sizeof(manBtns[0]); ++i) {
        QPushButton *btn = new QPushButton(QString::fromUtf8(manBtns[i].text), m_manualPanel);
        btn->setObjectName(QString::fromLatin1(manBtns[i].obj));
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        btn->setMinimumSize(0, 0);
        if (manBtns[i].addr < 0) {
            m_btnBackManual = btn;
            connect(btn, SIGNAL(clicked()), this, SLOT(onBackHome()));
        } else {
            btn->setEnabled(false);
            btn->setProperty("touchAddr", manBtns[i].addr);
            connect(btn, SIGNAL(clicked()), this, SLOT(onManualCmd()));
        }
        manGrid->addWidget(btn, manBtns[i].row, manBtns[i].col);
    }
    for (int c = 0; c < 3; ++c)
        manGrid->setColumnStretch(c, 1);
    for (int r = 0; r < 3; ++r)
        manGrid->setRowStretch(r, 1);
    manLay->addLayout(manGrid, 1);

    m_sideStack->addWidget(m_metricCol);
    m_sideStack->addWidget(m_manualPanel);
    m_sideStack->setCurrentWidget(m_metricCol);

    QHBoxLayout *mainRow = new QHBoxLayout;
    mainRow->setContentsMargins(0, 0, 0, 0);
    mainRow->setSpacing(10);
    mainRow->addWidget(m_sectionView, 1);
    mainRow->addWidget(m_sideStack, 0);
    home->addLayout(mainRow, 1);

    m_homeBtnBar = new QWidget(m_pageHome);
    m_btnManual = new QPushButton(QStringLiteral("手动"), m_homeBtnBar);
    m_btnMenu = new QPushButton(QStringLiteral("菜单"), m_homeBtnBar);
    m_btnExit = new QPushButton(QStringLiteral("退出"), m_homeBtnBar);
    m_btnExit->setObjectName(QStringLiteral("dangerBtn"));
    QHBoxLayout *btns = new QHBoxLayout(m_homeBtnBar);
    btns->setContentsMargins(0, 0, 0, 0);
    btns->setSpacing(10);
    btns->addWidget(m_btnManual, 1);
    btns->addWidget(m_btnMenu, 1);
    btns->addWidget(m_btnExit, 1);
    home->addWidget(m_homeBtnBar);

    /* ---------- Menu ---------- */
    m_pageMenu = new QWidget(m_stack);
    {
        QVBoxLayout *vl = new QVBoxLayout(m_pageMenu);
        vl->setContentsMargins(12, 0, 12, 10);
        vl->setSpacing(10);
        vl->addWidget(makePageTop(m_pageMenu, QStringLiteral("功能菜单")));
        QGridLayout *g = new QGridLayout;
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(10);
        QPushButton *bLog = makeMenuBtn(QStringLiteral("运行日志"), m_pageMenu);
        QPushButton *bBlank = makeMenuBtn(QStringLiteral("息屏设置"), m_pageMenu);
        QPushButton *bConf = makeMenuBtn(QStringLiteral("配置文件"), m_pageMenu);
        QPushButton *bReboot = makeMenuBtn(QStringLiteral("重启设备"), m_pageMenu);
        bReboot->setObjectName(QStringLiteral("dangerBtn"));
        QPushButton *bBack = makeMenuBtn(QStringLiteral("返回首页"), m_pageMenu);
        bBack->setObjectName(QStringLiteral("backBtn"));
        g->addWidget(bLog, 0, 0);
        g->addWidget(bBlank, 0, 1);
        g->addWidget(bConf, 1, 0);
        g->addWidget(bReboot, 1, 1);
        g->addWidget(bBack, 2, 0, 1, 2);
        for (int r = 0; r < 3; ++r)
            g->setRowStretch(r, 1);
        for (int c = 0; c < 2; ++c)
            g->setColumnStretch(c, 1);
        vl->addLayout(g, 1);
        connect(bLog, SIGNAL(clicked()), this, SLOT(onShowLogPage()));
        connect(bBlank, SIGNAL(clicked()), this, SLOT(onShowBlankPage()));
        connect(bConf, SIGNAL(clicked()), this, SLOT(onShowConfPage()));
        connect(bReboot, SIGNAL(clicked()), this, SLOT(onRebootDevice()));
        connect(bBack, SIGNAL(clicked()), this, SLOT(onBackHome()));
    }

    /* ---------- Log ---------- */
    m_pageLog = new QWidget(m_stack);
    {
        QVBoxLayout *logLay = new QVBoxLayout(m_pageLog);
        logLay->setContentsMargins(12, 0, 12, 10);
        logLay->setSpacing(10);
        logLay->addWidget(makePageTop(m_pageLog, QStringLiteral("运行日志")));
        m_log = new QTextEdit(m_pageLog);
        m_log->setObjectName(QStringLiteral("logView"));
        m_log->setReadOnly(true);
        m_btnBackLog = new QPushButton(QStringLiteral("返回"), m_pageLog);
        m_btnBackLog->setObjectName(QStringLiteral("backBtn"));
        logLay->addWidget(m_log, 1);
        logLay->addWidget(m_btnBackLog);
        connect(m_btnBackLog, SIGNAL(clicked()), this, SLOT(onShowMenuPage()));
    }

    /* ---------- Blank settings ---------- */
    m_pageBlank = new QWidget(m_stack);
    {
        QVBoxLayout *vl = new QVBoxLayout(m_pageBlank);
        vl->setContentsMargins(12, 0, 12, 10);
        vl->setSpacing(10);
        vl->addWidget(makePageTop(m_pageBlank, QStringLiteral("息屏设置")));
        m_lblBlankHint = new QLabel(m_pageBlank);
        m_lblBlankHint->setWordWrap(true);
        m_lblBlankHint->setObjectName(QStringLiteral("metaChip"));
        vl->addWidget(m_lblBlankHint);
        QHBoxLayout *row = new QHBoxLayout;
        row->setSpacing(10);
        const int mins[] = { 0, 1, 3, 5 };
        const char *labels[] = { "不息屏", "1 分钟", "3 分钟", "5 分钟" };
        for (int i = 0; i < 4; ++i) {
            QPushButton *btn = new QPushButton(QString::fromUtf8(labels[i]), m_pageBlank);
            btn->setProperty("blankMin", mins[i]);
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            connect(btn, SIGNAL(clicked()), this, SLOT(onBlankMinutesChosen()));
            m_btnBlankMin.append(btn);
            row->addWidget(btn, 1);
        }
        vl->addLayout(row, 1);
        QPushButton *back = new QPushButton(QStringLiteral("返回"), m_pageBlank);
        back->setObjectName(QStringLiteral("backBtn"));
        vl->addWidget(back);
        connect(back, SIGNAL(clicked()), this, SLOT(onShowMenuPage()));
    }

    /* ---------- Conf view ---------- */
    m_pageConf = new QWidget(m_stack);
    {
        QVBoxLayout *vl = new QVBoxLayout(m_pageConf);
        vl->setContentsMargins(12, 0, 12, 10);
        vl->setSpacing(10);
        vl->addWidget(makePageTop(m_pageConf, QStringLiteral("配置文件")));
        m_confView = new QTextEdit(m_pageConf);
        m_confView->setObjectName(QStringLiteral("confView"));
        m_confView->setReadOnly(true);
        QPushButton *bEdit = new QPushButton(QStringLiteral("修改关键项"), m_pageConf);
        QPushButton *back = new QPushButton(QStringLiteral("返回"), m_pageConf);
        back->setObjectName(QStringLiteral("backBtn"));
        QHBoxLayout *row = new QHBoxLayout;
        row->addWidget(bEdit, 1);
        row->addWidget(back, 1);
        vl->addWidget(m_confView, 1);
        vl->addLayout(row);
        connect(bEdit, SIGNAL(clicked()), this, SLOT(onShowConfEditPage()));
        connect(back, SIGNAL(clicked()), this, SLOT(onShowMenuPage()));
    }

    /* ---------- Conf edit ---------- */
    m_pageConfEdit = new QWidget(m_stack);
    m_pageConfEdit->setObjectName(QStringLiteral("confEditPage"));
    {
        QVBoxLayout *vl = new QVBoxLayout(m_pageConfEdit);
        vl->setContentsMargins(12, 4, 12, 8);
        vl->setSpacing(6);
        vl->addWidget(makePageTop(m_pageConfEdit, QStringLiteral("修改关键项")));

        QGridLayout *form = new QGridLayout;
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(6);
        QLabel *l1 = new QLabel(QStringLiteral("服务器 IP"), m_pageConfEdit);
        l1->setObjectName(QStringLiteral("confFieldLab"));
        m_editServerIp = new QLineEdit(m_pageConfEdit);
        m_editServerIp->setObjectName(QStringLiteral("confEdit"));
        QLabel *l2 = new QLabel(QStringLiteral("端口"), m_pageConfEdit);
        l2->setObjectName(QStringLiteral("confFieldLab"));
        m_editServerPort = new QLineEdit(m_pageConfEdit);
        m_editServerPort->setObjectName(QStringLiteral("confEdit"));
        form->addWidget(l1, 0, 0);
        form->addWidget(m_editServerIp, 0, 1);
        form->addWidget(l2, 1, 0);
        form->addWidget(m_editServerPort, 1, 1);
        form->setColumnStretch(1, 1);
        vl->addLayout(form);

        QHBoxLayout *cycles = new QHBoxLayout;
        cycles->setContentsMargins(0, 0, 0, 0);
        cycles->setSpacing(8);
        QPushButton *bPrefer = new QPushButton(m_pageConfEdit);
        QPushButton *bWeb = new QPushButton(m_pageConfEdit);
        QPushButton *bDog = new QPushButton(m_pageConfEdit);
        bPrefer->setObjectName(QStringLiteral("confCycleBtn"));
        bWeb->setObjectName(QStringLiteral("confCycleBtn"));
        bDog->setObjectName(QStringLiteral("confCycleBtn"));
        bPrefer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        bWeb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        bDog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        /* 用隐藏 label 存当前值，按钮文案在 refreshConfCycleButtons 里刷新 */
        m_lblPrefer = new QLabel(m_pageConfEdit);
        m_lblWebLink = new QLabel(m_pageConfEdit);
        m_lblWatchdog = new QLabel(m_pageConfEdit);
        m_lblPrefer->hide();
        m_lblWebLink->hide();
        m_lblWatchdog->hide();
        bPrefer->setProperty("cycleKind", QStringLiteral("prefer"));
        bWeb->setProperty("cycleKind", QStringLiteral("weblink"));
        bDog->setProperty("cycleKind", QStringLiteral("watchdog"));
        cycles->addWidget(bPrefer, 1);
        cycles->addWidget(bWeb, 1);
        cycles->addWidget(bDog, 1);
        vl->addLayout(cycles);
        connect(bPrefer, SIGNAL(clicked()), this, SLOT(onCyclePreferUplink()));
        connect(bWeb, SIGNAL(clicked()), this, SLOT(onCycleWebLink()));
        connect(bDog, SIGNAL(clicked()), this, SLOT(onCycleWatchdog()));
        m_editServerIp->installEventFilter(this);
        m_editServerPort->installEventFilter(this);

        QWidget *padHost = new QWidget(m_pageConfEdit);
        QGridLayout *pad = new QGridLayout(padHost);
        pad->setContentsMargins(0, 0, 0, 0);
        pad->setHorizontalSpacing(6);
        pad->setVerticalSpacing(6);
        const char *keys[] = {
            "1", "2", "3",
            "4", "5", "6",
            "7", "8", "9",
            ".", "0", "删"
        };
        for (int i = 0; i < 12; ++i) {
            QPushButton *k = new QPushButton(QString::fromUtf8(keys[i]), padHost);
            k->setObjectName(QStringLiteral("confPadKey"));
            k->setProperty("padKey", QString::fromUtf8(keys[i]));
            k->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            k->setMinimumSize(0, 0);
            connect(k, SIGNAL(clicked()), this, SLOT(onConfPad()));
            pad->addWidget(k, i / 3, i % 3);
        }
        for (int c = 0; c < 3; ++c)
            pad->setColumnStretch(c, 1);
        for (int r = 0; r < 4; ++r)
            pad->setRowStretch(r, 1);
        vl->addWidget(padHost, 1);

        QHBoxLayout *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(10);
        QPushButton *save = new QPushButton(QStringLiteral("保存"), m_pageConfEdit);
        QPushButton *back = new QPushButton(QStringLiteral("返回"), m_pageConfEdit);
        back->setObjectName(QStringLiteral("backBtn"));
        save->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        back->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->addWidget(save, 1);
        row->addWidget(back, 1);
        vl->addLayout(row);
        connect(save, SIGNAL(clicked()), this, SLOT(onConfSave()));
        connect(back, SIGNAL(clicked()), this, SLOT(onShowConfPage()));
        m_confFocusEdit = m_editServerIp;
    }

    /* ---------- OTA ---------- */
    m_pageOta = new QWidget(m_stack);
    {
        QVBoxLayout *otaLay = new QVBoxLayout(m_pageOta);
        otaLay->setContentsMargins(12, 0, 12, 10);
        otaLay->setSpacing(12);
        QFrame *otaBar = new QFrame(m_pageOta);
        otaBar->setObjectName(QStringLiteral("topBar"));
        QLabel *otaTitle = new QLabel(QStringLiteral("固件升级"), otaBar);
        otaTitle->setObjectName(QStringLiteral("brand"));
        m_btnBackOta = new QPushButton(QStringLiteral("返回"), otaBar);
        m_btnBackOta->setObjectName(QStringLiteral("backBtn"));
        m_btnBackOta->setVisible(false);
        QHBoxLayout *otaTop = new QHBoxLayout(otaBar);
        otaTop->setContentsMargins(4, 8, 4, 8);
        otaTop->addWidget(otaTitle);
        otaTop->addStretch(1);
        otaTop->addWidget(m_btnBackOta);
        m_lblOtaPercent = new QLabel(QStringLiteral("0%"), m_pageOta);
        m_lblOtaPercent->setObjectName(QStringLiteral("otaPercent"));
        m_lblOtaPercent->setAlignment(Qt::AlignCenter);
        m_lblOtaState = new QLabel(QStringLiteral("等待网页下发固件"), m_pageOta);
        m_lblOtaState->setObjectName(QStringLiteral("otaState"));
        m_lblOtaState->setAlignment(Qt::AlignCenter);
        m_otaProgress = new QProgressBar(m_pageOta);
        m_otaProgress->setObjectName(QStringLiteral("otaBar"));
        m_otaProgress->setRange(0, 100);
        m_otaProgress->setValue(0);
        m_otaProgress->setTextVisible(false);
        m_lblOtaDetail = new QLabel(QStringLiteral("升级过程中请勿断电"), m_pageOta);
        m_lblOtaDetail->setObjectName(QStringLiteral("otaDetail"));
        m_lblOtaDetail->setAlignment(Qt::AlignCenter);
        m_lblOtaDetail->setWordWrap(true);
        otaLay->addWidget(otaBar);
        otaLay->addStretch(1);
        otaLay->addWidget(m_lblOtaPercent);
        otaLay->addWidget(m_lblOtaState);
        otaLay->addWidget(m_otaProgress);
        otaLay->addWidget(m_lblOtaDetail);
        otaLay->addStretch(1);
        connect(m_btnBackOta, SIGNAL(clicked()), this, SLOT(onBackHome()));
    }

    m_stack->addWidget(m_pageHome);
    m_stack->addWidget(m_pageMenu);
    m_stack->addWidget(m_pageLog);
    m_stack->addWidget(m_pageBlank);
    m_stack->addWidget(m_pageConf);
    m_stack->addWidget(m_pageConfEdit);
    m_stack->addWidget(m_pageOta);
    m_stack->setCurrentWidget(m_pageHome);

    m_blankCover = new QWidget(this);
    m_blankCover->setObjectName(QStringLiteral("blankCover"));
    m_blankCover->hide();
    m_blankCover->setGeometry(0, 0, BOARD_WIDTH, BOARD_HEIGHT);

    connect(m_btnManual, SIGNAL(clicked()), this, SLOT(onShowManualPage()));
    connect(m_btnMenu, SIGNAL(clicked()), this, SLOT(onShowMenuPage()));
    connect(m_btnExit, SIGNAL(clicked()), this, SLOT(onExit()));

    refreshBlankButtons();
    appendUiLog(QStringLiteral("界面已显示，等待启动业务…"));
}

void Tel15MainWindow::onInstallFilters()
{
    if (m_blankCover)
        m_blankCover->installEventFilter(this);
    qApp->installEventFilter(this);
}

void Tel15MainWindow::appendUiLog(const QString &line)
{
    if (!m_log)
        return;
    const QString stamped = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"))
            + QStringLiteral("  ") + line;
    m_log->append(stamped);
    if (m_log->document()->blockCount() > 200)
        m_log->clear();
}

void Tel15MainWindow::setBizEnabled(bool on)
{
    m_bizOk = on;
    if (!m_manualPanel)
        return;
    const QList<QPushButton *> btns = m_manualPanel->findChildren<QPushButton *>();
    for (int i = 0; i < btns.size(); ++i) {
        if (btns.at(i)->objectName() == QLatin1String("manBack"))
            continue;
        btns.at(i)->setEnabled(on);
    }
}

void Tel15MainWindow::setManualMode(bool on)
{
    m_manualMode = on;
    if (m_sideStack)
        m_sideStack->setCurrentWidget(on ? m_manualPanel : m_metricCol);
    if (m_homeBtnBar)
        m_homeBtnBar->setVisible(!on);
    if (on && m_stack)
        m_stack->setCurrentWidget(m_pageHome);
}

QString Tel15MainWindow::measureRunText() const
{
    const AddMeasureState st = g_addMeasureState;
    const int cur = g_addMeasureCtx.currentLineIndex + 1;
    const int tot = g_addMeasureCtx.totalLineCount;
    QString lineTag;
    if (tot > 0 && cur > 0)
        lineTag = QStringLiteral(" %1/%2").arg(cur > tot ? tot : cur).arg(tot);

    switch (st) {
    case FSM_IDLE:
        return g_bRunning_State ? QStringLiteral("测流中") : QStringLiteral("待机");
    case DOOR_OPEN_WAIT:
        return QStringLiteral("开门中");
    case FSM_START:
        return QStringLiteral("启动中");
    case FISH_UP_TO_TOP_INIT:
    case CAR_BACK_TO_HOME_INIT:
        return QStringLiteral("回零中");
    case CAR_MOVE_TO_LINE:
        return QStringLiteral("前往测线") + lineTag;
    case FISH_DOWN_TO_DEPTH:
        return QStringLiteral("下放铅鱼") + lineTag;
    case MEASURE_WAIT_DURATION:
    case MEASURE_POST_READ:
        return QStringLiteral("转子测流") + lineTag;
    case MEASURE_NCS:
        return QStringLiteral("非接触测流") + lineTag;
    case NEXT_DEPTH:
        return QStringLiteral("换测点") + lineTag;
    case FISH_UP_AFTER_LINE:
        return QStringLiteral("收铅鱼") + lineTag;
    case NEXT_LINE:
        return QStringLiteral("换测线") + lineTag;
    case ALL_LINE_DONE:
    case FISH_UP_TO_TOP_FINAL:
    case CAR_BACK_TO_HOME_FINAL:
    case FISH_DOWN_TO_PLATFORM:
        return QStringLiteral("收仓中");
    case SEND_RESULT_FINAL:
        return QStringLiteral("上报结果");
    case FSM_PAUSED:
        return QStringLiteral("已暂停");
    case FSM_COMPLETE:
        return QStringLiteral("已完成");
    case FSM_ERROR:
        return QStringLiteral("故障");
    default:
        return g_bRunning_State ? QStringLiteral("测流中") : QStringLiteral("待机");
    }
}

void Tel15MainWindow::updateMetaLabels()
{
    char fw[32] = {0};
    FirmwareVersion_Format(fw, sizeof(fw));
    uint16_t seri = g_stSection_new.u16SectionSeri;
    uint16_t num = g_stSection_new.u16SectionNum;
    if (num > MAX_LINE_NUM) {
        seri = swap_u16(seri);
        num = swap_u16(num);
    }
    const QString meta = (num >= 2)
            ? QStringLiteral("断面#%1(%2点)  Ver %3")
                  .arg(seri).arg(num).arg(QString::fromLatin1(fw))
            : QStringLiteral("断面未下发  Ver %1").arg(QString::fromLatin1(fw));
    setLabel(m_lblMeta, meta);
}

void Tel15MainWindow::onShowMenuPage()
{
    noteUserActivity();
    setManualMode(false);
    m_stack->setCurrentWidget(m_pageMenu);
}

void Tel15MainWindow::onShowLogPage()
{
    noteUserActivity();
    setManualMode(false);
    m_stack->setCurrentWidget(m_pageLog);
}

void Tel15MainWindow::onShowManualPage()
{
    noteUserActivity();
    setManualMode(true);
}

void Tel15MainWindow::onShowBlankPage()
{
    noteUserActivity();
    refreshBlankButtons();
    const QString hint = (m_blankMinutes <= 0)
            ? QStringLiteral("当前：不息屏。点屏幕任意处可亮屏；升级中不息屏。")
            : QStringLiteral("当前：无操作 %1 分钟后息屏。点屏幕任意处亮屏；升级中不息屏。")
                  .arg(m_blankMinutes);
    setLabel(m_lblBlankHint, hint);
    m_stack->setCurrentWidget(m_pageBlank);
}

void Tel15MainWindow::onShowConfPage()
{
    noteUserActivity();
    if (m_confView)
        m_confView->setPlainText(loadConfText());
    m_stack->setCurrentWidget(m_pageConf);
}

void Tel15MainWindow::onShowConfEditPage()
{
    noteUserActivity();
    loadConfEditFields();
    m_confFocusEdit = m_editServerIp;
    m_stack->setCurrentWidget(m_pageConfEdit);
}

void Tel15MainWindow::onManualCmd()
{
    noteUserActivity();
    if (!m_bizOk)
        return;
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;
    const int addr = btn->property("touchAddr").toInt();
    appendUiLog(QStringLiteral("手动：%1").arg(btn->text()));
    ProcTouchCommand(swap_u16(static_cast<uint16_t>(addr)));
}

void Tel15MainWindow::onBackHome()
{
    noteUserActivity();
    setManualMode(false);
    m_stack->setCurrentWidget(m_pageHome);
}

void Tel15MainWindow::onExit()
{
    noteUserActivity();
    BoardBuzzer::forceOff();
    BoardDisplay::unblank();
    ::_exit(0);
}

void Tel15MainWindow::onRebootDevice()
{
    noteUserActivity();
    const int ret = QMessageBox::question(
        this, QStringLiteral("重启设备"),
        QStringLiteral("确认重启整机？未保存的运行状态将丢失。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;
    appendUiLog(QStringLiteral("用户确认重启设备"));
    BoardBuzzer::forceOff();
    BoardDisplay::unblank();
    sync();
    reboot(RB_AUTOBOOT);
}

void Tel15MainWindow::onStartBusiness()
{
    appendUiLog(QStringLiteral("正在初始化串口 / 定时器 / 链路…"));
    pthread_t th;
    if (pthread_create(&th, NULL, tel15BusinessStartThread, this) != 0) {
        appendUiLog(QStringLiteral("无法创建业务线程，改为同步启动"));
        onBusinessReady(Tel15_BusinessStart());
        return;
    }
    pthread_detach(th);
}

void Tel15MainWindow::onBusinessReady(int result)
{
    if (result == 0) {
        setBizEnabled(true);
        appendUiLog(QStringLiteral("业务启动成功"));
        FirmwareVersion_ReportBoot(NULL);
        {
            char fw[32] = {0};
            FirmwareVersion_Format(fw, sizeof(fw));
            appendUiLog(QStringLiteral("固件版本: %1").arg(QString::fromLatin1(fw)));
        }
    } else {
        setBizEnabled(false);
        appendUiLog(QStringLiteral("业务启动失败，请检查 tyls.conf 与串口"));
    }
    updateMetaLabels();
    onRefresh();
}

void Tel15MainWindow::onRefresh()
{
    setLabel(m_lblClock,
             QDateTime::currentDateTime().toString(QStringLiteral("MM-dd  hh:mm:ss")));
    const int sec = static_cast<int>(m_uptime.elapsed() / 1000);
    setLabel(m_lblUptime,
             QStringLiteral("运行 %1:%2:%3")
                 .arg(sec / 3600, 2, 10, QChar('0'))
                 .arg((sec % 3600) / 60, 2, 10, QChar('0'))
                 .arg(sec % 60, 2, 10, QChar('0')));

    bool linkOk = false;
    if (m_bizOk) {
        if (WebLink_IsSocketMode())
            linkOk = SocketLink_IsConnected() != 0;
        else
            linkOk = true;
    }
    setStatusChip(m_lblBiz, QStringLiteral("业务"), m_bizOk);
    setStatusChip(m_lblLink, QStringLiteral("链路"), linkOk);
    updateMetaLabels();

    updateOtaUi();

    if (!m_screenBlanked && !m_otaWasBusy && m_blankMinutes >= 1) {
        if (m_idleSince.elapsed() >= m_blankMinutes * 60LL * 1000LL)
            blankScreen();
    }

    if (m_sectionView) {
        if (m_bizOk && !m_otaWasBusy) {
            const double waterM = g_stTouchScreenReportData.u16WaterLevelData / 100.0;
            const double carM = GetCarHoriPosCm() / 100.0;
            const double fishM = GetFishInwaterDeepForTouchScreen() / 100.0;
            m_sectionView->setLiveData(waterM, true, carM, true, fishM);
        } else if (!m_bizOk) {
            m_sectionView->setLiveData(0.0, false, 0.0, false, 0.0);
        }
    }
    if (m_otaWasBusy || !m_bizOk)
        return;

    const QString runText = measureRunText();
    setLabel(m_valWater,
             QString::number(g_stTouchScreenReportData.u16WaterLevelData / 100.0, 'f', 2));
    setLabel(m_valFlow,
             QString::number(g_stTouchScreenReportData.u16WaterFlow / 1000.0, 'f', 3));
    setLabel(m_valSpeed,
             QString::number(g_stTouchScreenReportData.u16SpeedData / 1000.0, 'f', 3));
    {
        const int32_t carCm = (int32_t)(int16_t)GetCarHoriPosForTouchScreen();
        setLabel(m_valCar, QString::number(carCm / 100.0, 'f', 2));
    }
    setLabel(m_valFish,
             QString::number(GetFishInwaterDeepForTouchScreen() / 100.0, 'f', 2));
    setLabel(m_valRun, runText);
    if (m_lblManualStatus) {
        const QString status = QStringLiteral("水位 %1 m    %4\n小车 %2 m    铅鱼 %3 m")
                .arg(g_stTouchScreenReportData.u16WaterLevelData / 100.0, 0, 'f', 2)
                .arg(((int32_t)(int16_t)GetCarHoriPosForTouchScreen()) / 100.0, 0, 'f', 2)
                .arg(GetFishInwaterDeepForTouchScreen() / 100.0, 0, 'f', 2)
                .arg(runText);
        setLabel(m_lblManualStatus, status);
    }
}

void Tel15MainWindow::updateOtaUi()
{
    if (!m_pageOta || !m_lblOtaPercent || !m_otaProgress)
        return;

    const bool busy = OtaUpgrade_IsBusy() != 0;
    uint16_t pktExpect = 0;
    uint16_t pktTotal = 0;
    uint32_t recvSize = 0;
    int pct = 0;

    if (busy)
        OtaUpgrade_GetProgress(&pktExpect, &pktTotal, &recvSize);
    if (pktTotal > 0) {
        unsigned done = (pktExpect > 0) ? (unsigned)(pktExpect - 1U) : 0U;
        if (done > pktTotal)
            done = pktTotal;
        pct = (int)((done * 100U) / pktTotal);
    }

    if (busy) {
        if (!m_otaWasBusy) {
            m_otaWasBusy = true;
            setManualMode(false);
            unblankScreen();
            appendUiLog(QStringLiteral("OTA 开始升级"));
        }
        if (m_btnBackOta)
            m_btnBackOta->setVisible(false);
        if (m_stack->currentWidget() != m_pageOta)
            m_stack->setCurrentWidget(m_pageOta);
        setLabel(m_lblOtaPercent, QStringLiteral("%1%").arg(pct));
        setLabel(m_lblOtaState, QStringLiteral("正在升级，请勿断电"));
        if (pktTotal > 0) {
            setLabel(m_lblOtaDetail,
                     QStringLiteral("已收 %1 / %2 包")
                         .arg(pktExpect > 0 ? pktExpect - 1 : 0)
                         .arg(pktTotal));
        } else {
            setLabel(m_lblOtaDetail,
                     QStringLiteral("已接收 %1 字节").arg(recvSize));
        }
        m_otaProgress->setValue(pct);
        return;
    }

    if (m_otaWasBusy) {
        m_otaWasBusy = false;
        appendUiLog(QStringLiteral("OTA 会话结束"));
        setLabel(m_lblOtaState, QStringLiteral("升级结束"));
        setLabel(m_lblOtaDetail, QStringLiteral("若未自动重启，可返回首页"));
        if (m_btnBackOta)
            m_btnBackOta->setVisible(true);
    }
}

void Tel15MainWindow::noteUserActivity()
{
    m_idleSince.restart();
    if (m_screenBlanked)
        unblankScreen();
}

void Tel15MainWindow::blankScreen()
{
    if (m_screenBlanked || m_otaWasBusy)
        return;
    m_screenBlanked = true;
    BoardDisplay::blank();
    if (m_blankCover) {
        m_blankCover->setGeometry(rect());
        m_blankCover->show();
        m_blankCover->raise();
    }
}

void Tel15MainWindow::unblankScreen()
{
    BoardDisplay::unblank();
    if (m_blankCover)
        m_blankCover->hide();
    m_screenBlanked = false;
    m_idleSince.restart();
}

void Tel15MainWindow::refreshBlankButtons()
{
    for (int i = 0; i < m_btnBlankMin.size(); ++i) {
        QPushButton *btn = m_btnBlankMin.at(i);
        if (!btn)
            continue;
        const int mins = btn->property("blankMin").toInt();
        btn->setObjectName(mins == m_blankMinutes ? QStringLiteral("idleOn")
                                                   : QStringLiteral("idleOff"));
        btn->setStyle(btn->style()); /* 触发样式刷新，避免 unpolish 空指针 */
        btn->update();
    }
}

void Tel15MainWindow::applyBlankMinutes(int minutes, bool save)
{
    if (minutes < 0)
        minutes = 0;
    if (minutes > 5)
        minutes = 5;
    m_blankMinutes = minutes;
    refreshBlankButtons();
    m_idleSince.restart();
    if (save) {
        if (saveConfKey(QStringLiteral("ScreenBlankMinutes"), QString::number(minutes)))
            appendUiLog(minutes <= 0 ? QStringLiteral("息屏：已关闭")
                                     : QStringLiteral("息屏：%1 分钟").arg(minutes));
        else
            appendUiLog(QStringLiteral("息屏设置保存失败"));
    }
    const QString hint = (m_blankMinutes <= 0)
            ? QStringLiteral("当前：不息屏。点屏幕任意处可亮屏；升级中不息屏。")
            : QStringLiteral("当前：无操作 %1 分钟后息屏。点屏幕任意处亮屏；升级中不息屏。")
                  .arg(m_blankMinutes);
    setLabel(m_lblBlankHint, hint);
}

void Tel15MainWindow::onBlankMinutesChosen()
{
    noteUserActivity();
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;
    applyBlankMinutes(btn->property("blankMin").toInt(), true);
}

QString Tel15MainWindow::confFilePath() const
{
    char dir[MAX_PATH];
    memset(dir, 0, sizeof(dir));
    if (!GetAppDir(dir))
        return QString();
    return QString::fromLocal8Bit(dir) + QStringLiteral("/tyls.conf");
}

QString Tel15MainWindow::loadConfText() const
{
    QFile f(confFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("无法打开 tyls.conf");
    return QString::fromUtf8(f.readAll());
}

int Tel15MainWindow::loadBlankMinutesFromConf() const
{
    const QString text = loadConfText();
    const QStringList lines = text.split(QChar('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i).trimmed();
        if (line.startsWith(QLatin1Char('#')) || line.isEmpty())
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        if (line.left(eq).trimmed().compare(QStringLiteral("ScreenBlankMinutes"),
                                            Qt::CaseInsensitive) != 0)
            continue;
        bool ok = false;
        const int n = line.mid(eq + 1).trimmed().toInt(&ok);
        if (ok && n >= 0 && n <= 5)
            return n;
    }
    return 5;
}

bool Tel15MainWindow::saveConfKey(const QString &key, const QString &value) const
{
    const QString path = confFilePath();
    if (path.isEmpty())
        return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QString text = QString::fromUtf8(f.readAll());
    f.close();

    QStringList lines = text.split(QChar('\n'));
    bool found = false;
    for (int i = 0; i < lines.size(); ++i) {
        QString trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith(QLatin1Char('#')) || trimmed.isEmpty())
            continue;
        const int eq = trimmed.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        if (trimmed.left(eq).trimmed().compare(key, Qt::CaseInsensitive) != 0)
            continue;
        lines[i] = key + QStringLiteral(" = ") + value;
        found = true;
    }
    if (!found)
        lines.append(key + QStringLiteral(" = ") + value);

    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    QByteArray out = lines.join(QChar('\n')).toUtf8();
    if (!out.endsWith('\n'))
        out.append('\n');
    const bool ok = (f.write(out) == out.size());
    f.close();
    return ok;
}

void Tel15MainWindow::refreshConfCycleButtons()
{
    if (!m_pageConfEdit)
        return;
    const QList<QPushButton *> btns = m_pageConfEdit->findChildren<QPushButton *>();
    for (int i = 0; i < btns.size(); ++i) {
        QPushButton *btn = btns.at(i);
        const QString kind = btn->property("cycleKind").toString();
        if (kind == QLatin1String("prefer"))
            btn->setText(QStringLiteral("上行口\n%1").arg(m_preferUplink));
        else if (kind == QLatin1String("weblink"))
            btn->setText(QStringLiteral("链路\n%1")
                             .arg(m_webLinkMode == 0 ? QStringLiteral("DTU")
                                                     : QStringLiteral("Socket")));
        else if (kind == QLatin1String("watchdog"))
            btn->setText(QStringLiteral("看门狗\n%1")
                             .arg(m_watchdogEnable ? QStringLiteral("开")
                                                   : QStringLiteral("关")));
    }
    if (m_lblPrefer)
        m_lblPrefer->setText(m_preferUplink);
    if (m_lblWebLink)
        m_lblWebLink->setText(m_webLinkMode == 0 ? QStringLiteral("DTU")
                                                 : QStringLiteral("Socket"));
    if (m_lblWatchdog)
        m_lblWatchdog->setText(m_watchdogEnable ? QStringLiteral("开")
                                                : QStringLiteral("关"));
}

void Tel15MainWindow::loadConfEditFields()
{
    const QString text = loadConfText();
    const QStringList lines = text.split(QChar('\n'));
    m_preferUplink = QStringLiteral("auto");
    m_webLinkMode = 1;
    m_watchdogEnable = 0;
    QString ip = QStringLiteral("222.180.168.25");
    QString port = QStringLiteral("6105");
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i).trimmed();
        if (line.startsWith(QLatin1Char('#')) || line.isEmpty())
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1).trimmed();
        if (key.compare(QStringLiteral("SocketServerIP"), Qt::CaseInsensitive) == 0)
            ip = val;
        else if (key.compare(QStringLiteral("SocketServerPort"), Qt::CaseInsensitive) == 0)
            port = val;
        else if (key.compare(QStringLiteral("PreferUplink"), Qt::CaseInsensitive) == 0)
            m_preferUplink = val.toLower();
        else if (key.compare(QStringLiteral("WebLinkMode"), Qt::CaseInsensitive) == 0)
            m_webLinkMode = val.toInt();
        else if (key.compare(QStringLiteral("WatchdogEnable"), Qt::CaseInsensitive) == 0)
            m_watchdogEnable = val.toInt();
    }
    if (m_editServerIp)
        m_editServerIp->setText(ip);
    if (m_editServerPort)
        m_editServerPort->setText(port);
    refreshConfCycleButtons();
}

void Tel15MainWindow::onCyclePreferUplink()
{
    noteUserActivity();
    if (m_preferUplink == QLatin1String("auto"))
        m_preferUplink = QStringLiteral("eth0");
    else if (m_preferUplink == QLatin1String("eth0"))
        m_preferUplink = QStringLiteral("usb0");
    else
        m_preferUplink = QStringLiteral("auto");
    refreshConfCycleButtons();
}

void Tel15MainWindow::onCycleWebLink()
{
    noteUserActivity();
    m_webLinkMode = m_webLinkMode == 0 ? 1 : 0;
    refreshConfCycleButtons();
}

void Tel15MainWindow::onCycleWatchdog()
{
    noteUserActivity();
    m_watchdogEnable = m_watchdogEnable ? 0 : 1;
    refreshConfCycleButtons();
}

void Tel15MainWindow::onConfPad()
{
    noteUserActivity();
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn || !m_confFocusEdit)
        return;
    const QString key = btn->property("padKey").toString();
    if (key.isEmpty())
        return;
    if (key == QStringLiteral("删")) {
        m_confFocusEdit->backspace();
        return;
    }
    m_confFocusEdit->insert(key);
}

void Tel15MainWindow::onConfSave()
{
    noteUserActivity();
    const QString ip = m_editServerIp ? m_editServerIp->text().trimmed() : QString();
    const QString port = m_editServerPort ? m_editServerPort->text().trimmed() : QString();
    bool ok = true;
    ok = saveConfKey(QStringLiteral("SocketServerIP"), ip) && ok;
    ok = saveConfKey(QStringLiteral("SocketServerPort"), port) && ok;
    ok = saveConfKey(QStringLiteral("PreferUplink"), m_preferUplink) && ok;
    ok = saveConfKey(QStringLiteral("WebLinkMode"), QString::number(m_webLinkMode)) && ok;
    ok = saveConfKey(QStringLiteral("WatchdogEnable"), QString::number(m_watchdogEnable)) && ok;
    if (ok) {
        appendUiLog(QStringLiteral("配置已保存（上行口/链路等需重启后完全生效）"));
        QMessageBox::information(this, QStringLiteral("配置"),
                                 QStringLiteral("已写入 tyls.conf。\n部分项需重启后生效。"));
        onShowConfPage();
    } else {
        QMessageBox::warning(this, QStringLiteral("配置"),
                             QStringLiteral("保存失败，请检查 tyls.conf 写权限。"));
    }
}

bool Tel15MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (!event)
        return QMainWindow::eventFilter(watched, event);

    if (watched == m_blankCover && m_screenBlanked) {
        if (event->type() == QEvent::MouseButtonPress
                || event->type() == QEvent::TouchBegin) {
            noteUserActivity();
            return true;
        }
    }
    if ((watched == m_editServerIp || watched == m_editServerPort)
            && (event->type() == QEvent::MouseButtonPress
                || event->type() == QEvent::FocusIn)) {
        m_confFocusEdit = qobject_cast<QLineEdit *>(watched);
    }

    /* 只把「按下」当作用户活动，避免 MouseMove 刷屏；且忽略遮罩自身 */
    if (watched != m_blankCover) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::TouchBegin:
        case QEvent::KeyPress:
            noteUserActivity();
            break;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void Tel15MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_blankCover)
        m_blankCover->setGeometry(rect());
}
