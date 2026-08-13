#include "doppler_mainwindow.h"
#include "board_buzzer.h"

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFont>
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
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTextCursor>
#include <QTextEdit>
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
}

namespace {
const size_t kDeviceLogExportMax = 512U * 1024U;
}

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
    , m_cfgActiveEdit(0)
    , m_bizOk(false)
    , m_otaWasBusy(false)
{
    buildUi();
    m_uptime.start();
    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(onRefresh()));
    m_timer->start(1000);
    setBizEnabled(false);
    appendUiLog(QStringLiteral("界面已显示，正在启动业务…"));
}

DopplerMainWindow::~DopplerMainWindow()
{
}

QString DopplerMainWindow::configFilePath() const
{
    char dir[MAX_PATH];
    memset(dir, 0, sizeof(dir));
    if (!GetAppDir(dir))
        return QString();
    return QString::fromLocal8Bit(dir) + QStringLiteral(CONFIG_FILE_PATH);
}

QWidget *DopplerMainWindow::makeMetricTile(const QString &caption, QLabel **valueLabel)
{
    QFrame *tile = new QFrame(m_pageHome);
    tile->setObjectName(QStringLiteral("metricTile"));
    tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QLabel *cap = new QLabel(caption, tile);
    cap->setObjectName(QStringLiteral("metricCap"));
    cap->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    cap->setWordWrap(true);

    QLabel *val = new QLabel(QStringLiteral("--"), tile);
    val->setObjectName(QStringLiteral("metricVal"));
    val->setAlignment(Qt::AlignCenter);
    val->setWordWrap(true);

    QVBoxLayout *vl = new QVBoxLayout(tile);
    vl->setContentsMargins(12, 14, 12, 14);
    vl->setSpacing(6);
    vl->addWidget(cap);
    vl->addWidget(val, 1);

    *valueLabel = val;
    return tile;
}

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

