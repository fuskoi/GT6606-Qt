#ifndef DOPPLER_MAINWINDOW_H
#define DOPPLER_MAINWINDOW_H

#include <QMainWindow>
#include <QElapsedTimer>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QTimer;
class QWidget;
class QFrame;
class QGridLayout;
class QEvent;
class QResizeEvent;
class SectionView;

/* 时差法流量监测主窗口：首页断面+指标、菜单功能页、OTA/配置/息屏等。 */
class DopplerMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    /* 构造主窗口：建 UI、启定时刷新、启动闲计时。 */
    explicit DopplerMainWindow(QWidget *parent = 0);

    /* 析构主窗口。 */
    ~DopplerMainWindow();

public slots:
    /* 在事件循环中异步启动 Doppler 业务线程/初始化。 */
    void onStartBusiness();

    /* 业务初始化完成回调：result 非 0 表示失败。 */
    void onBusinessReady(int result);

private slots:
    /* 每秒刷新：时钟、运行时长、状态灯、指标、断面、息屏与 OTA。 */
    void onRefresh();

    /* 菜单：手动触发一轮传感器采集。 */
    void onCollect();

    /* 菜单：手动触发一次数据上报。 */
    void onReport();

    /* 进入退出鉴权页（清空密码框）。 */
    void onExit();

    /* 打开屏显运行日志页。 */
    void onShowLogPage();

    /* 返回首页。 */
    void onBackHome();

    /* 打开功能菜单页。 */
    void onShowFuncPage();

    /* 加测：采一轮并请求上报。 */
    void onStartTest();

    /* 水位加测页：应用输入水位（空则用传感器水位）。 */
    void onApplyWaterCm();

    /* 打开只读配置查看页。 */
    void onShowConfigPage();

    /* 打开可编辑配置页并加载当前 conf。 */
    void onShowConfigEditPage();

    /* 打开设置水位加测页。 */
    void onShowWaterPage();

    /* 将编辑页字段写回 conf 并热加载。 */
    void onSaveConfigFields();

    /* 确认后重启设备。 */
    void onRebootDevice();

    /* 水位页数字键盘：追加一位数字。 */
    void onNumPadDigit();

    /* 水位页数字键盘：退格。 */
    void onNumPadBackspace();

    /* 水位页数字键盘：清空。 */
    void onNumPadClear();

    /* 打开 OTA 全屏进度页。 */
    void onShowOtaPage();

    /* 打开设备完整日志页并加载全部。 */
    void onShowDeviceLogPage();

    /* 设备日志过滤：全部。 */
    void onLoadDeviceLogAll();

    /* 设备日志过滤：警告。 */
    void onLoadDeviceLogWarn();

    /* 设备日志过滤：错误。 */
    void onLoadDeviceLogError();

    /* 配置编辑：某输入框获得焦点，记为当前编辑目标。 */
    void onCfgFieldFocus();

    /* 配置数字键盘：追加数字到当前字段。 */
    void onCfgNumDigit();

    /* 配置数字键盘：退格。 */
    void onCfgNumBackspace();

    /* 配置数字键盘：清空当前字段。 */
    void onCfgNumClear();

    /* 打开息屏分钟设置页。 */
    void onShowBlankSettingsPage();

    /* 选择息屏分钟数（1~5）并保存。 */
    void onBlankMinutesChosen();

    /* 校验退出密码后退出程序。 */
    void onExitConfirm();

    /* 退出密码数字键盘：追加数字。 */
    void onExitPwdDigit();

    /* 退出密码数字键盘：退格。 */
    void onExitPwdBackspace();

    /* 退出密码数字键盘：清空。 */
    void onExitPwdClear();

