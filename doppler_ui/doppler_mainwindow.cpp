#include "doppler_mainwindow.h"
#include "section_view.h"
#include "board_buzzer.h"
#include "board_display.h"

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
#include <QMap>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTextCodec>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <cstring>
#include <cstdlib>
#include <unistd.h>

extern "C" {
#include "Common.h"
#include "Sensor.h"
#include "Command_proc.h"
#include "DopplerApp.h"
#include "WebSeriver.h"
#include "OtaUpgrade.h"
#include "log.h"
#include "FirmwareVersion.h"
#include "SocketLink.h"
#include "UplinkOffline.h"
#include "SectionParam.h"
#include "Dam5213.h"
}

namespace {
const size_t kDeviceLogExportMax = 512U * 1024U;

/* 仅当文案变化时 setText，减少无谓重绘。 */
void setLabelTextIfChanged(QLabel *lab, const QString &text)
{
    if (lab == 0)
        return;
    if (lab->text() == text)
        return;
    lab->setText(text);
}

/* 设置顶栏状态灯 HTML（绿/红圆点 + 名称）。 */
void setStatusChip(QLabel *lab, const QString &name, bool ok)
{
    if (lab == 0)
        return;
    const QString color = ok ? QStringLiteral("#1a7f37") : QStringLiteral("#b42318");
    const QString html = QStringLiteral(
        "<span style=\"color:%1;font-size:22px;\">●</span>&nbsp;%2")
            .arg(color, name);
    if (lab->text() == html)
        return;
    lab->setText(html);
}

/* 更新 OTA 徽章文案与色调（busy/ok/fail/idle）。 */
void setOtaBadge(QLabel *lab, const QString &tone, const QString &text)
{
    if (lab == 0)
        return;
    setLabelTextIfChanged(lab, text);
    if (lab->property("otaTone").toString() == tone)
        return;
    lab->setProperty("otaTone", tone);
    QString bg = QStringLiteral("#64748b");
    if (tone == QLatin1String("busy"))
        bg = QStringLiteral("#0b7a7a");
    else if (tone == QLatin1String("ok"))
        bg = QStringLiteral("#1a7f37");
    else if (tone == QLatin1String("fail"))
        bg = QStringLiteral("#b42318");
    lab->setStyleSheet(QStringLiteral(
        "QLabel#otaBadge { color:#ffffff; background-color:%1; border-radius:8px;"
        " padding:8px 18px; font-size:20px; font-weight:bold; }").arg(bg));
}

/* 检测字符串是否含中日韩统一表意文字。 */
bool confTextHasCjk(const QString &text)
{
    for (int i = 0; i < text.size(); ++i) {
        const uint u = text.at(i).unicode();
        if (u >= 0x4E00U && u <= 0x9FFFU)
            return true;
    }
    return false;
}

/* 将 conf 原始字节按 UTF-8/本地编码解码为 QString。 */
QString decodeConfBytes(const QByteArray &rawIn)
{
    QByteArray raw = rawIn;
    if (raw.startsWith("\xEF\xBB\xBF"))
        raw = raw.mid(3);

    const QString utf8 = QString::fromUtf8(raw);
    const int utf8Bad = utf8.count(QChar::ReplacementCharacter);
    if (utf8Bad == 0 && (confTextHasCjk(utf8) || !raw.contains((char)0x80)))
        return utf8;
    /* 纯 ASCII / 无高位也可直接用 UTF-8 */
    if (utf8Bad == 0)
        return utf8;

    QTextCodec *gb = QTextCodec::codecForName("GB18030");
    if (gb == 0)
        gb = QTextCodec::codecForName("GBK");
    if (gb != 0) {
        const QString gbk = gb->toUnicode(raw);
        if (gbk.count(QChar::ReplacementCharacter) < utf8Bad || confTextHasCjk(gbk))
            return gbk;
    }

    if (utf8Bad == 0)
        return utf8;
    return QString::fromLocal8Bit(raw);
}

/* 为控件设置可显示中文的 UI 字体族。 */
void applyCjkUiFont(QWidget *w)
{
    if (w == 0)
        return;
    static QString s_family;
    if (s_family.isEmpty()) {
        const char *names[] = {
            "WenQuanYi Micro Hei",
            "WenQuanYi Zen Hei",
            "Noto Sans CJK SC",
            "Source Han Sans SC",
            "Droid Sans Fallback",
            "DroidSansFallback",
            0
        };
        const QStringList families = QFontDatabase().families();
        for (int i = 0; names[i] != 0; ++i) {
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
                "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
                "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
                "/usr/share/fonts/DroidSansFallback.ttf",
                0
            };
            for (int i = 0; paths[i] != 0; ++i) {
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
}

/* 构造主窗口：初始化成员、buildUi、启动 1s 刷新定时器。 */
DopplerMainWindow::DopplerMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_root(0)
    , m_stack(0)
    , m_pageHome(0)
    , m_pageLog(0)
    , m_pageFunc(0)
    , m_pageConfig(0)
    , m_pageConfigEdit(0)
    , m_pageWater(0)
    , m_pageOta(0)
    , m_pageDevLog(0)
    , m_pageBlankSettings(0)
    , m_pageExitAuth(0)
    , m_blankCover(0)
    , m_lblStatBiz(0)
    , m_lblStatSensor(0)
    , m_lblStatLink(0)
    , m_sectionView(0)
    , m_cfgActiveEdit(0)
    , m_blankMinutes(5)
    , m_screenBlanked(false)
    , m_bizOk(false)
    , m_otaWasBusy(false)
    , m_otaUiLocked(false)
{
    for (int i = 0; i < 5; ++i)
        m_btnBlankMin[i] = 0;
    buildUi();
    m_blankMinutes = loadBlankMinutesFromConf();
    if (m_blankMinutes < 1 || m_blankMinutes > 5)
        m_blankMinutes = 5;
    refreshBlankMinuteButtons();
    m_uptime.start();
    m_idleSince.start();
    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(onRefresh()));
    m_timer->start(1000);
    qApp->installEventFilter(this);
    setBizEnabled(false);
    appendUiLog(QStringLiteral("界面已显示，正在启动业务…"));
    appendUiLog(QStringLiteral("息屏时间：%1 分钟（菜单可改）").arg(m_blankMinutes));
}

/* 析构主窗口。 */
DopplerMainWindow::~DopplerMainWindow()
{
}

/* 拼接并返回 doppler.conf 绝对路径。 */
QString DopplerMainWindow::configFilePath() const
{
    char dir[MAX_PATH];
    memset(dir, 0, sizeof(dir));
    if (!GetAppDir(dir))
        return QString();
    return QString::fromLocal8Bit(dir) + QStringLiteral(CONFIG_FILE_PATH);
}

/* 创建首页指标卡片；compact 用于右侧竖排小卡片。 */
QWidget *DopplerMainWindow::makeMetricTile(const QString &caption, QLabel **valueLabel, bool compact)
{
    QFrame *tile = new QFrame(m_pageHome);
    tile->setObjectName(compact ? QStringLiteral("metricTileCompact")
                                : QStringLiteral("metricTile"));
    tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QLabel *cap = new QLabel(caption, tile);
    cap->setObjectName(compact ? QStringLiteral("metricCapCompact")
                               : QStringLiteral("metricCap"));
    cap->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    cap->setWordWrap(true);

    QLabel *val = new QLabel(QStringLiteral("--"), tile);
    val->setObjectName(compact ? QStringLiteral("metricValCompact")
                               : QStringLiteral("metricVal"));
    val->setAlignment(Qt::AlignCenter);
    val->setWordWrap(true);

    QVBoxLayout *vl = new QVBoxLayout(tile);
    vl->setContentsMargins(compact ? 8 : 12, compact ? 6 : 14,
                           compact ? 8 : 12, compact ? 6 : 14);
    vl->setSpacing(compact ? 2 : 6);
    vl->addWidget(cap);
    vl->addWidget(val, 1);

    *valueLabel = val;
    return tile;
}

/* 在配置编辑网格中创建「标题 + 输入框」一对控件。 */
QLineEdit *DopplerMainWindow::makeCfgField(QWidget *parent, const QString &caption,
                                          QGridLayout *grid, int row, int pair,
                                          const char *keyName)
{
    QLabel *lab = new QLabel(caption, parent);
    lab->setObjectName(QStringLiteral("cfgFieldLab"));
    lab->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QLineEdit *edit = new QLineEdit(parent);
    edit->setObjectName(QStringLiteral("cfgFieldEdit"));
    edit->setReadOnly(true);
    edit->setProperty("confKey", QString::fromLatin1(keyName));
    edit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    edit->setFixedHeight(44);
    edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    edit->installEventFilter(this);
    const int baseCol = pair * 2;
    grid->addWidget(lab, row, baseCol);
    grid->addWidget(edit, row, baseCol + 1);
    return edit;
}

/* 搭建全部页面布局、样式表与信号槽。 */
void DopplerMainWindow::buildUi()
{
    m_root = new QWidget(this);
    setCentralWidget(m_root);
    m_root->setObjectName(QStringLiteral("root"));

    m_root->setStyleSheet(QStringLiteral(
        "QWidget#root { background-color:#f3f6f8; color:#1a2330; }"
        "QFrame#topBar { background-color:#ffffff; border:none; border-bottom:3px solid #0b7a7a; }"
        "QLabel#brand { color:#102033; font-size:26px; font-weight:bold; }"
        "QLabel#clock { color:#334155; font-size:20px; font-weight:bold; }"
        "QLabel#statusChip {"
        "  color:#243447; font-size:18px; font-weight:bold;"
        "  padding:2px 4px;"
        "}"
        "QLabel#metaText { color:#243447; font-size:20px; font-weight:bold; }"
        "QFrame#metricTile { background-color:#ffffff; border:2px solid #c9d4df; border-radius:10px; }"
        "QFrame#metricTileCompact { background-color:#ffffff; border:2px solid #c9d4df; border-radius:8px; }"
        "QLabel#metricCap { color:#3b4d61; font-size:18px; font-weight:bold; }"
        "QLabel#metricCapCompact { color:#3b4d61; font-size:13px; font-weight:bold; }"
        "QLabel#metricVal { color:#0b1c2c; font-size:40px; font-weight:bold; }"
        "QLabel#metricValCompact { color:#0b1c2c; font-size:22px; font-weight:bold; }"
        "QLabel#uptimeChip { color:#334155; font-size:16px; font-weight:bold; }"
        "QLabel#pageTitle { color:#102033; font-size:24px; font-weight:bold; }"
        "QLabel#hintText { color:#3b4d61; font-size:16px; }"
        "QPushButton {"
        "  background-color:#0b7a7a; color:#ffffff; border:none; border-radius:8px;"
        "  padding:10px 14px; font-size:20px; font-weight:bold; min-height:52px;"
        "}"
        "QPushButton:pressed { background-color:#096666; }"
        "QPushButton:disabled { background-color:#b7c2cc; color:#5b6b7a; }"
        /* 选中态（息屏分钟等）：同色加深描边，避免菜单里蓝/青混排 */
        "QPushButton#selectedBtn {"
        "  background-color:#086363; border:3px solid #0a2f2f;"
        "}"
        "QPushButton#selectedBtn:pressed { background-color:#064949; }"
        "QPushButton#dangerBtn { background-color:#b42318; }"
        "QPushButton#dangerBtn:pressed { background-color:#912018; }"
        "QPushButton#exitBtn { background-color:#b42318; min-width:100px; min-height:48px;"
        "  padding:8px 16px; font-size:18px; }"
        "QPushButton#exitBtn:pressed { background-color:#912018; }"
        "QPushButton#backBtn { background-color:#475569; min-width:140px; }"
        "QPushButton#backBtn:pressed { background-color:#334155; }"
        "QPushButton#cfgPadBtn { min-height:32px; max-height:42px; padding:4px 6px; font-size:16px; }"
        "QPushButton#padActionBtn { background-color:#475569; }"
        "QPushButton#padActionBtn:pressed { background-color:#334155; }"
        "QLabel#cfgFieldLab { color:#3b4d61; font-size:18px; font-weight:bold; }"
        "QLineEdit {"
        "  background:#ffffff; border:2px solid #c9d4df; border-radius:6px;"
        "  font-size:18px; padding:4px 8px; min-height:36px; color:#102033;"
        "}"
        /* 配置项：字大一些；框高配合字号，避免裁切 */
        "QLineEdit#cfgFieldEdit {"
        "  font-size:22px; font-weight:bold; padding:0px 8px; margin:0px;"
        "  min-height:0px; max-height:44px;"
        "  border:2px solid #c9d4df; border-radius:4px;"
        "}"
        "QLineEdit:focus, QLineEdit#cfgFieldEdit:focus { border:2px solid #0b7a7a; }"
        "QTextEdit#logView, QTextEdit#cfgView {"
        "  background-color:#ffffff; color:#102033; border:2px solid #c9d4df;"
        "  border-radius:8px; font-size:18px; padding:10px;"
        "}"
        /* OTA 全屏页：与首页同一套青绿工业风，信息层次更清晰 */
        "QWidget#otaPage { background-color:#e8eef2; }"
        "QFrame#otaTopBar { background-color:#ffffff; border:none; border-bottom:3px solid #0b7a7a; }"
        "QLabel#otaBrand { color:#102033; font-size:26px; font-weight:bold; }"
        "QFrame#otaHero {"
        "  background-color:#ffffff; border:2px solid #c9d4df; border-radius:14px;"
        "}"
        "QLabel#otaPercent { color:#0b7a7a; font-size:92px; font-weight:bold; }"
        "QLabel#otaState { color:#102033; font-size:28px; font-weight:bold; }"
        "QLabel#otaBadge {"
        "  color:#ffffff; background-color:#0b7a7a; border-radius:8px;"
        "  padding:8px 18px; font-size:20px; font-weight:bold;"
        "}"
        "QLabel#otaBadge[otaTone=\"busy\"] { background-color:#0b7a7a; }"
        "QLabel#otaBadge[otaTone=\"ok\"] { background-color:#1a7f37; }"
        "QLabel#otaBadge[otaTone=\"fail\"] { background-color:#b42318; }"
        "QLabel#otaBadge[otaTone=\"idle\"] { background-color:#64748b; }"
        "QProgressBar#otaBar {"
        "  border:2px solid #9db0c0; border-radius:12px; background:#dce5ec;"
        "  text-align:center; color:#102033; font-size:18px; font-weight:bold;"
        "  min-height:28px;"
        "}"
        "QProgressBar#otaBar::chunk {"
        "  background-color:#0b7a7a; border-radius:10px; margin:2px;"
        "}"
        "QFrame#otaDetailCard {"
        "  background-color:#ffffff; border:2px solid #c9d4df; border-radius:12px;"
        "}"
        "QLabel#otaDetail {"
        "  color:#243447; font-size:20px; font-weight:bold; line-height:28px;"
        "}"
    ));

    m_stack = new QStackedWidget(m_root);

    /* ---------- Home ---------- */
    m_pageHome = new QWidget(m_stack);
    QFrame *topBar = new QFrame(m_pageHome);
    topBar->setObjectName(QStringLiteral("topBar"));
    topBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_title = new QLabel(QStringLiteral("时差法 流量监测"), topBar);
    m_title->setObjectName(QStringLiteral("brand"));

    m_lblStatBiz = new QLabel(topBar);
    m_lblStatBiz->setObjectName(QStringLiteral("statusChip"));
    m_lblStatBiz->setTextFormat(Qt::RichText);
    m_lblStatSensor = new QLabel(topBar);
    m_lblStatSensor->setObjectName(QStringLiteral("statusChip"));
    m_lblStatSensor->setTextFormat(Qt::RichText);
    m_lblStatLink = new QLabel(topBar);
    m_lblStatLink->setObjectName(QStringLiteral("statusChip"));
    m_lblStatLink->setTextFormat(Qt::RichText);
    setStatusChip(m_lblStatBiz, QStringLiteral("业务"), false);
    setStatusChip(m_lblStatSensor, QStringLiteral("传感器"), false);
    setStatusChip(m_lblStatLink, QStringLiteral("4G"), false);

    m_lblClock = new QLabel(topBar);
    m_lblClock->setObjectName(QStringLiteral("clock"));
    m_valUptime = new QLabel(QStringLiteral("运行 00:00:00"), topBar);
    m_valUptime->setObjectName(QStringLiteral("uptimeChip"));

    QHBoxLayout *topLay = new QHBoxLayout(topBar);
    topLay->setContentsMargins(16, 10, 16, 10);
    topLay->setSpacing(10);
    topLay->addWidget(m_title, 0);
    topLay->addSpacing(8);
    topLay->addWidget(m_lblStatBiz, 0);
    topLay->addWidget(m_lblStatSensor, 0);
    topLay->addWidget(m_lblStatLink, 0);
    topLay->addStretch(1);
    topLay->addWidget(m_valUptime, 0);
    topLay->addSpacing(8);
    topLay->addWidget(m_lblClock, 0);

    m_sectionView = new SectionView(m_pageHome);

    QWidget *tileVel1 = makeMetricTile(QStringLiteral("流速 1 (m/s)"), &m_valVel1, true);
    QWidget *tileVel2 = makeMetricTile(QStringLiteral("流速 2 (m/s)"), &m_valVel2, true);
    QWidget *tileWater = makeMetricTile(QStringLiteral("水位 (m)"), &m_valWater, true);
    QWidget *tileSec = makeMetricTile(QStringLiteral("秒流量 (m³/s)"), &m_valSecFlow, true);
    QWidget *tileHour = makeMetricTile(QStringLiteral("小时流量 (m³/h)"), &m_valHourFlow, true);
    QWidget *tileVolt = makeMetricTile(QStringLiteral("电压 (V)"), &m_valVoltage, true);

    QWidget *metricCol = new QWidget(m_pageHome);
    metricCol->setMinimumWidth(248);
    metricCol->setMaximumWidth(280);
    QVBoxLayout *metricLay = new QVBoxLayout(metricCol);
    metricLay->setContentsMargins(0, 0, 0, 0);
    metricLay->setSpacing(6);
    metricLay->addWidget(tileVel1, 1);
    metricLay->addWidget(tileVel2, 1);
    metricLay->addWidget(tileWater, 1);
    metricLay->addWidget(tileSec, 1);
    metricLay->addWidget(tileHour, 1);
    metricLay->addWidget(tileVolt, 1);

    QHBoxLayout *mainRow = new QHBoxLayout;
    mainRow->setContentsMargins(0, 0, 0, 0);
    mainRow->setSpacing(12);
    mainRow->addWidget(m_sectionView, 1);
    mainRow->addWidget(metricCol, 0);

    m_btnFunc = new QPushButton(QStringLiteral("菜单"), m_pageHome);
    m_btnOpenWater = new QPushButton(QStringLiteral("设置水位加测"), m_pageHome);
    m_btnFunc->setMinimumHeight(56);
    m_btnOpenWater->setMinimumHeight(56);
    m_btnFunc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_btnOpenWater->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_btnFunc, SIGNAL(clicked()), this, SLOT(onShowFuncPage()));
    connect(m_btnOpenWater, SIGNAL(clicked()), this, SLOT(onShowWaterPage()));

    QWidget *homeBtnBar = new QWidget(m_pageHome);
    homeBtnBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    homeBtnBar->setMinimumHeight(64);
    QHBoxLayout *btnLay = new QHBoxLayout(homeBtnBar);
    btnLay->setSpacing(12);
    btnLay->setContentsMargins(0, 4, 0, 0);
    btnLay->addWidget(m_btnFunc, 1);
    btnLay->addWidget(m_btnOpenWater, 1);

    QVBoxLayout *homeLay = new QVBoxLayout(m_pageHome);
    homeLay->setContentsMargins(0, 0, 0, 0);
    homeLay->setSpacing(0);
    homeLay->addWidget(topBar, 0);
    QVBoxLayout *body = new QVBoxLayout;
    body->setContentsMargins(14, 10, 14, 10);
    body->setSpacing(8);
    body->addLayout(mainRow, 1);
    body->addWidget(homeBtnBar, 0);
    homeLay->addLayout(body, 1);

    /* ---------- Log page ---------- */
    m_pageLog = new QWidget(m_stack);
    QLabel *logTitle = new QLabel(QStringLiteral("显示屏运行日志"), m_pageLog);
    logTitle->setObjectName(QStringLiteral("pageTitle"));
    m_btnBackLog = new QPushButton(QStringLiteral("返回"), m_pageLog);
    m_btnBackLog->setObjectName(QStringLiteral("backBtn"));
    connect(m_btnBackLog, SIGNAL(clicked()), this, SLOT(onShowFuncPage()));
    QHBoxLayout *logTop = new QHBoxLayout;
    logTop->addWidget(logTitle);
    logTop->addStretch(1);
    logTop->addWidget(m_btnBackLog);
    m_log = new QTextEdit(m_pageLog);
    m_log->setObjectName(QStringLiteral("logView"));
    m_log->setReadOnly(true);
    QVBoxLayout *logLay = new QVBoxLayout(m_pageLog);
    logLay->setContentsMargins(16, 14, 16, 14);
    logLay->addLayout(logTop);
    logLay->addWidget(m_log, 1);

    /* ---------- Functions page ---------- */
    m_pageFunc = new QWidget(m_stack);
    QLabel *funcTitle = new QLabel(QStringLiteral("功能"), m_pageFunc);
    funcTitle->setObjectName(QStringLiteral("pageTitle"));
    m_btnBackFunc = new QPushButton(QStringLiteral("返回"), m_pageFunc);
    m_btnBackFunc->setObjectName(QStringLiteral("backBtn"));
    connect(m_btnBackFunc, SIGNAL(clicked()), this, SLOT(onBackHome()));
    QHBoxLayout *funcTop = new QHBoxLayout;
    funcTop->addWidget(funcTitle);
    funcTop->addStretch(1);
    funcTop->addWidget(m_btnBackFunc);

    m_btnStartTest = new QPushButton(QStringLiteral("加测（采一轮并上报）"), m_pageFunc);
    m_btnFuncCollect = new QPushButton(QStringLiteral("手动采集"), m_pageFunc);
    m_btnFuncReport = new QPushButton(QStringLiteral("手动上报"), m_pageFunc);
    m_btnLog = new QPushButton(QStringLiteral("屏显日志"), m_pageFunc);
    m_btnViewConfig = new QPushButton(QStringLiteral("查看配置"), m_pageFunc);
    m_btnDevLogPage = new QPushButton(QStringLiteral("设备完整日志"), m_pageFunc);
    m_btnBlankSettings = new QPushButton(QStringLiteral("息屏设置"), m_pageFunc);
    m_btnReboot = new QPushButton(QStringLiteral("重启设备"), m_pageFunc);
    m_btnReboot->setObjectName(QStringLiteral("dangerBtn"));
    m_btnExit = new QPushButton(QStringLiteral("退出程序"), m_pageFunc);
    m_btnExit->setObjectName(QStringLiteral("dangerBtn"));
    connect(m_btnStartTest, SIGNAL(clicked()), this, SLOT(onStartTest()));
    connect(m_btnFuncCollect, SIGNAL(clicked()), this, SLOT(onCollect()));
    connect(m_btnFuncReport, SIGNAL(clicked()), this, SLOT(onReport()));
    connect(m_btnLog, SIGNAL(clicked()), this, SLOT(onShowLogPage()));
    connect(m_btnViewConfig, SIGNAL(clicked()), this, SLOT(onShowConfigPage()));
    connect(m_btnDevLogPage, SIGNAL(clicked()), this, SLOT(onShowDeviceLogPage()));
    connect(m_btnBlankSettings, SIGNAL(clicked()), this, SLOT(onShowBlankSettingsPage()));
    connect(m_btnReboot, SIGNAL(clicked()), this, SLOT(onRebootDevice()));
    connect(m_btnExit, SIGNAL(clicked()), this, SLOT(onExit()));

    QGridLayout *funcGrid = new QGridLayout;
    funcGrid->setHorizontalSpacing(12);
    funcGrid->setVerticalSpacing(12);
    funcGrid->addWidget(m_btnFuncCollect, 0, 0);
    funcGrid->addWidget(m_btnFuncReport, 0, 1);
    funcGrid->addWidget(m_btnStartTest, 1, 0);
    funcGrid->addWidget(m_btnLog, 1, 1);
    funcGrid->addWidget(m_btnViewConfig, 2, 0);
    funcGrid->addWidget(m_btnDevLogPage, 2, 1);
    funcGrid->addWidget(m_btnBlankSettings, 3, 0);
    funcGrid->addWidget(m_btnReboot, 3, 1);
    funcGrid->addWidget(m_btnExit, 4, 0, 1, 2);

    QVBoxLayout *funcLay = new QVBoxLayout(m_pageFunc);
    funcLay->setContentsMargins(16, 14, 16, 14);
    funcLay->setSpacing(10);
    funcLay->addLayout(funcTop);
    funcLay->addLayout(funcGrid);
    funcLay->addStretch(1);
    QLabel *otaNote = new QLabel(
        QStringLiteral("说明：固件由网页下发；升级时本机自动全屏显示进度并锁定操作。可查看配置，并可修改站号/密码/间隔等。"),
        m_pageFunc);
    otaNote->setObjectName(QStringLiteral("hintText"));
    otaNote->setWordWrap(true);
    funcLay->addWidget(otaNote);

    /* ---------- Water level page ---------- */
    m_pageWater = new QWidget(m_stack);
    QLabel *waterTitle = new QLabel(QStringLiteral("设置水位测流"), m_pageWater);
    waterTitle->setObjectName(QStringLiteral("pageTitle"));
    m_btnBackWater = new QPushButton(QStringLiteral("返回"), m_pageWater);
    m_btnBackWater->setObjectName(QStringLiteral("backBtn"));
    connect(m_btnBackWater, SIGNAL(clicked()), this, SLOT(onBackHome()));
    QHBoxLayout *waterTop = new QHBoxLayout;
    waterTop->addWidget(waterTitle);
    waterTop->addStretch(1);
    waterTop->addWidget(m_btnBackWater);

    QLabel *waterHint = new QLabel(
        QStringLiteral("输入水位（厘米），不输入水位直接点加测用原水位数据"), m_pageWater);
    waterHint->setObjectName(QStringLiteral("hintText"));
    waterHint->setWordWrap(true);
    m_editWaterCm = new QLineEdit(m_pageWater);
    m_editWaterCm->setPlaceholderText(QStringLiteral("例如 1500"));
    m_editWaterCm->setReadOnly(true);
    m_btnApplyWater = new QPushButton(QStringLiteral("加测"), m_pageWater);
    connect(m_btnApplyWater, SIGNAL(clicked()), this, SLOT(onApplyWaterCm()));
    QHBoxLayout *waterLay = new QHBoxLayout;
    waterLay->addWidget(m_editWaterCm, 1);
    waterLay->addWidget(m_btnApplyWater);

    QWidget *numPadHost = new QWidget(m_pageWater);
    QGridLayout *numPad = new QGridLayout(numPadHost);
    numPad->setContentsMargins(0, 0, 0, 0);
    numPad->setHorizontalSpacing(10);
    numPad->setVerticalSpacing(10);
    const char *keys[12] = {
        "1", "2", "3",
        "4", "5", "6",
        "7", "8", "9",
        "清除", "0", "删除"
    };
    for (int i = 0; i < 12; ++i) {
        QPushButton *kb = new QPushButton(QString::fromUtf8(keys[i]), numPadHost);
        kb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        kb->setMinimumHeight(44);
        if (i == 9) {
            kb->setObjectName(QStringLiteral("padActionBtn"));
            connect(kb, SIGNAL(clicked()), this, SLOT(onNumPadClear()));
        } else if (i == 11) {
            kb->setObjectName(QStringLiteral("padActionBtn"));
            connect(kb, SIGNAL(clicked()), this, SLOT(onNumPadBackspace()));
        } else {
            connect(kb, SIGNAL(clicked()), this, SLOT(onNumPadDigit()));
        }
        numPad->addWidget(kb, i / 3, i % 3);
    }
    for (int r = 0; r < 4; ++r)
        numPad->setRowStretch(r, 1);
    for (int c = 0; c < 3; ++c)
        numPad->setColumnStretch(c, 1);

    QVBoxLayout *waterPageLay = new QVBoxLayout(m_pageWater);
    waterPageLay->setContentsMargins(16, 14, 16, 14);
    waterPageLay->setSpacing(10);
    waterPageLay->addLayout(waterTop);
    waterPageLay->addWidget(waterHint);
    waterPageLay->addLayout(waterLay);
    waterPageLay->addWidget(numPadHost, 1);

    /* ---------- Config view (read-only) ---------- */
    m_pageConfig = new QWidget(m_stack);
    QLabel *cfgTitle = new QLabel(QStringLiteral("查看配置"), m_pageConfig);
    cfgTitle->setObjectName(QStringLiteral("pageTitle"));
    m_btnBackConfig = new QPushButton(QStringLiteral("返回"), m_pageConfig);
    m_btnBackConfig->setObjectName(QStringLiteral("backBtn"));
    m_btnEditConfig = new QPushButton(QStringLiteral("修改配置"), m_pageConfig);
    connect(m_btnBackConfig, SIGNAL(clicked()), this, SLOT(onShowFuncPage()));
    connect(m_btnEditConfig, SIGNAL(clicked()), this, SLOT(onShowConfigEditPage()));
    QHBoxLayout *cfgTop = new QHBoxLayout;
    cfgTop->addWidget(cfgTitle);
    cfgTop->addStretch(1);
    cfgTop->addWidget(m_btnEditConfig);
    cfgTop->addWidget(m_btnBackConfig);
    QLabel *cfgHint = new QLabel(
        QStringLiteral("只读显示 doppler.conf。需要改站号/密码/间隔时点「修改配置」。"),
        m_pageConfig);
    cfgHint->setObjectName(QStringLiteral("hintText"));
    cfgHint->setWordWrap(true);
    m_cfgView = new QTextEdit(m_pageConfig);
    m_cfgView->setObjectName(QStringLiteral("cfgView"));
    m_cfgView->setReadOnly(true);
    applyCjkUiFont(m_cfgView);
    QVBoxLayout *cfgLay = new QVBoxLayout(m_pageConfig);
    cfgLay->setContentsMargins(16, 14, 16, 14);
    cfgLay->setSpacing(8);
    cfgLay->addLayout(cfgTop);
    cfgLay->addWidget(cfgHint);
    cfgLay->addWidget(m_cfgView, 1);

    /* ---------- Config edit (limited fields) ---------- */
    m_pageConfigEdit = new QWidget(m_stack);
    QLabel *cfgEditTitle = new QLabel(QStringLiteral("修改配置"), m_pageConfigEdit);
    cfgEditTitle->setObjectName(QStringLiteral("pageTitle"));
    m_btnBackConfigEdit = new QPushButton(QStringLiteral("返回"), m_pageConfigEdit);
    m_btnBackConfigEdit->setObjectName(QStringLiteral("backBtn"));
    m_btnBackConfigEdit->setMinimumHeight(48);
    m_btnSaveConfigFields = new QPushButton(QStringLiteral("保存"), m_pageConfigEdit);
    m_btnSaveConfigFields->setMinimumHeight(48);
    connect(m_btnBackConfigEdit, SIGNAL(clicked()), this, SLOT(onShowConfigPage()));
    connect(m_btnSaveConfigFields, SIGNAL(clicked()), this, SLOT(onSaveConfigFields()));
    QHBoxLayout *cfgEditTop = new QHBoxLayout;
    cfgEditTop->addWidget(cfgEditTitle);
    cfgEditTop->addStretch(1);
    cfgEditTop->addWidget(m_btnSaveConfigFields);
    cfgEditTop->addWidget(m_btnBackConfigEdit);

    QGridLayout *fieldGrid = new QGridLayout;
    fieldGrid->setHorizontalSpacing(8);
    fieldGrid->setVerticalSpacing(4);
    fieldGrid->setContentsMargins(0, 0, 0, 0);
    fieldGrid->setColumnStretch(1, 1);
    fieldGrid->setColumnStretch(3, 1);
    /* 两项一行：标签+输入 | 标签+输入 */
    m_editStationId = makeCfgField(m_pageConfigEdit, QStringLiteral("站号"),
                                   fieldGrid, 0, 0, "StationID");
    m_editCenterId = makeCfgField(m_pageConfigEdit, QStringLiteral("中心站"),
                                  fieldGrid, 0, 1, "CenterStationID");
    m_editPassword = makeCfgField(m_pageConfigEdit, QStringLiteral("密码"),
                                  fieldGrid, 1, 0, "Password");
    m_editCollect = makeCfgField(m_pageConfigEdit, QStringLiteral("采集间隔"),
                                 fieldGrid, 1, 1, "CollectInterval");
    m_editReport = makeCfgField(m_pageConfigEdit, QStringLiteral("上报间隔"),
                                fieldGrid, 2, 0, "ReportInterval");
    m_editTimeout = makeCfgField(m_pageConfigEdit, QStringLiteral("超时秒"),
                                 fieldGrid, 2, 1, "SensorTimeoutSec");

    QWidget *cfgPadHost = new QWidget(m_pageConfigEdit);
    cfgPadHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QGridLayout *cfgPad = new QGridLayout(cfgPadHost);
    cfgPad->setContentsMargins(0, 4, 0, 0);
    cfgPad->setHorizontalSpacing(8);
    cfgPad->setVerticalSpacing(8);
    const char *cfgKeys[12] = {
        "1", "2", "3",
        "4", "5", "6",
        "7", "8", "9",
        "清除", "0", "删除"
    };
    for (int i = 0; i < 12; ++i) {
        QPushButton *kb = new QPushButton(QString::fromUtf8(cfgKeys[i]), cfgPadHost);
        kb->setObjectName(QStringLiteral("cfgPadBtn"));
        kb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        kb->setMinimumHeight(32);
        kb->setMaximumHeight(42);
        if (i == 9)
            connect(kb, SIGNAL(clicked()), this, SLOT(onCfgNumClear()));
        else if (i == 11)
            connect(kb, SIGNAL(clicked()), this, SLOT(onCfgNumBackspace()));
        else
            connect(kb, SIGNAL(clicked()), this, SLOT(onCfgNumDigit()));
        cfgPad->addWidget(kb, i / 3, i % 3);
    }
    for (int r = 0; r < 4; ++r)
        cfgPad->setRowStretch(r, 1);
    for (int c = 0; c < 3; ++c)
        cfgPad->setColumnStretch(c, 1);

    QVBoxLayout *cfgEditLay = new QVBoxLayout(m_pageConfigEdit);
    cfgEditLay->setContentsMargins(14, 8, 14, 8);
    cfgEditLay->setSpacing(6);
    cfgEditLay->addLayout(cfgEditTop, 0);
    cfgEditLay->addLayout(fieldGrid, 0);
    cfgEditLay->addSpacing(6);
    cfgEditLay->addWidget(cfgPadHost, 1);
    cfgPadHost->setMaximumHeight(220);

    /* ---------- OTA status page ---------- */
    m_pageOta = new QWidget(m_stack);
    m_pageOta->setObjectName(QStringLiteral("otaPage"));

    QFrame *otaTopBar = new QFrame(m_pageOta);
    otaTopBar->setObjectName(QStringLiteral("otaTopBar"));
    QLabel *otaBrand = new QLabel(QStringLiteral("固件升级"), otaTopBar);
    otaBrand->setObjectName(QStringLiteral("otaBrand"));
    m_btnBackOta = new QPushButton(QStringLiteral("返回首页"), otaTopBar);
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
    m_otaHero->setObjectName(QStringLiteral("otaHero"));
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
    m_lblOtaBadge->setStyleSheet(QStringLiteral(
        "QLabel#otaBadge { color:#ffffff; background-color:#64748b; border-radius:8px;"
        " padding:8px 18px; font-size:20px; font-weight:bold; }"));
    m_otaProgress = new QProgressBar(m_otaHero);
    m_otaProgress->setObjectName(QStringLiteral("otaBar"));
    m_otaProgress->setRange(0, 100);
    m_otaProgress->setValue(0);
    m_otaProgress->setTextVisible(true);
    m_otaProgress->setFormat(QStringLiteral("%p%"));
    m_otaProgress->setMinimumHeight(36);
    QHBoxLayout *badgeRow = new QHBoxLayout;
    badgeRow->addStretch(1);
    badgeRow->addWidget(m_lblOtaBadge);
    badgeRow->addStretch(1);
    QVBoxLayout *heroLay = new QVBoxLayout(m_otaHero);
    heroLay->setContentsMargins(24, 18, 24, 20);
    heroLay->setSpacing(10);
    heroLay->addWidget(m_lblOtaPercent);
    heroLay->addWidget(m_lblOtaState);
    heroLay->addLayout(badgeRow);
    heroLay->addSpacing(4);
    heroLay->addWidget(m_otaProgress);

    m_otaDetailCard = new QFrame(m_pageOta);
    m_otaDetailCard->setObjectName(QStringLiteral("otaDetailCard"));
    m_lblOtaDetail = new QLabel(QStringLiteral("等待网页下发固件…"), m_otaDetailCard);
    m_lblOtaDetail->setObjectName(QStringLiteral("otaDetail"));
    m_lblOtaDetail->setWordWrap(true);
    m_lblOtaDetail->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    QVBoxLayout *detailLay = new QVBoxLayout(m_otaDetailCard);
    detailLay->setContentsMargins(18, 14, 18, 14);
    detailLay->addWidget(m_lblOtaDetail);

    QVBoxLayout *otaBody = new QVBoxLayout;
    otaBody->setContentsMargins(20, 16, 20, 16);
    otaBody->setSpacing(14);
    otaBody->addWidget(m_otaHero, 0);
    otaBody->addWidget(m_otaDetailCard, 1);

    QVBoxLayout *otaLay = new QVBoxLayout(m_pageOta);
    otaLay->setContentsMargins(0, 0, 0, 0);
    otaLay->setSpacing(0);
    otaLay->addWidget(otaTopBar, 0);
    otaLay->addLayout(otaBody, 1);

    /* ---------- Device log page (log.lg) ---------- */
    m_pageDevLog = new QWidget(m_stack);
    QLabel *devLogTitle = new QLabel(QStringLiteral("设备完整日志"), m_pageDevLog);
    devLogTitle->setObjectName(QStringLiteral("pageTitle"));
    m_btnBackDevLog = new QPushButton(QStringLiteral("返回"), m_pageDevLog);
    m_btnBackDevLog->setObjectName(QStringLiteral("backBtn"));
    connect(m_btnBackDevLog, SIGNAL(clicked()), this, SLOT(onShowFuncPage()));
    QHBoxLayout *devLogTop = new QHBoxLayout;
    devLogTop->addWidget(devLogTitle);
    devLogTop->addStretch(1);
    devLogTop->addWidget(m_btnBackDevLog);
    m_btnDevLogAll = new QPushButton(QStringLiteral("全部"), m_pageDevLog);
    m_btnDevLogWarn = new QPushButton(QStringLiteral("警告+错误"), m_pageDevLog);
    m_btnDevLogError = new QPushButton(QStringLiteral("仅错误"), m_pageDevLog);
    m_btnDevLogError->setObjectName(QStringLiteral("dangerBtn"));
    connect(m_btnDevLogAll, SIGNAL(clicked()), this, SLOT(onLoadDeviceLogAll()));
    connect(m_btnDevLogWarn, SIGNAL(clicked()), this, SLOT(onLoadDeviceLogWarn()));
    connect(m_btnDevLogError, SIGNAL(clicked()), this, SLOT(onLoadDeviceLogError()));
    QHBoxLayout *devLogBtns = new QHBoxLayout;
    devLogBtns->addWidget(m_btnDevLogAll);
    devLogBtns->addWidget(m_btnDevLogWarn);
    devLogBtns->addWidget(m_btnDevLogError);
    m_lblDevLogMeta = new QLabel(
        QStringLiteral("点上方按钮从 log.lg 导出最近内容（最多约 512KB）"), m_pageDevLog);
    m_lblDevLogMeta->setObjectName(QStringLiteral("hintText"));
    m_lblDevLogMeta->setWordWrap(true);
    m_devLog = new QTextEdit(m_pageDevLog);
    m_devLog->setObjectName(QStringLiteral("logView"));
    m_devLog->setReadOnly(true);
    QVBoxLayout *devLogLay = new QVBoxLayout(m_pageDevLog);
    devLogLay->setContentsMargins(16, 14, 16, 14);
    devLogLay->addLayout(devLogTop);
    devLogLay->addLayout(devLogBtns);
    devLogLay->addWidget(m_lblDevLogMeta);
    devLogLay->addWidget(m_devLog, 1);

    /* ---------- Blank timeout settings ---------- */
    m_pageBlankSettings = new QWidget(m_stack);
    QLabel *blankTitle = new QLabel(QStringLiteral("息屏设置"), m_pageBlankSettings);
    blankTitle->setObjectName(QStringLiteral("pageTitle"));
    m_btnBackBlank = new QPushButton(QStringLiteral("返回"), m_pageBlankSettings);
    m_btnBackBlank->setObjectName(QStringLiteral("backBtn"));
    connect(m_btnBackBlank, SIGNAL(clicked()), this, SLOT(onShowFuncPage()));
    QHBoxLayout *blankTop = new QHBoxLayout;
    blankTop->addWidget(blankTitle);
    blankTop->addStretch(1);
    blankTop->addWidget(m_btnBackBlank);
    m_lblBlankHint = new QLabel(
        QStringLiteral("无操作达到设定时间后自动息屏；点按屏幕任意处亮屏。升级进行中不会息屏。"),
        m_pageBlankSettings);
    m_lblBlankHint->setObjectName(QStringLiteral("hintText"));
    m_lblBlankHint->setWordWrap(true);

    QGridLayout *blankGrid = new QGridLayout;
    blankGrid->setHorizontalSpacing(12);
    blankGrid->setVerticalSpacing(12);
    for (int i = 0; i < 5; ++i) {
        m_btnBlankMin[i] = new QPushButton(
            QStringLiteral("%1 分钟").arg(i + 1), m_pageBlankSettings);
        m_btnBlankMin[i]->setProperty("blankMin", i + 1);
        connect(m_btnBlankMin[i], SIGNAL(clicked()), this, SLOT(onBlankMinutesChosen()));
        blankGrid->addWidget(m_btnBlankMin[i], i / 3, i % 3);
    }

    QVBoxLayout *blankLay = new QVBoxLayout(m_pageBlankSettings);
    blankLay->setContentsMargins(16, 14, 16, 14);
    blankLay->setSpacing(12);
    blankLay->addLayout(blankTop);
    blankLay->addWidget(m_lblBlankHint);
    blankLay->addLayout(blankGrid);
    blankLay->addStretch(1);

    /* ---------- Exit password ---------- */
    m_pageExitAuth = new QWidget(m_stack);
    QLabel *exitTitle = new QLabel(QStringLiteral("退出程序"), m_pageExitAuth);
    exitTitle->setObjectName(QStringLiteral("pageTitle"));
    m_btnBackExitAuth = new QPushButton(QStringLiteral("取消"), m_pageExitAuth);
    m_btnBackExitAuth->setObjectName(QStringLiteral("backBtn"));
    connect(m_btnBackExitAuth, SIGNAL(clicked()), this, SLOT(onShowFuncPage()));
    QHBoxLayout *exitTop = new QHBoxLayout;
    exitTop->addWidget(exitTitle);
    exitTop->addStretch(1);
    exitTop->addWidget(m_btnBackExitAuth);

    m_lblExitPwdHint = new QLabel(
        QStringLiteral("请输入退出密码（默认 111111）"), m_pageExitAuth);
    m_lblExitPwdHint->setObjectName(QStringLiteral("hintText"));
    m_lblExitPwdHint->setWordWrap(true);
    m_editExitPwd = new QLineEdit(m_pageExitAuth);
    m_editExitPwd->setEchoMode(QLineEdit::Password);
    m_editExitPwd->setReadOnly(true);
    m_editExitPwd->setAlignment(Qt::AlignCenter);
    m_editExitPwd->setPlaceholderText(QStringLiteral("请输入密码"));
    m_editExitPwd->setMinimumHeight(48);
    m_btnExitConfirm = new QPushButton(QStringLiteral("确认退出"), m_pageExitAuth);
    m_btnExitConfirm->setObjectName(QStringLiteral("dangerBtn"));
    connect(m_btnExitConfirm, SIGNAL(clicked()), this, SLOT(onExitConfirm()));

    QWidget *exitPadHost = new QWidget(m_pageExitAuth);
    QGridLayout *exitPad = new QGridLayout(exitPadHost);
    exitPad->setContentsMargins(0, 0, 0, 0);
    exitPad->setHorizontalSpacing(10);
    exitPad->setVerticalSpacing(10);
    const char *exitKeys[12] = {
        "1", "2", "3",
        "4", "5", "6",
        "7", "8", "9",
        "清除", "0", "删除"
    };
    for (int i = 0; i < 12; ++i) {
        QPushButton *kb = new QPushButton(QString::fromUtf8(exitKeys[i]), exitPadHost);
        kb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        kb->setMinimumHeight(44);
        if (i == 9) {
            kb->setObjectName(QStringLiteral("padActionBtn"));
            connect(kb, SIGNAL(clicked()), this, SLOT(onExitPwdClear()));
        } else if (i == 11) {
            kb->setObjectName(QStringLiteral("padActionBtn"));
            connect(kb, SIGNAL(clicked()), this, SLOT(onExitPwdBackspace()));
        } else {
            connect(kb, SIGNAL(clicked()), this, SLOT(onExitPwdDigit()));
        }
        exitPad->addWidget(kb, i / 3, i % 3);
    }
    for (int r = 0; r < 4; ++r)
        exitPad->setRowStretch(r, 1);
    for (int c = 0; c < 3; ++c)
        exitPad->setColumnStretch(c, 1);

    QVBoxLayout *exitLay = new QVBoxLayout(m_pageExitAuth);
    exitLay->setContentsMargins(16, 14, 16, 14);
    exitLay->setSpacing(10);
    exitLay->addLayout(exitTop);
    exitLay->addWidget(m_lblExitPwdHint);
    exitLay->addWidget(m_editExitPwd);
    exitLay->addWidget(m_btnExitConfirm);
    exitLay->addWidget(exitPadHost, 1);

    m_stack->addWidget(m_pageHome);        /* 0 */
    m_stack->addWidget(m_pageLog);         /* 1 */
    m_stack->addWidget(m_pageFunc);        /* 2 */
    m_stack->addWidget(m_pageConfig);      /* 3 */
    m_stack->addWidget(m_pageWater);       /* 4 */
    m_stack->addWidget(m_pageOta);         /* 5 */
    m_stack->addWidget(m_pageDevLog);      /* 6 */
    m_stack->addWidget(m_pageConfigEdit);  /* 7 */
    m_stack->addWidget(m_pageBlankSettings); /* 8 */
    m_stack->addWidget(m_pageExitAuth);    /* 9 */

    for (int i = 0; i < m_stack->count(); ++i) {
        QWidget *page = m_stack->widget(i);
        if (page)
            page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    /* Touch UI: no keyboard focus ring (dashed rect) after button press. */
    const QList<QPushButton *> buttons = findChildren<QPushButton *>();
    for (int i = 0; i < buttons.size(); ++i)
        buttons.at(i)->setFocusPolicy(Qt::NoFocus);

    QVBoxLayout *rootLay = new QVBoxLayout(m_root);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);
    rootLay->addWidget(m_stack);
    m_root->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    /* 遮罩挂在主窗口上，不要做 m_root 布局的子控件，否则会撑破 600 屏高 */
    m_blankCover = new QWidget(this);
    m_blankCover->setObjectName(QStringLiteral("blankCover"));
    m_blankCover->setStyleSheet(QStringLiteral(
        "QWidget#blankCover { background-color:#000000; }"));
    m_blankCover->hide();
    m_blankCover->installEventFilter(this);
    m_blankCover->setGeometry(0, 0, BOARD_WIDTH, BOARD_HEIGHT);
}

/* 按业务是否就绪启用/禁用采集上报等按钮。 */
void DopplerMainWindow::setBizEnabled(bool on)
{
    m_bizOk = on;
    if (m_otaUiLocked)
        return;
    m_btnFuncCollect->setEnabled(on);
    m_btnFuncReport->setEnabled(on);
    m_btnStartTest->setEnabled(on);
    m_btnApplyWater->setEnabled(on);
    m_btnEditConfig->setEnabled(on);
    m_btnSaveConfigFields->setEnabled(on);
}

/* 向屏显日志追加带时间戳的一行。 */
void DopplerMainWindow::appendUiLog(const QString &line)
{
    const QString stamped = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"))
            + QStringLiteral("  ") + line;
    m_log->append(stamped);
    if (m_log->document()->blockCount() > 120)
        m_log->clear();
}

/* 切换到屏显运行日志页。 */
void DopplerMainWindow::onShowLogPage()
{
    if (m_otaUiLocked)
        return;
    m_stack->setCurrentWidget(m_pageLog);
}

/* 切换到功能菜单页。 */
void DopplerMainWindow::onShowFuncPage()
{
    if (m_otaUiLocked)
        return;
    m_stack->setCurrentWidget(m_pageFunc);
}

/* 切换到设置水位加测页。 */
void DopplerMainWindow::onShowWaterPage()
{
    if (m_otaUiLocked)
        return;
    m_stack->setCurrentWidget(m_pageWater);
}

/* 切换到 OTA 全屏进度页。 */
void DopplerMainWindow::onShowOtaPage()
{
    updateOtaUi();
    m_stack->setCurrentWidget(m_pageOta);
}

/* OTA 锁定/解锁界面：锁定时强制进入 OTA 页。 */
void DopplerMainWindow::setOtaUiLocked(bool locked)
{
    if (m_otaUiLocked == locked)
        return;
    m_otaUiLocked = locked;

    m_btnFuncCollect->setEnabled(!locked && m_bizOk);
    m_btnFuncReport->setEnabled(!locked && m_bizOk);
    m_btnLog->setEnabled(!locked);
    m_btnOpenWater->setEnabled(!locked && m_bizOk);
    m_btnFunc->setEnabled(!locked);
    m_btnBlankSettings->setEnabled(!locked);
    m_btnExit->setEnabled(!locked);
    m_btnBackOta->setVisible(!locked);
    m_btnBackOta->setEnabled(!locked);

    if (locked) {
        wakeScreen();
        m_stack->setCurrentWidget(m_pageOta);
        appendUiLog(QStringLiteral("OTA 进行中，界面已锁定"));
    }
}

/* 打开设备完整日志页并默认加载全部。 */
void DopplerMainWindow::onShowDeviceLogPage()
{
    if (m_otaUiLocked)
        return;
    m_stack->setCurrentWidget(m_pageDevLog);
}

/* 加载设备日志（全部）。 */
void DopplerMainWindow::onLoadDeviceLogAll()
{
    loadDeviceLog(0);
}

/* 加载设备日志（警告）。 */
void DopplerMainWindow::onLoadDeviceLogWarn()
{
    loadDeviceLog(1);
}

/* 加载设备日志（错误）。 */
void DopplerMainWindow::onLoadDeviceLogError()
{
    loadDeviceLog(2);
}

/* OTA 状态码转中文说明。 */
QString DopplerMainWindow::otaStatusText(unsigned status)
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

/* OTA 错误码转中文说明。 */
QString DopplerMainWindow::otaErrText(unsigned err)
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

/* 刷新 OTA 进度/状态/徽章，并维护 UI 锁定。 */
void DopplerMainWindow::updateOtaUi()
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
            if (m_stack->currentWidget() == m_pageOta)
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

/* 导出设备日志并按过滤模式显示到文本框。 */
void DopplerMainWindow::loadDeviceLog(int filterMode)
{
    if (!m_bizOk) {
        QMessageBox::warning(this, QStringLiteral("设备日志"),
                             QStringLiteral("业务尚未就绪，稍后再试。"));
        return;
    }

    QByteArray buf;
    buf.resize((int)kDeviceLogExportMax);
    const size_t n = log_export_recent(reinterpret_cast<uint8_t *>(buf.data()),
                                       kDeviceLogExportMax);
    if (n == 0) {
        m_devLog->setPlainText(QStringLiteral("(log.lg 为空或无法读取)"));
        m_lblDevLogMeta->setText(QStringLiteral("导出 0 字节"));
        return;
    }
    buf.resize((int)n);

    QString text = QString::fromLocal8Bit(buf.constData(), (int)n);
    if (filterMode > 0) {
        const QStringList lines = text.split(QChar('\n'));
        QStringList keep;
        keep.reserve(lines.size());
        for (int i = 0; i < lines.size(); ++i) {
            const QString &line = lines.at(i);
            const bool isErr = line.contains(QStringLiteral("[ERROR]"));
            const bool isWarn = line.contains(QStringLiteral("[WARNING]"));
            if (filterMode == 2) {
                if (isErr)
                    keep.append(line);
            } else if (isErr || isWarn) {
                keep.append(line);
            }
        }
        text = keep.join(QChar('\n'));
    }

    m_devLog->setPlainText(text);
    QTextCursor cursor = m_devLog->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_devLog->setTextCursor(cursor);
    const char *modeName = (filterMode == 2) ? "仅错误"
            : (filterMode == 1) ? "警告+错误" : "全部";
    m_lblDevLogMeta->setText(
        QStringLiteral("已导出原始 %1 字节 · 筛选：%2 · 显示约 %3 字符")
            .arg((qulonglong)n)
            .arg(QString::fromUtf8(modeName))
            .arg(text.size()));
    appendUiLog(QStringLiteral("已加载设备日志（%1）").arg(QString::fromUtf8(modeName)));
}

/* 返回首页。 */
void DopplerMainWindow::onBackHome()
{
    if (m_otaUiLocked)
        return;
    m_stack->setCurrentWidget(m_pageHome);
}

/* 触发加测：采一轮并请求上报。 */
void DopplerMainWindow::onStartTest()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    Sensor_RequestStartTestReport();
    appendUiLog(QStringLiteral("已触发加测"));
    QMessageBox::information(this, QStringLiteral("加测"),
                             QStringLiteral("已请求加测，采集有效后会自动上报。"));
}

