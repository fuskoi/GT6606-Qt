#ifndef NISHA_MAINWINDOW_H
#define NISHA_MAINWINDOW_H

#include <QMainWindow>
#include <QElapsedTimer>

class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QStackedWidget;
class QTextEdit;
class QTimer;
class QWidget;
class QFrame;
class QEvent;
class QVBoxLayout;
class DepthProfileView;

/* 通辽泥沙升降 GT6606 主窗：首页监测 + 默认/VMM/VDIM/手动模式 + OTA 全屏页。 */
class NishaMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit NishaMainWindow(QWidget *parent = 0);
    ~NishaMainWindow();

public slots:
    void onStartBusiness();
    void onBusinessReady(int result);

private slots:
    void onRefresh();
    void onBackHome();
    void onShowDefaultPage();
    void onShowVmmPage();
    void onShowVdimPage();
    void onShowManualPage();
    void onShowLogPage();
    void onExit();

    void onDefaultStart();
    void onVmmStart();
    void onVdimStart();
    void onStopTask();

    void onManualUp();
    void onManualDown();
    void onManualStop();

    void onNumPadDigit();
    void onNumPadBackspace();
    void onNumPadClear();

protected:
    bool eventFilter(QObject *watched, QEvent *event);

private:
    void buildUi();
    void appendUiLog(const QString &line);
    void setBizEnabled(bool on);
    void applyCjkFont(QWidget *w);
    QString modeText(int mode) const;
    QString stateText(int state) const;
    void setActiveEdit(QLineEdit *edit);
    QLineEdit *makeDepthEdit(QWidget *parent);
    void addNumPad(QVBoxLayout *lay, QWidget *parent);

    void setOtaUiLocked(bool locked);
    void updateOtaUi();
    static QString otaStatusText(unsigned status);
    static QString otaErrText(unsigned err);

    QWidget *m_root;
    QStackedWidget *m_stack;
    QWidget *m_pageHome;
    QWidget *m_pageDefault;
    QWidget *m_pageVmm;
    QWidget *m_pageVdim;
    QWidget *m_pageManual;
    QWidget *m_pageLog;
    QWidget *m_pageOta;

    QLabel *m_lblClock;
    QLabel *m_lblUptime;
    QLabel *m_lblMode;
    QLabel *m_lblState;
    DepthProfileView *m_depthProfile;
    QLabel *m_valDepth;
    QLabel *m_valSediment;
    QLabel *m_valKeepDepth;
    QLabel *m_lblDevDepth;
    QLabel *m_lblDevSediment;
    QLabel *m_lblDevMotor;
    QLabel *m_lblDevRelay;

    QLineEdit *m_editDefaultDepth;
    QLineEdit *m_editVmm1;
    QLineEdit *m_editVmm2;
    QLineEdit *m_editVmm3;
    QLineEdit *m_editVmm4;
    QLineEdit *m_editVdimA;
    QLineEdit *m_editVdimB;
    QLineEdit *m_activeEdit;

    QPushButton *m_btnBackOta;
    QLabel *m_lblOtaState;
    QLabel *m_lblOtaPercent;
    QLabel *m_lblOtaWarn;
    QLabel *m_lblOtaStep1;
    QLabel *m_lblOtaStep2;
    QLabel *m_lblOtaStep3;
    QLabel *m_lblOtaDetail;
    QFrame *m_otaHero;
    QFrame *m_otaDetailCard;
    QProgressBar *m_otaProgress;

    QTextEdit *m_log;
    QTimer *m_timer;
    QElapsedTimer m_uptime;
    bool m_bizOk;
    bool m_otaWasBusy;
    bool m_otaUiLocked;
};

#endif