private:
    /* 搭建全部页面、样式与信号槽连接。 */
    void buildUi();

    /* 向屏显日志追加一行带时间戳的文本。 */
    void appendUiLog(const QString &line);

    /* 按业务是否就绪启用/禁用相关操作按钮。 */
    void setBizEnabled(bool on);

    /* OTA 进行中锁定界面并切到 OTA 页；结束则解锁。 */
    void setOtaUiLocked(bool locked);

    /* 刷新首页顶栏业务/传感器/链路状态灯。 */
    void updateHomeStatus();

    /* 把 doppler.conf 全文载入只读配置查看框。 */
    void loadConfigToViewer();

    /* 从 conf 填充可编辑字段（站号/密码/间隔等）。 */
    void loadEditableFieldsFromConf();

    /* 将可编辑字段写回 conf 文件；成功返回 true。 */
    bool writeEditableFieldsToConf();

    /* 根据 OtaUpgrade 状态刷新进度条与文案，并处理锁 UI。 */
    void updateOtaUi();

    /* 按过滤模式导出并显示设备日志。 */
    void loadDeviceLog(int filterMode);

    /* 返回 doppler.conf 绝对路径。 */
    QString configFilePath() const;

    /* 返回硬编码退出密码。 */
    QString exitPassword() const;

    /* 关蜂鸣并退出进程。 */
    void doExitProgram();

    /* 创建首页指标卡片；compact=true 用于右侧竖排小卡片。 */
    QWidget *makeMetricTile(const QString &caption, QLabel **valueLabel, bool compact = false);

    /* 在配置编辑网格中创建一对「标题+只读输入框」。 */
    QLineEdit *makeCfgField(QWidget *parent, const QString &caption, QGridLayout *grid,
                            int row, int pair, const char *keyName);

    /* 全局事件过滤：触摸/点击唤醒息屏并重置空闲计时。 */
    bool eventFilter(QObject *watched, QEvent *event);

    /* 窗口尺寸变化时同步息屏遮罩大小。 */
    void resizeEvent(QResizeEvent *event);

    /* 记录用户操作，重置息屏空闲计时。 */
    void noteUserActivity();

    /* 显示黑屏遮罩并调用硬件息屏。 */
    void blankScreen();

    /* 去掉遮罩并硬件亮屏。 */
    void wakeScreen();

    /* 设置息屏分钟；save=true 时写入 conf。 */
    void applyBlankMinutes(int minutes, bool save);

    /* 刷新息屏分钟按钮的选中样式。 */
    void refreshBlankMinuteButtons();

    /* 从 conf 读取 ScreenBlankMinutes，非法则返回默认。 */
    int loadBlankMinutesFromConf() const;

    /* 把息屏分钟写入 conf（UTF-8 安全）。 */
    bool saveBlankMinutesToConf(int minutes) const;

    /* OTA 状态码转中文说明。 */
    static QString otaStatusText(unsigned status);

    /* OTA 错误码转中文说明。 */
    static QString otaErrText(unsigned err);

    QWidget *m_root;
    QStackedWidget *m_stack;
    QWidget *m_pageHome;
    QWidget *m_pageLog;
    QWidget *m_pageFunc;
    QWidget *m_pageConfig;
    QWidget *m_pageConfigEdit;
    QWidget *m_pageWater;
    QWidget *m_pageOta;
    QWidget *m_pageDevLog;
    QWidget *m_pageBlankSettings;
    QWidget *m_pageExitAuth;
    QWidget *m_blankCover;

    QLabel *m_title;
    QLabel *m_lblClock;
    QLabel *m_lblStatBiz;
    QLabel *m_lblStatSensor;
    QLabel *m_lblStatLink;
    SectionView *m_sectionView;
    QLabel *m_valVel1;
    QLabel *m_valVel2;
    QLabel *m_valWater;
    QLabel *m_valSecFlow;
    QLabel *m_valHourFlow;
    QLabel *m_valVoltage;
    QLabel *m_valUptime;
    QPushButton *m_btnFuncCollect;
    QPushButton *m_btnFuncReport;
    QPushButton *m_btnLog;
    QPushButton *m_btnFunc;
    QPushButton *m_btnExit;
    QPushButton *m_btnBackLog;
    QPushButton *m_btnBackFunc;
    QPushButton *m_btnBackConfig;
    QPushButton *m_btnBackConfigEdit;
    QPushButton *m_btnBackWater;
    QPushButton *m_btnStartTest;
    QPushButton *m_btnApplyWater;
    QPushButton *m_btnOpenWater;
    QPushButton *m_btnViewConfig;
    QPushButton *m_btnEditConfig;
    QPushButton *m_btnSaveConfigFields;
    QPushButton *m_btnReboot;
    QPushButton *m_btnDevLogPage;
    QPushButton *m_btnBlankSettings;
    QPushButton *m_btnBackBlank;
    QPushButton *m_btnBlankMin[5];
    QPushButton *m_btnBackExitAuth;
    QPushButton *m_btnExitConfirm;
    QLineEdit *m_editExitPwd;
    QLabel *m_lblExitPwdHint;
    QPushButton *m_btnBackOta;
    QPushButton *m_btnBackDevLog;
    QPushButton *m_btnDevLogAll;
    QPushButton *m_btnDevLogWarn;
    QPushButton *m_btnDevLogError;
    QLineEdit *m_editWaterCm;
    QLineEdit *m_editStationId;
    QLineEdit *m_editCenterId;
    QLineEdit *m_editPassword;
    QLineEdit *m_editCollect;
    QLineEdit *m_editReport;
    QLineEdit *m_editTimeout;
    QLineEdit *m_cfgActiveEdit;
    QTextEdit *m_log;
    QTextEdit *m_cfgView;
    QTextEdit *m_devLog;
    QLabel *m_lblOtaState;
    QLabel *m_lblOtaPercent;
    QLabel *m_lblOtaBadge;
    QLabel *m_lblOtaDetail;
    QLabel *m_lblDevLogMeta;
    QLabel *m_lblBlankHint;
    QFrame *m_otaHero;
    QFrame *m_otaDetailCard;
    QProgressBar *m_otaProgress;
    QTimer *m_timer;
    QElapsedTimer m_uptime;
    QElapsedTimer m_idleSince;
    int m_blankMinutes;
    bool m_screenBlanked;
    bool m_bizOk;
    bool m_otaWasBusy;
    bool m_otaUiLocked;
};

#endif
