#include "nisha_mainwindow.h"
#include "depth_profile_view.h"
#include "board_buzzer.h"
#include "screen_idle_filter.h"

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QFile>
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
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QMetaObject>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <pthread.h>

extern "C" {
#include "NishaApp.h"
#include "nishatask.h"
#include "depth_sensor.h"
#include "sediment_sensor.h"
#include "dc_motor.h"
#include "relay_module.h"
#include "FirmwareVersion.h"
#include "OtaUpgrade.h"
#include "config.h"
#include "SocketLink.h"
#include "Common.h"
}

namespace {

void setLabelTextIfChanged(QLabel *lab, const QString &text)
{
    if (!lab || lab->text() == text)
        return;
    lab->setText(text);
}

/* 业务启动放后台，避免阻塞 Qt 主线程触控 */
void *nishaBusinessStartThread(void *arg)
{
    NishaMainWindow *w = static_cast<NishaMainWindow *>(arg);
    const int rc = Nisha_BusinessStart();
    QMetaObject::invokeMethod(w, "onBusinessReady", Qt::QueuedConnection,
                              Q_ARG(int, rc));
    return NULL;
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

void setOtaBadge(QLabel *lab, const QString &tone, const QString &text)
{
    if (!lab)
        return;
    if (lab->text() == text && lab->property("otaTone").toString() == tone)
        return;
    lab->setText(text);
    lab->setProperty("otaTone", tone);
    QString bg = QStringLiteral("#64748b");
    if (tone == QStringLiteral("busy"))
        bg = QStringLiteral("#0b7a7a");
    else if (tone == QStringLiteral("ok"))
        bg = QStringLiteral("#1a7f37");
    else if (tone == QStringLiteral("fail"))
        bg = QStringLiteral("#b42318");
    lab->setStyleSheet(QStringLiteral(
        "QLabel#otaBadge { color:#ffffff; background-color:%1; border-radius:8px;"
        " padding:8px 18px; font-size:20px; font-weight:bold; }").arg(bg));
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
    box->setObjectName(QStringLiteral("metricBox"));
    QVBoxLayout *vl = new QVBoxLayout(box);
    vl->setContentsMargins(14, 12, 14, 12);
    vl->setSpacing(0);

    QLabel *c = new QLabel(cap, box);
    c->setObjectName(QStringLiteral("metricCap"));
    c->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QFrame *line = new QFrame(box);
    line->setObjectName(QStringLiteral("metricSep"));
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);

    QLabel *v = new QLabel(QStringLiteral("--"), box);
    v->setObjectName(QStringLiteral("metricVal"));
    v->setAlignment(Qt::AlignCenter);

    vl->addWidget(c);
    vl->addSpacing(8);
    vl->addWidget(line);
    vl->addSpacing(10);
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
    , m_pageMenu(0)
    , m_pageScreenIdle(0)
    , m_pageLog(0)
    , m_pageConf(0)
    , m_pageConfEdit(0)
    , m_pageOta(0)
    , m_lblClock(0)
    , m_lblUptime(0)
    , m_lblMode(0)
    , m_lblState(0)
    , m_depthProfile(0)
    , m_depthProfileManual(0)
    , m_valDepthManual(0)
    , m_valDepth(0)
    , m_valSediment(0)
    , m_valKeepDepth(0)
    , m_lblDevDepth(0)
    , m_lblDevSediment(0)
    , m_lblDevMotor(0)
    , m_lblDevRelay(0)
    , m_lblDev4G(0)
    , m_editDefaultDepth(0)
    , m_editVmm1(0)
    , m_editVmm2(0)
    , m_editVmm3(0)
    , m_editVmm4(0)
    , m_editVdimA(0)
    , m_editVdimB(0)
    , m_editDog(0)
    , m_editLift(0)
    , m_editMaxDepth(0)
    , m_editPortSediment(0)
    , m_editPortDepth(0)
    , m_editPortMotor(0)
    , m_editPortRelay(0)
    , m_editPortDtu(0)
    , m_activeEdit(0)
    , m_lblManualLimit(0)
    , m_btnBackOta(0)
    , m_lblOtaState(0)
    , m_lblOtaPercent(0)
    , m_lblOtaBadge(0)
    , m_lblOtaDetail(0)
    , m_otaHero(0)
    , m_otaDetailCard(0)
    , m_otaProgress(0)
    , m_btnScreenIdle()
    , m_log(0)
    , m_confView(0)
    , m_timer(0)
    , m_bizOk(false)
    , m_otaWasBusy(false)
    , m_otaUiLocked(false)
{
    for (int i = 0; i < 5; ++i)
        m_btnScreenIdle[i] = 0;
    /* 先读 tyls.conf，供 EnableManualLift 等界面开关使用 */
    (void)ReadConfig();
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
    addNumPad(lay, parent, 0, 0);
}

void NishaMainWindow::addNumPad(QVBoxLayout *lay, QWidget *parent,
                                QPushButton *extraBtn1, QPushButton *extraBtn2)
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
    clr->setMinimumHeight(48);
    connect(clr, SIGNAL(clicked()), this, SLOT(onNumPadClear()));

    lay->addLayout(pad);
    if (extraBtn1 || extraBtn2) {
        QHBoxLayout *footer = new QHBoxLayout();
        footer->setSpacing(10);
        footer->addWidget(clr, 1);
        if (extraBtn1) {
            extraBtn1->setMinimumHeight(48);
            footer->addWidget(extraBtn1, 1);
        }
        if (extraBtn2) {
            extraBtn2->setMinimumHeight(48);
            footer->addWidget(extraBtn2, 1);
        }
        lay->addLayout(footer);
    } else {
        lay->addWidget(clr);
    }
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
        "QPushButton#danger:pressed { background:#912018; }"
        "QPushButton#secondary { background:#4a5568; }"
        "QPushButton#menuBtn { background:#1a1a1a; }"
        "QPushButton#menuBtn:pressed { background:#000000; }"
        "QPushButton#idleOn { background:#0b7a7a; }"
        "QLabel#title { font-size:28px; font-weight:bold; color:#0f2a44; }"
        "QFrame#appTopBar {"
        "  background-color:#ffffff; border:none; border-bottom:3px solid #0b7a7a;"
        "  border-radius:0px;"
        "}"
        "QLabel#appBrand {"
        "  color:#0f2a44; font-size:32px; font-weight:bold; background:transparent;"
        "}"
        "QLabel#appMeta {"
        "  color:#5a6a7a; font-size:16px; background:transparent;"
        "}"
        "QWidget#metricBox { background:#ffffff; border:1px solid #d5dee8; border-radius:12px; }"
        "QLabel#metricVal { font-size:36px; font-weight:bold; color:#0b4f6c; background:transparent; }"
        "QLabel#metricCap { font-size:15px; font-weight:bold; color:#5a6a7a; background:transparent; }"
        "QFrame#metricSep { background:#d5dee8; border:none; max-height:1px; }"
        "QTextEdit { background:#0f1720; color:#d7e2ec; font-family:monospace;"
        " font-size:14px; border-radius:8px; }"
        /* OTA 全屏页：单块面板，避免百分比与状态之间露底色缝 */
        "QWidget#otaPage { background-color:#dfe8ee; }"
        "QFrame#otaTopBar { background-color:#ffffff; border:none; border-bottom:3px solid #0b7a7a; }"
        "QLabel#otaBrand { color:#102033; font-size:26px; font-weight:bold; background:transparent; }"
        "QFrame#otaPanel {"
        "  background-color:#ffffff; border:2px solid #c9d4df; border-radius:16px;"
        "}"
        "QWidget#otaPage QLabel { background:transparent; }"
        "QLabel#otaPercent { color:#0b7a7a; font-size:96px; font-weight:bold; background:transparent; }"
        "QLabel#otaState { color:#102033; font-size:28px; font-weight:bold; background:transparent; }"
        "QLabel#otaBadge {"
        "  color:#ffffff; background-color:#0b7a7a; border-radius:8px;"
        "  padding:8px 18px; font-size:20px; font-weight:bold;"
        "}"
        "QProgressBar#otaBar {"
        "  border:2px solid #9db0c0; border-radius:12px; background:#dce5ec;"
        "  text-align:center; color:#102033; font-size:18px; font-weight:bold;"
        "  min-height:28px;"
        "}"
        "QProgressBar#otaBar::chunk {"
        "  background-color:#0b7a7a; border-radius:10px; margin:2px;"
        "}"
        "QLabel#otaDetail {"
        "  color:#334155; font-size:18px; background:transparent;"
        "}"
        "QFrame#otaDivider { background-color:#d5dee6; max-height:1px; min-height:1px; border:none; }"
        "QPushButton#backBtn { background:#4a5568; color:#fff; border:none;"
        " border-radius:8px; padding:10px 20px; font-size:18px; font-weight:bold; }"));

    QVBoxLayout *rootLay = new QVBoxLayout(m_root);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    QFrame *topBar = new QFrame(m_root);
    topBar->setObjectName(QStringLiteral("appTopBar"));
    QHBoxLayout *top = new QHBoxLayout(topBar);
    top->setContentsMargins(20, 14, 20, 14);
    top->setSpacing(16);
    QLabel *title = new QLabel(QStringLiteral("泥沙"), topBar);
    title->setObjectName(QStringLiteral("appBrand"));
    m_lblClock = new QLabel(topBar);
    m_lblUptime = new QLabel(topBar);
    m_lblClock->setObjectName(QStringLiteral("appMeta"));
    m_lblUptime->setObjectName(QStringLiteral("appMeta"));
    top->addWidget(title, 1);
    top->addWidget(m_lblUptime);
    top->addWidget(m_lblClock);
    rootLay->addWidget(topBar, 0);

    QWidget *body = new QWidget(m_root);
    QVBoxLayout *bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(12, 12, 12, 10);
    bodyLay->setSpacing(8);
    m_stack = new QStackedWidget(body);
    bodyLay->addWidget(m_stack, 1);
    rootLay->addWidget(body, 1);

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
        m_lblDev4G = new QLabel(m_pageHome);
        const bool showLiftDev = (Config_GetInt("EnableManualLift", 1) != 0);
        if (showLiftDev) {
            m_lblDevMotor = new QLabel(m_pageHome);
            m_lblDevRelay = new QLabel(m_pageHome);
        } else {
            m_lblDevMotor = 0;
            m_lblDevRelay = 0;
        }
        statRow->addWidget(m_lblMode);
        statRow->addWidget(m_lblState);
        statRow->addStretch(1);
        statRow->addWidget(m_lblDevDepth);
        statRow->addWidget(m_lblDevSediment);
        if (showLiftDev) {
            statRow->addWidget(m_lblDevMotor);
            statRow->addWidget(m_lblDevRelay);
        }
        statRow->addWidget(m_lblDev4G);
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
        metricLay->setSpacing(12);
        metricLay->addWidget(makeMetricBox(metricCol, QStringLiteral("当前深度 (m)"), &m_valDepth), 1);
        metricLay->addWidget(makeMetricBox(metricCol, QStringLiteral("泥沙浓度 (kg/m³)"), &m_valSediment), 1);
        if (showLiftDev) {
            metricLay->addWidget(makeMetricBox(metricCol, QStringLiteral("保持深度 (m)"), &m_valKeepDepth), 1);
        } else {
            m_valKeepDepth = 0;
        }
        mainRow->addWidget(metricCol, 0);
        homeLay->addLayout(mainRow, 1);

        QGridLayout *nav = new QGridLayout();
        nav->setSpacing(10);
        QPushButton *bMenu = makeBtn(QStringLiteral("功能菜单"), m_pageHome);
        bMenu->setObjectName(QStringLiteral("secondary"));
        bMenu->setMinimumHeight(56);
        if (showLiftDev) {
            QPushButton *bDef = makeBtn(QStringLiteral("默认定深"), m_pageHome);
            QPushButton *bVmm = makeBtn(QStringLiteral("垂线混合"), m_pageHome);
            QPushButton *bVdim = makeBtn(QStringLiteral("垂线积深"), m_pageHome);
            bDef->setMinimumHeight(64);
            bVmm->setMinimumHeight(64);
            bVdim->setMinimumHeight(64);
            nav->addWidget(bDef, 0, 0);
            nav->addWidget(bVmm, 0, 1);
            nav->addWidget(bVdim, 0, 2);
            nav->addWidget(bMenu, 1, 0, 1, 3);
            connect(bDef, SIGNAL(clicked()), this, SLOT(onShowDefaultPage()));
            connect(bVmm, SIGNAL(clicked()), this, SLOT(onShowVmmPage()));
            connect(bVdim, SIGNAL(clicked()), this, SLOT(onShowVdimPage()));
        } else {
            nav->addWidget(bMenu, 0, 0);
        }
        homeLay->addLayout(nav);

        connect(bMenu, SIGNAL(clicked()), this, SLOT(onShowMenuPage()));
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

    /* 手动（EnableManualLift=0 时不建页）：左侧实时剖面，右侧升降控制 */
    if (Config_GetInt("EnableManualLift", 1) != 0) {
        m_pageManual = new QWidget(m_stack);
        QHBoxLayout *hl = new QHBoxLayout(m_pageManual);
        hl->setContentsMargins(12, 10, 12, 10);
        hl->setSpacing(12);

        m_depthProfileManual = new DepthProfileView(m_pageManual);
        m_depthProfileManual->setMinimumWidth(360);
        hl->addWidget(m_depthProfileManual, 1);

        QWidget *ctrl = new QWidget(m_pageManual);
        ctrl->setMinimumWidth(280);
        ctrl->setMaximumWidth(340);
        QVBoxLayout *vl = new QVBoxLayout(ctrl);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(10);

        QLabel *h = new QLabel(QStringLiteral("手动升降"), ctrl);
        h->setObjectName(QStringLiteral("title"));
        vl->addWidget(h);
        vl->addWidget(new QLabel(QStringLiteral("点按上升/下降，停止立即刹车。"), ctrl));

        m_lblManualLimit = new QLabel(ctrl);
        m_lblManualLimit->setWordWrap(true);
        m_lblManualLimit->setStyleSheet(QStringLiteral("color:#64748b; font-size:13px;"));
        vl->addWidget(m_lblManualLimit);

        QLabel *depthCap = new QLabel(QStringLiteral("当前深度 (m)"), ctrl);
        depthCap->setStyleSheet(QStringLiteral("color:#64748b; font-size:14px;"));
        vl->addWidget(depthCap);
        m_valDepthManual = new QLabel(QStringLiteral("--"), ctrl);
        m_valDepthManual->setAlignment(Qt::AlignCenter);
        m_valDepthManual->setMinimumHeight(56);
        m_valDepthManual->setStyleSheet(QStringLiteral(
            "QLabel { font-size:28px; font-weight:600; background:#f4f7fb;"
            " border:1px solid #c5d0dc; border-radius:8px; }"));
        vl->addWidget(m_valDepthManual);

        QPushButton *up = makeBtn(QStringLiteral("上升"), ctrl);
        QPushButton *down = makeBtn(QStringLiteral("下降"), ctrl);
        QPushButton *stop = makeBtn(QStringLiteral("停止"), ctrl);
        stop->setObjectName(QStringLiteral("danger"));
        up->setMinimumHeight(72);
        down->setMinimumHeight(72);
        stop->setMinimumHeight(72);
        vl->addWidget(up);
        vl->addWidget(down);
        vl->addWidget(stop);

        QPushButton *back = makeBtn(QStringLiteral("返回"), ctrl);
        back->setObjectName(QStringLiteral("secondary"));
        vl->addWidget(back);
        vl->addStretch(1);

        hl->addWidget(ctrl, 0);

        connect(up, SIGNAL(clicked()), this, SLOT(onManualUp()));
        connect(down, SIGNAL(clicked()), this, SLOT(onManualDown()));
        connect(stop, SIGNAL(clicked()), this, SLOT(onManualStop()));
        connect(back, SIGNAL(clicked()), this, SLOT(onShowMenuPage()));
    } else {
        m_pageManual = 0;
        m_depthProfileManual = 0;
        m_valDepthManual = 0;
    }

    /* 功能菜单：黑按钮两列；重启/退出同行红色；底部返回首页 */
    m_pageMenu = new QWidget(m_stack);
    {
        const bool manualLift = (Config_GetInt("EnableManualLift", 1) != 0);
        QVBoxLayout *vl = new QVBoxLayout(m_pageMenu);
        vl->setContentsMargins(24, 20, 24, 20);
        vl->setSpacing(12);
        QLabel *h = new QLabel(QStringLiteral("功能菜单"), m_pageMenu);
        h->setObjectName(QStringLiteral("title"));
        vl->addWidget(h);

        QGridLayout *grid = new QGridLayout();
        grid->setSpacing(12);
        int row = 0;
        if (manualLift) {
            QPushButton *bMan = makeBtn(QStringLiteral("手动升降"), m_pageMenu);
            bMan->setObjectName(QStringLiteral("menuBtn"));
            bMan->setMinimumHeight(56);
            grid->addWidget(bMan, row, 0, 1, 2);
            connect(bMan, SIGNAL(clicked()), this, SLOT(onShowManualPage()));
            row++;
        }
        QPushButton *bLog = makeBtn(QStringLiteral("运行日志"), m_pageMenu);
        bLog->setObjectName(QStringLiteral("menuBtn"));
        bLog->setMinimumHeight(56);
        QPushButton *bIdle = makeBtn(QStringLiteral("息屏设置"), m_pageMenu);
        bIdle->setObjectName(QStringLiteral("menuBtn"));
        bIdle->setMinimumHeight(56);
        grid->addWidget(bLog, row, 0);
        grid->addWidget(bIdle, row, 1);
        row++;
        QPushButton *bConf = makeBtn(QStringLiteral("配置文件"), m_pageMenu);
        bConf->setObjectName(QStringLiteral("menuBtn"));
        bConf->setMinimumHeight(56);
        QPushButton *bStop = makeBtn(QStringLiteral("停止任务"), m_pageMenu);
        bStop->setObjectName(QStringLiteral("menuBtn"));
        bStop->setMinimumHeight(56);
        grid->addWidget(bConf, row, 0);
        grid->addWidget(bStop, row, 1);
        row++;
        QPushButton *bReboot = makeBtn(QStringLiteral("重启设备"), m_pageMenu);
        bReboot->setObjectName(QStringLiteral("danger"));
        bReboot->setMinimumHeight(56);
        QPushButton *bExit = makeBtn(QStringLiteral("退出"), m_pageMenu);
        bExit->setObjectName(QStringLiteral("danger"));
        bExit->setMinimumHeight(56);
        grid->addWidget(bReboot, row, 0);
        grid->addWidget(bExit, row, 1);
        vl->addLayout(grid);

        vl->addStretch(1);
        QPushButton *back = makeBtn(QStringLiteral("返回首页"), m_pageMenu);
        back->setObjectName(QStringLiteral("menuBtn"));
        back->setMinimumHeight(52);
        vl->addWidget(back);

        connect(bLog, SIGNAL(clicked()), this, SLOT(onShowLogPage()));
        connect(bIdle, SIGNAL(clicked()), this, SLOT(onShowScreenIdlePage()));
        connect(bConf, SIGNAL(clicked()), this, SLOT(onShowConfPage()));
        connect(bStop, SIGNAL(clicked()), this, SLOT(onStopTask()));
        connect(bReboot, SIGNAL(clicked()), this, SLOT(onRebootDevice()));
        connect(bExit, SIGNAL(clicked()), this, SLOT(onExit()));
        connect(back, SIGNAL(clicked()), this, SLOT(onBackHome()));
    }

    /* 息屏设置 */
    m_pageScreenIdle = new QWidget(m_stack);
    {
        QVBoxLayout *vl = new QVBoxLayout(m_pageScreenIdle);
        vl->setContentsMargins(24, 20, 24, 20);
        vl->setSpacing(12);
        QLabel *h = new QLabel(QStringLiteral("息屏设置"), m_pageScreenIdle);
        h->setObjectName(QStringLiteral("title"));
        vl->addWidget(h);
        vl->addWidget(new QLabel(
            QStringLiteral("无操作一段时间后黑屏，触摸任意处唤醒（不关背光）。"),
            m_pageScreenIdle));

        static const int minutes[] = {1, 3, 5, 10, 0};
        QGridLayout *grid = new QGridLayout();
        grid->setSpacing(12);
        const QString labels[5] = {
            QStringLiteral("1 分钟"),
            QStringLiteral("3 分钟"),
            QStringLiteral("5 分钟"),
            QStringLiteral("10 分钟"),
            QStringLiteral("不息屏")
        };
        for (int i = 0; i < 5; ++i) {
            m_btnScreenIdle[i] = makeBtn(labels[i], m_pageScreenIdle);
            m_btnScreenIdle[i]->setMinimumHeight(64);
            m_btnScreenIdle[i]->setProperty("idleMinutes", minutes[i]);
            connect(m_btnScreenIdle[i], SIGNAL(clicked()), this, SLOT(onScreenIdleChoice()));
        }
        grid->addWidget(m_btnScreenIdle[0], 0, 0);
        grid->addWidget(m_btnScreenIdle[1], 0, 1);
        grid->addWidget(m_btnScreenIdle[2], 1, 0);
        grid->addWidget(m_btnScreenIdle[3], 1, 1);
        grid->addWidget(m_btnScreenIdle[4], 2, 0, 1, 2);
        vl->addLayout(grid);
        vl->addStretch(1);
        QPushButton *back = makeBtn(QStringLiteral("返回"), m_pageScreenIdle);
        back->setObjectName(QStringLiteral("secondary"));
        back->setMinimumHeight(52);
        vl->addWidget(back);
        connect(back, SIGNAL(clicked()), this, SLOT(onShowMenuPage()));
        refreshScreenIdleButtons();
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
        connect(back, SIGNAL(clicked()), this, SLOT(onShowMenuPage()));
    }

    /* 配置文件（只读显示 tyls.conf） */
    m_pageConf = new QWidget(m_stack);
    {
        QVBoxLayout *vl = new QVBoxLayout(m_pageConf);
        vl->setContentsMargins(12, 10, 12, 10);
        vl->setSpacing(10);

        QHBoxLayout *top = new QHBoxLayout();
        QLabel *h = new QLabel(QStringLiteral("配置文件"), m_pageConf);
        h->setObjectName(QStringLiteral("title"));
        QPushButton *bEdit = makeBtn(QStringLiteral("修改配置"), m_pageConf);
        bEdit->setMinimumHeight(48);
        bEdit->setMinimumWidth(140);
        top->addWidget(h, 1);
        top->addWidget(bEdit);
        vl->addLayout(top);

        m_confView = new QTextEdit(m_pageConf);
        m_confView->setReadOnly(true);
        vl->addWidget(m_confView, 1);
        QPushButton *back = makeBtn(QStringLiteral("返回"), m_pageConf);
        back->setObjectName(QStringLiteral("secondary"));
        vl->addWidget(back);
        connect(bEdit, SIGNAL(clicked()), this, SLOT(onShowConfEditPage()));
        connect(back, SIGNAL(clicked()), this, SLOT(onShowMenuPage()));
    }

    /* 修改配置：左表单 + 右数字键盘；底栏「清空 | 保存 | 返回」 */
    m_pageConfEdit = new QWidget(m_stack);
    {
        QVBoxLayout *vl = new QVBoxLayout(m_pageConfEdit);
        vl->setContentsMargins(12, 8, 12, 10);
        vl->setSpacing(8);

        QLabel *h = new QLabel(QStringLiteral("修改配置"), m_pageConfEdit);
        h->setObjectName(QStringLiteral("title"));
        vl->addWidget(h);
        QLabel *hint = new QLabel(
            QStringLiteral("点选输入框后用右侧键盘填写；串口改后需重启生效。"),
            m_pageConfEdit);
        hint->setStyleSheet(QStringLiteral("color:#5a6a7a; font-size:14px;"));
        hint->setWordWrap(true);
        vl->addWidget(hint);

        QHBoxLayout *body = new QHBoxLayout();
        body->setSpacing(12);

        QFrame *formCard = new QFrame(m_pageConfEdit);
        formCard->setObjectName(QStringLiteral("metricBox"));
        QGridLayout *grid = new QGridLayout(formCard);
        grid->setContentsMargins(12, 10, 12, 10);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(8);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);

        const int labW = 88;
        const int editH = 40;
        auto placeField = [&](int r, int cLab, const QString &name,
                              QLineEdit **editOut, const QString &placeholder) {
            QLabel *lab = new QLabel(name, formCard);
            lab->setFixedWidth(labW);
            lab->setMinimumHeight(editH);
            lab->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
            lab->setStyleSheet(QStringLiteral(
                "font-size:17px; font-weight:bold; color:#0f2a44; background:transparent;"));
            QLineEdit *e = makeDepthEdit(formCard);
            e->setPlaceholderText(placeholder);
            e->setMinimumHeight(editH);
            e->setMaximumHeight(editH);
            e->setMinimumWidth(72);
            e->setStyleSheet(QStringLiteral(
                "QLineEdit { font-size:20px; font-weight:600; background:#f4f7fb;"
                " border:1px solid #c5d0dc; border-radius:8px; padding:2px 6px; }"));
            grid->addWidget(lab, r, cLab, Qt::AlignVCenter | Qt::AlignRight);
            grid->addWidget(e, r, cLab + 1);
            *editOut = e;
        };

        placeField(0, 0, QStringLiteral("看门狗"), &m_editDog, QStringLiteral("0/1"));
        placeField(0, 2, QStringLiteral("手动升降"), &m_editLift, QStringLiteral("0/1"));
        placeField(1, 0, QStringLiteral("最大水深"), &m_editMaxDepth, QStringLiteral("米"));
        placeField(2, 0, QStringLiteral("泥沙口"), &m_editPortSediment, QStringLiteral("1-6"));
        placeField(2, 2, QStringLiteral("深度口"), &m_editPortDepth, QStringLiteral("1-6"));
        placeField(3, 0, QStringLiteral("电机口"), &m_editPortMotor, QStringLiteral("1-6"));
        placeField(3, 2, QStringLiteral("继电器口"), &m_editPortRelay, QStringLiteral("1-6"));
        placeField(4, 0, QStringLiteral("4G口"), &m_editPortDtu, QStringLiteral("1-6"));
        body->addWidget(formCard, 3);

        QFrame *padCard = new QFrame(m_pageConfEdit);
        padCard->setObjectName(QStringLiteral("metricBox"));
        QVBoxLayout *padLay = new QVBoxLayout(padCard);
        padLay->setContentsMargins(10, 10, 10, 10);
        padLay->setSpacing(6);
        QGridLayout *pad = new QGridLayout();
        pad->setHorizontalSpacing(8);
        pad->setVerticalSpacing(8);
        const char *keys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "<-"};
        for (int i = 0; i < 12; ++i) {
            QPushButton *kb = makeBtn(QString::fromLatin1(keys[i]), padCard);
            kb->setMinimumHeight(48);
            kb->setMinimumWidth(56);
            kb->setObjectName(QStringLiteral("secondary"));
            pad->addWidget(kb, i / 3, i % 3);
            if (i == 11)
                connect(kb, SIGNAL(clicked()), this, SLOT(onNumPadBackspace()));
            else
                connect(kb, SIGNAL(clicked()), this, SLOT(onNumPadDigit()));
        }
        padLay->addLayout(pad, 1);
        body->addWidget(padCard, 2);
        vl->addLayout(body, 1);

        QHBoxLayout *footer = new QHBoxLayout();
        footer->setSpacing(12);
        QPushButton *clr = makeBtn(QStringLiteral("清空"), m_pageConfEdit);
        clr->setObjectName(QStringLiteral("secondary"));
        clr->setMinimumHeight(52);
        QPushButton *save = makeBtn(QStringLiteral("保存"), m_pageConfEdit);
        save->setMinimumHeight(52);
        QPushButton *back = makeBtn(QStringLiteral("返回"), m_pageConfEdit);
        back->setObjectName(QStringLiteral("secondary"));
        back->setMinimumHeight(52);
        footer->addWidget(clr, 1);
        footer->addWidget(save, 2);
        footer->addWidget(back, 1);
        vl->addLayout(footer);

        connect(clr, SIGNAL(clicked()), this, SLOT(onNumPadClear()));
        connect(save, SIGNAL(clicked()), this, SLOT(onConfSave()));
        connect(back, SIGNAL(clicked()), this, SLOT(onShowConfPage()));
    }

    /* OTA 全屏进度页（对齐多普勒：百分比 + 徽章 + 进度条 + 详情） */
    m_pageOta = new QWidget(m_stack);
    m_pageOta->setObjectName(QStringLiteral("otaPage"));
    {
        QFrame *otaTopBar = new QFrame(m_pageOta);
        otaTopBar->setObjectName(QStringLiteral("otaTopBar"));
        QLabel *otaBrand = new QLabel(QStringLiteral("固件升级"), otaTopBar);
        otaBrand->setObjectName(QStringLiteral("otaBrand"));
        m_btnBackOta = makeBtn(QStringLiteral("返回首页"), otaTopBar);
        m_btnBackOta->setObjectName(QStringLiteral("backBtn"));
        connect(m_btnBackOta, SIGNAL(clicked()), this, SLOT(onBackHome()));
        /* 升级中隐藏返回，结束后失败才允许离开 */
        m_btnBackOta->setVisible(false);
        QHBoxLayout *otaTop = new QHBoxLayout(otaTopBar);
        otaTop->setContentsMargins(18, 12, 18, 12);
        otaTop->setSpacing(12);
        otaTop->addWidget(otaBrand);
        otaTop->addStretch(1);
        otaTop->addWidget(m_btnBackOta);

        m_otaHero = new QFrame(m_pageOta);
        m_otaHero->setObjectName(QStringLiteral("otaPanel"));
        m_lblOtaPercent = new QLabel(QStringLiteral("0%"), m_otaHero);
        m_lblOtaPercent->setObjectName(QStringLiteral("otaPercent"));
        m_lblOtaPercent->setAlignment(Qt::AlignCenter);
        m_lblOtaState = new QLabel(QStringLiteral("等待升级"), m_otaHero);
        m_lblOtaState->setObjectName(QStringLiteral("otaState"));
        m_lblOtaState->setAlignment(Qt::AlignCenter);
        m_lblOtaState->setWordWrap(true);
        m_lblOtaBadge = new QLabel(QStringLiteral("空闲"), m_otaHero);
        m_lblOtaBadge->setObjectName(QStringLiteral("otaBadge"));
        m_lblOtaBadge->setAlignment(Qt::AlignCenter);
        m_lblOtaBadge->setProperty("otaTone", QStringLiteral("idle"));
        m_lblOtaBadge->setMinimumWidth(160);
        setOtaBadge(m_lblOtaBadge, QStringLiteral("idle"), QStringLiteral("空闲"));
        m_otaProgress = new QProgressBar(m_otaHero);
        m_otaProgress->setObjectName(QStringLiteral("otaBar"));
        m_otaProgress->setRange(0, 100);
        m_otaProgress->setValue(0);
        m_otaProgress->setTextVisible(true);
        m_otaProgress->setFormat(QStringLiteral("%p%"));
        m_otaProgress->setMinimumHeight(36);

        QFrame *divider = new QFrame(m_otaHero);
        divider->setObjectName(QStringLiteral("otaDivider"));
        divider->setFrameShape(QFrame::NoFrame);

        m_otaDetailCard = 0;
        m_lblOtaDetail = new QLabel(QStringLiteral("等待网页下发固件…"), m_otaHero);
        m_lblOtaDetail->setObjectName(QStringLiteral("otaDetail"));
        m_lblOtaDetail->setWordWrap(true);
        m_lblOtaDetail->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        QHBoxLayout *badgeRow = new QHBoxLayout();
        badgeRow->setContentsMargins(0, 0, 0, 0);
        badgeRow->setSpacing(0);
        badgeRow->addStretch(1);
        badgeRow->addWidget(m_lblOtaBadge);
        badgeRow->addStretch(1);

        QVBoxLayout *panelLay = new QVBoxLayout(m_otaHero);
        panelLay->setContentsMargins(28, 22, 28, 22);
        panelLay->setSpacing(8);
        panelLay->addStretch(1);
        panelLay->addWidget(m_lblOtaPercent);
        panelLay->addWidget(m_lblOtaState);
        panelLay->addLayout(badgeRow);
        panelLay->addSpacing(6);
        panelLay->addWidget(m_otaProgress);
        panelLay->addSpacing(10);
        panelLay->addWidget(divider);
        panelLay->addSpacing(10);
        panelLay->addWidget(m_lblOtaDetail, 0);
        panelLay->addStretch(1);

        QVBoxLayout *otaBody = new QVBoxLayout;
        otaBody->setContentsMargins(20, 16, 20, 20);
        otaBody->setSpacing(0);
        otaBody->addWidget(m_otaHero, 1);

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
    if (m_pageManual)
        m_stack->addWidget(m_pageManual);
    m_stack->addWidget(m_pageMenu);
    m_stack->addWidget(m_pageScreenIdle);
    m_stack->addWidget(m_pageLog);
    m_stack->addWidget(m_pageConf);
    m_stack->addWidget(m_pageConfEdit);
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
        else if (watched == m_editDog)
            setActiveEdit(m_editDog);
        else if (watched == m_editLift)
            setActiveEdit(m_editLift);
        else if (watched == m_editMaxDepth)
            setActiveEdit(m_editMaxDepth);
        else if (watched == m_editPortSediment)
            setActiveEdit(m_editPortSediment);
        else if (watched == m_editPortDepth)
            setActiveEdit(m_editPortDepth);
        else if (watched == m_editPortMotor)
            setActiveEdit(m_editPortMotor);
        else if (watched == m_editPortRelay)
            setActiveEdit(m_editPortRelay);
        else if (watched == m_editPortDtu)
            setActiveEdit(m_editPortDtu);
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
    pthread_t th;
    if (pthread_create(&th, NULL, nishaBusinessStartThread, this) != 0) {
        appendUiLog(QStringLiteral("无法创建业务启动线程，改为同步启动"));
        onBusinessReady(Nisha_BusinessStart());
        return;
    }
    pthread_detach(th);
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
            /* Kick 留在业务定时器线程，避免 UI 刷新路径同步发网络 */
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
    if (m_valDepthManual)
        setLabelTextIfChanged(m_valDepthManual,
                              depthOk ? QString::number(depthM, 'f', 3) : QStringLiteral("--"));

    /* 在线优先显示瞬时浓度；平均浓度仍留给测量/上报（满 50 点才更新） */
    float sed = 0.0f;
    if (is_sediment_sensor_online()) {
        sed = sediment_fill_milli_look(sediment_sensor_info.sediment_value);
        setLabelTextIfChanged(m_valSediment, QString::number(sed, 'f', 3));
    } else if (g_sediment_data_average > 0.0f) {
        sed = g_sediment_data_average;
        setLabelTextIfChanged(m_valSediment, QString::number(sed, 'f', 3));
    } else {
        setLabelTextIfChanged(m_valSediment, QStringLiteral("--"));
    }

    const bool showLift = (Config_GetInt("EnableManualLift", 1) != 0);
    const int keepMm = get_default_mode_keep_depth();
    const bool keepOk = showLift && keepMm > 0;
    const double keepM = keepMm / 1000.0;
    if (m_valKeepDepth)
        setLabelTextIfChanged(m_valKeepDepth, QString::number(keepM, 'f', 3));

    QVector<double> targets;
    const int mode = get_nisha_task_mode();
    if (showLift && mode == SedimentUD_Mode_VMM_Measurement) {
        for (int i = 1; i <= 4; ++i) {
            const int mm = get_mode_one_para_depth(i);
            if (mm > 0)
                targets.append(mm / 1000.0);
        }
    } else if (showLift && mode == SedimentUD_Mode_VDIM_Measurement) {
        for (int i = 1; i <= 2; ++i) {
            const int mm = get_mode_two_para_depth(i);
            if (mm > 0)
                targets.append(mm / 1000.0);
        }
    }
    if (m_depthProfile)
        m_depthProfile->setLiveData(depthM, depthOk, keepM, keepOk, (double)sed, targets,
                                    Config_GetMaxWaterDepthMm() / 1000.0);
    if (m_depthProfileManual)
        m_depthProfileManual->setLiveData(depthM, depthOk, keepM, keepOk, (double)sed, targets,
                                          Config_GetMaxWaterDepthMm() / 1000.0);
    if (m_lblManualLimit) {
        const double minM = Config_GetMinWaterDepthMm() / 1000.0;
        const double maxM = Config_GetMaxWaterDepthMm() / 1000.0;
        setLabelTextIfChanged(m_lblManualLimit,
                              QStringLiteral("软限位：最高升至 %1 m，最低降至 %2 m")
                                  .arg(minM, 0, 'f', 2).arg(maxM, 0, 'f', 2));
    }

    setDevChip(m_lblDevDepth, QStringLiteral("深度"), depthOk);
    setDevChip(m_lblDevSediment, QStringLiteral("泥沙"), is_sediment_sensor_online());
    if (m_lblDevMotor)
        setDevChip(m_lblDevMotor, QStringLiteral("电机"), is_dc_motor_online());
    if (m_lblDevRelay)
        setDevChip(m_lblDevRelay, QStringLiteral("继电器"), isRelayModuleOnline());
    if (m_lblDev4G)
        setDevChip(m_lblDev4G, QStringLiteral("4G"), WebLink_IsReady() != 0);
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
    if (m_otaUiLocked || !m_bizOk || Config_GetInt("EnableManualLift", 1) == 0)
        return;
    const int keep = get_default_mode_keep_depth();
    if (keep > 0)
        m_editDefaultDepth->setText(QString::number(keep / 1000.0, 'f', 3));
    setActiveEdit(m_editDefaultDepth);
    m_stack->setCurrentWidget(m_pageDefault);
}

