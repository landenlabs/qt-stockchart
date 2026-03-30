#include "ChartManager.h"
#include <QDateTimeAxis>
#include <QValueAxis>
#include <QLineSeries>
#include <QAreaSeries>
#include <QLegendMarker>
#include <QPen>
#include <QTimeZone>
#include <QDateTime>
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

        QVector<QPair<qint64, double>> pts;
        for (const StockDataPoint &pt : data) {
            if (rangeStart.isValid() && pt.timestamp < rangeStart) continue;
            double pct = (pt.price / basePrice - 1.0) * 100.0;
            pts.append({pt.timestamp.toMSecsSinceEpoch(), pct});
            minPct = std::min(minPct, pct);
            maxPct = std::max(maxPct, pct);
            if (minTime.isNull() || pt.timestamp < minTime) minTime = pt.timestamp;
            if (maxTime.isNull() || pt.timestamp > maxTime) maxTime = pt.timestamp;
        }
        if (pts.isEmpty()) continue;

        if (singleStock) {
            auto *upperPos     = new QLineSeries();
            auto *zeroLine     = new QLineSeries();
            auto *lowerNeg     = new QLineSeries();
            auto *mainLine     = new QLineSeries();
            auto *zeroLineGreen = new QLineSeries();
            mainLine->setName(sym);

            for (const auto &[msecs, pct] : pts) {
                upperPos->append(msecs, qMax(0.0, pct));
                zeroLine->append(msecs, 0.0);
                lowerNeg->append(msecs, qMin(0.0, pct));
                mainLine->append(msecs, pct);
                zeroLineGreen->append(msecs, 0.0);
            }

            auto *greenArea = new QAreaSeries(upperPos, zeroLineGreen);
            greenArea->setBrush(QColor(56, 142, 60, 130));
            greenArea->setPen(QPen(Qt::transparent));

            auto *redArea = new QAreaSeries(zeroLine, lowerNeg);
            redArea->setBrush(QColor(198, 40, 40, 130));
            redArea->setPen(QPen(Qt::transparent));

            QPen linePen(QColor(40, 40, 40));
            linePen.setWidthF(1.5);
            mainLine->setPen(linePen);

            m_chart->addSeries(greenArea);
            m_chart->addSeries(redArea);
            m_chart->addSeries(mainLine);
            greenArea->attachAxis(axisX); greenArea->attachAxis(axisY);
            redArea->attachAxis(axisX);   redArea->attachAxis(axisY);
            mainLine->attachAxis(axisX);  mainLine->attachAxis(axisY);

            for (QLegendMarker *mk : m_chart->legend()->markers(greenArea)) mk->setVisible(false);
            for (QLegendMarker *mk : m_chart->legend()->markers(redArea))   mk->setVisible(false);
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

void ChartManager::updateCrosshair()
{
    updateZeroLine();

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
