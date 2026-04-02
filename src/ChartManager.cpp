#include "ChartManager.h"
#include <QDateTimeAxis>
#include <QValueAxis>
#include <QLineSeries>
#include <QAreaSeries>
#include <QLegendMarker>
#include <QPen>
#include <QTimeZone>
#include <QDateTime>
#include <QLinearGradient>
#include <limits>
#include <cmath>

ChartManager::ChartManager(QChart *chart, QChartView *chartView,
                           QComboBox *yScaleCombo, StockCacheManager *cache,
                           QObject *parent)
    : QObject(parent)
    , m_chart(chart)
    , m_chartView(chartView)
    , m_yScaleCombo(yScaleCombo)
    , m_cache(cache)
{
    // Background image shown when no chart data is active
    m_bgPixmap = QPixmap(":/bg_graph.jpg");
    if (!m_bgPixmap.isNull()) {
        m_bgImageItem = new QGraphicsPixmapItem(m_chart);
        m_bgImageItem->setZValue(0);
        m_bgImageItem->setVisible(true);
    }

    // Zero-line (dashed horizontal at 0%)
    m_zeroLine = new QGraphicsLineItem(m_chart);
    QPen zeroPen(Qt::darkGray, 1, Qt::DashLine);
    m_zeroLine->setPen(zeroPen);
}

// ── Chart rendering ───────────────────────────────────────────────────────────

