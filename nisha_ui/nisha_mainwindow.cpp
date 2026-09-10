#include "nisha_mainwindow.h"
#include "depth_profile_view.h"
#include "board_buzzer.h"
#include "board_display.h"

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <cstring>
#include <unistd.h>

extern "C" {
#include "NishaApp.h"
#include "nishatask.h"
#include "depth_sensor.h"
#include "sediment_sensor.h"
#include "dc_motor.h"
#include "relay_module.h"
#include "FirmwareVersion.h"
#include "OtaUpgrade.h"
}

namespace {

void setLabelTextIfChanged(QLabel *lab, const QString &text)
{
    if (!lab || lab->text() == text)
        return;
    lab->setText(text);
}

void setDevChip(QLabel *lab, const QString &name, bool ok)
{
    if (!lab)
        return;
    const QString color = ok ? QStringLiteral("#1a7f37") : QStringLiteral("#b42318");
    const QString html = QStringLiteral(
        "<span style=\"color:%1;font-size:18px;\">●</span>&nbsp;%2")
                             .arg(color, name);
    setLabelTextIfChanged(lab, html);
}

void setOtaStep(QLabel *lab, bool done, bool active, const QString &text)
{
    if (!lab)
        return;
    const QString mark = done ? QStringLiteral("✓")
                              : (active ? QStringLiteral("●") : QStringLiteral("○"));
    const QString color = done ? QStringLiteral("#3dd68c")
                               : (active ? QStringLiteral("#5eead4") : QStringLiteral("#64748b"));
    const QString html = QStringLiteral(
        "<span style=\"color:%1;font-size:20px;\">%2</span>&nbsp;%3")
                             .arg(color, mark, text);
    if (lab->text() == html)
        return;
    lab->setText(html);
}

QPushButton *makeBtn(const QString &text, QWidget *parent)
{
    QPushButton *b = new QPushButton(text, parent);
    b->setMinimumHeight(48);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QWidget *makeMetricBox(QWidget *parent, const QString &cap, QLabel **valOut)
{
    QWidget *box = new QWidget(parent);
    box->setStyleSheet(QStringLiteral("QWidget { background:#ffffff; border-radius:10px; }"));
    QVBoxLayout *vl = new QVBoxLayout(box);
    QLabel *c = new QLabel(cap, box);
    c->setObjectName(QStringLiteral("metricCap"));
    QLabel *v = new QLabel(QStringLiteral("--"), box);
    v->setObjectName(QStringLiteral("metricVal"));
    v->setAlignment(Qt::AlignCenter);
    vl->addWidget(c);
    vl->addWidget(v, 1);
    *valOut = v;
    return box;
}

} /* namespace */

NishaMainWindow::NishaMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_root(0)
    , m_stack(0)
    , m_pageHome(0)
    , m_pageDefault(0)
    , m_pageVmm(0)
    , m_pageVdim(0)
    , m_pageManual(0)
    , m_pageLog(0)
    , m_pageOta(0)
    , m_lblClock(0)
    , m_lblUptime(0)
    , m_lblMode(0)
    , m_lblState(0)
    , m_depthProfile(0)
    , m_valDepth(0)
    , m_valSediment(0)
    , m_valKeepDepth(0)
    , m_lblDevDepth(0)
    , m_lblDevSediment(0)
    , m_lblDevMotor(0)
    , m_lblDevRelay(0)
    , m_editDefaultDepth(0)
    , m_editVmm1(0)
    , m_editVmm2(0)
    , m_editVmm3(0)
    , m_editVmm4(0)
    , m_editVdimA(0)
    , m_editVdimB(0)
    , m_activeEdit(0)
    , m_btnBackOta(0)
    , m_lblOtaState(0)
    , m_lblOtaPercent(0)
    , m_lblOtaWarn(0)
    , m_lblOtaStep1(0)
    , m_lblOtaStep2(0)
    , m_lblOtaStep3(0)
    , m_lblOtaDetail(0)
    , m_otaHero(0)
    , m_otaDetailCard(0)
    , m_otaProgress(0)
    , m_log(0)
    , m_timer(0)
    , m_bizOk(false)
    , m_otaWasBusy(false)
    , m_otaUiLocked(false)
{
    buildUi();
    m_uptime.start();
    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(onRefresh()));
    m_timer->start(1000);
    setBizEnabled(false);
    appendUiLog(QStringLiteral("界面已显示，正在启动业务…"));
}

NishaMainWindow::~NishaMainWindow()
{
}

void NishaMainWindow::applyCjkFont(QWidget *w)
{
    if (!w)
        return;
    static QString s_family;
    if (s_family.isEmpty()) {
        const char *names[] = {
            "WenQuanYi Micro Hei", "WenQuanYi Zen Hei", "Noto Sans CJK SC",
            "Source Han Sans SC", "Droid Sans Fallback", "DroidSansFallback", 0
        };
        const QStringList families = QFontDatabase().families();
        for (int i = 0; names[i]; ++i) {
            const QString n = QString::fromLatin1(names[i]);
            if (families.contains(n)) {
                s_family = n;
                break;
            }
        }
        if (s_family.isEmpty()) {
            const char *paths[] = {
                "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
                "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
                "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
                0
            };
            for (int i = 0; paths[i]; ++i) {
                const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(paths[i]));
                if (id < 0)
                    continue;
                const QStringList faces = QFontDatabase::applicationFontFamilies(id);
                if (!faces.isEmpty()) {
                    s_family = faces.at(0);
                    break;
                }
            }
        }
    }
    if (s_family.isEmpty())
        return;
    QFont f = w->font();
    f.setFamily(s_family);
    w->setFont(f);
}

QString NishaMainWindow::modeText(int mode) const
{
    switch (mode) {
    case SedimentUD_Mode_Default:
        return QStringLiteral("默认定深");
    case SedimentUD_Mode_VMM_Measurement:
        return QStringLiteral("垂线混合");
    case SedimentUD_Mode_VDIM_Measurement:
        return QStringLiteral("垂线积深");
    case SedimentUD_Mode_Manual:
        return QStringLiteral("手动");
    default:
        return QStringLiteral("未知");
    }
}