/* 水位页数字键盘：向输入框追加一位。 */
void DopplerMainWindow::onNumPadDigit()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn || !m_editWaterCm)
        return;
    /* Limit length to avoid accidental huge numbers on touch. */
    if (m_editWaterCm->text().size() >= 6)
        return;
    m_editWaterCm->setText(m_editWaterCm->text() + btn->text());
}

/* 水位页数字键盘：退格删除一位。 */
void DopplerMainWindow::onNumPadBackspace()
{
    if (!m_editWaterCm)
        return;
    QString t = m_editWaterCm->text();
    if (!t.isEmpty())
        t.chop(1);
    m_editWaterCm->setText(t);
}

/* 水位页数字键盘：清空输入。 */
void DopplerMainWindow::onNumPadClear()
{
    if (m_editWaterCm)
        m_editWaterCm->clear();
}

/* 应用水位加测：空输入用传感器水位，否则覆盖一次后恢复。 */
void DopplerMainWindow::onApplyWaterCm()
{
    if (m_otaUiLocked || !m_bizOk)
        return;

    const QString input = m_editWaterCm ? m_editWaterCm->text().trimmed() : QString();
    if (input.isEmpty()) {
        /* 未输入：按当前传感器水位加测上报 */
        Sensor_SetOverrideWaterCm(SHUIWEI_CELIU_USE_SENSOR);
        {
            int force = 1;
            SendDopplerRealData(&force);
        }
        appendUiLog(QStringLiteral("未输入水位，已按传感器原水位加测上报"));
        QMessageBox::information(this, QStringLiteral("水位测流"),
                                 QStringLiteral("未输入水位，已按当前传感器水位加测上报。"));
        onRefresh();
        return;
    }

    bool ok = false;
    const int cm = input.toInt(&ok);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("水位"),
                             QStringLiteral("请输入整数厘米，例如 150；留空则用传感器水位。"));
        return;
    }
    Sensor_SetOverrideWaterCm(cm);
    {
        int force = 1;
        SendDopplerRealData(&force);
    }
    /* 与现场预期一致：仅本次数值上报，随后恢复传感器水位 */
    Sensor_SetOverrideWaterCm(SHUIWEI_CELIU_USE_SENSOR);
    appendUiLog(QStringLiteral("已按水位 %1 cm 计算上报，并恢复传感器水位").arg(cm));
    QMessageBox::information(this, QStringLiteral("水位"),
                             QStringLiteral("已按 %1 cm 上报一次，后续仍用传感器水位。").arg(cm));
    onRefresh();
}