void DopplerMainWindow::buildUi()
{
    m_root = new QWidget(this);
    setCentralWidget(m_root);
    m_root->setObjectName(QStringLiteral("root"));

    m_root->setStyleSheet(QStringLiteral(
        "QWidget#root { background-color:#f3f6f8; color:#1a2330; }"
        "QFrame#topBar { background-color:#ffffff; border:none; border-bottom:3px solid #0b7a7a; }"
        "QLabel#brand { color:#102033; font-size:28px; font-weight:bold; }"
        "QLabel#clock { color:#334155; font-size:22px; font-weight:bold; }"
        "QLabel#metaText { color:#243447; font-size:20px; font-weight:bold; }"
        "QFrame#metricTile { background-color:#ffffff; border:2px solid #c9d4df; border-radius:10px; }"
        "QLabel#metricCap { color:#3b4d61; font-size:18px; font-weight:bold; }"
        "QLabel#metricVal { color:#0b1c2c; font-size:40px; font-weight:bold; }"
        "QLabel#pageTitle { color:#102033; font-size:24px; font-weight:bold; }"
        "QLabel#hintText { color:#3b4d61; font-size:16px; }"
        "QPushButton {"
        "  background-color:#0b7a7a; color:#ffffff; border:none; border-radius:8px;"
        "  padding:12px 16px; font-size:20px; font-weight:bold; min-height:60px;"
        "}"
        "QPushButton:pressed { background-color:#096666; }"
        "QPushButton:disabled { background-color:#b7c2cc; color:#5b6b7a; }"
        "QPushButton#secondaryBtn { background-color:#1d4f7a; }"
        "QPushButton#secondaryBtn:pressed { background-color:#163d5e; }"
        "QPushButton#dangerBtn { background-color:#b42318; }"
        "QPushButton#dangerBtn:pressed { background-color:#912018; }"
        "QPushButton#exitBtn { background-color:#b42318; min-width:100px; min-height:48px;"
        "  padding:8px 16px; font-size:18px; }"
        "QPushButton#exitBtn:pressed { background-color:#912018; }"
        "QPushButton#backBtn { background-color:#334155; min-width:140px; }"
        "QPushButton#cfgPadBtn { min-height:32px; max-height:42px; padding:4px 6px; font-size:16px; }"
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
    ));

    m_stack = new QStackedWidget(m_root);

    /* ---------- Home ---------- */
    m_pageHome = new QWidget(m_stack);
    QFrame *topBar = new QFrame(m_pageHome);
    topBar->setObjectName(QStringLiteral("topBar"));
    m_title = new QLabel(QStringLiteral("时差法 流量监测"), topBar);
    m_title->setObjectName(QStringLiteral("brand"));
    m_lblClock = new QLabel(topBar);
    m_lblClock->setObjectName(QStringLiteral("clock"));
    m_btnExit = new QPushButton(QStringLiteral("退出"), topBar);
    m_btnExit->setObjectName(QStringLiteral("exitBtn"));
    connect(m_btnExit, SIGNAL(clicked()), this, SLOT(onExit()));

    QHBoxLayout *topLay = new QHBoxLayout(topBar);
    topLay->setContentsMargins(16, 12, 16, 12);
    topLay->setSpacing(12);
    topLay->addWidget(m_title);
    topLay->addStretch(1);
    topLay->addWidget(m_lblClock);
    topLay->addWidget(m_btnExit);

    QWidget *tileVel1 = makeMetricTile(QStringLiteral("流速 1\n(m/s)"), &m_valVel1);
    QWidget *tileVel2 = makeMetricTile(QStringLiteral("流速 2\n(m/s)"), &m_valVel2);
    QWidget *tileWater = makeMetricTile(QStringLiteral("水位\n(m)"), &m_valWater);
    QWidget *tileSec = makeMetricTile(QStringLiteral("秒流量\n(m³/s)"), &m_valSecFlow);
    QWidget *tileHour = makeMetricTile(QStringLiteral("小时流量\n(m³/h)"), &m_valHourFlow);
    QWidget *tileUp = makeMetricTile(QStringLiteral("运行时长"), &m_valUptime);

    QGridLayout *grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(14);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->addWidget(tileVel1, 0, 0);
    grid->addWidget(tileVel2, 0, 1);
    grid->addWidget(tileWater, 0, 2);
    grid->addWidget(tileSec, 1, 0);
    grid->addWidget(tileHour, 1, 1);
    grid->addWidget(tileUp, 1, 2);
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);

    m_btnCollect = new QPushButton(QStringLiteral("手动采集"), m_pageHome);
    m_btnReport = new QPushButton(QStringLiteral("手动上报"), m_pageHome);
    m_btnFunc = new QPushButton(QStringLiteral("菜单"), m_pageHome);
    m_btnFunc->setObjectName(QStringLiteral("secondaryBtn"));
    m_btnLog = new QPushButton(QStringLiteral("屏显日志"), m_pageHome);
    m_btnLog->setObjectName(QStringLiteral("secondaryBtn"));
    connect(m_btnCollect, SIGNAL(clicked()), this, SLOT(onCollect()));
    connect(m_btnReport, SIGNAL(clicked()), this, SLOT(onReport()));
    connect(m_btnFunc, SIGNAL(clicked()), this, SLOT(onShowFuncPage()));
    connect(m_btnLog, SIGNAL(clicked()), this, SLOT(onShowLogPage()));

    QHBoxLayout *btnLay = new QHBoxLayout;
    btnLay->setSpacing(12);
    btnLay->setContentsMargins(0, 4, 0, 0);
    btnLay->addWidget(m_btnCollect, 1);
    btnLay->addWidget(m_btnReport, 1);
    btnLay->addWidget(m_btnFunc, 1);
    btnLay->addWidget(m_btnLog, 1);

    QVBoxLayout *homeLay = new QVBoxLayout(m_pageHome);
    homeLay->setContentsMargins(0, 0, 0, 0);
    homeLay->setSpacing(0);
    homeLay->addWidget(topBar);
    QVBoxLayout *body = new QVBoxLayout;
    body->setContentsMargins(18, 16, 18, 16);
    body->setSpacing(14);
    body->addLayout(grid, 1);
    body->addLayout(btnLay, 0);
    homeLay->addLayout(body, 1);

    /* ---------- Log page ---------- */
    m_pageLog = new QWidget(m_stack);
    QLabel *logTitle = new QLabel(QStringLiteral("显示屏运行日志"), m_pageLog);
    logTitle->setObjectName(QStringLiteral("pageTitle"));
    m_btnBackLog = new QPushButton(QStringLiteral("返回"), m_pageLog);
    m_btnBackLog->setObjectName(QStringLiteral("backBtn"));
    connect(m_btnBackLog, SIGNAL(clicked()), this, SLOT(onBackHome()));
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
    m_btnOpenWater = new QPushButton(QStringLiteral("设置水位后测流"), m_pageFunc);
    m_btnViewConfig = new QPushButton(QStringLiteral("查看配置"), m_pageFunc);
    m_btnViewConfig->setObjectName(QStringLiteral("secondaryBtn"));
    m_btnOtaPage = new QPushButton(QStringLiteral("升级进度 / 结果"), m_pageFunc);
    m_btnOtaPage->setObjectName(QStringLiteral("secondaryBtn"));
    m_btnDevLogPage = new QPushButton(QStringLiteral("设备完整日志"), m_pageFunc);
    m_btnDevLogPage->setObjectName(QStringLiteral("secondaryBtn"));
    m_btnReboot = new QPushButton(QStringLiteral("重启设备"), m_pageFunc);
    m_btnReboot->setObjectName(QStringLiteral("dangerBtn"));
    connect(m_btnStartTest, SIGNAL(clicked()), this, SLOT(onStartTest()));
    connect(m_btnOpenWater, SIGNAL(clicked()), this, SLOT(onShowWaterPage()));
    connect(m_btnViewConfig, SIGNAL(clicked()), this, SLOT(onShowConfigPage()));
    connect(m_btnOtaPage, SIGNAL(clicked()), this, SLOT(onShowOtaPage()));
    connect(m_btnDevLogPage, SIGNAL(clicked()), this, SLOT(onShowDeviceLogPage()));
    connect(m_btnReboot, SIGNAL(clicked()), this, SLOT(onRebootDevice()));

    QGridLayout *funcGrid = new QGridLayout;
    funcGrid->setHorizontalSpacing(12);
    funcGrid->setVerticalSpacing(12);
    funcGrid->addWidget(m_btnStartTest, 0, 0);
    funcGrid->addWidget(m_btnOpenWater, 0, 1);
    funcGrid->addWidget(m_btnViewConfig, 1, 0);
    funcGrid->addWidget(m_btnOtaPage, 1, 1);
    funcGrid->addWidget(m_btnDevLogPage, 2, 0);
    funcGrid->addWidget(m_btnReboot, 2, 1);

    QVBoxLayout *funcLay = new QVBoxLayout(m_pageFunc);
    funcLay->setContentsMargins(16, 14, 16, 14);
    funcLay->setSpacing(10);
    funcLay->addLayout(funcTop);
    funcLay->addLayout(funcGrid);
    funcLay->addStretch(1);
    QLabel *otaNote = new QLabel(
        QStringLiteral("说明：固件升级仍由网页下发。本机可查看配置，并可修改站号/密码/采集上报间隔等常用项。"),
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
    connect(m_btnBackWater, SIGNAL(clicked()), this, SLOT(onShowFuncPage()));
    QHBoxLayout *waterTop = new QHBoxLayout;
    waterTop->addWidget(waterTitle);
    waterTop->addStretch(1);
    waterTop->addWidget(m_btnBackWater);

    QLabel *waterHint = new QLabel(
        QStringLiteral("输入水位（厘米），点数字键盘，无需外接键盘"), m_pageWater);
    waterHint->setObjectName(QStringLiteral("hintText"));
    waterHint->setWordWrap(true);
    m_editWaterCm = new QLineEdit(m_pageWater);
    m_editWaterCm->setPlaceholderText(QStringLiteral("例如 150"));
    m_editWaterCm->setReadOnly(true);
    m_btnApplyWater = new QPushButton(QStringLiteral("应用并上报"), m_pageWater);
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
            connect(kb, SIGNAL(clicked()), this, SLOT(onNumPadClear()));
        } else if (i == 11) {
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
    /* 键盘不要抢太多垂直空间 */
    cfgEditLay->addWidget(cfgPadHost, 1);
    cfgPadHost->setMaximumHeight(220);

    /* ---------- OTA status page ---------- */
    m_pageOta = new QWidget(m_stack);
    QLabel *otaTitle = new QLabel(QStringLiteral("升级进度 / 结果"), m_pageOta);
    otaTitle->setObjectName(QStringLiteral("pageTitle"));
    m_btnBackOta = new QPushButton(QStringLiteral("返回"), m_pageOta);
    m_btnBackOta->setObjectName(QStringLiteral("backBtn"));
    connect(m_btnBackOta, SIGNAL(clicked()), this, SLOT(onShowFuncPage()));
    QHBoxLayout *otaTop = new QHBoxLayout;
    otaTop->addWidget(otaTitle);
    otaTop->addStretch(1);
    otaTop->addWidget(m_btnBackOta);
    m_lblOtaState = new QLabel(QStringLiteral("状态：空闲"), m_pageOta);
    m_lblOtaState->setObjectName(QStringLiteral("pageTitle"));
    m_lblOtaState->setWordWrap(true);
    m_otaProgress = new QProgressBar(m_pageOta);
    m_otaProgress->setRange(0, 100);
    m_otaProgress->setValue(0);
    m_otaProgress->setTextVisible(true);
    m_otaProgress->setMinimumHeight(36);
    m_lblOtaDetail = new QLabel(QStringLiteral("等待网页下发固件…"), m_pageOta);
    m_lblOtaDetail->setObjectName(QStringLiteral("hintText"));
    m_lblOtaDetail->setWordWrap(true);
    m_lblOtaResult = new QLabel(QStringLiteral("最近结果：尚无"), m_pageOta);
    m_lblOtaResult->setObjectName(QStringLiteral("metaText"));
    m_lblOtaResult->setWordWrap(true);
    QLabel *otaHint = new QLabel(
        QStringLiteral("本页只显示本机接收进度与上报结果；成功后设备会自动重启。"),
        m_pageOta);
    otaHint->setObjectName(QStringLiteral("hintText"));
    otaHint->setWordWrap(true);
    QVBoxLayout *otaLay = new QVBoxLayout(m_pageOta);
    otaLay->setContentsMargins(16, 14, 16, 14);
    otaLay->setSpacing(12);
    otaLay->addLayout(otaTop);
    otaLay->addWidget(m_lblOtaState);
    otaLay->addWidget(m_otaProgress);
    otaLay->addWidget(m_lblOtaDetail);
    otaLay->addWidget(m_lblOtaResult);
    otaLay->addStretch(1);
    otaLay->addWidget(otaHint);

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
    m_btnDevLogWarn->setObjectName(QStringLiteral("secondaryBtn"));
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

    m_stack->addWidget(m_pageHome);        /* 0 */
    m_stack->addWidget(m_pageLog);         /* 1 */
    m_stack->addWidget(m_pageFunc);        /* 2 */
    m_stack->addWidget(m_pageConfig);      /* 3 */
    m_stack->addWidget(m_pageWater);       /* 4 */
    m_stack->addWidget(m_pageOta);         /* 5 */
    m_stack->addWidget(m_pageDevLog);      /* 6 */
    m_stack->addWidget(m_pageConfigEdit);  /* 7 */

    /* Touch UI: no keyboard focus ring (dashed rect) after button press. */
    const QList<QPushButton *> buttons = findChildren<QPushButton *>();
    for (int i = 0; i < buttons.size(); ++i)
        buttons.at(i)->setFocusPolicy(Qt::NoFocus);

    QVBoxLayout *rootLay = new QVBoxLayout(m_root);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->addWidget(m_stack);
}