QString NishaMainWindow::stateText(int state) const
{
    switch (state) {
    case SedimentUD_IDLE:
        return QStringLiteral("待机");
    case SedimentUD_INITIALIZING:
        return QStringLiteral("初始化");
    case SedimentUD_RUNNING:
        return QStringLiteral("运行中");
    case SedimentUD_ERROR:
        return QStringLiteral("故障");
    default:
        return QStringLiteral("--");
    }
}

QLineEdit *NishaMainWindow::makeDepthEdit(QWidget *parent)
{
    QLineEdit *e = new QLineEdit(parent);
    e->setReadOnly(true);
    e->setAlignment(Qt::AlignCenter);
    e->setMinimumHeight(44);
    e->setPlaceholderText(QStringLiteral("米"));
    e->setStyleSheet(QStringLiteral(
        "QLineEdit { font-size:22px; background:#f4f7fb; border:1px solid #c5d0dc;"
        " border-radius:6px; padding:4px 8px; }"));
    e->installEventFilter(this);
    return e;
}

void NishaMainWindow::setActiveEdit(QLineEdit *edit)
{
    m_activeEdit = edit;
    if (edit)
        edit->setFocus(Qt::OtherFocusReason);
}

void NishaMainWindow::addNumPad(QVBoxLayout *lay, QWidget *parent)
{
    QGridLayout *pad = new QGridLayout();
    const char *keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "<-"};
    for (int i = 0; i < 12; ++i) {
        QPushButton *kb = makeBtn(QString::fromLatin1(keys[i]), parent);
        kb->setMinimumHeight(42);
        kb->setObjectName(QStringLiteral("secondary"));
        pad->addWidget(kb, i / 3, i % 3);
        if (i == 11)
            connect(kb, SIGNAL(clicked()), this, SLOT(onNumPadBackspace()));
        else
            connect(kb, SIGNAL(clicked()), this, SLOT(onNumPadDigit()));
    }
    QPushButton *clr = makeBtn(QStringLiteral("清空"), parent);
    clr->setObjectName(QStringLiteral("secondary"));
    connect(clr, SIGNAL(clicked()), this, SLOT(onNumPadClear()));
    lay->addLayout(pad);
    lay->addWidget(clr);
}