/* 读取 conf 全文到只读配置查看页。 */
void DopplerMainWindow::loadConfigToViewer()
{
    const QString path = configFilePath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_cfgView->setPlainText(QStringLiteral("# 无法打开: %1").arg(path));
        return;
    }
    const QByteArray raw = f.readAll();
    f.close();

    QString text = decodeConfBytes(raw);
    /* 文件里曾有高位字节，解码后却无汉字：多半是旧版息屏写入把 UTF-8 写坏了 */
    bool hasHigh = false;
    for (int i = 0; i < raw.size(); ++i) {
        if ((unsigned char)raw.at(i) >= 0x80U) {
            hasHigh = true;
            break;
        }
    }
    if (hasHigh && !confTextHasCjk(text)) {
        text += QStringLiteral(
            "\n\n# —— 提示：配置注释中文异常，请重新部署仓库里的 doppler.conf（UTF-8）——");
    }
    applyCjkUiFont(m_cfgView);
    m_cfgView->setPlainText(text);
}

/* 从 conf 文本中解析指定 key 的值。 */
static QString confValueOf(const QString &text, const QString &key)
{
    const QStringList lines = text.split(QChar('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i).trimmed();
        if (line.startsWith(QChar('#')) || line.isEmpty())
            continue;
        const int eq = line.indexOf(QChar('='));
        if (eq <= 0)
            continue;
        if (line.left(eq).trimmed().compare(key, Qt::CaseInsensitive) == 0)
            return line.mid(eq + 1).trimmed();
    }
    return QString();
}

/* 从 conf 填充站号/密码/间隔等可编辑字段。 */
void DopplerMainWindow::loadEditableFieldsFromConf()
{
    const QString path = configFilePath();
    QFile f(path);
    QString text;
    if (f.open(QIODevice::ReadOnly)) {
        text = decodeConfBytes(f.readAll());
        f.close();
    }
    m_editStationId->setText(confValueOf(text, QStringLiteral("StationID")));
    m_editCenterId->setText(confValueOf(text, QStringLiteral("CenterStationID")));
    m_editPassword->setText(confValueOf(text, QStringLiteral("Password")));
    m_editCollect->setText(confValueOf(text, QStringLiteral("CollectInterval")));
    m_editReport->setText(confValueOf(text, QStringLiteral("ReportInterval")));
    m_editTimeout->setText(confValueOf(text, QStringLiteral("SensorTimeoutSec")));
    m_cfgActiveEdit = m_editStationId;
    m_editStationId->setFocus();
}

/* 将可编辑字段写回 conf；成功返回 true。 */
bool DopplerMainWindow::writeEditableFieldsToConf()
{
    const QString path = configFilePath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QString text = decodeConfBytes(f.readAll());
    f.close();

    QMap<QString, QString> updates;
    updates.insert(QStringLiteral("StationID"), m_editStationId->text().trimmed());
    updates.insert(QStringLiteral("CenterStationID"), m_editCenterId->text().trimmed());
    updates.insert(QStringLiteral("Password"), m_editPassword->text().trimmed());
    updates.insert(QStringLiteral("CollectInterval"), m_editCollect->text().trimmed());
    updates.insert(QStringLiteral("ReportInterval"), m_editReport->text().trimmed());
    updates.insert(QStringLiteral("SensorTimeoutSec"), m_editTimeout->text().trimmed());

    QMap<QString, bool> seen;
    QStringList keys = updates.keys();
    for (int i = 0; i < keys.size(); ++i)
        seen.insert(keys.at(i), false);

    QStringList lines = text.split(QChar('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString rawLine = lines.at(i);
        QString line = rawLine.trimmed();
        if (line.startsWith(QChar('#')) || line.isEmpty())
            continue;
        const int eq = line.indexOf(QChar('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        QString matched;
        for (QMap<QString, QString>::const_iterator it = updates.constBegin();
             it != updates.constEnd(); ++it) {
            if (it.key().compare(key, Qt::CaseInsensitive) == 0) {
                matched = it.key();
                break;
            }
        }
        if (matched.isEmpty())
            continue;
        lines[i] = matched + QStringLiteral(" = ") + updates.value(matched);
        seen[matched] = true;
    }
    for (int i = 0; i < keys.size(); ++i) {
        const QString &key = keys.at(i);
        if (!seen.value(key))
            lines.append(key + QStringLiteral(" = ") + updates.value(key));
    }

    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray out = lines.join(QChar('\n')).toUtf8();
    const bool ok = (f.write(out) == out.size());
    f.close();
    return ok;
}

/* 打开只读配置查看页。 */
void DopplerMainWindow::onShowConfigPage()
{
    if (m_otaUiLocked)
        return;
    loadConfigToViewer();
    m_stack->setCurrentWidget(m_pageConfig);
}

/* 打开配置编辑页并加载当前值。 */
void DopplerMainWindow::onShowConfigEditPage()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    loadEditableFieldsFromConf();
    m_stack->setCurrentWidget(m_pageConfigEdit);
}

/* 拦截触摸/点击：唤醒息屏并重置空闲计时。 */
bool DopplerMainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type t = event->type();
    if (t == QEvent::MouseButtonPress || t == QEvent::MouseButtonDblClick
            || t == QEvent::TouchBegin || t == QEvent::KeyPress) {
        if (watched == m_blankCover && m_screenBlanked) {
            wakeScreen();
            return true;
        }
        noteUserActivity();
    }

    if (t == QEvent::MouseButtonPress) {
        QLineEdit *edit = qobject_cast<QLineEdit *>(watched);
        if (edit == m_editStationId || edit == m_editCenterId || edit == m_editPassword
                || edit == m_editCollect || edit == m_editReport || edit == m_editTimeout) {
            m_cfgActiveEdit = edit;
            edit->setFocus();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

/* 同步息屏遮罩尺寸到主窗口。 */
void DopplerMainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_blankCover)
        m_blankCover->setGeometry(rect());
}

/* 记录用户活动，重置息屏空闲计时器。 */
void DopplerMainWindow::noteUserActivity()
{
    m_idleSince.restart();
    if (m_screenBlanked)
        wakeScreen();
}

/* 显示黑屏遮罩并调用硬件息屏。 */
void DopplerMainWindow::blankScreen()
{
    if (m_screenBlanked || m_otaUiLocked)
        return;
    m_screenBlanked = true;
    BoardDisplay::blank();
    if (m_blankCover) {
        m_blankCover->setGeometry(rect());
        m_blankCover->show();
        m_blankCover->raise();
    }
}

/* 隐藏遮罩并硬件亮屏。 */
void DopplerMainWindow::wakeScreen()
{
    m_idleSince.restart();
    if (!m_screenBlanked)
        return;
    m_screenBlanked = false;
    BoardDisplay::unblank();
    if (m_blankCover)
        m_blankCover->hide();
}

/* 设置息屏分钟数；可选写入 conf。 */
void DopplerMainWindow::applyBlankMinutes(int minutes, bool save)
{
    if (minutes < 1)
        minutes = 1;
    if (minutes > 5)
        minutes = 5;
    m_blankMinutes = minutes;
    m_idleSince.restart();
    refreshBlankMinuteButtons();
    if (save) {
        if (saveBlankMinutesToConf(minutes)) {
            appendUiLog(QStringLiteral("息屏已设为 %1 分钟").arg(minutes));
            setLabelTextIfChanged(m_lblBlankHint,
                                  QStringLiteral("当前：无操作 %1 分钟后息屏。点屏幕任意处亮屏；升级中不息屏。")
                                      .arg(minutes));
        } else {
            QMessageBox::warning(this, QStringLiteral("息屏设置"),
                                 QStringLiteral("无法写入 doppler.conf，本次仅本次运行有效。"));
        }
    }
}

/* 刷新息屏分钟按钮选中样式。 */
void DopplerMainWindow::refreshBlankMinuteButtons()
{
    for (int i = 0; i < 5; ++i) {
        if (!m_btnBlankMin[i])
            continue;
        const bool sel = (i + 1 == m_blankMinutes);
        m_btnBlankMin[i]->setObjectName(sel ? QStringLiteral("selectedBtn")
                                              : QString());
        /* 触发样式刷新 */
        m_btnBlankMin[i]->style()->unpolish(m_btnBlankMin[i]);
        m_btnBlankMin[i]->style()->polish(m_btnBlankMin[i]);
        m_btnBlankMin[i]->update();
    }
}

/* 从 conf 读取 ScreenBlankMinutes。 */
int DopplerMainWindow::loadBlankMinutesFromConf() const
{
    const QString path = configFilePath();
    if (path.isEmpty())
        return 5;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return 5;
    const QString text = decodeConfBytes(f.readAll());
    f.close();
    const QStringList lines = text.split(QChar('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i).trimmed();
        if (line.startsWith(QLatin1Char('#')) || line.isEmpty())
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        QString key = line.left(eq).trimmed();
        QString val = line.mid(eq + 1).trimmed();
        if (key.compare(QStringLiteral("ScreenBlankMinutes"), Qt::CaseInsensitive) == 0) {
            bool ok = false;
            const int n = val.toInt(&ok);
            if (ok && n >= 1 && n <= 5)
                return n;
        }
    }
    return 5;
}

/* 将息屏分钟以 UTF-8 安全方式写入 conf。 */
bool DopplerMainWindow::saveBlankMinutesToConf(int minutes) const
{
    const QString path = configFilePath();
    if (path.isEmpty())
        return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QString text = decodeConfBytes(f.readAll());
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
        QString key = trimmed.left(eq).trimmed();
        if (key.compare(QStringLiteral("ScreenBlankMinutes"), Qt::CaseInsensitive) == 0) {
            lines[i] = QStringLiteral("ScreenBlankMinutes = %1").arg(minutes);
            found = true;
        }
    }
    if (!found)
        lines.append(QStringLiteral("ScreenBlankMinutes = %1").arg(minutes));

    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    /* 统一写成 UTF-8，保留中文注释 */
    QByteArray out = lines.join(QChar('\n')).toUtf8();
    if (!out.endsWith('\n'))
        out.append('\n');
    const bool ok = (f.write(out) == out.size());
    f.close();
    return ok;
}

/* 打开息屏设置页。 */
void DopplerMainWindow::onShowBlankSettingsPage()
{
    if (m_otaUiLocked)
        return;
    noteUserActivity();
    refreshBlankMinuteButtons();
    setLabelTextIfChanged(m_lblBlankHint,
                          QStringLiteral("当前：无操作 %1 分钟后息屏。点屏幕任意处亮屏；升级中不息屏。")
                              .arg(m_blankMinutes));
    m_stack->setCurrentWidget(m_pageBlankSettings);
}

/* 处理息屏分钟按钮点击并保存。 */
void DopplerMainWindow::onBlankMinutesChosen()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;
    const int minutes = btn->property("blankMin").toInt();
    applyBlankMinutes(minutes, true);
}

/* 配置编辑：记录当前获得焦点的输入框。 */
void DopplerMainWindow::onCfgFieldFocus()
{
    QLineEdit *edit = qobject_cast<QLineEdit *>(sender());
    if (edit)
        m_cfgActiveEdit = edit;
}

/* 配置数字键盘：向当前字段追加数字。 */
void DopplerMainWindow::onCfgNumDigit()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn || !m_cfgActiveEdit)
        return;
    m_cfgActiveEdit->setText(m_cfgActiveEdit->text() + btn->text());
}