void DopplerMainWindow::setBizEnabled(bool on)
{
    m_bizOk = on;
    m_btnCollect->setEnabled(on);
    m_btnReport->setEnabled(on);
    m_btnStartTest->setEnabled(on);
    m_btnApplyWater->setEnabled(on);
    m_btnEditConfig->setEnabled(on);
    m_btnSaveConfigFields->setEnabled(on);
}

void DopplerMainWindow::appendUiLog(const QString &line)
{
    const QString stamped = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"))
            + QStringLiteral("  ") + line;
    m_log->append(stamped);
    if (m_log->document()->blockCount() > 120)
        m_log->clear();
}

void DopplerMainWindow::onShowLogPage()
{
    m_stack->setCurrentWidget(m_pageLog);
}

void DopplerMainWindow::onShowFuncPage()
{
    m_stack->setCurrentWidget(m_pageFunc);
}

void DopplerMainWindow::onShowWaterPage()
{
    m_stack->setCurrentWidget(m_pageWater);
}

void DopplerMainWindow::onShowOtaPage()
{
    updateOtaUi();
    m_stack->setCurrentWidget(m_pageOta);
}

void DopplerMainWindow::onShowDeviceLogPage()
{
    m_stack->setCurrentWidget(m_pageDevLog);
}

void DopplerMainWindow::onLoadDeviceLogAll()
{
    loadDeviceLog(0);
}