void NishaMainWindow::buildUi()
{
    m_root = new QWidget(this);
    setCentralWidget(m_root);
    applyCjkFont(m_root);
    m_root->setStyleSheet(QStringLiteral(
        "QWidget { background-color:#e8eef4; color:#1a2332; font-size:18px; }"
        "QPushButton { background:#2b6cb0; color:#fff; border:none; border-radius:8px;"
        " padding:8px 14px; font-size:18px; font-weight:bold; }"
        "QPushButton:pressed { background:#1e4e8c; }"
        "QPushButton:disabled { background:#9aa8b8; }"
        "QPushButton#danger { background:#b42318; }"
        "QPushButton#secondary { background:#4a5568; }"
        "QLabel#title { font-size:28px; font-weight:bold; color:#0f2a44; }"
        "QLabel#metricVal { font-size:36px; font-weight:bold; color:#0b4f6c; }"
        "QLabel#metricCap { font-size:16px; color:#5a6a7a; }"
        "QTextEdit { background:#0f1720; color:#d7e2ec; font-family:monospace;"
        " font-size:14px; border-radius:8px; }"
        "QWidget#otaPage { background-color:#0f1c24; }"
        "QFrame#otaTopBar { background-color:#132833; border:none;"
        " border-bottom:2px solid #1f4a52; }"
        "QLabel#otaBrand { color:#e8f4f4; font-size:24px; font-weight:bold; }"
        "QFrame#otaHero { background-color:#16303a; border:1px solid #24545c;"
        " border-radius:16px; }"
        "QLabel#otaPercent { color:#5eead4; font-size:104px; font-weight:bold; }"
        "QLabel#otaState { color:#f1f5f9; font-size:26px; font-weight:bold; }"
        "QLabel#otaWarn { color:#fbbf24; font-size:18px; font-weight:bold; }"
        "QLabel#otaStep { color:#94a3b8; font-size:18px; }"
        "QProgressBar#otaBar { border:none; border-radius:10px;"
        " background:#0b1620; text-align:center; font-size:0px; }"
        "QProgressBar#otaBar::chunk { background-color:#14b8a6; border-radius:10px; }"
        "QFrame#otaDetailCard { background-color:#132833; border:1px solid #24545c;"
        " border-radius:14px; }"
        "QLabel#otaDetail { color:#94a3b8; font-size:17px; }"
        "QPushButton#otaBack { background:#334155; color:#fff; border:none;"
        " border-radius:10px; padding:12px 28px; font-size:18px; font-weight:bold; }"));

    QVBoxLayout *rootLay = new QVBoxLayout(m_root);
    rootLay->setContentsMargins(12, 10, 12, 10);
    rootLay->setSpacing(8);

    QHBoxLayout *top = new QHBoxLayout();
    QLabel *title = new QLabel(QStringLiteral("泥沙"), m_root);
    title->setObjectName(QStringLiteral("title"));
    m_lblClock = new QLabel(m_root);
    m_lblUptime = new QLabel(m_root);
    top->addWidget(title, 1);
    top->addWidget(m_lblUptime);
    top->addSpacing(12);
    top->addWidget(m_lblClock);
    rootLay->addLayout(top);

    m_stack = new QStackedWidget(m_root);
    rootLay->addWidget(m_stack, 1);

    /* 首页 */
    m_pageHome = new QWidget(m_stack);
    {
        QVBoxLayout *homeLay = new QVBoxLayout(m_pageHome);
        homeLay->setSpacing(10);

        QHBoxLayout *statRow = new QHBoxLayout();
        m_lblMode = new QLabel(m_pageHome);
        m_lblState = new QLabel(m_pageHome);
        m_lblDevDepth = new QLabel(m_pageHome);
        m_lblDevSediment = new QLabel(m_pageHome);
        m_lblDevMotor = new QLabel(m_pageHome);
        m_lblDevRelay = new QLabel(m_pageHome);
        statRow->addWidget(m_lblMode);
        statRow->addWidget(m_lblState);
        statRow->addStretch(1);
        statRow->addWidget(m_lblDevDepth);
        statRow->addWidget(m_lblDevSediment);
        statRow->addWidget(m_lblDevMotor);
        statRow->addWidget(m_lblDevRelay);
        homeLay->addLayout(statRow);

        QHBoxLayout *mainRow = new QHBoxLayout();
        mainRow->setSpacing(10);
        m_depthProfile = new DepthProfileView(m_pageHome);
        m_depthProfile->setMinimumWidth(360);
        mainRow->addWidget(m_depthProfile, 1);

        QWidget *metricCol = new QWidget(m_pageHome);
        metricCol->setMinimumWidth(248);
        metricCol->setMaximumWidth(300);
        QVBoxLayout *metricLay = new QVBoxLayout(metricCol);
        metricLay->setContentsMargins(0, 0, 0, 0);
        metricLay->setSpacing(8);
        metricLay->addWidget(makeMetricBox(metricCol, QStringLiteral("当前深度 (m)"), &m_valDepth), 1);
        metricLay->addWidget(makeMetricBox(metricCol, QStringLiteral("泥沙浓度 (kg/m³)"), &m_valSediment), 1);
        metricLay->addWidget(makeMetricBox(metricCol, QStringLiteral("保持深度 (m)"), &m_valKeepDepth), 1);
        mainRow->addWidget(metricCol, 0);
        homeLay->addLayout(mainRow, 1);

        QGridLayout *nav = new QGridLayout();
        QPushButton *bDef = makeBtn(QStringLiteral("默认定深"), m_pageHome);
        QPushButton *bVmm = makeBtn(QStringLiteral("垂线混合"), m_pageHome);
        QPushButton *bVdim = makeBtn(QStringLiteral("垂线积深"), m_pageHome);
        QPushButton *bMan = makeBtn(QStringLiteral("手动升降"), m_pageHome);
        QPushButton *bLog = makeBtn(QStringLiteral("运行日志"), m_pageHome);
        bLog->setObjectName(QStringLiteral("secondary"));
        QPushButton *bStop = makeBtn(QStringLiteral("停止任务"), m_pageHome);
        bStop->setObjectName(QStringLiteral("danger"));
        QPushButton *bExit = makeBtn(QStringLiteral("退出"), m_pageHome);
        bExit->setObjectName(QStringLiteral("secondary"));
        nav->addWidget(bDef, 0, 0);
        nav->addWidget(bVmm, 0, 1);
        nav->addWidget(bVdim, 0, 2);
        nav->addWidget(bMan, 1, 0);
        nav->addWidget(bLog, 1, 1);
        nav->addWidget(bStop, 1, 2);
        nav->addWidget(bExit, 2, 0, 1, 3);
        homeLay->addLayout(nav);

        connect(bDef, SIGNAL(clicked()), this, SLOT(onShowDefaultPage()));
        connect(bVmm, SIGNAL(clicked()), this, SLOT(onShowVmmPage()));
        connect(bVdim, SIGNAL(clicked()), this, SLOT(onShowVdimPage()));
        connect(bMan, SIGNAL(clicked()), this, SLOT(onShowManualPage()));
        connect(bLog, SIGNAL(clicked()), this, SLOT(onShowLogPage()));
        connect(bStop, SIGNAL(clicked()), this, SLOT(onStopTask()));
        connect(bExit, SIGNAL(clicked()), this, SLOT(onExit()));
    }

    /* 默认定深 */
    m_pageDefault = new QWidget(m_stack);
    {
        QHBoxLayout *hl = new QHBoxLayout(m_pageDefault);
        QVBoxLayout *left = new QVBoxLayout();
        QLabel *h = new QLabel(QStringLiteral("默认定深模式"), m_pageDefault);
        h->setObjectName(QStringLiteral("title"));
        left->addWidget(h);
        left->addWidget(new QLabel(QStringLiteral("目标深度 (米)"), m_pageDefault));
        m_editDefaultDepth = makeDepthEdit(m_pageDefault);
        left->addWidget(m_editDefaultDepth);
        QPushButton *start = makeBtn(QStringLiteral("启动"), m_pageDefault);
        QPushButton *back = makeBtn(QStringLiteral("返回"), m_pageDefault);
        back->setObjectName(QStringLiteral("secondary"));
        left->addWidget(start);
        left->addWidget(back);
        left->addStretch(1);
        QVBoxLayout *right = new QVBoxLayout();
        addNumPad(right, m_pageDefault);
        hl->addLayout(left, 1);
        hl->addLayout(right, 1);
        connect(start, SIGNAL(clicked()), this, SLOT(onDefaultStart()));
        connect(back, SIGNAL(clicked()), this, SLOT(onBackHome()));
    }

    /* VMM */
    m_pageVmm = new QWidget(m_stack);
    {
        QHBoxLayout *hl = new QHBoxLayout(m_pageVmm);
        QVBoxLayout *left = new QVBoxLayout();
        QLabel *h = new QLabel(QStringLiteral("垂线混合法"), m_pageVmm);
        h->setObjectName(QStringLiteral("title"));
        left->addWidget(h);
        left->addWidget(new QLabel(QStringLiteral("深度1~4 (米)"), m_pageVmm));
        m_editVmm1 = makeDepthEdit(m_pageVmm);
        m_editVmm2 = makeDepthEdit(m_pageVmm);
        m_editVmm3 = makeDepthEdit(m_pageVmm);
        m_editVmm4 = makeDepthEdit(m_pageVmm);
        left->addWidget(m_editVmm1);
        left->addWidget(m_editVmm2);
        left->addWidget(m_editVmm3);
        left->addWidget(m_editVmm4);
        QPushButton *start = makeBtn(QStringLiteral("启动测量"), m_pageVmm);
        QPushButton *back = makeBtn(QStringLiteral("返回"), m_pageVmm);
        back->setObjectName(QStringLiteral("secondary"));
        left->addWidget(start);
        left->addWidget(back);
        left->addStretch(1);
        QVBoxLayout *right = new QVBoxLayout();
        addNumPad(right, m_pageVmm);
        hl->addLayout(left, 1);
        hl->addLayout(right, 1);
        connect(start, SIGNAL(clicked()), this, SLOT(onVmmStart()));
        connect(back, SIGNAL(clicked()), this, SLOT(onBackHome()));
    }

    /* VDIM */
    m_pageVdim = new QWidget(m_stack);
    {
        QHBoxLayout *hl = new QHBoxLayout(m_pageVdim);
        QVBoxLayout *left = new QVBoxLayout();
        QLabel *h = new QLabel(QStringLiteral("垂线积深法"), m_pageVdim);
        h->setObjectName(QStringLiteral("title"));
        left->addWidget(h);
        left->addWidget(new QLabel(QStringLiteral("深度 A / B (米)"), m_pageVdim));
        m_editVdimA = makeDepthEdit(m_pageVdim);
        m_editVdimB = makeDepthEdit(m_pageVdim);
        left->addWidget(m_editVdimA);
        left->addWidget(m_editVdimB);
        QPushButton *start = makeBtn(QStringLiteral("启动测量"), m_pageVdim);
        QPushButton *back = makeBtn(QStringLiteral("返回"), m_pageVdim);
        back->setObjectName(QStringLiteral("secondary"));
        left->addWidget(start);
        left->addWidget(back);
        left->addStretch(1);
        QVBoxLayout *right = new QVBoxLayout();
        addNumPad(right, m_pageVdim);
        hl->addLayout(left, 1);
        hl->addLayout(right, 1);
        connect(start, SIGNAL(clicked()), this, SLOT(onVdimStart()));
        connect(back, SIGNAL(clicked()), this, SLOT(onBackHome()));
    }

    /* 手动 */
    m_pageManual = new QWidget(m_stack);
    {
        QVBoxLayout *vl = new QVBoxLayout(m_pageManual);
        QLabel *h = new QLabel(QStringLiteral("手动升降"), m_pageManual);
        h->setObjectName(QStringLiteral("title"));
        vl->addWidget(h);
        vl->addWidget(new QLabel(QStringLiteral("点按上升/下降，停止立即刹车。"), m_pageManual));
        QHBoxLayout *row = new QHBoxLayout();
        QPushButton *up = makeBtn(QStringLiteral("上升"), m_pageManual);
        QPushButton *down = makeBtn(QStringLiteral("下降"), m_pageManual);
        QPushButton *stop = makeBtn(QStringLiteral("停止"), m_pageManual);
        stop->setObjectName(QStringLiteral("danger"));
        up->setMinimumHeight(80);
        down->setMinimumHeight(80);
        stop->setMinimumHeight(80);
        row->addWidget(up);
        row->addWidget(down);
        row->addWidget(stop);
        vl->addLayout(row);
        QPushButton *back = makeBtn(QStringLiteral("返回"), m_pageManual);
        back->setObjectName(QStringLiteral("secondary"));
        vl->addWidget(back);
        vl->addStretch(1);
        connect(up, SIGNAL(clicked()), this, SLOT(onManualUp()));
        connect(down, SIGNAL(clicked()), this, SLOT(onManualDown()));
        connect(stop, SIGNAL(clicked()), this, SLOT(onManualStop()));
        connect(back, SIGNAL(clicked()), this, SLOT(onBackHome()));
    }

    /* 日志 */
    m_pageLog = new QWidget(m_stack);
    {
        QVBoxLayout *vl = new QVBoxLayout(m_pageLog);
        QLabel *h = new QLabel(QStringLiteral("运行日志"), m_pageLog);
        h->setObjectName(QStringLiteral("title"));
        vl->addWidget(h);
        m_log = new QTextEdit(m_pageLog);
        m_log->setReadOnly(true);
        vl->addWidget(m_log, 1);
        QPushButton *back = makeBtn(QStringLiteral("返回"), m_pageLog);
        back->setObjectName(QStringLiteral("secondary"));
        vl->addWidget(back);
        connect(back, SIGNAL(clicked()), this, SLOT(onBackHome()));
    }

    /* OTA 全屏进度页：网页下发固件时自动切入并锁定，无入口按钮 */
    m_pageOta = new QWidget(m_stack);
    m_pageOta->setObjectName(QStringLiteral("otaPage"));
    {
        QFrame *otaTopBar = new QFrame(m_pageOta);
        otaTopBar->setObjectName(QStringLiteral("otaTopBar"));
        QLabel *otaBrand = new QLabel(QStringLiteral("远程固件升级"), otaTopBar);
        otaBrand->setObjectName(QStringLiteral("otaBrand"));
        m_btnBackOta = makeBtn(QStringLiteral("返回首页"), otaTopBar);
        m_btnBackOta->setObjectName(QStringLiteral("otaBack"));
        connect(m_btnBackOta, SIGNAL(clicked()), this, SLOT(onBackHome()));
        m_btnBackOta->setVisible(false);
        QHBoxLayout *otaTop = new QHBoxLayout(otaTopBar);
        otaTop->setContentsMargins(22, 14, 22, 14);
        otaTop->setSpacing(12);
        otaTop->addWidget(otaBrand);
        otaTop->addStretch(1);
        otaTop->addWidget(m_btnBackOta);

        m_otaHero = new QFrame(m_pageOta);
        m_otaHero->setObjectName(QStringLiteral("otaHero"));
        m_lblOtaPercent = new QLabel(QStringLiteral("0%"), m_otaHero);
        m_lblOtaPercent->setObjectName(QStringLiteral("otaPercent"));
        m_lblOtaPercent->setAlignment(Qt::AlignCenter);
        m_lblOtaState = new QLabel(QStringLiteral("准备接收固件"), m_otaHero);
        m_lblOtaState->setObjectName(QStringLiteral("otaState"));
        m_lblOtaState->setAlignment(Qt::AlignCenter);
        m_lblOtaState->setWordWrap(true);

        m_lblOtaStep1 = new QLabel(m_otaHero);
        m_lblOtaStep1->setObjectName(QStringLiteral("otaStep"));
        m_lblOtaStep1->setAlignment(Qt::AlignCenter);
        m_lblOtaStep2 = new QLabel(m_otaHero);
        m_lblOtaStep2->setObjectName(QStringLiteral("otaStep"));
        m_lblOtaStep2->setAlignment(Qt::AlignCenter);
        m_lblOtaStep3 = new QLabel(m_otaHero);
        m_lblOtaStep3->setObjectName(QStringLiteral("otaStep"));
        m_lblOtaStep3->setAlignment(Qt::AlignCenter);
        setOtaStep(m_lblOtaStep1, false, false, QStringLiteral("接收"));
        setOtaStep(m_lblOtaStep2, false, false, QStringLiteral("校验"));
        setOtaStep(m_lblOtaStep3, false, false, QStringLiteral("重启"));
        QHBoxLayout *stepRow = new QHBoxLayout();
        stepRow->setSpacing(28);
        stepRow->addStretch(1);
        stepRow->addWidget(m_lblOtaStep1);
        stepRow->addWidget(m_lblOtaStep2);
        stepRow->addWidget(m_lblOtaStep3);
        stepRow->addStretch(1);

        m_otaProgress = new QProgressBar(m_otaHero);
        m_otaProgress->setObjectName(QStringLiteral("otaBar"));
        m_otaProgress->setRange(0, 100);
        m_otaProgress->setValue(0);
        m_otaProgress->setTextVisible(false);
        m_otaProgress->setMinimumHeight(18);
        m_otaProgress->setMaximumHeight(18);

        m_lblOtaWarn = new QLabel(QStringLiteral("升级过程中请勿断电或退出"), m_otaHero);
        m_lblOtaWarn->setObjectName(QStringLiteral("otaWarn"));
        m_lblOtaWarn->setAlignment(Qt::AlignCenter);

        QVBoxLayout *heroLay = new QVBoxLayout(m_otaHero);
        heroLay->setContentsMargins(28, 28, 28, 24);
        heroLay->setSpacing(12);
        heroLay->addStretch(1);
        heroLay->addWidget(m_lblOtaPercent);
        heroLay->addWidget(m_lblOtaState);
        heroLay->addSpacing(6);
        heroLay->addLayout(stepRow);
        heroLay->addSpacing(10);
        heroLay->addWidget(m_otaProgress);
        heroLay->addSpacing(8);
        heroLay->addWidget(m_lblOtaWarn);
        heroLay->addStretch(1);

        m_otaDetailCard = new QFrame(m_pageOta);
        m_otaDetailCard->setObjectName(QStringLiteral("otaDetailCard"));
        m_lblOtaDetail = new QLabel(QStringLiteral(""), m_otaDetailCard);
        m_lblOtaDetail->setObjectName(QStringLiteral("otaDetail"));
        m_lblOtaDetail->setWordWrap(true);
        m_lblOtaDetail->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        QVBoxLayout *detailLay = new QVBoxLayout(m_otaDetailCard);
        detailLay->setContentsMargins(22, 16, 22, 16);
        detailLay->addWidget(m_lblOtaDetail);

        QVBoxLayout *otaBody = new QVBoxLayout;
        otaBody->setContentsMargins(24, 18, 24, 20);
        otaBody->setSpacing(14);
        otaBody->addWidget(m_otaHero, 1);
        otaBody->addWidget(m_otaDetailCard, 0);

        QVBoxLayout *otaLay = new QVBoxLayout(m_pageOta);
        otaLay->setContentsMargins(0, 0, 0, 0);
        otaLay->setSpacing(0);
        otaLay->addWidget(otaTopBar, 0);
        otaLay->addLayout(otaBody, 1);
    }

    m_stack->addWidget(m_pageHome);
    m_stack->addWidget(m_pageDefault);
    m_stack->addWidget(m_pageVmm);
    m_stack->addWidget(m_pageVdim);
    m_stack->addWidget(m_pageManual);
    m_stack->addWidget(m_pageLog);
    m_stack->addWidget(m_pageOta);
}

