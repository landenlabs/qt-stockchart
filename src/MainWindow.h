#pragma once
#include <QMainWindow>
#include <QTreeWidget>
#include <QPushButton>
#include <QToolButton>
#include <QButtonGroup>
#include <QTableWidget>
#include <QChartView>
#include <QChart>
#include <QSplitter>
#include <QLabel>
#include <QFrame>
#include <QDate>
#include <QBoxLayout>
#include <QActionGroup>
#include <QList>
#include <QMap>
#include <QSet>
#include <QIcon>
#include <QComboBox> 
#include "StockDataProvider.h"

class QGraphicsLineItem;
class QCloseEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    enum TreeRoles {
        StarRole = Qt::UserRole + 10,
        PurPriceRole,
        PurDateRole
    };

private slots:
    void onStockSelectionChanged();
    void onDataReady(const QString &symbol, const QVector<StockDataPoint> &data);
    void onError(const QString &symbol, const QString &message);
    void onSymbolTypeReady(const QString &symbol, SymbolType type);
    void openSettings();
    void onAddGroupClicked();
    void onTreeContextMenu(const QPoint &pos);
    void onTableStateChanged(int id);
    void onToggleDisplayMode(bool checked);
    void configurePeriods();
    void onTableColumnClicked(int col);
    void onChartRangeChanged(int days);
    void showHelp();
    void exportGroups();
    void importGroups();
    void onYScaleChanged(int index);  
    void onEditStockDetails(QTreeWidgetItem *item);
    void onSetStar(QTreeWidgetItem *item, int starIndex);

private:
    void setupUI();
    void setupMenu();
    void setupRightPanel(QWidget *parent, QBoxLayout *layout);
    void updateChart(const QStringList &selectedSymbols);
    void setTableState(int stateId);     // 0=Collapsed 1=Half 2=Full
    void refreshTable();
    void loadTableSettings();
    void saveTableSettings();
    void setActiveProvider(const QString &id);
    StockDataProvider *activeProvider() const;
    void loadSettings();
    void saveSettings();
    void loadCache();
    void saveCache();
    void closeEvent(QCloseEvent *event) override;
    QStringList selectedSymbols() const;

    bool eventFilter(QObject *obj, QEvent *event) override;
    void onChartClicked(const QPointF &chartPos);
    void updateCrosshair();
    void updateZeroLine();

    // Group management
    QTreeWidgetItem *addGroup(const QString &name, bool expanded = true);
    void addStockToGroup(QTreeWidgetItem *groupItem, const QString &symbol, 
                         int star = 0, double purPrice = 0.0, const QString &purDate = "");
    void showAddStockDialog(QTreeWidgetItem *groupItem);
    void loadGroups();
    void saveGroups();

    static QIcon makeStarIcon(int index);
    // API call tracking
    void setupApiInfoPanel(QWidget *parent, QBoxLayout *layout);
    void updateApiInfoPanel();
    void incrementCallCount(const QString &providerId);
    void loadDailyCallCounts();
    void saveDailyCallCounts();

    // Symbol icons
    void updateTreeItemIcon(const QString &symbol);
    void loadSymbolTypeCache();
    void saveSymbolType(const QString &symbol, SymbolType type);
    static QIcon makeTypeIcon(SymbolType type);
    static QIcon makeErrorIcon();
    void refreshAllStockCacheVisuals();

    // ── Widgets ──────────────────────────────────────────────────────────────
    QSplitter      *m_splitter;          // horizontal: left | right
    QTreeWidget    *m_stockTree;
    QPushButton    *m_addGroupBtn;
    QSplitter      *m_vertSplitter;      // vertical: chart | table
    QChartView     *m_chartView;
    QChart         *m_chart;
    QTableWidget   *m_stockTable;
    QButtonGroup   *m_tableStateBtnGroup;
    QButtonGroup   *m_chartRangeBtnGroup;
    QPushButton    *m_displayModeBtn;
    QLabel         *m_statusLabel;
    QActionGroup   *m_providerActionGroup;
    QMap<QString, QLabel*> m_providerNameLabels;
    QMap<QString, QLabel*> m_callCountLabels;
    QGraphicsLineItem *m_crosshairLine = nullptr;
    QGraphicsLineItem *m_zeroLine      = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    QList<StockDataProvider*> m_providers;
    QString m_activeProviderId;
    QMap<QString, QVector<StockDataPoint>> m_cache;
    QMap<QString, int> m_dailyCallCounts;
    QDate m_currentDay;

    // Table state (0=Collapsed, 1=Half, 2=Full)
    int         m_tableStateId       = 0;
    bool        m_showPercentChange  = false;
    QList<int>  m_periods;

    // Chart range (days back from today; 0 = all data)
    int         m_chartRangeDays     = 0;

    // Symbol metadata
    QMap<QString, SymbolType> m_symbolTypes;
    QSet<QString>             m_symbolErrors;

    // Crosshair & click column
    qint64 m_clickedMsecs = -1;
    QDate  m_clickedDate;
    int    m_refColIndex  = 0;

    QComboBox* m_yScaleCombo; // New UI element
    const QString kAppVersion = "1.1.0";
};
