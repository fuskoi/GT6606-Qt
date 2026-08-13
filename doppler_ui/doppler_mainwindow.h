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
class QGridLayout;
class QEvent;

class DopplerMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit DopplerMainWindow(QWidget *parent = 0);
    ~DopplerMainWindow();

public slots:
    void onStartBusiness();
    void onBusinessReady(int result);

private slots:
    void onRefresh();
    void onCollect();
    void onReport();
    void onExit();
    void onShowLogPage();
    void onBackHome();
    void onShowFuncPage();
    void onStartTest();
    void onApplyWaterCm();
    void onShowConfigPage();
    void onShowConfigEditPage();
    void onShowWaterPage();
    void onSaveConfigFields();
    void onRebootDevice();
    void onNumPadDigit();
    void onNumPadBackspace();
    void onNumPadClear();
    void onShowOtaPage();
    void onShowDeviceLogPage();
    void onLoadDeviceLogAll();
    void onLoadDeviceLogWarn();
    void onLoadDeviceLogError();
    void onCfgFieldFocus();
    void onCfgNumDigit();
    void onCfgNumBackspace();
    void onCfgNumClear();

private:
    void buildUi();
    void appendUiLog(const QString &line);
    void setBizEnabled(bool on);
    void loadConfigToViewer();
    void loadEditableFieldsFromConf();
    bool writeEditableFieldsToConf();
    void updateOtaUi();
    void loadDeviceLog(int filterMode);
    QString configFilePath() const;
    QWidget *makeMetricTile(const QString &caption, QLabel **valueLabel);
    QLineEdit *makeCfgField(QWidget *parent, const QString &caption, QGridLayout *grid,
                            int row, int pair, const char *keyName);
    bool eventFilter(QObject *watched, QEvent *event);
    static QString otaStatusText(unsigned status);
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

    QLabel *m_title;
    QLabel *m_lblClock;
    QLabel *m_valVel1;
    QLabel *m_valVel2;
    QLabel *m_valWater;
    QLabel *m_valSecFlow;
    QLabel *m_valHourFlow;
    QLabel *m_valUptime;
    QPushButton *m_btnCollect;
    QPushButton *m_btnReport;
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
    QPushButton *m_btnOtaPage;
    QPushButton *m_btnDevLogPage;
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
    QLabel *m_lblOtaDetail;
    QLabel *m_lblOtaResult;
    QLabel *m_lblDevLogMeta;
    QProgressBar *m_otaProgress;
    QTimer *m_timer;
    QElapsedTimer m_uptime;
    bool m_bizOk;
    bool m_otaWasBusy;
};

#endif