bool NishaMainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        if (watched == m_editDefaultDepth)
            setActiveEdit(m_editDefaultDepth);
        else if (watched == m_editVmm1)
            setActiveEdit(m_editVmm1);
        else if (watched == m_editVmm2)
            setActiveEdit(m_editVmm2);
        else if (watched == m_editVmm3)
            setActiveEdit(m_editVmm3);
        else if (watched == m_editVmm4)
            setActiveEdit(m_editVmm4);
        else if (watched == m_editVdimA)
            setActiveEdit(m_editVdimA);
        else if (watched == m_editVdimB)
            setActiveEdit(m_editVdimB);
    }
    return QMainWindow::eventFilter(watched, event);
}

void NishaMainWindow::appendUiLog(const QString &line)
{
    if (!m_log)
        return;
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
    m_log->append(QStringLiteral("[%1] %2").arg(ts, line));
}

void NishaMainWindow::setBizEnabled(bool on)
{
    m_bizOk = on;
}

void NishaMainWindow::onStartBusiness()
{
    appendUiLog(QStringLiteral("正在初始化串口 / 定时器 / 任务线程…"));
    const int rc = Nisha_BusinessStart();
    onBusinessReady(rc);
}

void NishaMainWindow::onBusinessReady(int result)
{
    if (result == 0) {
        setBizEnabled(true);
        appendUiLog(QStringLiteral("业务启动成功"));
        char ver[64];
        memset(ver, 0, sizeof(ver));
        FirmwareVersion_Format(ver, sizeof(ver));
        appendUiLog(QStringLiteral("固件版本: %1").arg(QString::fromLocal8Bit(ver)));
        onRefresh();
    } else {
        setBizEnabled(false);
        appendUiLog(QStringLiteral("业务启动失败，请检查 tyls.conf 与串口"));
    }
}

