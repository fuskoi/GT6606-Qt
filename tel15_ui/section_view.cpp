#include "section_view.h"

#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QtMath>

extern "C" {
#include "WebSeriver.h"
#include "CRC.h"
}

namespace {

double scaledToMeter(int32_t v)
{
    return static_cast<double>(v) / 100.0;
}

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

int32_t maybeSwapI32(int32_t v, bool swapBytes)
{
    return swapBytes ? swap_i32(v) : v;
}

} // namespace

SectionView::SectionView(QWidget *parent)
    : QWidget(parent)
    , m_geomReady(false)
    , m_geomFingerprint(0)
    , m_waterElev(0.0)
    , m_waterValid(false)
    , m_carX(0.0)
    , m_carValid(false)
    , m_fishDepthM(0.0)
{
    setObjectName(QStringLiteral("sectionView"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void SectionView::setLiveData(double waterElevM, bool waterValid,
                              double carXM, bool carValid,
                              double fishDepthM)
{
    m_waterElev = waterElevM;
    m_waterValid = waterValid;
    m_carX = carXM;
    m_carValid = carValid;
    m_fishDepthM = fishDepthM;
    reloadGeometryIfNeeded();
    update();
}

void SectionView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    reloadGeometryIfNeeded();
    update();
}

void SectionView::reloadGeometryIfNeeded()
{
    const uint16_t rawNum = g_stSection_new.u16SectionNum;
    int n = static_cast<int>(rawNum);
    bool swapBytes = false;
    if (n < 2 || n > MAX_LINE_NUM) {
        const int swapped = static_cast<int>(swap_u16(rawNum));
        if (swapped >= 2 && swapped <= MAX_LINE_NUM) {
            n = swapped;
            swapBytes = true;
        } else {
            n = 0;
        }
    }

    int fp = n * 1315423911;
    fp ^= swapBytes ? 0x5a5a : 0;
    if (n >= 2) {
        fp ^= static_cast<int>(g_stSection_new.u16SectionSeri) << 4;
        for (int i = 0; i < n; ++i) {
            fp = (fp * 33)
                    ^ maybeSwapI32(g_stSection_new.stSection[i].u32StartDestence, swapBytes)
                    ^ (maybeSwapI32(g_stSection_new.stSection[i].u32RiverBedElevation, swapBytes) << 1);
        }
    }

    if (fp == m_geomFingerprint && (m_geomReady || n < 2))
        return;

    m_bed.clear();
    m_drawBed.clear();
    m_geomReady = false;
    m_geomFingerprint = fp;

    if (n < 2)
        return;

    QVector<BedPt> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i) {
        BedPt p;
        p.x = scaledToMeter(maybeSwapI32(g_stSection_new.stSection[i].u32StartDestence, swapBytes));
        p.z = scaledToMeter(maybeSwapI32(g_stSection_new.stSection[i].u32RiverBedElevation, swapBytes));
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
    rebuildDrawBed();
    m_geomReady = !m_drawBed.isEmpty() && m_drawBed.size() >= 2;
}

/* 两端岸坡两点直线太硬，用凹向主槽的三次贝塞尔；中间测点仍按折线 */
void SectionView::appendBankCurve(QVector<BedPt> *out, const BedPt &a, const BedPt &b,
                                  int samples) const
{
    if (!out)
        return;
    if (samples < 4)
        samples = 4;

    const double dx = b.x - a.x;
    const double dz = b.z - a.z;
    const double len = qSqrt(dx * dx + dz * dz);
    if (len < 1e-6) {
        if (out->isEmpty())
            out->append(a);
        return;
    }

    /* 弦长中部下压，岸坡略凹，看起来不像刀切直线 */
    const double bulge = qMin(0.28 * qAbs(dz) + 0.05 * qAbs(dx), 0.30 * len);
    BedPt c1;
    BedPt c2;
    c1.x = a.x + dx / 3.0;
    c1.z = a.z + dz / 3.0 - bulge;
    c2.x = a.x + 2.0 * dx / 3.0;
    c2.z = a.z + 2.0 * dz / 3.0 - bulge;

    for (int i = 0; i <= samples; ++i) {
        if (i == 0 && !out->isEmpty())
            continue;
        const double t = static_cast<double>(i) / samples;
        const double u = 1.0 - t;
        BedPt p;
        p.x = u * u * u * a.x
                + 3.0 * u * u * t * c1.x
                + 3.0 * u * t * t * c2.x
                + t * t * t * b.x;
        p.z = u * u * u * a.z
                + 3.0 * u * u * t * c1.z
                + 3.0 * u * t * t * c2.z
                + t * t * t * b.z;
        out->append(p);
    }
}

void SectionView::rebuildDrawBed()
{
    m_drawBed.clear();
    const int n = m_bed.size();
    if (n < 2)
        return;

    if (n == 2) {
        appendBankCurve(&m_drawBed, m_bed.at(0), m_bed.at(1), 16);
        return;
    }

    /* 左岸：第 1→2 点 */
    appendBankCurve(&m_drawBed, m_bed.at(0), m_bed.at(1), 16);

    if (n == 3) {
        /* 只有 3 点时两段都是岸坡 */
        appendBankCurve(&m_drawBed, m_bed.at(1), m_bed.at(2), 16);
        return;
    }

    /* 中间主槽：实测点折线 */
    for (int i = 2; i <= n - 2; ++i)
        m_drawBed.append(m_bed.at(i));

    /* 右岸：倒数第 2→1 点 */
    appendBankCurve(&m_drawBed, m_bed.at(n - 2), m_bed.at(n - 1), 16);
}

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

void SectionView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect rc = rect();
    p.fillRect(rc, QColor(0xe8, 0xee, 0xf2));

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
                   QStringLiteral("断面未就绪\n请由网页下发断面参数"));
        return;
    }

    double xMin = m_drawBed.first().x;
    double xMax = m_drawBed.last().x;
    double zMin = m_drawBed.first().z;
    double zMax = m_drawBed.first().z;
    for (int i = 0; i < m_drawBed.size(); ++i) {
        zMin = qMin(zMin, m_drawBed.at(i).z);
        zMax = qMax(zMax, m_drawBed.at(i).z);
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

    const bool carInView = m_carValid && m_carX >= xMin && m_carX <= xMax;

    p.setPen(QPen(QColor(0xc9, 0xd4, 0xdf), 2));
    p.setBrush(QColor(0xff, 0xff, 0xff));
    p.drawRoundedRect(plot.adjusted(-2, -2, 2, 2), 6, 6);

    {
        QPolygonF bedPoly;
        for (int i = 0; i < m_drawBed.size(); ++i) {
            QPointF pt;
            mapWorldToPixel(m_drawBed.at(i).x, m_drawBed.at(i).z, xMin, xMax, zMin, zMax, plot, &pt);
            bedPoly << pt;
        }
        QPointF br;
        QPointF bl;
        mapWorldToPixel(m_drawBed.last().x, zMin, xMin, xMax, zMin, zMax, plot, &br);
        mapWorldToPixel(m_drawBed.first().x, zMin, xMin, xMax, zMin, zMax, plot, &bl);
        bedPoly << br << bl;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xc4, 0xa4, 0x84));
        p.drawPolygon(bedPoly);
    }

    if (m_waterValid) {
        bool anyUnder = false;
        for (int i = 0; i < m_drawBed.size(); ++i) {
            if (m_drawBed.at(i).z <= m_waterElev) {
                anyUnder = true;
                break;
            }
        }
        if (anyUnder) {
            double leftX = m_drawBed.first().x;
            double rightX = m_drawBed.last().x;
            bool haveEdge = false;
            for (int i = 0; i < m_drawBed.size() - 1; ++i) {
                double xi = 0.0;
                if (intersectHoriz(m_drawBed.at(i).x, m_drawBed.at(i).z,
                                   m_drawBed.at(i + 1).x, m_drawBed.at(i + 1).z,
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
                leftX = m_drawBed.first().x;
                rightX = m_drawBed.last().x;
            }

            QPolygonF waterPoly;
            QPointF pt;
            mapWorldToPixel(leftX, m_waterElev, xMin, xMax, zMin, zMax, plot, &pt);
            waterPoly << pt;

            for (int i = 0; i < m_drawBed.size(); ++i) {
                const BedPt &bp = m_drawBed.at(i);
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

    {
        QPolygonF bedLine;
        for (int i = 0; i < m_drawBed.size(); ++i) {
            QPointF pt;
            mapWorldToPixel(m_drawBed.at(i).x, m_drawBed.at(i).z, xMin, xMax, zMin, zMax, plot, &pt);
            bedLine << pt;
        }
        p.setPen(QPen(QColor(0x6b, 0x4f, 0x3a), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(bedLine);
    }

    /* 圆点仍标实测点，不标岸坡插值点 */
    for (int i = 0; i < m_bed.size(); ++i) {
        QPointF pt;
        mapWorldToPixel(m_bed.at(i).x, m_bed.at(i).z, xMin, xMax, zMin, zMax, plot, &pt);
        p.setPen(QPen(QColor(0x0b, 0x7a, 0x7a), 2));
        p.setBrush(QColor(0xff, 0xff, 0xff));
        p.drawEllipse(pt, 4, 4);
    }

    if (carInView) {
        double bedZ = m_drawBed.first().z;
        if (m_carX <= m_drawBed.first().x) {
            bedZ = m_drawBed.first().z;
        } else if (m_carX >= m_drawBed.last().x) {
            bedZ = m_drawBed.last().z;
        } else {
            for (int i = 0; i < m_drawBed.size() - 1; ++i) {
                const double x0 = m_drawBed.at(i).x;
                const double x1 = m_drawBed.at(i + 1).x;
                if (m_carX + 1e-9 < x0 || m_carX - 1e-9 > x1)
                    continue;
                const double dx = x1 - x0;
                const double t = (qAbs(dx) < 1e-9) ? 0.0 : (m_carX - x0) / dx;
                bedZ = m_drawBed.at(i).z + t * (m_drawBed.at(i + 1).z - m_drawBed.at(i).z);
                break;
            }
        }

        QPointF topPt;
        QPointF bedPt;
        mapWorldToPixel(m_carX, zMax, xMin, xMax, zMin, zMax, plot, &topPt);
        mapWorldToPixel(m_carX, bedZ, xMin, xMax, zMin, zMax, plot, &bedPt);
        QPen carPen(QColor(0xb4, 0x23, 0x18), 2, Qt::DashLine);
        p.setPen(carPen);
        p.drawLine(topPt, bedPt);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xb4, 0x23, 0x18));
        p.drawEllipse(topPt, 5, 5);

        if (m_waterValid && m_fishDepthM > 0.02) {
            const double fishZ = m_waterElev - m_fishDepthM;
            if (fishZ <= m_waterElev && fishZ >= bedZ - 0.05) {
                QPointF fishPt;
                mapWorldToPixel(m_carX, fishZ, xMin, xMax, zMin, zMax, plot, &fishPt);
                p.setPen(QPen(QColor(0x0b, 0x3a, 0x5b), 2));
                p.setBrush(QColor(0xf5, 0xc5, 0x42));
                p.drawEllipse(fishPt, 6, 6);
            }
        }

        QFont tag = font();
        tag.setPixelSize(13);
        tag.setBold(true);
        p.setFont(tag);
        p.setPen(QColor(0xb4, 0x23, 0x18));
        p.drawText(QRectF(topPt.x() + 6, topPt.y() + 2, 72, 18),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("小车"));
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
