#include "section_view.h"

#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QtMath>
#include <cmath>

extern "C" {
#include "SectionParam.h"
}

namespace {

/* 断面内部整型（×100）转为米。 */
double scaledToMeter(int32_t v)
{
    return static_cast<double>(v) / 100.0;
}

/* 判断线段 (x0,z0)-(x1,z1) 是否与水平水位 zWater 相交，交点 x 写入 xOut。 */
bool intersectHoriz(double x0, double z0, double x1, double z1, double zWater,
                    double *xOut)
{
    const double dz = z1 - z0;
    if (qAbs(dz) < 1e-9)
        return false;
    if ((z0 - zWater) * (z1 - zWater) > 0.0)
        return false;
    const double t = (zWater - z0) / dz;
    if (t < -1e-9 || t > 1.0 + 1e-9)
        return false;
    *xOut = x0 + t * (x1 - x0);
    return true;
}

/* 计算美观刻度步长（约 targetTicks 格）。 */
double niceStep(double span, int targetTicks)
{
    if (span <= 1e-9)
        return 1.0;
    if (targetTicks < 2)
        targetTicks = 2;
    const double raw = span / targetTicks;
    const double expv = qFloor(qLn(raw) / qLn(10.0));
    const double base = qPow(10.0, expv);
    const double frac = raw / base;
    double niceFrac = 1.0;
    if (frac <= 1.5)
        niceFrac = 1.0;
    else if (frac <= 3.0)
        niceFrac = 2.0;
    else if (frac <= 7.0)
        niceFrac = 5.0;
    else
        niceFrac = 10.0;
    return niceFrac * base;
}

} // namespace

