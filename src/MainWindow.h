#pragma once
#include <QMainWindow>
#include <QSplitter>
#include <QTreeWidget>
#include <QPushButton>
#include <QToolButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QLabel>
#include <QList>
#include <QActionGroup>
#include "StockDataProvider.h"
#include "StockCacheManager.h"
#include "StockGroupManager.h"
#include "ApiCallTracker.h"
#include "ChartManager.h"
#include "TableManager.h"
#include "CsvPorter.h"

class QChart;
class QChartView;
class QTableWidget;
class QCloseEvent;
class QBoxLayout;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onStockSelectionChanged();
    void onDataReady(const QString &symbol, const QVector<StockDataPoint> &data);
    void onError(const QString &symbol, const QString &message);
    void onSymbolTypeReady(const QString &symbol, SymbolType type);
    void openSettings();
    void showHelp();

private:
    void setupUI();
    void setupMenu();
    void setupRightPanel(QWidget *parent, QBoxLayout *layout);
    void setActiveProvider(const QString &id);
    StockDataProvider *activeProvider() const;
    void loadSettings();
    void saveSettings();
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

    void rebuildPeriodButtons(const QList<int> &periods);

    // ── Widgets ──────────────────────────────────────────────────────────────
    QSplitter     *m_splitter    = nullptr;
    QTreeWidget   *m_stockTree   = nullptr;
    QLabel        *m_statusLabel = nullptr;
    QWidget       *m_periodBtnsContainer  = nullptr;
    QButtonGroup  *m_chartRangeBtnGroup   = nullptr;
    QComboBox     *m_yScaleCombo          = nullptr;
    QActionGroup  *m_providerActionGroup  = nullptr;

    // ── Helpers ───────────────────────────────────────────────────────────────
    StockCacheManager *m_cacheManager  = nullptr;
    StockGroupManager *m_groupManager  = nullptr;
    ApiCallTracker    *m_apiTracker    = nullptr;
    ChartManager      *m_chartManager  = nullptr;
    TableManager      *m_tableManager  = nullptr;
    CsvPorter         *m_csvPorter     = nullptr;

    // ── Providers ─────────────────────────────────────────────────────────────
    QList<StockDataProvider*> m_providers;
    QString                   m_activeProviderId;

    const QString kAppVersion = "1.1.0";
};