void NishaMainWindow::onShowVmmPage()
{
    if (m_otaUiLocked || !m_bizOk || Config_GetInt("EnableManualLift", 1) == 0)
        return;
    setActiveEdit(m_editVmm1);
    m_stack->setCurrentWidget(m_pageVmm);
}

void NishaMainWindow::onShowVdimPage()
{
    if (m_otaUiLocked || !m_bizOk || Config_GetInt("EnableManualLift", 1) == 0)
        return;
    setActiveEdit(m_editVdimA);
    m_stack->setCurrentWidget(m_pageVdim);
}

void NishaMainWindow::onShowManualPage()
{
    if (m_otaUiLocked || !m_bizOk || !m_pageManual)
        return;
    m_stack->setCurrentWidget(m_pageManual);
}

void NishaMainWindow::onShowMenuPage()
{
    if (m_otaUiLocked)
        return;
    m_stack->setCurrentWidget(m_pageMenu);
}

void NishaMainWindow::onShowScreenIdlePage()
{
    if (m_otaUiLocked)
        return;
    refreshScreenIdleButtons();
    m_stack->setCurrentWidget(m_pageScreenIdle);
}

void NishaMainWindow::refreshScreenIdleButtons()
{
    const int cur = Config_GetInt("ScreenIdleMinutes", 0);
    for (int i = 0; i < 5; ++i) {
        if (!m_btnScreenIdle[i])
            continue;
        const int mins = m_btnScreenIdle[i]->property("idleMinutes").toInt();
        if (mins == cur)
            m_btnScreenIdle[i]->setObjectName(QStringLiteral("idleOn"));
        else
            m_btnScreenIdle[i]->setObjectName(QString());
        m_btnScreenIdle[i]->style()->unpolish(m_btnScreenIdle[i]);
        m_btnScreenIdle[i]->style()->polish(m_btnScreenIdle[i]);
        m_btnScreenIdle[i]->update();
    }
}