void NishaMainWindow::onRefresh()
{
    setLabelTextIfChanged(m_lblClock,
                          QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd  hh:mm:ss")));
    const int sec = static_cast<int>(m_uptime.elapsed() / 1000);
    setLabelTextIfChanged(m_lblUptime,
                          QStringLiteral("运行 %1:%2:%3")
                              .arg(sec / 3600, 2, 10, QChar('0'))
                              .arg((sec % 3600) / 60, 2, 10, QChar('0'))
                              .arg(sec % 60, 2, 10, QChar('0')));

    /* OTA：空闲轻量探测；进行中刷新进度并锁定界面 */
    {
        const bool otaBusy = OtaUpgrade_IsBusy();
        if (otaBusy || m_otaWasBusy || m_otaUiLocked
                || (m_pageOta && m_stack->currentWidget() == m_pageOta)) {
            updateOtaUi();
            if (otaBusy)
                (void)OtaUpgrade_KickIfStalled(0U);
        }
        if (m_otaUiLocked)
            return;
    }

    if (!m_bizOk)
        return;

    setLabelTextIfChanged(m_lblMode,
                          QStringLiteral("模式：%1").arg(modeText(get_nisha_task_mode())));
    setLabelTextIfChanged(m_lblState,
                          QStringLiteral("状态：%1").arg(stateText(GetNiShaUD_Task_State())));

    const bool depthOk = is_depth_sensor_online();
    const double depthM = get_depth_sensor_depth() / 1000.0;
    setLabelTextIfChanged(m_valDepth, QString::number(depthM, 'f', 3));

    float sed = g_sediment_data_average;
    if (sed <= 0.0f && sediment_sensor_info.sediment_value > 0.0f)
        sed = sediment_fill_milli_look(sediment_sensor_info.sediment_value);
    setLabelTextIfChanged(m_valSediment, QString::number(sed, 'f', 3));

    const int keepMm = get_default_mode_keep_depth();
    const bool keepOk = keepMm > 0;
    const double keepM = keepMm / 1000.0;
    setLabelTextIfChanged(m_valKeepDepth, QString::number(keepM, 'f', 3));

    QVector<double> targets;
    const int mode = get_nisha_task_mode();
    if (mode == SedimentUD_Mode_VMM_Measurement) {
        for (int i = 1; i <= 4; ++i) {
            const int mm = get_mode_one_para_depth(i);
            if (mm > 0)
                targets.append(mm / 1000.0);
        }
    } else if (mode == SedimentUD_Mode_VDIM_Measurement) {
        for (int i = 1; i <= 2; ++i) {
            const int mm = get_mode_two_para_depth(i);
            if (mm > 0)
                targets.append(mm / 1000.0);
        }
    }
    if (m_depthProfile)
        m_depthProfile->setLiveData(depthM, depthOk, keepM, keepOk, (double)sed, targets);

    setDevChip(m_lblDevDepth, QStringLiteral("深度"), depthOk);
    setDevChip(m_lblDevSediment, QStringLiteral("泥沙"), is_sediment_sensor_online());
    setDevChip(m_lblDevMotor, QStringLiteral("电机"), is_dc_motor_online());
    setDevChip(m_lblDevRelay, QStringLiteral("继电器"), isRelayModuleOnline());
}

void NishaMainWindow::onBackHome()
{
    if (m_otaUiLocked)
        return;
    m_stack->setCurrentWidget(m_pageHome);
    m_activeEdit = 0;
}

void NishaMainWindow::onShowDefaultPage()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    const int keep = get_default_mode_keep_depth();
    if (keep > 0)
        m_editDefaultDepth->setText(QString::number(keep / 1000.0, 'f', 3));
    setActiveEdit(m_editDefaultDepth);
    m_stack->setCurrentWidget(m_pageDefault);
}