void ChartManager::updateChart(const QStringList &selectedSymbols)
{
    static volatile bool isUpdating = false;
    if (isUpdating) return;
    isUpdating = true;

    m_isSingleStock = false;
    m_chart->removeAllSeries();
    for (QAbstractAxis *ax : m_chart->axes()) m_chart->removeAxis(ax);

    QStringList ready;
    for (const QString &sym : selectedSymbols)
        if (m_cache->cache().contains(sym) && !m_cache->cache()[sym].isEmpty()) ready << sym;

    if (ready.isEmpty()) {
        if (m_bgImageItem) { m_bgImageItem->setVisible(true); updateBgImage(); }
        updateCrosshair();
        isUpdating = false;
        return;
    }

    if (m_bgImageItem) m_bgImageItem->setVisible(false);

    auto *axisX = new QDateTimeAxis();
    axisX->setFormat("MMM dd");
    m_chart->addAxis(axisX, Qt::AlignBottom);

    auto *axisY = new QValueAxis();
    axisY->setTitleText("% Change");
    axisY->setLabelFormat("%.1f%%");
    m_chart->addAxis(axisY, Qt::AlignLeft);

    const QDateTime rangeStart = (m_chartRangeDays > 0)
        ? QDateTime(QDate::currentDate().addDays(-m_chartRangeDays), QTime(0, 0), QTimeZone::utc())
        : QDateTime();

    double    minPct = std::numeric_limits<double>::max();
    double    maxPct = std::numeric_limits<double>::lowest();
    QDateTime minTime, maxTime;

    const bool singleStock = (ready.size() == 1);

    for (const QString &sym : ready) {
        const auto &data = m_cache->cache()[sym];

        double basePrice = std::numeric_limits<double>::quiet_NaN();
        for (const StockDataPoint &pt : data) {
            if (!rangeStart.isValid() || pt.timestamp >= rangeStart) {
                basePrice = pt.price;
                break;
            }
        }
        if (std::isnan(basePrice) || basePrice == 0.0) continue;

        double singleMinPct   = std::numeric_limits<double>::max();
        double singleMaxPct   = std::numeric_limits<double>::lowest();
        double singleMinPrice = 0.0;
        double singleMaxPrice = 0.0;

        QVector<QPair<qint64, double>> pts;
        for (const StockDataPoint &pt : data) {
            if (rangeStart.isValid() && pt.timestamp < rangeStart) continue;
            double pct = (pt.price / basePrice - 1.0) * 100.0;
            pts.append({pt.timestamp.toMSecsSinceEpoch(), pct});
            minPct = std::min(minPct, pct);
            maxPct = std::max(maxPct, pct);
            if (singleStock) {
                if (pct < singleMinPct) { singleMinPct = pct; singleMinPrice = pt.price; }
                if (pct > singleMaxPct) { singleMaxPct = pct; singleMaxPrice = pt.price; }
            }
            if (minTime.isNull() || pt.timestamp < minTime) minTime = pt.timestamp;
            if (maxTime.isNull() || pt.timestamp > maxTime) maxTime = pt.timestamp;
        }
        if (pts.isEmpty()) continue;

        if (singleStock) {
            // ── Build augmented point list with interpolated zero crossings ──
            // Inserting exact crossing points ensures the fill polygons meet cleanly
            // at zero rather than having a diagonal wedge across the boundary.
            QVector<QPair<qint64, double>> augPts;
            augPts.reserve(pts.size() * 2);
            for (int i = 0; i < pts.size(); ++i) {
                if (i > 0) {
                    const auto &[prevMs, prevPct] = pts[i - 1];
                    const auto &[curMs,  curPct]  = pts[i];
                    if (prevPct != 0.0 && curPct != 0.0 && ((prevPct > 0) != (curPct > 0))) {
                        double t       = prevPct / (prevPct - curPct);
                        qint64 crossMs = prevMs + static_cast<qint64>(t * static_cast<double>(curMs - prevMs));
                        augPts.append({crossMs, 0.0});
                    }
                }
                augPts.append(pts[i]);
            }

            // ── Area fill series ──
            auto *upperPos    = new QLineSeries();
            auto *zeroLinePos = new QLineSeries();
            auto *lowerNeg    = new QLineSeries();
            auto *zeroLineNeg = new QLineSeries();
            for (const auto &[ms, pct] : augPts) {
                upperPos->append(ms,    qMax(0.0, pct));
                zeroLinePos->append(ms, 0.0);
                lowerNeg->append(ms,    qMin(0.0, pct));
                zeroLineNeg->append(ms, 0.0);
            }

            // Green gradient: 20% alpha at top (max value) → 10% alpha at bottom (zero)
            QLinearGradient greenGrad(0, 0, 0, 1);
            greenGrad.setCoordinateMode(QGradient::ObjectBoundingMode);
            greenGrad.setColorAt(0.0, QColor(56, 142, 60, 51));  // top = max value = 20%
            greenGrad.setColorAt(1.0, QColor(56, 142, 60, 26));  // bottom = zero line = 10%
            auto *greenArea = new QAreaSeries(upperPos, zeroLinePos);
            greenArea->setBrush(QBrush(greenGrad));
            greenArea->setPen(QPen(Qt::transparent));

            // Red gradient: 10% alpha at top (zero) → 20% alpha at bottom (min value)
            QLinearGradient redGrad(0, 0, 0, 1);
            redGrad.setCoordinateMode(QGradient::ObjectBoundingMode);
            redGrad.setColorAt(0.0, QColor(198, 40, 40, 26));    // top = zero line = 10%
            redGrad.setColorAt(1.0, QColor(198, 40, 40, 51));    // bottom = min value = 20%
            auto *redArea = new QAreaSeries(zeroLineNeg, lowerNeg);
            redArea->setBrush(QBrush(redGrad));
            redArea->setPen(QPen(Qt::transparent));

            m_chart->addSeries(greenArea);
            m_chart->addSeries(redArea);
            greenArea->attachAxis(axisX); greenArea->attachAxis(axisY);
            redArea->attachAxis(axisX);   redArea->attachAxis(axisY);
            for (QLegendMarker *mk : m_chart->legend()->markers(greenArea)) mk->setVisible(false);
            for (QLegendMarker *mk : m_chart->legend()->markers(redArea))   mk->setVisible(false);

            // ── Colored line segments: green where positive, red where negative ──
            // Each zero crossing (inserted in augPts) starts a new segment, shared
            // as the last point of the ending segment and first of the next.
            struct ColorSeg {
                bool positive = true;
                QVector<QPair<qint64, double>> points;
            };
            QVector<ColorSeg> colorSegs;
            {
                ColorSeg cur;
                bool hasSign = false;
                for (int i = 0; i < augPts.size(); ++i) {
                    auto [ms, pct] = augPts[i];
                    if (!hasSign) {
                        if (pct != 0.0) { cur.positive = (pct > 0); hasSign = true; }
                        cur.points.append({ms, pct});
                        continue;
                    }
                    // At an interpolated zero crossing: end current segment here,
                    // start new one from this point if the next point changes sign.
                    if (pct == 0.0 && i + 1 < augPts.size()) {
                        bool nextPos = (augPts[i + 1].second >= 0);
                        if (nextPos != cur.positive) {
                            cur.points.append({ms, pct});
                            colorSegs.append(cur);
                            cur = ColorSeg();
                            cur.positive = nextPos;
                            cur.points.append({ms, pct});
                            continue;
                        }
                    }
                    cur.points.append({ms, pct});
                }
                if (!cur.points.isEmpty()) colorSegs.append(cur);
            }

            for (int si = 0; si < colorSegs.size(); ++si) {
                const auto &seg = colorSegs[si];
                auto *segLine = new QLineSeries();
                QPen segPen(seg.positive ? QColor(56, 142, 60) : QColor(198, 40, 40));
                segPen.setWidthF(1.5);
                segLine->setPen(segPen);
                if (si == 0) segLine->setName(sym);  // first segment shows in legend
                for (auto [ms, p] : seg.points) segLine->append(ms, p);
                m_chart->addSeries(segLine);
                segLine->attachAxis(axisX);
                segLine->attachAxis(axisY);
                if (si > 0) {
                    for (QLegendMarker *mk : m_chart->legend()->markers(segLine))
                        mk->setVisible(false);
                }
            }

            m_isSingleStock = true;
            m_minPct        = singleMinPct;
            m_maxPct        = singleMaxPct;
            m_minPrice      = singleMinPrice;
            m_maxPrice      = singleMaxPrice;

        } else {
            auto *series = new QLineSeries();
            series->setName(sym);
            for (const auto &[msecs, pct] : pts)
                series->append(msecs, pct);
            m_chart->addSeries(series);
            series->attachAxis(axisX);
            series->attachAxis(axisY);
        }
    }

    if (minTime.isNull()) {
        updateCrosshair();
        isUpdating = false;
        return;
    }

    axisX->setRange(minTime, maxTime);

    axisY = qobject_cast<QValueAxis*>(m_chart->axes(Qt::Vertical).constFirst());
    if (axisY) {
        int scaleIdx = m_yScaleCombo->currentIndex();
        if (scaleIdx == 0) {
            double pad = std::max(0.5, (maxPct - minPct) * 0.08);
            axisY->setRange(minPct - pad, maxPct + pad);
        } else {
            double percent = 0.0;
            if      (scaleIdx == 1) percent = 30.0;
            else if (scaleIdx == 2) percent = 20.0;
            else if (scaleIdx == 3) percent = 10.0;
            axisY->setRange(-percent, percent);
        }
    }

    m_chart->legend()->setVisible(true);
    updateCrosshair();
    updateZeroLine();

    isUpdating = false;
}