void NishaMainWindow::onScreenIdleChoice()
{
    if (m_otaUiLocked)
        return;
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;
    const int mins = btn->property("idleMinutes").toInt();
    if (Config_SetInt("ScreenIdleMinutes", mins) != 0) {
        appendUiLog(QStringLiteral("息屏设置保存失败"));
        QMessageBox::warning(this, QStringLiteral("息屏设置"),
                             QStringLiteral("写入配置文件失败。"));
        return;
    }
    if (ScreenIdleFilter::instance())
        ScreenIdleFilter::instance()->setIdleMinutes(mins);
    refreshScreenIdleButtons();
    if (mins <= 0)
        appendUiLog(QStringLiteral("息屏：已关闭"));
    else
        appendUiLog(QStringLiteral("息屏：%1 分钟").arg(mins));
}

void NishaMainWindow::onShowLogPage()
{
    if (m_otaUiLocked)
        return;
    m_stack->setCurrentWidget(m_pageLog);
}

void NishaMainWindow::loadConfView()
{
    if (!m_confView)
        return;

    char appDir[MAX_PATH];
    QString path;
    memset(appDir, 0, sizeof(appDir));
    if (GetAppDir(appDir))
        path = QString::fromLocal8Bit(appDir) + QStringLiteral("/tyls.conf");
    else
        path = QStringLiteral("tyls.conf");

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_confView->setPlainText(QStringLiteral("无法打开配置文件：\n%1").arg(path));
        return;
    }
    const QByteArray raw = f.readAll();
    f.close();
    m_confView->setPlainText(QString::fromUtf8(raw));
}

