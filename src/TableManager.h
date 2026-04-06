#pragma once
#include <QColor>
#include <QDate>
#include <QList>
#include <QMap>
#include <QObject>
#include <QTableWidget>
#include <QSplitter>
#include <QToolButton>
#include <QPushButton>
#include "StockCacheManager.h"

class QWidget;

// Manages the performance table: expand/collapse, refresh, period config, and settings persistence.
class TableManager : public QObject
{
    Q_OBJECT
public:
    explicit TableManager(QTableWidget *table, QSplitter *vertSplitter,
                          QToolButton *toggleBtn, QPushButton *displayModeBtn,
                          StockCacheManager *cache, QWidget *dialogParent,
                          QObject *parent = nullptr);

    void refresh(const QStringList &symbols, const QDate &clickedDate);
    void loadSettings();
    void saveSettings();

    void setExpanded(bool expanded);
    bool isExpanded() const { return m_tableExpanded; }

    void setActivePeriodDays(int days) { m_activePeriodDays = days; }
    void setSeriesColors(const QMap<QString, QColor> &colors) { m_seriesColors = colors; }
    void setPurchasePrices(const QMap<QString, double> &prices) { m_purchasePrices = prices; }
    void setPurPctMode(bool v) { m_purPctMode = v; }

    void restoreTableSplitter(); // call once from MainWindow::showEvent
    void configurePeriods();
    void onToggleDisplayMode(bool checked);
    void onToggle();
    void onSplitterMoved(); // call from MainWindow splitterMoved signal

    const QList<int> &periods() const { return m_periods; }

signals:
    void periodsChanged(const QList<int> &periods);

private:
    QTableWidget  *m_table;
    QSplitter     *m_vertSplitter;
    QToolButton   *m_toggleBtn;
    QPushButton   *m_displayModeBtn;
    StockCacheManager *m_cache;
    QWidget       *m_dialogParent;

    bool       m_tableExpanded     = false;
    int        m_savedTableHeight  = -1;
    bool       m_showPercentChange = false;
    bool       m_purPctMode        = false;
    QList<int> m_periods;
    int        m_activePeriodDays  = 0;

    // Stored so onToggle can refresh with last-known state
    QDate                 m_clickedDate;
    QStringList           m_lastSymbols;
    QMap<QString, QColor>  m_seriesColors;
    QMap<QString, double>  m_purchasePrices;
};