/* 配置数字键盘：当前字段退格。 */
void DopplerMainWindow::onCfgNumBackspace()
{
    if (!m_cfgActiveEdit)
        return;
    QString t = m_cfgActiveEdit->text();
    if (!t.isEmpty())
        t.chop(1);
    m_cfgActiveEdit->setText(t);
}

/* 配置数字键盘：清空当前字段。 */
void DopplerMainWindow::onCfgNumClear()
{
    if (m_cfgActiveEdit)
        m_cfgActiveEdit->clear();
}

/* 保存可编辑配置并热加载业务配置。 */
void DopplerMainWindow::onSaveConfigFields()
{
    if (!m_bizOk)
        return;

    if (m_editStationId->text().trimmed().isEmpty()
            || m_editCollect->text().trimmed().isEmpty()
            || m_editReport->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("修改配置"),
                             QStringLiteral("StationID / CollectInterval / ReportInterval 不能为空"));
        return;
    }

    const int ret = QMessageBox::question(
        this, QStringLiteral("保存配置"),
        QStringLiteral("将更新这 6 项并热加载，是否继续？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    if (!writeEditableFieldsToConf()) {
        QMessageBox::warning(this, QStringLiteral("保存失败"),
                             QStringLiteral("无法写入 doppler.conf"));
        return;
    }

    if (!ReloadConfigKeepUart()) {
        appendUiLog(QStringLiteral("配置已写盘，但热加载失败"));
        QMessageBox::warning(this, QStringLiteral("热加载"),
                             QStringLiteral("文件已保存，热加载失败，建议重启设备。"));
        onShowConfigPage();
        return;
    }
    appendUiLog(QStringLiteral("已保存站号/密码/间隔等配置并热加载"));
    QMessageBox::information(this, QStringLiteral("保存成功"),
                             QStringLiteral("配置已保存并热加载。"));
    onShowConfigPage();
}