void NishaMainWindow::onShowConfPage()
{
    if (m_otaUiLocked)
        return;
    loadConfView();
    m_stack->setCurrentWidget(m_pageConf);
}

void NishaMainWindow::loadConfEditForm()
{
    if (m_editDog)
        m_editDog->setText(QString::number(Config_GetInt("DOG", 0) ? 1 : 0));
    if (m_editLift)
        m_editLift->setText(QString::number(Config_GetInt("EnableManualLift", 1) ? 1 : 0));
    if (m_editMaxDepth)
        m_editMaxDepth->setText(QString::number(Config_GetMaxWaterDepthMm() / 1000.0, 'f', 3));
    if (m_editPortSediment)
        m_editPortSediment->setText(QString::number(Config_GetInt("SEDIMENT_SENSOR", 3)));
    if (m_editPortDepth)
        m_editPortDepth->setText(QString::number(Config_GetInt("DEPTH_SENSOR", 3)));
    if (m_editPortMotor)
        m_editPortMotor->setText(QString::number(Config_GetInt("DC_MOTOR", 1)));
    if (m_editPortRelay)
        m_editPortRelay->setText(QString::number(Config_GetInt("RELAY_MODULE", 4)));
    if (m_editPortDtu)
        m_editPortDtu->setText(QString::number(Config_GetInt("4G_DTU", 2)));
    setActiveEdit(m_editDog ? m_editDog : m_editMaxDepth);
}