void NishaMainWindow::onShowVmmPage()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    setActiveEdit(m_editVmm1);
    m_stack->setCurrentWidget(m_pageVmm);
}

void NishaMainWindow::onShowVdimPage()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    setActiveEdit(m_editVdimA);
    m_stack->setCurrentWidget(m_pageVdim);
}

void NishaMainWindow::onShowManualPage()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    m_stack->setCurrentWidget(m_pageManual);
}

void NishaMainWindow::onShowLogPage()
{
    if (m_otaUiLocked)
        return;
    m_stack->setCurrentWidget(m_pageLog);
}

void NishaMainWindow::setOtaUiLocked(bool locked)
{
    if (m_otaUiLocked == locked)
        return;
    m_otaUiLocked = locked;
    if (m_btnBackOta) {
        /* 升级中隐藏返回；失败解锁后才可回首页 */
        m_btnBackOta->setVisible(!locked);
        m_btnBackOta->setEnabled(!locked);
    }
    if (locked) {
        m_stack->setCurrentWidget(m_pageOta);
        appendUiLog(QStringLiteral("OTA 进行中，界面已锁定"));
    }
}

QString NishaMainWindow::otaStatusText(unsigned status)
{
    switch (status) {
    case OTA_STATUS_IDLE: return QStringLiteral("空闲");
    case OTA_STATUS_RECEIVING: return QStringLiteral("正在接收固件");
    case OTA_STATUS_APPLYING: return QStringLiteral("正在校验并切换固件");
    case OTA_STATUS_OK: return QStringLiteral("升级完成，即将重启");
    case OTA_STATUS_ERROR: return QStringLiteral("升级失败");
    default: return QStringLiteral("状态异常 (%1)").arg(status);
    }
}

