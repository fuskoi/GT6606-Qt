#include "depth_profile_view.h"

#include <QPainter>
#include <QPaintEvent>
#include <QtGlobal>
#include <cmath>

/* 构造垂线深度示意控件。 */
DepthProfileView::DepthProfileView(QWidget *parent)
    : QWidget(parent)
    , m_depth(0.0)
    , m_depthOk(false)
    , m_keep(0.0)
    , m_keepOk(false)
    , m_sediment(0.0)
    , m_maxHint(0.0)
{
    setObjectName(QStringLiteral("depthProfile"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

/* 更新深度/保持深度/浓度与测点，并重绘。 */
void DepthProfileView::setLiveData(double depthM, bool depthOk,
                                   double keepM, bool keepOk,
                                   double sediment,
                                   const QVector<double> &targets,
                                   double maxHintM)
{
    m_depth = depthM;
    m_depthOk = depthOk;
    m_keep = keepM;
    m_keepOk = keepOk;
    m_sediment = sediment;
    m_targets = targets;
    m_maxHint = maxHintM;
    update();
}

/* 绘制垂线柱：水面、水色(随浓度)、探头、保持线、测点与刻度。 */
void DepthProfileView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect rc = rect();
    p.fillRect(rc, QColor(0xe8, 0xee, 0xf4));

    p.setPen(QColor(0x0f, 0x2a, 0x44));
    QFont title = font();
    title.setPixelSize(16);
    title.setBold(true);
    p.setFont(title);
    p.drawText(QRect(rc.left() + 12, rc.top() + 4, rc.width() - 24, 22),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("垂线深度示意"));

    QRect plot = rc.adjusted(56, 30, -14, -36);
    if (plot.width() < 40 || plot.height() < 60)
        return;

    /* 纵轴范围：0 ~ maxDepth（向下为正深度） */
    double maxD = 1.0;
    if (m_maxHint > maxD)
        maxD = m_maxHint;
    if (m_depthOk && m_depth > maxD)
        maxD = m_depth;
    if (m_keepOk && m_keep > maxD)
        maxD = m_keep;
    for (int i = 0; i < m_targets.size(); ++i) {
        if (m_targets.at(i) > maxD)
            maxD = m_targets.at(i);
    }
    maxD = qMax(1.0, maxD * 1.05);

    p.setPen(QPen(QColor(0xc9, 0xd4, 0xdf), 2));
    p.setBrush(QColor(0xff, 0xff, 0xff));
    p.drawRoundedRect(plot.adjusted(-2, -2, 2, 2), 8, 8);

    /* 水面以上留空白；水面以下整段水色（浓度越高越浑） */
    const int airH = qMax(18, plot.height() / 8);
    const int bedH = 10;
    const int waterTop = plot.top() + airH;
    const int waterBottom = plot.bottom() - bedH;
    const int waterH = qMax(20, waterBottom - waterTop);

    /* 水色：低浓度拉开（现场常见约 0.01~5），高浓度再趋近浑浊上限 */
    const double sedRef = 5.0; /* kg/m3，约此值接近最浑 */
    const double sedNorm = qBound(0.0, std::sqrt(qMax(0.0, m_sediment) / sedRef), 1.0);
    /* 清澈蓝 → 浑浊黄褐 */
    const int wr = (int)(70 + sedNorm * 150);   /* 70 → 220 */
    const int wg = (int)(170 - sedNorm * 90);   /* 170 → 80 */
    const int wb = (int)(230 - sedNorm * 190);  /* 230 → 40 */
    const int wa = (int)(160 + sedNorm * 70);   /* 160 → 230 */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(wr, wg, wb, wa));
    p.drawRect(QRect(plot.left(), waterTop, plot.width(), waterH));

    /* 水面线 */
    p.setPen(QPen(QColor(0x1e, 0x6b, 0xb0), 2));
    p.drawLine(QPoint(plot.left() + 2, waterTop), QPoint(plot.right() - 2, waterTop));
    {
        QFont f = font();
        f.setPixelSize(11);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(0x1e, 0x6b, 0xb0));
        p.drawText(QRect(plot.left() + 6, waterTop - 16, 80, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("水面"));
    }

    /* 河床底色 */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xc4, 0xa4, 0x84));
    p.drawRect(QRect(plot.left(), waterBottom, plot.width(), bedH));

    /* Y 轴刻度：0 在水面，向下为正深度 */
    p.setPen(QPen(QColor(0x33, 0x41, 0x55), 2));
    p.drawLine(QPoint(plot.left(), waterTop), QPoint(plot.left(), plot.bottom()));
    QFont tick = font();
    tick.setPixelSize(12);
    p.setFont(tick);
    const int nTick = 5;
    for (int i = 0; i <= nTick; ++i) {
        const double d = maxD * i / nTick;
        const int y = waterTop + (int)(waterH * (d / maxD));
        p.drawLine(QPoint(plot.left() - 4, y), QPoint(plot.left(), y));
        p.drawText(QRect(plot.left() - 52, y - 8, 46, 16),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(d, 'f', 2));
    }

    QFont unit = font();
    unit.setPixelSize(12);
    unit.setBold(true);
    p.setFont(unit);
    p.setPen(QColor(0x10, 0x20, 0x33));
    p.save();
    p.translate(10, (waterTop + waterBottom) / 2);
    p.rotate(-90);
    p.drawText(QRect(-50, -8, 100, 16), Qt::AlignCenter, QStringLiteral("深度 (m)"));
    p.restore();

    auto yOf = [&](double d) -> int {
        return waterTop + (int)(waterH * qBound(0.0, d / maxD, 1.0));
    };

    /* 保持深度虚线 */
    if (m_keepOk && m_keep > 0.01) {
        const int y = yOf(m_keep);
        p.setPen(QPen(QColor(0xb4, 0x23, 0x18), 2, Qt::DashLine));
        p.drawLine(QPoint(plot.left() + 4, y), QPoint(plot.right() - 4, y));
        p.setPen(QColor(0xb4, 0x23, 0x18));
        QFont f = font();
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(plot.left() + 6, y - 18, plot.width() - 12, 16),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("保持 %1 m").arg(m_keep, 0, 'f', 2));
    }

    /* 模式测点 */
    for (int i = 0; i < m_targets.size(); ++i) {
        const double d = m_targets.at(i);
        if (d <= 0.01)
            continue;
        const int y = yOf(d);
        p.setPen(QPen(QColor(0x0b, 0x7a, 0x7a), 1, Qt::DotLine));
        p.drawLine(QPoint(plot.left() + 4, y), QPoint(plot.right() - 4, y));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x0b, 0x7a, 0x7a));
        p.drawEllipse(QPoint(plot.center().x() + 28, y), 4, 4);
    }

    /* 探头当前位置 */
    if (m_depthOk) {
        const int y = yOf(qMax(0.0, m_depth));
        /* 缆绳从水面拉到探头 */
        p.setPen(QPen(QColor(0x4a, 0x55, 0x68), 2));
        p.drawLine(QPoint(plot.center().x(), waterTop), QPoint(plot.center().x(), y));
        /* 探头 */
        p.setPen(QPen(QColor(0x1e, 0x4e, 0x8c), 2));
        p.setBrush(QColor(0x2b, 0x6c, 0xb0));
        p.drawEllipse(QPoint(plot.center().x(), y), 9, 9);
        p.setPen(QColor(0x0f, 0x2a, 0x44));
        QFont f = font();
        f.setPixelSize(13);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(plot.left() + 6, y + 6, plot.width() - 12, 18),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QStringLiteral("探头 %1 m").arg(m_depth, 0, 'f', 3));
    }

    /* 角标浓度（放在水面以上空白区） */
    QFont meta = font();
    meta.setPixelSize(13);
    meta.setBold(true);
    p.setFont(meta);
    p.setPen(QColor(0x24, 0x34, 0x47));
    p.drawText(QRect(plot.left() + 6, plot.top() + 4, plot.width() - 12, airH - 6),
               Qt::AlignTop | Qt::AlignRight,
               QStringLiteral("浓度 %1").arg(m_sediment, 0, 'f', 3));
}
