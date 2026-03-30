#pragma once
#include <QObject>
#include <QDate>
#include <QPointF>
#include <QStringList>
#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QPixmap>
#include "StockCacheManager.h"

// Manages chart rendering, crosshair, zero line, background image, and chart range.
class ChartManager : public QObject
{
    Q_OBJECT
public:
    explicit ChartManager(QChart *chart, QChartView *chartView,
                          QComboBox *yScaleCombo, StockCacheManager *cache,
                          QObject *parent = nullptr);

    // Rebuild chart series for the given symbols using the current range
    void updateChart(const QStringList &symbols);

    // Called when user clicks inside the chart viewport (viewport-local coordinates)
    void handleViewportMousePress(const QPoint &viewportPos);

    // Returns true if `obj` is this chart's viewport (for use in eventFilter)
    bool isChartViewport(QObject *obj) const;

    // Called by MainWindow when plotAreaChanged fires
    void updateCrosshair();
    void updateBgImage();

    // Range control
    void setRangeDays(int days) { m_chartRangeDays = days; }
    int  rangeDays() const      { return m_chartRangeDays; }

    // Clicked date (exposed so TableManager can add a column for it)
    QDate  clickedDate()  const { return m_clickedDate; }
    qint64 clickedMsecs() const { return m_clickedMsecs; }

signals:
    void dateClicked(const QDate &date); // emitted after a chart click snaps to a data point

private:
    void onChartClicked(const QPointF &chartPos);
    void updateZeroLine();

    QChart            *m_chart;
    QChartView        *m_chartView;
    QComboBox         *m_yScaleCombo;
    StockCacheManager *m_cache;

    QGraphicsLineItem   *m_crosshairLine = nullptr;
    QGraphicsLineItem   *m_zeroLine      = nullptr;
    QGraphicsPixmapItem *m_bgImageItem   = nullptr;
    QPixmap              m_bgPixmap;

    int    m_chartRangeDays = 0;
    qint64 m_clickedMsecs   = -1;
    QDate  m_clickedDate;
};