// ── Crosshair & click ─────────────────────────────────────────────────────────

bool ChartManager::isChartViewport(QObject *obj) const
{
    return m_chartView && obj == m_chartView->viewport();
}

void ChartManager::handleViewportMousePress(const QPoint &viewportPos)
{
    QPointF scenePos = m_chartView->mapToScene(viewportPos);
    QPointF chartPos = m_chart->mapFromScene(scenePos);
    onChartClicked(chartPos);
}

void ChartManager::onChartClicked(const QPointF &chartPos)
{
    if (m_chart->series().isEmpty()) return;
    if (!m_chart->plotArea().contains(chartPos)) return;

    QPointF value        = m_chart->mapToValue(chartPos);
    qint64  clickedMsecs = static_cast<qint64>(value.x());

    qint64 bestMsecs = -1;
    qint64 bestDiff  = std::numeric_limits<qint64>::max();
    for (const auto &data : std::as_const(m_cache->cache())) {
        for (const StockDataPoint &pt : data) {
            qint64 diff = std::abs(pt.timestamp.toMSecsSinceEpoch() - clickedMsecs);
            if (diff < bestDiff) {
                bestDiff  = diff;
                bestMsecs = pt.timestamp.toMSecsSinceEpoch();
            }
        }
    }

    if (bestMsecs < 0) return;
    m_clickedMsecs = bestMsecs;
    m_clickedDate  = QDateTime::fromMSecsSinceEpoch(bestMsecs).date();
    updateCrosshair();
    emit dateClicked(m_clickedDate);
}

void ChartManager::updateZeroLine()
{
    if (!m_zeroLine || !m_chart || !m_chartView) return;

    const auto vertAxes = m_chart->axes(Qt::Vertical);
    if (vertAxes.isEmpty()) return;
    auto *axisY = qobject_cast<QValueAxis*>(vertAxes.first());
    if (!axisY) return;

    if (0 < axisY->min() || 0 > axisY->max()) {
        m_zeroLine->setVisible(false);
        return;
    }

    QPointF zeroPoint = m_chart->mapToPosition(QPointF(0, 0));
    QRectF  plotRect  = m_chart->plotArea();
    m_zeroLine->setLine(plotRect.left(), zeroPoint.y(), plotRect.right(), zeroPoint.y());
    m_zeroLine->setZValue(1);
    m_zeroLine->setVisible(true);
}