void DopplerMainWindow::onLoadDeviceLogWarn()
{
    loadDeviceLog(1);
}

void DopplerMainWindow::onLoadDeviceLogError()
{
    loadDeviceLog(2);
}

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

void DopplerMainWindow::updateOtaUi()
{
    ota_ui_status_t st;
    memset(&st, 0, sizeof(st));
    OtaUpgrade_QueryUiStatus(&st);

    char fw[64];
    memset(fw, 0, sizeof(fw));
    FirmwareVersion_Format(fw, sizeof(fw));

    if (!st.inited) {
        m_lblOtaState->setText(QStringLiteral("状态：OTA 未初始化"));
        m_otaProgress->setValue(0);
        m_lblOtaDetail->setText(QStringLiteral("当前固件版本：%1").arg(QString::fromLatin1(fw)));
        m_lblOtaResult->setText(QStringLiteral("最近结果：尚无"));
        return;
    }

    const QString state = otaStatusText(st.status);
    if (st.busy) {
        m_lblOtaState->setText(QStringLiteral("状态：进行中 · %1").arg(state));
        if (!m_otaWasBusy) {
            appendUiLog(QStringLiteral("OTA 开始升级"));
            m_otaWasBusy = true;
        }
    } else {
        m_lblOtaState->setText(QStringLiteral("状态：%1").arg(state));
        if (m_otaWasBusy) {
            appendUiLog(QStringLiteral("OTA 会话结束"));
            m_otaWasBusy = false;
        }
    }

    int pct = 0;
    if (st.pkt_total > 0) {
        unsigned done = (st.pkt_expect > 0) ? (unsigned)(st.pkt_expect - 1U) : 0U;
        if (done > st.pkt_total)
            done = st.pkt_total;
        pct = (int)((done * 100U) / st.pkt_total);
        if (st.status == OTA_STATUS_APPLYING || st.status == OTA_STATUS_OK)
            pct = 100;
    } else if (st.file_size > 0 && st.recv_size > 0) {
        pct = (int)((st.recv_size * 100ULL) / st.file_size);
        if (pct > 100)
            pct = 100;
    }
    m_otaProgress->setValue(pct);

    const QString slot = (st.active_slot == OTA_SLOT_B)
            ? QStringLiteral("B") : QStringLiteral("A");
    QString detail = QStringLiteral("当前固件：%1 · 活动槽 %2\n")
            .arg(QString::fromLatin1(fw)).arg(slot);
    if (st.busy || st.status == OTA_STATUS_RECEIVING || st.status == OTA_STATUS_APPLYING) {
        detail += QStringLiteral("会话版本：%1\n包进度：%2 / %3 · 已收 %4 字节 / 文件 %5")
                .arg(QString::fromLatin1(st.version))
                .arg(st.pkt_expect > 0 ? st.pkt_expect - 1 : 0)
                .arg(st.pkt_total)
                .arg(st.recv_size)
                .arg(st.file_size);
        if (st.reboot_pending)
            detail += QStringLiteral("\n即将重启以激活新固件…");
    } else {
        detail += QStringLiteral("等待网页下发固件包。");
    }
    m_lblOtaDetail->setText(detail);

    if (st.have_last_result) {
        m_lblOtaResult->setText(
            QStringLiteral("最近结果：%1 · 错误 %2 · 版本 %3")
                .arg(st.last_ok ? QStringLiteral("成功") : QStringLiteral("失败"))
                .arg(otaErrText(st.last_err))
                .arg(QString::fromLatin1(st.last_version)));
    } else {
        m_lblOtaResult->setText(QStringLiteral("最近结果：尚无（本次启动后未完成过升级会话）"));
    }
}

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