/* 确认后重启设备。 */
void DopplerMainWindow::onRebootDevice()
{
    if (m_otaUiLocked)
        return;
    const int ret = QMessageBox::warning(
        this, QStringLiteral("重启设备"),
        QStringLiteral("确定要重启整机吗？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;
    appendUiLog(QStringLiteral("即将重启设备…"));
    QApplication::processEvents();
    (void)system("reboot");
}

/* 业务初始化完成：更新就绪状态与日志。 */
void DopplerMainWindow::onBusinessReady(int result)
{
    if (result == 0) {
        setBizEnabled(true);
        appendUiLog(QStringLiteral("业务启动成功"));
        updateHomeStatus();
        onRefresh();
    } else {
        setBizEnabled(false);
        appendUiLog(QStringLiteral("业务启动失败，请检查 doppler.conf"));
        updateHomeStatus();
    }
}

/* 启动 Doppler 业务（由 main 延迟单次触发）。 */
void DopplerMainWindow::onStartBusiness()
{
    appendUiLog(QStringLiteral("正在初始化串口 / 定时器 / 链路…"));
    const int rc = Doppler_BusinessStart();
    onBusinessReady(rc);
}

/* 刷新首页业务/传感器/链路状态灯。 */
void DopplerMainWindow::updateHomeStatus()
{
    const bool bizOk = m_bizOk;
    const bool sensorReal = bizOk && (g_stMeasureConfig.u8SensorSimulate == 0);
    bool linkOk = false;
    if (bizOk) {
        if (WebLink_IsSocketMode())
            linkOk = SocketLink_IsConnected() != 0;
        else
            linkOk = UplinkOffline_IsOnline() != 0;
    }

    setStatusChip(m_lblStatBiz, QStringLiteral("业务"), bizOk);
    setStatusChip(m_lblStatSensor, QStringLiteral("传感器"), sensorReal);
    setStatusChip(m_lblStatLink, QStringLiteral("4G"), linkOk);
}

/* 定时刷新时钟、时长、指标、断面、息屏与 OTA。 */
void DopplerMainWindow::onRefresh()
{
    setLabelTextIfChanged(m_lblClock,
                          QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd  hh:mm:ss")));

    const qint64 ms = m_uptime.elapsed();
    const int sec = static_cast<int>(ms / 1000);
    const int h = sec / 3600;
    const int m = (sec % 3600) / 60;
    const int s = sec % 60;
    setLabelTextIfChanged(m_valUptime,
                          QStringLiteral("运行 %1:%2:%3")
                              .arg(h, 2, 10, QChar('0'))
                              .arg(m, 2, 10, QChar('0'))
                              .arg(s, 2, 10, QChar('0')));

    updateHomeStatus();

    /* 空闲只做轻量 busy 探测；进行中或已在 OTA 页再刷详情 */
    if (m_bizOk) {
        const bool otaBusy = OtaUpgrade_IsBusy();
        if (otaBusy || m_otaWasBusy || m_otaUiLocked
                || m_stack->currentWidget() == m_pageOta) {
            updateOtaUi();
        }
        if (otaBusy || m_otaUiLocked)
            wakeScreen();
    }

    if (!m_screenBlanked && !m_otaUiLocked && m_blankMinutes >= 1) {
        const qint64 idleMs = m_idleSince.elapsed();
        if (idleMs >= (qint64)m_blankMinutes * 60LL * 1000LL)
            blankScreen();
    }

    if (!m_bizOk || m_otaUiLocked || OtaUpgrade_IsBusy())
        return;

    TrafficReportSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    if (Sensor_GetTrafficReportSnapshot(&snap) == SUCCESS && snap.u8Valid) {
        float vel1 = 0.f, vel2 = 0.f, water = 0.f, secQ = 0.f, hourQ = 0.f;
        if (snap.u8LineCount > 0 && snap.astLines[0].u8Valid) {
            vel1 = snap.astLines[0].stRaw.fFlowVelocity;
            water = snap.astLines[0].stRaw.fWaterLevel;
            secQ = snap.astLines[0].stRaw.fSecFlow;
            hourQ = snap.astLines[0].stRaw.fHourFlow;
        }
        if (snap.u8LineCount > 1 && snap.astLines[1].u8Valid)
            vel2 = snap.astLines[1].stRaw.fFlowVelocity;

        setLabelTextIfChanged(m_valVel1, QStringLiteral("%1").arg(vel1, 0, 'f', 3));
        setLabelTextIfChanged(m_valVel2, QStringLiteral("%1").arg(vel2, 0, 'f', 3));
        setLabelTextIfChanged(m_valWater, QStringLiteral("%1").arg(water, 0, 'f', 2));
        setLabelTextIfChanged(m_valSecFlow, QStringLiteral("%1").arg(secQ, 0, 'f', 3));
        setLabelTextIfChanged(m_valHourFlow, QStringLiteral("%1").arg(hourQ, 0, 'f', 1));

        const double waterElev = static_cast<double>(snap.i32AvgWaterScaled) / 100.0;
        if (m_sectionView)
            m_sectionView->setLiveData(waterElev, true);
    } else {
        setLabelTextIfChanged(m_valVel1, QStringLiteral("--"));
        setLabelTextIfChanged(m_valVel2, QStringLiteral("--"));
        setLabelTextIfChanged(m_valWater, QStringLiteral("--"));
        setLabelTextIfChanged(m_valSecFlow, QStringLiteral("--"));
        setLabelTextIfChanged(m_valHourFlow, QStringLiteral("--"));
        if (m_sectionView)
            m_sectionView->setLiveData(0.0, false);
    }

    float volt = 0.f;
    if (Dam5213_GetVoltage(&volt) == SUCCESS && volt > 0.01f)
        setLabelTextIfChanged(m_valVoltage, QStringLiteral("%1").arg(volt, 0, 'f', 2));
    else
        setLabelTextIfChanged(m_valVoltage, QStringLiteral("--"));
}

/* 手动触发传感器采集。 */
void DopplerMainWindow::onCollect()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    ReadFlowMeterData(NULL);
    appendUiLog(QStringLiteral("已触发手动采集"));
    onRefresh();
}

/* 手动触发数据上报。 */
void DopplerMainWindow::onReport()
{
    if (m_otaUiLocked || !m_bizOk)
        return;
    SendDopplerRealData(NULL);
    appendUiLog(QStringLiteral("已触发手动上报"));
}

/* 进入退出密码鉴权页。 */
void DopplerMainWindow::onExit()
{
    if (m_otaUiLocked) {
        QMessageBox::warning(this, QStringLiteral("升级中"),
                             QStringLiteral("正在升级固件，请勿退出或断电。"));
        return;
    }
    noteUserActivity();
    if (m_editExitPwd)
        m_editExitPwd->clear();
    if (m_lblExitPwdHint) {
        setLabelTextIfChanged(m_lblExitPwdHint,
                              QStringLiteral("请输入退出密码后点「确认退出」。"));
    }
    m_stack->setCurrentWidget(m_pageExitAuth);
}

/* 退出密码键盘：追加数字。 */
void DopplerMainWindow::onExitPwdDigit()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn || !m_editExitPwd)
        return;
    if (m_editExitPwd->text().size() >= 12)
        return;
    m_editExitPwd->setText(m_editExitPwd->text() + btn->text());
}