void NishaMainWindow::onShowConfEditPage()
{
    if (m_otaUiLocked)
        return;
    loadConfEditForm();
    m_stack->setCurrentWidget(m_pageConfEdit);
}

bool NishaMainWindow::parse01Field(const QString &text, int *out, const QString &name, QString *err) const
{
    bool ok = false;
    const int v = text.trimmed().toInt(&ok);
    if (!ok || (v != 0 && v != 1)) {
        if (err)
            *err = QStringLiteral("%1 只能填 0 或 1。").arg(name);
        return false;
    }
    if (out)
        *out = v;
    return true;
}

bool NishaMainWindow::parsePortField(const QString &text, int *out, const QString &name, QString *err) const
{
    bool ok = false;
    const int v = text.trimmed().toInt(&ok);
    if (!ok || v < 1 || v > 6) {
        if (err)
            *err = QStringLiteral("%1 须为串口号 1～6（COM1～COM6）。").arg(name);
        return false;
    }
    if (out)
        *out = v;
    return true;
}

bool NishaMainWindow::parseDepthMeters(const QString &text, double *outMeters, QString *err) const
{
    bool ok = false;
    const double meters = text.trimmed().toDouble(&ok);
    const double minM = Config_GetMinWaterDepthMm() / 1000.0;
    const double maxM = Config_GetMaxWaterDepthMm() / 1000.0;
    if (!ok || meters < minM || meters > maxM) {
        if (err) {
            *err = QStringLiteral("请输入有效深度（米），范围 %1～%2。")
                       .arg(minM, 0, 'f', 2).arg(maxM, 0, 'f', 2);
        }
        return false;
    }
    if (outMeters)
        *outMeters = meters;
    return true;
}