void DopplerMainWindow::onBackHome()
{
    m_stack->setCurrentWidget(m_pageHome);
}

void DopplerMainWindow::onStartTest()
{
    if (!m_bizOk)
        return;
    Sensor_RequestStartTestReport();
    appendUiLog(QStringLiteral("已触发加测"));
    QMessageBox::information(this, QStringLiteral("加测"),
                             QStringLiteral("已请求加测，采集有效后会自动上报。"));
}

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

void DopplerMainWindow::onNumPadBackspace()
{
    if (!m_editWaterCm)
        return;
    QString t = m_editWaterCm->text();
    if (!t.isEmpty())
        t.chop(1);
    m_editWaterCm->setText(t);
}

void DopplerMainWindow::onNumPadClear()
{
    if (m_editWaterCm)
        m_editWaterCm->clear();
}

void DopplerMainWindow::onApplyWaterCm()
{
    if (!m_bizOk)
        return;
    bool ok = false;
    const int cm = m_editWaterCm->text().trimmed().toInt(&ok);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("水位"),
                             QStringLiteral("请输入整数厘米，例如 150"));
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

    QString text = QString::fromUtf8(raw);
    if (text.contains(QChar::ReplacementCharacter))
        text = QString::fromLocal8Bit(raw);
    m_cfgView->setPlainText(text);
}

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