QString NishaMainWindow::otaErrText(unsigned err)
{
    switch (err) {
    case OTA_ERR_NONE: return QStringLiteral("无");
    case OTA_ERR_PARAM: return QStringLiteral("参数错误");
    case OTA_ERR_BUSY: return QStringLiteral("设备忙");
    case OTA_ERR_SIZE: return QStringLiteral("固件尺寸错误");
    case OTA_ERR_IO: return QStringLiteral("读写失败");
    case OTA_ERR_APPLY: return QStringLiteral("激活失败");
    case OTA_ERR_STATE: return QStringLiteral("状态错误");
    case OTA_ERR_TIMEOUT: return QStringLiteral("收包超时");
    case OTA_ERR_VERIFY: return QStringLiteral("校验失败");
    default: return QStringLiteral("错误码 %1").arg(err);
    }
}

void NishaMainWindow::updateOtaUi()
{
    ota_ui_status_t st;
    memset(&st, 0, sizeof(st));
    OtaUpgrade_QueryUiStatus(&st);

    char fw[64];
    memset(fw, 0, sizeof(fw));
    FirmwareVersion_Format(fw, sizeof(fw));

    if (!st.inited) {
        setLabelTextIfChanged(m_lblOtaState, QStringLiteral("升级模块未就绪"));
        setLabelTextIfChanged(m_lblOtaPercent, QStringLiteral("--"));
        setOtaStep(m_lblOtaStep1, false, false, QStringLiteral("接收"));
        setOtaStep(m_lblOtaStep2, false, false, QStringLiteral("校验"));
        setOtaStep(m_lblOtaStep3, false, false, QStringLiteral("重启"));
        if (m_otaProgress)
            m_otaProgress->setValue(0);
        setLabelTextIfChanged(m_lblOtaWarn, QStringLiteral("请稍后重试远程升级"));
        setLabelTextIfChanged(m_lblOtaDetail,
                              QStringLiteral("当前版本  %1").arg(QString::fromLatin1(fw)));
        setOtaUiLocked(false);
        return;
    }

    const bool blocking = st.busy
            || st.reboot_pending
            || st.status == OTA_STATUS_RECEIVING
            || st.status == OTA_STATUS_APPLYING
            || st.status == OTA_STATUS_OK;
    const bool failed = !blocking && st.have_last_result && !st.last_ok;
    const bool success = blocking &&
            (st.reboot_pending || st.status == OTA_STATUS_OK);

    if (blocking) {
        if (!m_otaWasBusy) {
            appendUiLog(QStringLiteral("OTA 开始升级"));
            m_otaWasBusy = true;
        }
        setOtaUiLocked(true);
        if (m_stack->currentWidget() != m_pageOta)
            m_stack->setCurrentWidget(m_pageOta);

        if (success) {
            setLabelTextIfChanged(m_lblOtaState, QStringLiteral("升级完成，即将重启"));
            setLabelTextIfChanged(m_lblOtaWarn, QStringLiteral("正在激活新固件，请稍候…"));
            setOtaStep(m_lblOtaStep1, true, false, QStringLiteral("接收"));
            setOtaStep(m_lblOtaStep2, true, false, QStringLiteral("校验"));
            setOtaStep(m_lblOtaStep3, false, true, QStringLiteral("重启"));
        } else if (st.status == OTA_STATUS_APPLYING) {
            setLabelTextIfChanged(m_lblOtaState, otaStatusText(st.status));
            setLabelTextIfChanged(m_lblOtaWarn, QStringLiteral("升级过程中请勿断电或退出"));
            setOtaStep(m_lblOtaStep1, true, false, QStringLiteral("接收"));
            setOtaStep(m_lblOtaStep2, false, true, QStringLiteral("校验"));
            setOtaStep(m_lblOtaStep3, false, false, QStringLiteral("重启"));
        } else {
            setLabelTextIfChanged(m_lblOtaState, otaStatusText(st.status));
            setLabelTextIfChanged(m_lblOtaWarn, QStringLiteral("升级过程中请勿断电或退出"));
            setOtaStep(m_lblOtaStep1, false, true, QStringLiteral("接收"));
            setOtaStep(m_lblOtaStep2, false, false, QStringLiteral("校验"));
            setOtaStep(m_lblOtaStep3, false, false, QStringLiteral("重启"));
        }
    } else {
        if (m_otaWasBusy) {
            appendUiLog(QStringLiteral("OTA 会话结束"));
            m_otaWasBusy = false;
            setOtaUiLocked(false);
        }
        if (failed) {
            if (m_stack->currentWidget() != m_pageOta)
                m_stack->setCurrentWidget(m_pageOta);
            if (m_btnBackOta) {
                m_btnBackOta->setVisible(true);
                m_btnBackOta->setEnabled(true);
            }
            setLabelTextIfChanged(m_lblOtaState, QStringLiteral("升级失败"));
            setLabelTextIfChanged(m_lblOtaWarn,
                                  QStringLiteral("可返回首页，稍后再由网页重试"));
            setOtaStep(m_lblOtaStep1, false, false, QStringLiteral("接收"));
            setOtaStep(m_lblOtaStep2, false, false, QStringLiteral("校验"));
            setOtaStep(m_lblOtaStep3, false, false, QStringLiteral("重启"));
        }
    }

    int pct = 0;
    unsigned donePkt = 0;
    if (st.pkt_total > 0) {
        donePkt = (st.pkt_expect > 0) ? (unsigned)(st.pkt_expect - 1U) : 0U;
        if (donePkt > st.pkt_total)
            donePkt = st.pkt_total;
        pct = (int)((donePkt * 100U) / st.pkt_total);
        if (st.status == OTA_STATUS_APPLYING || st.status == OTA_STATUS_OK)
            pct = 100;
    } else if (st.file_size > 0 && st.recv_size > 0) {
        pct = (int)((st.recv_size * 100ULL) / st.file_size);
        if (pct > 100)
            pct = 100;
    }
    if (failed && pct == 0 && st.pkt_total > 0)
        pct = (int)((donePkt * 100U) / st.pkt_total);
    if (m_otaProgress)
        m_otaProgress->setValue(pct);
    setLabelTextIfChanged(m_lblOtaPercent, QStringLiteral("%1%").arg(pct));

    const QString slot = (st.active_slot == OTA_SLOT_B)
            ? QStringLiteral("B") : QStringLiteral("A");
    QString detail;
    if (blocking) {
        detail = QStringLiteral("当前版本  %1    活动槽  %2\n"
                                "目标版本  %3\n"
                                "传输进度  %4 / %5 包 · %6 / %7 字节")
                .arg(QString::fromLatin1(fw))
                .arg(slot)
                .arg(QString::fromLatin1(st.version))
                .arg(donePkt)
                .arg(st.pkt_total)
                .arg(st.recv_size)
                .arg(st.file_size);
        if (success)
            detail += QStringLiteral("\n新固件已就绪，设备即将自动重启");
    } else if (failed) {
        detail = QStringLiteral("当前版本  %1    活动槽  %2\n"
                                "失败原因  %3\n"
                                "目标版本  %4")
                .arg(QString::fromLatin1(fw))
                .arg(slot)
                .arg(otaErrText(st.last_err))
                .arg(QString::fromLatin1(st.last_version));
    } else {
        detail = QStringLiteral("当前版本  %1    活动槽  %2")
                .arg(QString::fromLatin1(fw))
                .arg(slot);
    }
    setLabelTextIfChanged(m_lblOtaDetail, detail);
}