void NishaMainWindow::onConfSave()
{
    if (m_otaUiLocked)
        return;

    int dog = 0, lift = 0;
    int portSed = 0, portDepth = 0, portMotor = 0, portRelay = 0, portDtu = 0;
    QString err;
    if (!parse01Field(m_editDog ? m_editDog->text() : QString(), &dog,
                      QStringLiteral("看门狗"), &err)
            || !parse01Field(m_editLift ? m_editLift->text() : QString(), &lift,
                             QStringLiteral("手动升降"), &err)
            || !parsePortField(m_editPortSediment ? m_editPortSediment->text() : QString(),
                               &portSed, QStringLiteral("泥沙口"), &err)
            || !parsePortField(m_editPortDepth ? m_editPortDepth->text() : QString(),
                               &portDepth, QStringLiteral("深度口"), &err)
            || !parsePortField(m_editPortMotor ? m_editPortMotor->text() : QString(),
                               &portMotor, QStringLiteral("电机口"), &err)
            || !parsePortField(m_editPortRelay ? m_editPortRelay->text() : QString(),
                               &portRelay, QStringLiteral("继电器口"), &err)
            || !parsePortField(m_editPortDtu ? m_editPortDtu->text() : QString(),
                               &portDtu, QStringLiteral("4G口"), &err)) {
        QMessageBox::warning(this, QStringLiteral("修改配置"), err);
        return;
    }

    bool okMax = false;
    const double maxM = m_editMaxDepth ? m_editMaxDepth->text().trimmed().toDouble(&okMax) : 0.0;
    if (!okMax || maxM < 0.5 || maxM > 50.0) {
        QMessageBox::warning(this, QStringLiteral("修改配置"),
                             QStringLiteral("最大水深须在 0.5～50 米之间。"));
        return;
    }
    const double minM = Config_GetDouble("MinWaterDepth", 0.1);
    if (maxM <= minM + 0.2) {
        QMessageBox::warning(this, QStringLiteral("修改配置"),
                             QStringLiteral("最大水深须明显大于最小水深（当前 Min=%1 m）。")
                                 .arg(minM, 0, 'f', 2));
        return;
    }

    if (Config_SetInt("DOG", dog) != 0
            || Config_SetInt("EnableManualLift", lift) != 0
            || Config_SetDouble("MaxWaterDepth", maxM) != 0
            || Config_SetInt("SEDIMENT_SENSOR", portSed) != 0
            || Config_SetInt("DEPTH_SENSOR", portDepth) != 0
            || Config_SetInt("DC_MOTOR", portMotor) != 0
            || Config_SetInt("RELAY_MODULE", portRelay) != 0
            || Config_SetInt("4G_DTU", portDtu) != 0) {
        QMessageBox::warning(this, QStringLiteral("修改配置"),
                             QStringLiteral("写入 tyls.conf 失败。"));
        return;
    }

    appendUiLog(QStringLiteral("配置已保存 DOG=%1 Lift=%2 Max=%3 口泥/深/电/继/4G=%4/%5/%6/%7/%8")
                    .arg(dog).arg(lift).arg(maxM, 0, 'f', 1)
                    .arg(portSed).arg(portDepth).arg(portMotor).arg(portRelay).arg(portDtu));
    QMessageBox::information(this, QStringLiteral("修改配置"),
                             QStringLiteral("已写入配置文件。\n"
                                            "最大水深可立即生效；串口映射 / 手动升降等需重启后生效。"));
    loadConfView();
    m_stack->setCurrentWidget(m_pageConf);
}