void DopplerMainWindow::loadEditableFieldsFromConf()
{
    const QString path = configFilePath();
    QFile f(path);
    QString text;
    if (f.open(QIODevice::ReadOnly)) {
        text = QString::fromUtf8(f.readAll());
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

bool DopplerMainWindow::writeEditableFieldsToConf()
{
    const QString path = configFilePath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QString text = QString::fromUtf8(f.readAll());
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

void DopplerMainWindow::onShowConfigPage()
{
    loadConfigToViewer();
    m_stack->setCurrentWidget(m_pageConfig);
}

void DopplerMainWindow::onShowConfigEditPage()
{
    if (!m_bizOk)
        return;
    loadEditableFieldsFromConf();
    m_stack->setCurrentWidget(m_pageConfigEdit);
}

bool DopplerMainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QLineEdit *edit = qobject_cast<QLineEdit *>(watched);
        if (edit == m_editStationId || edit == m_editCenterId || edit == m_editPassword
                || edit == m_editCollect || edit == m_editReport || edit == m_editTimeout) {
            m_cfgActiveEdit = edit;
            edit->setFocus();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void DopplerMainWindow::onCfgFieldFocus()
{
    QLineEdit *edit = qobject_cast<QLineEdit *>(sender());
    if (edit)
        m_cfgActiveEdit = edit;
}

void DopplerMainWindow::onCfgNumDigit()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn || !m_cfgActiveEdit)
        return;
    m_cfgActiveEdit->setText(m_cfgActiveEdit->text() + btn->text());
}

void DopplerMainWindow::onCfgNumBackspace()
{
    if (!m_cfgActiveEdit)
        return;
    QString t = m_cfgActiveEdit->text();
    if (!t.isEmpty())
        t.chop(1);
    m_cfgActiveEdit->setText(t);
}

void DopplerMainWindow::onCfgNumClear()
{
    if (m_cfgActiveEdit)
        m_cfgActiveEdit->clear();
}

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

void DopplerMainWindow::onRebootDevice()
{
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

void DopplerMainWindow::onBusinessReady(int result)
{
    if (result == 0) {
        setBizEnabled(true);
        appendUiLog(QStringLiteral("业务启动成功"));
        onRefresh();
    } else {
        setBizEnabled(false);
        appendUiLog(QStringLiteral("业务启动失败，请检查 doppler.conf"));
    }
}

void DopplerMainWindow::onStartBusiness()
{
    appendUiLog(QStringLiteral("正在初始化串口 / 定时器 / 链路…"));
    const int rc = Doppler_BusinessStart();
    onBusinessReady(rc);
}

void DopplerMainWindow::onRefresh()
{
    const QDateTime now = QDateTime::currentDateTime();
    m_lblClock->setText(now.toString(QStringLiteral("yyyy-MM-dd  hh:mm:ss")));

    const qint64 ms = m_uptime.elapsed();
    const int sec = static_cast<int>(ms / 1000);
    const int h = sec / 3600;
    const int m = (sec % 3600) / 60;
    const int s = sec % 60;
    m_valUptime->setText(QStringLiteral("%1:%2:%3")
                         .arg(h, 2, 10, QChar('0'))
                         .arg(m, 2, 10, QChar('0'))
                         .arg(s, 2, 10, QChar('0')));

    /* OTA 页每秒刷新；其它页也跟踪 busy，便于首页状态提示 */
    if (m_bizOk && (m_stack->currentWidget() == m_pageOta || OtaUpgrade_IsBusy() || m_otaWasBusy))
        updateOtaUi();

    if (!m_bizOk)
        return;

    if (OtaUpgrade_IsBusy())
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

        m_valVel1->setText(QStringLiteral("%1").arg(vel1, 0, 'f', 3));
        m_valVel2->setText(QStringLiteral("%1").arg(vel2, 0, 'f', 3));
        m_valWater->setText(QStringLiteral("%1").arg(water, 0, 'f', 2));
        m_valSecFlow->setText(QStringLiteral("%1").arg(secQ, 0, 'f', 3));
        m_valHourFlow->setText(QStringLiteral("%1").arg(hourQ, 0, 'f', 1));
    } else {
        m_valVel1->setText(QStringLiteral("--"));
        m_valVel2->setText(QStringLiteral("--"));
        m_valWater->setText(QStringLiteral("--"));
        m_valSecFlow->setText(QStringLiteral("--"));
        m_valHourFlow->setText(QStringLiteral("--"));
    }
}

void DopplerMainWindow::onCollect()
{
    if (!m_bizOk)
        return;
    ReadFlowMeterData(NULL);
    appendUiLog(QStringLiteral("已触发手动采集"));
    onRefresh();
}

void DopplerMainWindow::onReport()
{
    if (!m_bizOk)
        return;
    SendDopplerRealData(NULL);
    appendUiLog(QStringLiteral("已触发手动上报"));
}

void DopplerMainWindow::onExit()
{
    hide();
    QApplication::processEvents();
    BoardBuzzer::forceOff();
    ::_exit(0);
}
