#ifndef SECTION_VIEW_H
#define SECTION_VIEW_H

#include <QWidget>
#include <QVector>
#include <QPointF>

class QPainter;

/* 首页左侧：按断面测点画河床，水位画水面，小车画垂线 */
class SectionView : public QWidget
{
    Q_OBJECT
public:
    explicit SectionView(QWidget *parent = 0);

    void setLiveData(double waterElevM, bool waterValid,
                     double carXM, bool carValid,
                     double fishDepthM);

protected:
    void paintEvent(QPaintEvent *event);
    void showEvent(QShowEvent *event);

private:
    struct BedPt {
        double x;
        double z;
    };

    void reloadGeometryIfNeeded();
    void rebuildDrawBed();
    void appendBankCurve(QVector<BedPt> *out, const BedPt &a, const BedPt &b,
                         int samples) const;
    void mapWorldToPixel(double x, double z, double xMin, double xMax,
                         double zMin, double zMax, const QRect &plot,
                         QPointF *out) const;
    void drawAxes(QPainter &p, const QRect &plot,
                  double xMin, double xMax, double zMin, double zMax) const;

    QVector<BedPt> m_bed;      /* 实测断面点 */
    QVector<BedPt> m_drawBed;  /* 绘图折线（两端岸坡加了弧） */
    bool m_geomReady;
    int m_geomFingerprint;
    double m_waterElev;
    bool m_waterValid;
    double m_carX;
    bool m_carValid;
    double m_fishDepthM;
};

#endif
