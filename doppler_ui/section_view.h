#ifndef SECTION_VIEW_H
#define SECTION_VIEW_H

#include <QWidget>
#include <QVector>
#include <QPointF>

/* 首页左侧：按运行断面测线绘制河床，水位驱动水面 */
class SectionView : public QWidget
{
    Q_OBJECT
public:
    /* 构造断面视图控件。 */
    explicit SectionView(QWidget *parent = 0);

    /* 注入实时水位(m)；触发几何检查并重绘。 */
    void setLiveData(double waterElevM, bool waterValid);

protected:
    /* 绘制坐标轴、河床、水面与测点圆圈。 */
    void paintEvent(QPaintEvent *event);

    /* 显示时尝试加载断面几何并重绘。 */
    void showEvent(QShowEvent *event);

private:
    struct BedPt {
        double x; /* 起点距 m */
        double z; /* 河底高程 m */
    };

    /* 若运行断面数据指纹变化，则从 SectionParam 重建 m_bed。 */
    void reloadGeometryIfNeeded();

    /* 将世界坐标(起点距x, 高程z)映射到绘图区像素坐标。 */
    void mapWorldToPixel(double x, double z, double xMin, double xMax,
                         double zMin, double zMax, const QRect &plot,
                         QPointF *out) const;

    /* 在绘图区绘制带刻度与单位的 X/Y 轴。 */
    void drawAxes(QPainter &p, const QRect &plot,
                  double xMin, double xMax, double zMin, double zMax) const;

    QVector<BedPt> m_bed;
    bool m_geomReady;
    int m_geomFingerprint;
    double m_waterElev;
    bool m_waterValid;
    double m_meterX; /* 流速仪水平位置 m，无效为 NAN */
};

#endif