void NishaMainWindow::setOtaUiLocked(bool locked)
{
    if (m_otaUiLocked == locked)
        return;
    m_otaUiLocked = locked;
    if (m_btnBackOta) {
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
    case OTA_STATUS_RECEIVING: return QStringLiteral("接收固件");
    case OTA_STATUS_APPLYING: return QStringLiteral("校验 / 切槽");
    case OTA_STATUS_OK: return QStringLiteral("成功（即将重启）");
    case OTA_STATUS_ERROR: return QStringLiteral("失败");
    default: return QStringLiteral("未知(%1)").arg(status);
    }
}

QString NishaMainWindow::otaErrText(unsigned err)
{
    switch (err) {
    case OTA_ERR_NONE: return QStringLiteral("无");
    case OTA_ERR_PARAM: return QStringLiteral("参数错误");
    case OTA_ERR_BUSY: return QStringLiteral("忙");
    case OTA_ERR_SIZE: return QStringLiteral("尺寸错误");
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
        setLabelTextIfChanged(m_lblOtaState, QStringLiteral("OTA 未初始化"));
        setLabelTextIfChanged(m_lblOtaPercent, QStringLiteral("--"));
        setOtaBadge(m_lblOtaBadge, QStringLiteral("idle"), QStringLiteral("不可用"));
        if (m_otaProgress)
            m_otaProgress->setValue(0);
        setLabelTextIfChanged(m_lblOtaDetail,
                              QStringLiteral("当前固件版本：%1").arg(QString::fromLatin1(fw)));
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
            setLabelTextIfChanged(m_lblOtaState, QStringLiteral("升级成功，即将重启"));
            setOtaBadge(m_lblOtaBadge, QStringLiteral("ok"), QStringLiteral("成功"));
        } else {
            setLabelTextIfChanged(m_lblOtaState,
                                  QStringLiteral("正在升级 · %1").arg(otaStatusText(st.status)));
            setOtaBadge(m_lblOtaBadge, QStringLiteral("busy"), QStringLiteral("进行中"));
        }
    } else {
        if (m_otaWasBusy) {
            appendUiLog(QStringLiteral("OTA 会话结束"));
            m_otaWasBusy = false;
            setOtaUiLocked(false);
            if (m_stack->currentWidget() == m_pageOta && m_btnBackOta)
                m_btnBackOta->setVisible(true);
        }
        if (failed) {
            setLabelTextIfChanged(m_lblOtaState, QStringLiteral("升级失败"));
            setOtaBadge(m_lblOtaBadge, QStringLiteral("fail"), QStringLiteral("失败"));
        } else {
            setLabelTextIfChanged(m_lblOtaState, QStringLiteral("等待网页下发固件"));
            setOtaBadge(m_lblOtaBadge, QStringLiteral("idle"), QStringLiteral("空闲"));
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
    QString detail = QStringLiteral("当前固件  %1\n活动槽位  %2\n")
            .arg(QString::fromLatin1(fw)).arg(slot);
    if (blocking) {
        detail += QStringLiteral("会话版本  %1\n包进度    %2 / %3\n已接收    %4 / %5 字节")
                .arg(QString::fromLatin1(st.version))
                .arg(donePkt)
                .arg(st.pkt_total)
                .arg(st.recv_size)
                .arg(st.file_size);
        if (success)
            detail += QStringLiteral("\n\n新固件已就绪，设备即将重启激活…");
    } else if (failed) {
        detail += QStringLiteral("失败原因  %1\n目标版本  %2\n\n可点右上角返回首页，稍后由网页重试。")
                .arg(otaErrText(st.last_err))
                .arg(QString::fromLatin1(st.last_version));
    } else {
        detail += QStringLiteral("\n尚未开始传输。收到网页升级指令后将自动全屏锁定。");
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
    qApp->quit();
    ::_exit(0);
}

void NishaMainWindow::onRebootDevice()
{
    if (m_otaUiLocked || OtaUpgrade_IsBusy()) {
        QMessageBox::warning(this, QStringLiteral("升级中"),
                             QStringLiteral("正在升级固件，请勿重启或断电。"));
        return;
    }
    const int ret = QMessageBox::question(
        this,
        QStringLiteral("重启设备"),
        QStringLiteral("确定要重启设备吗？"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    appendUiLog(QStringLiteral("用户确认重启设备"));
    BoardBuzzer::forceOff();
    sync();
    (void)system("reboot");
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
    double meters = 0.0;
    QString err;
    if (!parseDepthMeters(m_editDefaultDepth->text(), &meters, &err)) {
        QMessageBox::warning(this, QStringLiteral("默认定深"), err);
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
    double d1 = 0.0, d2 = 0.0, d3 = 0.0, d4 = 0.0;
    QString err;
    if (!parseDepthMeters(m_editVmm1->text(), &d1, &err)
            || !parseDepthMeters(m_editVmm2->text(), &d2, &err)
            || !parseDepthMeters(m_editVmm3->text(), &d3, &err)
            || !parseDepthMeters(m_editVmm4->text(), &d4, &err)) {
        QMessageBox::warning(this, QStringLiteral("垂线混合"), err);
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
    double a = 0.0, b = 0.0;
    QString err;
    if (!parseDepthMeters(m_editVdimA->text(), &a, &err)
            || !parseDepthMeters(m_editVdimB->text(), &b, &err)) {
        QMessageBox::warning(this, QStringLiteral("垂线积深"), err);
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
    if (m_otaUiLocked || !m_bizOk || !m_pageManual)
        return;
    const int cur = get_depth_sensor_depth();
    const int minMm = Config_GetMinWaterDepthMm();
    if (is_depth_sensor_online() && cur <= minMm) {
        QMessageBox::information(this, QStringLiteral("手动升降"),
                                 QStringLiteral("已到最高位置（最浅 %1 m），无法再上升。")
                                     .arg(minMm / 1000.0, 0, 'f', 2));
        return;
    }
    NiShaUD_Stop_Running_Task();
    sleep_ms(10);
    set_dc_motor_speed(600);
    NiShaUD_Manual_Mode_Up_Task(NULL);
    appendUiLog(QStringLiteral("手动上升"));
}

void NishaMainWindow::onManualDown()
{
    if (m_otaUiLocked || !m_bizOk || !m_pageManual)
        return;
    const int cur = get_depth_sensor_depth();
    const int maxMm = Config_GetMaxWaterDepthMm();
    if (is_depth_sensor_online() && cur >= maxMm) {
        QMessageBox::information(this, QStringLiteral("手动升降"),
                                 QStringLiteral("已到最低位置（最深 %1 m），无法再下降。")
                                     .arg(maxMm / 1000.0, 0, 'f', 2));
        return;
    }
    NiShaUD_Stop_Running_Task();
    sleep_ms(10);
    set_dc_motor_speed(600);
    NiShaUD_Manual_Mode_Down_Task(NULL);
    appendUiLog(QStringLiteral("手动下降"));
}

void NishaMainWindow::onManualStop()
{
    if (m_otaUiLocked || !m_bizOk || !m_pageManual)
        return;
    NiShaUD_Stop_Running_Task();
    sleep_ms(10);
    NiShaUD_Manual_Mode_Stop_Task(NULL);
    appendUiLog(QStringLiteral("手动停止"));
}