void NishaMainWindow::onExit()
{
    if (m_otaUiLocked || OtaUpgrade_IsBusy()) {
        QMessageBox::warning(this, QStringLiteral("升级中"),
                             QStringLiteral("正在升级固件，请勿退出或断电。"));
        return;
    }
    BoardBuzzer::forceOff();
    BoardDisplay::blank();
    qApp->quit();
    ::_exit(0);
}

void NishaMainWindow::onNumPadDigit()
{
    QPushButton *b = qobject_cast<QPushButton *>(sender());
    if (!b || !m_activeEdit)
        return;
    m_activeEdit->setText(m_activeEdit->text() + b->text());
}

void NishaMainWindow::onNumPadBackspace()
{
    if (!m_activeEdit)
        return;
    QString t = m_activeEdit->text();
    if (!t.isEmpty())
        t.chop(1);
    m_activeEdit->setText(t);
}

void NishaMainWindow::onNumPadClear()
{
    if (m_activeEdit)
        m_activeEdit->clear();
}

void NishaMainWindow::onDefaultStart()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    bool ok = false;
    const double meters = m_editDefaultDepth->text().toDouble(&ok);
    if (!ok || meters <= 0.1) {
        QMessageBox::warning(this, QStringLiteral("默认定深"),
                             QStringLiteral("请输入有效深度（米），需大于 0.1。"));
        return;
    }
    const int mm = (int)(meters * 1000.0 + 0.5);
    set_default_mode_para_depth(mm);
    set_default_mode_keep_depth(mm);
    save_keep_depth_in_file(mm);
    NiShaUD_Default_Mode_Start_Task(NULL);
    appendUiLog(QStringLiteral("启动默认定深 %1 m").arg(meters, 0, 'f', 3));
    onBackHome();
}

void NishaMainWindow::onVmmStart()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
    const double d1 = m_editVmm1->text().toDouble(&ok1);
    const double d2 = m_editVmm2->text().toDouble(&ok2);
    const double d3 = m_editVmm3->text().toDouble(&ok3);
    const double d4 = m_editVmm4->text().toDouble(&ok4);
    if (!ok1 || !ok2 || !ok3 || !ok4) {
        QMessageBox::warning(this, QStringLiteral("垂线混合"),
                             QStringLiteral("请填齐四个深度（米）。"));
        return;
    }
    set_mode_one_para_depth(1, (int)(d1 * 1000.0 + 0.5));
    set_mode_one_para_depth(2, (int)(d2 * 1000.0 + 0.5));
    set_mode_one_para_depth(3, (int)(d3 * 1000.0 + 0.5));
    set_mode_one_para_depth(4, (int)(d4 * 1000.0 + 0.5));
    NiShaUD_VMM_Mode_Start_Task(NULL);
    appendUiLog(QStringLiteral("启动垂线混合测量"));
    onBackHome();
}

void NishaMainWindow::onVdimStart()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    bool oka = false, okb = false;
    const double a = m_editVdimA->text().toDouble(&oka);
    const double b = m_editVdimB->text().toDouble(&okb);
    if (!oka || !okb) {
        QMessageBox::warning(this, QStringLiteral("垂线积深"),
                             QStringLiteral("请填写深度 A / B（米）。"));
        return;
    }
    set_mode_two_para_depth(1, (int)(a * 1000.0 + 0.5));
    set_mode_two_para_depth(2, (int)(b * 1000.0 + 0.5));
    NiShaUD_VDIM_Mode_Start_Task(NULL);
    appendUiLog(QStringLiteral("启动垂线积深测量"));
    onBackHome();
}

void NishaMainWindow::onStopTask()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    NiShaUD_Stop_Running_Task();
    appendUiLog(QStringLiteral("已请求停止任务"));
}

void NishaMainWindow::onManualUp()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    NiShaUD_Stop_Running_Task();
    sleep_ms(10);
    set_dc_motor_speed(600);
    NiShaUD_Manual_Mode_Up_Task(NULL);
    appendUiLog(QStringLiteral("手动上升"));
}

void NishaMainWindow::onManualDown()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    NiShaUD_Stop_Running_Task();
    sleep_ms(10);
    set_dc_motor_speed(600);
    NiShaUD_Manual_Mode_Down_Task(NULL);
    appendUiLog(QStringLiteral("手动下降"));
}

void NishaMainWindow::onManualStop()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    NiShaUD_Stop_Running_Task();
    sleep_ms(10);
    NiShaUD_Manual_Mode_Stop_Task(NULL);
    appendUiLog(QStringLiteral("手动停止"));
}
