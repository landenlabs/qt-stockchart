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
#include <QTextEdit>
#include <QCheckBox>
#include <QSet>
#include <QStackedWidget>
#include "StockDataProvider.h"
#include "StockCacheManager.h"
#include "StockGroupManager.h"
#include "ApiCallTracker.h"
#include "ChartManager.h"
#include "TableManager.h"
#include "CsvPorter.h"
#include "WebBrowserWidget.h"

class QChart;
class QChartView;
class QTableWidget;
class QCloseEvent;
class QShowEvent;
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
    void onForceReload(const QString &symbol);
    void openSettings();
    void showHelp();
    void onLogToggle();
    void onBrowserToggle();

private:
    void setupUI();
    void setupMenu();
    void setupRightPanel(QWidget *parent, QBoxLayout *layout);
    void setActiveProvider(const QString &id);
    StockDataProvider *activeProvider() const;
    void loadSettings();
    void saveSettings();
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

    void rebuildPeriodButtons(const QList<int> &periods);
    void refreshChart(const QStringList &symbols);
    void applyStarFilter();
    void applyFontSize(int pointSize);

    // ── Widgets ──────────────────────────────────────────────────────────────
    QSplitter     *m_splitter        = nullptr;
    QSplitter     *m_leftVSplitter   = nullptr; // stock tree (top) | API tracker (bottom)
    QTreeWidget   *m_stockTree   = nullptr;
    QLabel        *m_statusLabel = nullptr;
    QWidget       *m_periodBtnsContainer  = nullptr;
    QButtonGroup  *m_chartRangeBtnGroup   = nullptr;
    QComboBox     *m_yScaleCombo          = nullptr;
    QActionGroup  *m_providerActionGroup  = nullptr;
    QSplitter     *m_outerSplitter        = nullptr; // chart+table (top) | log pane (bottom)
    QTextEdit     *m_logEdit              = nullptr;
    QCheckBox     *m_autoRefreshCheck     = nullptr;
    QToolButton   *m_starFilterBtn        = nullptr;
    QToolButton   *m_purPctBtn            = nullptr;
    QSet<int>      m_starFilterIndices;
    QToolButton      *m_logToggleBtn         = nullptr;
    bool              m_logExpanded          = true;
    QStackedWidget   *m_contentStack         = nullptr;
    WebBrowserWidget *m_webBrowser           = nullptr;
    QToolButton      *m_browserBtn           = nullptr;
    QToolButton      *m_gearBtn              = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    QSet<QString>  m_inFlightSymbols; // symbols with an active network request
    QSet<QString>  m_quoteInFlight;   // symbols with an active fetchLatestQuote request

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

    bool m_tableRestored = false; // guard: restoreTableSplitter() called only once
};
