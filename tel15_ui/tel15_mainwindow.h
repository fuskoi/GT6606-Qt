#ifndef TEL15_MAINWINDOW_H
#define TEL15_MAINWINDOW_H

#include <QMainWindow>
#include <QElapsedTimer>
#include <QVector>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTextEdit;
class QTimer;
class QWidget;
class SectionView;

class Tel15MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit Tel15MainWindow(QWidget *parent = 0);
    ~Tel15MainWindow();

public slots:
    void onStartBusiness();
    void onBusinessReady(int result);

protected:
    bool eventFilter(QObject *watched, QEvent *event);
    void resizeEvent(QResizeEvent *event);

private slots:
    void onRefresh();
    void onBackHome();
    void onShowMenuPage();
    void onShowLogPage();
    void onShowManualPage();
    void onShowBlankPage();
    void onShowConfPage();
    void onShowConfEditPage();
    void onManualCmd();
    void onBlankMinutesChosen();
    void onConfSave();
    void onConfPad();
    void onCyclePreferUplink();
    void onCycleWebLink();
    void onCycleWatchdog();
    void onRebootDevice();
    void onExit();
    void onInstallFilters();

private:
    void buildUi();
    void appendUiLog(const QString &line);
    void setBizEnabled(bool on);
    void setManualMode(bool on);
    void updateOtaUi();
    void noteUserActivity();
    void blankScreen();
    void unblankScreen();
    void refreshBlankButtons();
    void applyBlankMinutes(int minutes, bool save);
    int loadBlankMinutesFromConf() const;
    bool saveConfKey(const QString &key, const QString &value) const;
    QString confFilePath() const;
    QString loadConfText() const;
    void loadConfEditFields();
    void refreshConfCycleButtons();
    QString measureRunText() const;
    void updateMetaLabels();
    QPushButton *makeMenuBtn(const QString &text, QWidget *parent);

    QWidget *m_root;
    QStackedWidget *m_stack;
    QWidget *m_pageHome;
    QWidget *m_pageMenu;
    QWidget *m_pageLog;
    QWidget *m_pageBlank;
    QWidget *m_pageConf;
    QWidget *m_pageConfEdit;
    QWidget *m_pageOta;
    QStackedWidget *m_sideStack;
    QWidget *m_metricCol;
    QWidget *m_manualPanel;
    QWidget *m_homeBtnBar;
    QWidget *m_blankCover;

    QLabel *m_lblClock;
    QLabel *m_lblUptime;
    QLabel *m_lblBiz;
    QLabel *m_lblLink;
    QLabel *m_lblMeta;
    QLabel *m_valWater;
    QLabel *m_valFlow;
    QLabel *m_valSpeed;
    QLabel *m_valCar;
    QLabel *m_valFish;
    QLabel *m_valRun;
    QLabel *m_lblManualStatus;
    QLabel *m_lblBlankHint;
    SectionView *m_sectionView;
    QTextEdit *m_log;
    QTextEdit *m_confView;
    QLineEdit *m_editServerIp;
    QLineEdit *m_editServerPort;
    QLabel *m_lblPrefer;
    QLabel *m_lblWebLink;
    QLabel *m_lblWatchdog;
    QLineEdit *m_confFocusEdit;
    QPushButton *m_btnMenu;
    QPushButton *m_btnManual;
    QPushButton *m_btnExit;
    QPushButton *m_btnBackLog;
    QPushButton *m_btnBackManual;
    QPushButton *m_btnBackOta;
    QVector<QPushButton *> m_btnBlankMin;
    QLabel *m_lblOtaPercent;
    QLabel *m_lblOtaState;
    QLabel *m_lblOtaDetail;
    QProgressBar *m_otaProgress;
    QTimer *m_timer;
    QElapsedTimer m_uptime;
    QElapsedTimer m_idleSince;
    bool m_bizOk;
    bool m_otaWasBusy;
    bool m_manualMode;
    bool m_screenBlanked;
    int m_blankMinutes;
    QString m_preferUplink;
    int m_webLinkMode;
    int m_watchdogEnable;
};

#endif