/* 退出密码键盘：退格。 */
void DopplerMainWindow::onExitPwdBackspace()
{
    if (!m_editExitPwd)
        return;
    QString t = m_editExitPwd->text();
    if (!t.isEmpty())
        t.chop(1);
    m_editExitPwd->setText(t);
}

/* 退出密码键盘：清空。 */
void DopplerMainWindow::onExitPwdClear()
{
    if (m_editExitPwd)
        m_editExitPwd->clear();
}

/* 返回硬编码退出密码。 */
QString DopplerMainWindow::exitPassword() const
{
    /* 固定退出密码，不写 conf */
    return QStringLiteral("111111");
}

/* 校验退出密码，通过则退出程序。 */
void DopplerMainWindow::onExitConfirm()
{
    if (m_otaUiLocked)
        return;
    const QString expect = exitPassword();
    const QString got = m_editExitPwd ? m_editExitPwd->text() : QString();
    if (got != expect) {
        if (m_editExitPwd)
            m_editExitPwd->clear();
        setLabelTextIfChanged(m_lblExitPwdHint, QStringLiteral("密码错误，请重试。"));
        QMessageBox::warning(this, QStringLiteral("退出程序"),
                             QStringLiteral("密码错误"));
        return;
    }
    doExitProgram();
}

/* 关闭蜂鸣器并退出进程。 */
void DopplerMainWindow::doExitProgram()
{
    hide();
    QApplication::processEvents();
    BoardBuzzer::forceOff();
    BoardDisplay::unblank();
    ::_exit(0);
}
