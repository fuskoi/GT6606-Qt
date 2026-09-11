#ifndef DEPTH_PROFILE_VIEW_H
#define DEPTH_PROFILE_VIEW_H

#include <QWidget>
#include <QVector>

/* 首页左侧：垂线深度示意（水面/探头位置/保持深度/测点），非真实河床断面。 */
class DepthProfileView : public QWidget
{
    Q_OBJECT
public:
    explicit DepthProfileView(QWidget *parent = 0);

    /* depthM/keepM 单位米；sediment 用于水色深浅；targets 为当前模式测点深度(m)。
     * maxHintM：配置的最大水深，用作纵轴下限（实际点更深时仍会放大）。 */
    void setLiveData(double depthM, bool depthOk,
                     double keepM, bool keepOk,
                     double sediment,
                     const QVector<double> &targets,
                     double maxHintM = 0.0);

protected:
    void paintEvent(QPaintEvent *event);

private:
    double m_depth;
    bool m_depthOk;
    double m_keep;
    bool m_keepOk;
    double m_sediment;
    double m_maxHint;
    QVector<double> m_targets;
};

#endif