/* 构造：初始化几何/水位状态。 */
SectionView::SectionView(QWidget *parent)
    : QWidget(parent)
    , m_geomReady(false)
    , m_geomFingerprint(0)
    , m_waterElev(0.0)
    , m_waterValid(false)
    , m_meterX(qQNaN())
{
    setObjectName(QStringLiteral("sectionView"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

/* 更新实时水位，必要时重载断面几何并请求重绘。 */
void SectionView::setLiveData(double waterElevM, bool waterValid)
{
    const bool changed = (m_waterValid != waterValid)
            || (waterValid && qAbs(m_waterElev - waterElevM) > 0.005);
    m_waterElev = waterElevM;
    m_waterValid = waterValid;
    reloadGeometryIfNeeded();
    if (changed || !m_geomReady)
        update();
    else if (m_geomReady)
        update(); /* 几何指纹变化时 reload 已改状态，仍需重绘 */
}

/* 控件显示时重新检查断面是否已下发。 */
void SectionView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    reloadGeometryIfNeeded();
    update();
}

/* 从 SectionParam 读取运行断面测线，排序去重后缓存为河床折线。 */
void SectionView::reloadGeometryIfNeeded()
{
    const int ready = SectionParam_IsRunSectionReady();
    const RunSectionData *run = SectionParam_GetRunSection();
    const int n = (ready && run) ? static_cast<int>(run->i16SpeedLineCount) : 0;

    int fp = n * 1315423911;
    if (ready && run && n > 0) {
        const int last = (n < MAX_SPEED_LINE_COUNT) ? (n - 1) : (MAX_SPEED_LINE_COUNT - 1);
        fp ^= run->astLines[0].i32StartDistance;
        fp ^= run->astLines[0].i32RiverbedElevation << 1;
        fp ^= run->astLines[last].i32StartDistance << 2;
        fp ^= run->astLines[last].i32RiverbedElevation << 3;
    }
    if (SectionParam_IsCrossSectionReady()) {
        const CrossSectionData *cs = SectionParam_GetCrossSection();
        if (cs)
            fp ^= cs->i32MeterHorizontalPos << 4;
    }

    if (fp == m_geomFingerprint && (m_geomReady || n < 2))
        return;

    m_bed.clear();
    m_geomReady = false;
    m_geomFingerprint = fp;
    m_meterX = qQNaN();

    if (!ready || !run || n < 2)
        return;

    QVector<BedPt> pts;
    pts.reserve(n);
    for (int i = 0; i < n && i < MAX_SPEED_LINE_COUNT; ++i) {
        BedPt p;
        p.x = scaledToMeter(run->astLines[i].i32StartDistance);
        p.z = scaledToMeter(run->astLines[i].i32RiverbedElevation);
        pts.append(p);
    }

    for (int i = 1; i < pts.size(); ++i) {
        const BedPt t = pts.at(i);
        int k = i;
        while (k > 0 && pts.at(k - 1).x > t.x) {
            pts[k] = pts.at(k - 1);
            --k;
        }
        pts[k] = t;
    }

    QVector<BedPt> uniq;
    for (int i = 0; i < pts.size(); ++i) {
        if (!uniq.isEmpty() && qAbs(uniq.last().x - pts.at(i).x) < 1e-6)
            continue;
        uniq.append(pts.at(i));
    }
    if (uniq.size() < 2)
        return;

    m_bed = uniq;
    m_geomReady = true;

    if (SectionParam_IsCrossSectionReady()) {
        const CrossSectionData *cs = SectionParam_GetCrossSection();
        if (cs)
            m_meterX = scaledToMeter(cs->i32MeterHorizontalPos);
    }
}

/* 世界坐标(x起点距,z高程) → 绘图区像素；高程向上映射为屏幕 Y 向下。 */
void SectionView::mapWorldToPixel(double x, double z, double xMin, double xMax,
                                  double zMin, double zMax, const QRect &plot,
                                  QPointF *out) const
{
    const double dx = (xMax > xMin) ? (xMax - xMin) : 1.0;
    const double dz = (zMax > zMin) ? (zMax - zMin) : 1.0;
    const double px = plot.left() + (x - xMin) / dx * plot.width();
    const double py = plot.bottom() - (z - zMin) / dz * plot.height();
    *out = QPointF(px, py);
}

/* 绘制 X（起点距 m）/ Y（高程 m）坐标轴、刻度与单位。 */
void SectionView::drawAxes(QPainter &p, const QRect &plot,
                           double xMin, double xMax, double zMin, double zMax) const
{
    p.setPen(QPen(QColor(0x33, 0x41, 0x55), 2));
    p.drawLine(plot.bottomLeft(), plot.topLeft());
    p.drawLine(plot.bottomLeft(), plot.bottomRight());

    QFont tickFont = font();
    tickFont.setPixelSize(11);
    tickFont.setBold(false);
    p.setFont(tickFont);
    p.setPen(QColor(0x33, 0x41, 0x55));

    const double xStep = niceStep(xMax - xMin, 4);
    const double zStep = niceStep(zMax - zMin, 4);
    const double x0 = qCeil(xMin / xStep) * xStep;
    const double z0 = qCeil(zMin / zStep) * zStep;

    for (double x = x0; x <= xMax + xStep * 0.01; x += xStep) {
        QPointF pt;
        mapWorldToPixel(x, zMin, xMin, xMax, zMin, zMax, plot, &pt);
        p.drawLine(QPointF(pt.x(), plot.bottom()), QPointF(pt.x(), plot.bottom() + 4));
        const QString lab = QString::number(x, 'f', (xStep < 1.0) ? 2 : ((xStep < 10.0) ? 1 : 0));
        p.drawText(QRectF(pt.x() - 28, plot.bottom() + 4, 56, 16),
                   Qt::AlignHCenter | Qt::AlignTop, lab);
    }

    for (double z = z0; z <= zMax + zStep * 0.01; z += zStep) {
        QPointF pt;
        mapWorldToPixel(xMin, z, xMin, xMax, zMin, zMax, plot, &pt);
        p.drawLine(QPointF(plot.left() - 4, pt.y()), QPointF(plot.left(), pt.y()));
        const QString lab = QString::number(z, 'f', (zStep < 1.0) ? 2 : ((zStep < 10.0) ? 1 : 0));
        p.drawText(QRectF(plot.left() - 54, pt.y() - 8, 48, 16),
                   Qt::AlignRight | Qt::AlignVCenter, lab);
    }

    QFont unitFont = font();
    unitFont.setPixelSize(12);
    unitFont.setBold(true);
    p.setFont(unitFont);
    p.setPen(QColor(0x10, 0x20, 0x33));
    p.drawText(QRect(plot.left(), plot.bottom() + 18, plot.width(), 16),
               Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("起点距 (m)"));
    p.save();
    p.translate(8, plot.center().y());
    p.rotate(-90);
    p.drawText(QRect(-60, -8, 120, 16), Qt::AlignCenter, QStringLiteral("高程 (m)"));
    p.restore();
}

/* 绘制断面：坐标轴、河床、水面、测点圆圈与水位角标。 */
void SectionView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect rc = rect();
    p.fillRect(rc, QColor(0xe8, 0xee, 0xf2));

    /* 左/下留白给坐标轴刻度与单位 */
    QRect plot = rc.adjusted(56, 28, -12, -40);
    if (plot.width() < 40 || plot.height() < 40)
        return;

    p.setPen(QColor(0x10, 0x20, 0x33));
    QFont titleFont = font();
    titleFont.setPixelSize(16);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.drawText(QRect(rc.left() + 56, rc.top() + 4, rc.width() - 68, 22),
               Qt::AlignLeft | Qt::AlignVCenter,
               m_geomReady ? QStringLiteral("过水断面")
                           : QStringLiteral("过水断面（等待下发）"));

    if (!m_geomReady) {
        p.setPen(QColor(0x3b, 0x4d, 0x61));
        QFont hint = font();
        hint.setPixelSize(18);
        p.setFont(hint);
        p.drawText(plot, Qt::AlignCenter,
                   QStringLiteral("断面未就绪\n请由网页下发运行断面参数"));
        return;
    }

    double xMin = m_bed.first().x;
    double xMax = m_bed.last().x;
    double zMin = m_bed.first().z;
    double zMax = m_bed.first().z;
    for (int i = 0; i < m_bed.size(); ++i) {
        zMin = qMin(zMin, m_bed.at(i).z);
        zMax = qMax(zMax, m_bed.at(i).z);
    }
    if (m_waterValid) {
        zMin = qMin(zMin, m_waterElev);
        zMax = qMax(zMax, m_waterElev);
    }
    const double xPad = qMax(0.5, (xMax - xMin) * 0.04);
    const double zPad = qMax(0.2, (zMax - zMin) * 0.12);
    xMin -= xPad;
    xMax += xPad;
    zMin -= zPad;
    zMax += zPad;

    p.setPen(QPen(QColor(0xc9, 0xd4, 0xdf), 2));
    p.setBrush(QColor(0xff, 0xff, 0xff));
    p.drawRoundedRect(plot.adjusted(-2, -2, 2, 2), 6, 6);

    /* 河床土体 */
    {
        QPolygonF bedPoly;
        for (int i = 0; i < m_bed.size(); ++i) {
            QPointF pt;
            mapWorldToPixel(m_bed.at(i).x, m_bed.at(i).z, xMin, xMax, zMin, zMax, plot, &pt);
            bedPoly << pt;
        }
        QPointF br, bl;
        mapWorldToPixel(m_bed.last().x, zMin, xMin, xMax, zMin, zMax, plot, &br);
        mapWorldToPixel(m_bed.first().x, zMin, xMin, xMax, zMin, zMax, plot, &bl);
        bedPoly << br << bl;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xc4, 0xa4, 0x84));
        p.drawPolygon(bedPoly);
    }

    /* 水面：河床与水位的交点之间，沿河床闭合（无波动） */
    if (m_waterValid) {
        bool anyUnder = false;
        for (int i = 0; i < m_bed.size(); ++i) {
            if (m_bed.at(i).z <= m_waterElev) {
                anyUnder = true;
                break;
            }
        }
        if (anyUnder) {
            double leftX = m_bed.first().x;
            double rightX = m_bed.last().x;
            bool haveEdge = false;
            for (int i = 0; i < m_bed.size() - 1; ++i) {
                double xi = 0.0;
                if (intersectHoriz(m_bed.at(i).x, m_bed.at(i).z,
                                   m_bed.at(i + 1).x, m_bed.at(i + 1).z,
                                   m_waterElev, &xi)) {
                    if (!haveEdge) {
                        leftX = xi;
                        rightX = xi;
                        haveEdge = true;
                    } else {
                        leftX = qMin(leftX, xi);
                        rightX = qMax(rightX, xi);
                    }
                }
            }
            if (!haveEdge) {
                leftX = m_bed.first().x;
                rightX = m_bed.last().x;
            }

            QPolygonF waterPoly;
            QPointF pt;
            mapWorldToPixel(leftX, m_waterElev, xMin, xMax, zMin, zMax, plot, &pt);
            waterPoly << pt;

            for (int i = 0; i < m_bed.size(); ++i) {
                const BedPt &bp = m_bed.at(i);
                if (bp.x + 1e-6 < leftX || bp.x - 1e-6 > rightX)
                    continue;
                if (bp.z > m_waterElev + 1e-6)
                    continue;
                mapWorldToPixel(bp.x, bp.z, xMin, xMax, zMin, zMax, plot, &pt);
                waterPoly << pt;
            }

            mapWorldToPixel(rightX, m_waterElev, xMin, xMax, zMin, zMax, plot, &pt);
            waterPoly << pt;

            if (waterPoly.size() >= 3) {
                p.setBrush(QColor(0x3a, 0xa0, 0xd8, 200));
                p.setPen(Qt::NoPen);
                p.drawPolygon(waterPoly);
                p.setPen(QPen(QColor(0x1a, 0x6f, 0xa8), 2));
                p.drawLine(waterPoly.first(), waterPoly.last());
            }
        }
    }

    /* 河床线 */
    {
        QPolygonF bedLine;
        for (int i = 0; i < m_bed.size(); ++i) {
            QPointF pt;
            mapWorldToPixel(m_bed.at(i).x, m_bed.at(i).z, xMin, xMax, zMin, zMax, plot, &pt);
            bedLine << pt;
        }
        p.setPen(QPen(QColor(0x6b, 0x4f, 0x3a), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(bedLine);
    }

    /* 测点：交汇处画圆圈（无垂线） */
    for (int i = 0; i < m_bed.size(); ++i) {
        QPointF pt;
        mapWorldToPixel(m_bed.at(i).x, m_bed.at(i).z, xMin, xMax, zMin, zMax, plot, &pt);
        p.setPen(QPen(QColor(0x0b, 0x7a, 0x7a), 2));
        p.setBrush(QColor(0xff, 0xff, 0xff));
        p.drawEllipse(pt, 4, 4);
    }

    /* 流速仪水平位置：红点（不画贯通垂线） */
    if (!qIsNaN(m_meterX) && m_meterX >= xMin && m_meterX <= xMax) {
        QPointF pt;
        mapWorldToPixel(m_meterX, zMax, xMin, xMax, zMin, zMax, plot, &pt);
        p.setPen(QPen(QColor(0xb4, 0x23, 0x18), 2));
        p.setBrush(QColor(0xb4, 0x23, 0x18));
        p.drawEllipse(pt, 5, 5);
    }

    drawAxes(p, plot, xMin, xMax, zMin, zMax);

    QFont meta = font();
    meta.setPixelSize(14);
    meta.setBold(true);
    p.setFont(meta);
    p.setPen(QColor(0x24, 0x34, 0x47));
    const QString info = m_waterValid
            ? QStringLiteral("水位 %1 m").arg(m_waterElev, 0, 'f', 2)
            : QStringLiteral("水位 --");
    p.drawText(plot.adjusted(8, 4, -8, -4), Qt::AlignTop | Qt::AlignRight, info);
}