void ChartManager::updateMinMaxLines()
{
    // Lazy-init graphics items
    if (!m_minLine) {
        m_minLine = new QGraphicsLineItem(m_chart);
        m_minLine->setPen(QPen(QColor(198, 40, 40), 1, Qt::DotLine));
        m_minLine->setZValue(2);
        m_maxLine = new QGraphicsLineItem(m_chart);
        m_maxLine->setPen(QPen(QColor(56, 142, 60), 1, Qt::DotLine));
        m_maxLine->setZValue(2);
    }
    if (!m_minLabel) {
        QFont f;
        f.setPointSizeF(12.8);
        m_minLabel = new QGraphicsTextItem(m_chart);
        m_minLabel->setFont(f);
        m_minLabel->setDefaultTextColor(QColor(80, 80, 80));
        m_minLabel->setZValue(3);
        m_maxLabel = new QGraphicsTextItem(m_chart);
        m_maxLabel->setFont(f);
        m_maxLabel->setDefaultTextColor(QColor(80, 80, 80));
        m_maxLabel->setZValue(3);
    }

    if (!m_isSingleStock) {
        m_minLine->setVisible(false);
        m_maxLine->setVisible(false);
        m_minLabel->setVisible(false);
        m_maxLabel->setVisible(false);
        return;
    }

    const auto vertAxes = m_chart->axes(Qt::Vertical);
    if (vertAxes.isEmpty()) {
        m_minLine->setVisible(false); m_maxLine->setVisible(false);
        m_minLabel->setVisible(false); m_maxLabel->setVisible(false);
        return;
    }
    auto *axisY = qobject_cast<QValueAxis*>(vertAxes.first());
    if (!axisY) return;

    const QRectF plotRect = m_chart->plotArea();

    auto drawLine = [&](QGraphicsLineItem *line, QGraphicsTextItem *label,
                        double pct, double price, bool isMin) {
        if (pct == 0.0 || pct < axisY->min() || pct > axisY->max()) {
            line->setVisible(false);
            label->setVisible(false);
            return;
        }
        const QPointF pt = m_chart->mapToPosition(QPointF(0, pct));
        line->setLine(plotRect.left(), pt.y(), plotRect.right(), pt.y());
        line->setVisible(true);

        const QString sign = (pct >= 0.0) ? "+" : "";
        const QString text = QString("%1%2%  $%3")
                                 .arg(sign)
                                 .arg(pct,   0, 'f', 1)
                                 .arg(price, 0, 'f', 2);
        label->setPlainText(text);
        const double lw = label->boundingRect().width();
        const double lh = label->boundingRect().height();
        const double lx = plotRect.left() + (plotRect.width() - lw) / 2.0;
        // Min line is at the bottom: label goes above it. Max line at top: label below.
        const double ly = isMin ? (pt.y() - lh - 2) : (pt.y() + 2);
        label->setPos(lx, ly);
        label->setVisible(true);
    };

    drawLine(m_minLine, m_minLabel, m_minPct, m_minPrice, /*isMin=*/true);
    drawLine(m_maxLine, m_maxLabel, m_maxPct, m_maxPrice, /*isMin=*/false);
}

void ChartManager::updateCrosshair()
{
    updateZeroLine();
    updateMinMaxLines();

    if (m_clickedMsecs < 0 || m_chart->series().isEmpty()) {
        if (m_crosshairLine) m_crosshairLine->setVisible(false);
        return;
    }

    if (!m_crosshairLine) {
        m_crosshairLine = new QGraphicsLineItem(m_chart);
        QPen pen(QColor(80, 80, 80, 200));
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(1.0);
        m_crosshairLine->setPen(pen);
        m_crosshairLine->setZValue(10);
    }

    QRectF  plotArea = m_chart->plotArea();
    QPointF pt       = m_chart->mapToPosition(QPointF(static_cast<double>(m_clickedMsecs), 0.0));
    m_crosshairLine->setLine(pt.x(), plotArea.top(), pt.x(), plotArea.bottom());
    m_crosshairLine->setVisible(true);
}

void ChartManager::updateBgImage()
{
    if (!m_bgImageItem || !m_bgImageItem->isVisible()) return;
    const QRectF plot = m_chart->plotArea();
    if (plot.isEmpty()) return;
    m_bgImageItem->setPixmap(
        m_bgPixmap.scaled(plot.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    m_bgImageItem->setPos(plot.topLeft());
}
