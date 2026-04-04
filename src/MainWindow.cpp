#include "MainWindow.h"
#include "SettingsDialog.h"
#include "AlphaVantageProvider.h"
#include "FinnhubProvider.h"
#include "PolygonProvider.h"
#include "TwelveDataProvider.h"
#include "YahooFinanceProvider.h"
#include "Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QSettings>
#include <QMessageBox>
#include <QHeaderView>
#include <QChart>
#include <QChartView>
#include <QTableWidget>
#include <QTextEdit>
#include <QTextCursor>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QCloseEvent>
#include <QLinearGradient>
#include <QStyleFactory>
#include <QTimer>
#include <QApplication>
#include <QAbstractButton>

// ── Construction ──────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_providers = {
        new AlphaVantageProvider(this),
        new FinnhubProvider(this),
        new PolygonProvider(this),
        new TwelveDataProvider(this),
        new YahooFinanceProvider(this)
    };

    for (StockDataProvider *p : m_providers) {
        connect(p, &StockDataProvider::dataReady,       this, &MainWindow::onDataReady);
        connect(p, &StockDataProvider::errorOccurred,   this, &MainWindow::onError);
        connect(p, &StockDataProvider::symbolTypeReady, this, &MainWindow::onSymbolTypeReady);
    }

    m_cacheManager = new StockCacheManager();

    setupUI(); // creates widgets; helper managers allocated below after widgets exist
    setupMenu();
    loadSettings();
}

// ── UI setup ──────────────────────────────────────────────────────────────────

void MainWindow::setupUI()
{
    setWindowTitle(QString("StockChart v%1").arg(kAppVersion));
    resize(1200, 720);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->setHandleWidth(12);

    // ── Left panel ───────────────────────────────────────────────────────────
    QWidget *leftPanel = new QWidget(m_splitter);
    leftPanel->setMinimumWidth(150);

    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);

    // ── Top controls (above tree) ────────────────────────────────────────────
    auto *topControls = new QWidget(leftPanel);
    auto *topLayout = new QHBoxLayout(topControls);
    topLayout->setContentsMargins(4, 6, 4, 4);
    topLayout->setSpacing(4);

    auto *exportBtn = new QToolButton(topControls);
    exportBtn->setText("⬇");
    exportBtn->setAutoRaise(true);
    exportBtn->setFixedSize(26, 24);
    exportBtn->setToolTip("Export Stock groups to CSV file");

    auto *importBtn = new QToolButton(topControls);
    importBtn->setText("⬆");
    importBtn->setAutoRaise(true);
    importBtn->setFixedSize(26, 24);
    importBtn->setToolTip("Import stocks and groups from CSV file");

    m_autoRefreshCheck = new QCheckBox("Auto", topControls);
    m_autoRefreshCheck->setChecked(true);
    m_autoRefreshCheck->setToolTip("Automatically fetch data once a minute when market is open");

    topLayout->addWidget(exportBtn);
    topLayout->addWidget(importBtn);
    topLayout->addWidget(m_autoRefreshCheck);
    topLayout->addStretch();

    leftLayout->addWidget(topControls);

    m_stockTree = new QTreeWidget(leftPanel);
#ifdef Q_OS_MACOS
    m_stockTree->setStyle(QStyleFactory::create("Fusion"));
#endif
    m_stockTree->setColumnCount(7);
    m_stockTree->setHeaderLabels({"*", "Type", "Symbol", "Age", "Price", "Pur. $", "Pur. Date"});
    m_stockTree->setHeaderHidden(false);
    m_stockTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_stockTree->setIndentation(12);
    m_stockTree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_stockTree->header()->setSectionResizeMode(QHeaderView::Fixed);
    m_stockTree->header()->setSectionsClickable(true);
    m_stockTree->header()->setSortIndicatorShown(true);
    m_stockTree->header()->resizeSection(0, 32);
    m_stockTree->header()->resizeSection(1, 24);
    m_stockTree->header()->resizeSection(2, 80);
    m_stockTree->header()->resizeSection(3, 50);
    m_stockTree->header()->resizeSection(4, 70);
    m_stockTree->header()->resizeSection(5, 70);
    m_stockTree->header()->resizeSection(6, 100);

    connect(m_stockTree, &QTreeWidget::itemSelectionChanged,
            this, &MainWindow::onStockSelectionChanged);

    // Re-check freshness when an already-selected item is clicked (no selection
    // change fires in that case, so itemSelectionChanged would be skipped).
    connect(m_stockTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *, int) {
        StockDataProvider *p = activeProvider();
        if (!p || !p->hasCredentials()) return;
        for (const QString &sym : m_groupManager->selectedSymbols()) {
            if (!m_cacheManager->isDataFresh(sym) && !m_inFlightSymbols.contains(sym)) {
                m_inFlightSymbols.insert(sym);
                m_apiTracker->incrementCallCount(p->id());
                m_apiTracker->updatePanel(m_activeProviderId);
                p->fetchData(sym, "3mo");
            }
        }
    });

    auto *addGroupBtn = new QPushButton("+ Add Group", leftPanel);

    leftLayout->addWidget(m_stockTree, 1);
    leftLayout->addWidget(addGroupBtn);

    // Create StockGroupManager before setupRightPanel: the yScaleCombo and range
    // buttons connect to lambdas that call m_groupManager->selectedSymbols(), so
    // m_groupManager must exist before setCurrentIndex(2) fires currentIndexChanged.
    m_groupManager = new StockGroupManager(m_stockTree, m_cacheManager, this, this);

    // Every minute: refresh Age column; auto-fetch stale data if checkbox is on.
    auto *ageTimer = new QTimer(this);
    connect(ageTimer, &QTimer::timeout, this, [this]() {
        m_groupManager->refreshAllStockCacheVisuals();

        if (!m_autoRefreshCheck->isChecked()) return;
        StockDataProvider *p = activeProvider();
        if (!p || !p->hasCredentials()) return;
        for (const QString &sym : m_groupManager->selectedSymbols()) {
            if (!m_cacheManager->isDataFresh(sym) && !m_inFlightSymbols.contains(sym)) {
                m_inFlightSymbols.insert(sym);
                m_apiTracker->incrementCallCount(p->id());
                m_apiTracker->updatePanel(m_activeProviderId);
                p->fetchData(sym, "3mo");
            }
        }
    });
    ageTimer->start(60 * 1000);

    connect(addGroupBtn, &QPushButton::clicked, m_groupManager, &StockGroupManager::onAddGroupClicked);
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        m_csvPorter->exportGroups(m_statusLabel);
    });
    connect(importBtn, &QPushButton::clicked, this, [this]() {
        m_csvPorter->importGroups(m_statusLabel);
    });
    connect(m_stockTree, &QTreeWidget::customContextMenuRequested,
            m_groupManager, &StockGroupManager::onTreeContextMenu);
    connect(m_groupManager, &StockGroupManager::forceReloadRequested,
            this, &MainWindow::onForceReload);
    connect(m_stockTree->header(), &QHeaderView::sectionClicked,
            this, [this](int col) {
                if (col == 2) m_groupManager->sortBySymbol();
            });

    // ── Right panel ──────────────────────────────────────────────────────────
    QWidget *rightPanel = new QWidget(m_splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    setupRightPanel(rightPanel, rightLayout);
    m_yScaleCombo->setCurrentIndex(2); // safe: m_groupManager already exists

    m_splitter->addWidget(leftPanel);
    m_splitter->addWidget(rightPanel);
    m_splitter->setStretchFactor(1, 1);

    m_statusLabel = new QLabel(
        "Use Providers menu to select a provider and configure your API key.", central);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_statusLabel->setCursor(Qt::IBeamCursor);

    mainLayout->addWidget(m_splitter, 1);
    mainLayout->addWidget(m_statusLabel);
}

void MainWindow::setupRightPanel(QWidget *parent, QBoxLayout *layout)
{
    // ── Toolbar ───────────────────────────────────────────────────────────────
    QWidget *toolbar = new QWidget(parent);
    toolbar->setFixedHeight(30);
    auto *tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(6, 2, 6, 2);
    tbLayout->setSpacing(4);

    auto *displayModeBtn = new QPushButton("Price", toolbar);
    displayModeBtn->setCheckable(true);
    displayModeBtn->setFixedWidth(80);
    tbLayout->addWidget(displayModeBtn);

    auto *periodsBtn = new QPushButton("⚙ Periods", toolbar);
    tbLayout->addWidget(periodsBtn);

    auto *sep2 = new QFrame(toolbar);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFrameShadow(QFrame::Sunken);
    tbLayout->addWidget(sep2);

    m_chartRangeBtnGroup = new QButtonGroup(this);
    m_chartRangeBtnGroup->setExclusive(true);

    m_periodBtnsContainer = new QWidget(toolbar);
    auto *periodBtnsLayout = new QHBoxLayout(m_periodBtnsContainer);
    periodBtnsLayout->setContentsMargins(0, 0, 0, 0);
    periodBtnsLayout->setSpacing(2);
    tbLayout->addWidget(m_periodBtnsContainer);

    connect(m_chartRangeBtnGroup, &QButtonGroup::idClicked, this, [this](int days) {
        QSettings("StockChart", "StockChart").setValue("lastChartRangeDays", days);
        m_chartManager->setRangeDays(days);
        const QStringList sel = m_groupManager->selectedSymbols();
        m_chartManager->updateChart(sel);
        m_tableManager->setActivePeriodDays(days);
        m_tableManager->setSeriesColors(m_chartManager->seriesColors());
        m_tableManager->refresh(sel, m_chartManager->clickedDate());
    });

    m_yScaleCombo = new QComboBox(toolbar);
    m_yScaleCombo->addItems({ "Auto", "+/- 10%", "+/- 20%", "+/- 30%", "+/- 40%", "+/- 50%" });
    connect(m_yScaleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        m_chartManager->updateChart(m_groupManager->selectedSymbols());
    });

    tbLayout->addSpacing(10);
    tbLayout->addWidget(new QLabel("Y:"));
    tbLayout->addWidget(m_yScaleCombo);

    tbLayout->addStretch();

    auto *helpBtn = new QToolButton(toolbar);
    helpBtn->setText("?");
    helpBtn->setToolTip("About / Help");
    helpBtn->setFixedWidth(24);
    connect(helpBtn, &QToolButton::clicked, this, &MainWindow::showHelp);
    tbLayout->addWidget(helpBtn);

    layout->addWidget(toolbar);

    auto *hline = new QFrame(parent);
    hline->setFrameShape(QFrame::HLine);
    hline->setFrameShadow(QFrame::Sunken);
    layout->addWidget(hline);

    // ── Vertical splitter: chart (top) | table (bottom) ──────────────────────
    static const char *kVertSplitterStyle =
        "QSplitter::handle:vertical {"
        "  background-color: #a0a0a0;"
        "  border-top: 1px solid #707070;"
        "  border-bottom: 1px solid #707070;"
        "}"
        "QSplitter::handle:vertical:hover { background-color: #5588cc; }";

    auto *vertSplitter = new QSplitter(Qt::Vertical, parent);
    vertSplitter->setHandleWidth(20);
    vertSplitter->setChildrenCollapsible(true);
    vertSplitter->setStyleSheet(kVertSplitterStyle);

    auto *chart = new QChart();
    chart->legend()->setAlignment(Qt::AlignTop);
    chart->legend()->setContentsMargins(0, 0, 0, 0);

    QLinearGradient gradient(0, 0, 0, 400);
    gradient.setColorAt(0.0, QColor(240, 240, 240));
    gradient.setColorAt(1.0, QColor(210, 210, 210));
    chart->setBackgroundBrush(gradient);
    chart->setPlotAreaBackgroundVisible(true);
    chart->setPlotAreaBackgroundBrush(Qt::white);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setMargins(QMargins(4, 4, 4, 4));
    chart->legend()->setAlignment(Qt::AlignBottom);
    chart->legend()->setVisible(true);

    auto *chartView = new QChartView(chart, vertSplitter);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->viewport()->installEventFilter(this);

    auto *stockTable = new QTableWidget(vertSplitter);
    stockTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    stockTable->setAlternatingRowColors(true);
    stockTable->setSelectionMode(QAbstractItemView::SingleSelection);
    stockTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    stockTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    vertSplitter->addWidget(chartView);
    vertSplitter->addWidget(stockTable);
    stockTable->setMinimumHeight(0);

    // Embed the toggle button in the splitter handle so it stays visible when collapsed
    auto *splitHandle = vertSplitter->handle(1);
    auto *handleLayout = new QHBoxLayout(splitHandle);
    handleLayout->setContentsMargins(0, 0, 0, 0);
    auto *tableToggleBtn = new QToolButton(splitHandle);
    tableToggleBtn->setText("▼ Table");
    tableToggleBtn->setAutoRaise(true);
    tableToggleBtn->setCursor(Qt::ArrowCursor); // keep arrow cursor over button, not resize
    handleLayout->addWidget(tableToggleBtn);

    // ── Outer vertical splitter: chart+table (top) | log pane (bottom) ──────
    m_outerSplitter = new QSplitter(Qt::Vertical, parent);
    m_outerSplitter->setHandleWidth(20);
    m_outerSplitter->setChildrenCollapsible(true);
    m_outerSplitter->setStyleSheet(kVertSplitterStyle);
    m_outerSplitter->addWidget(vertSplitter);

    // Log pane
    auto *logPane = new QWidget(m_outerSplitter);
    auto *logLayout = new QVBoxLayout(logPane);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(0);

    auto *logHeader = new QWidget(logPane);
    logHeader->setFixedHeight(24);
    auto *logHeaderLayout = new QHBoxLayout(logHeader);
    logHeaderLayout->setContentsMargins(6, 2, 6, 2);
    logHeaderLayout->setSpacing(6);
    auto *logClearBtn = new QPushButton("Clear", logHeader);
    logClearBtn->setFixedHeight(20);
    logClearBtn->setFixedWidth(50);
    logHeaderLayout->addStretch();
    logHeaderLayout->addWidget(logClearBtn);
    logLayout->addWidget(logHeader);

    m_logEdit = new QTextEdit(logPane);
    m_logEdit->setReadOnly(true);
    m_logEdit->setAcceptRichText(true);
    m_logEdit->setFont(QFont("Courier", 10));
    m_logEdit->setStyleSheet(
        "QTextEdit { background-color: #1e1e1e; color: #d4d4d4; border: none; padding: 4px; }");
    logLayout->addWidget(m_logEdit, 1);

    m_outerSplitter->addWidget(logPane);
    m_outerSplitter->setSizes({ 10000, 120 });

    // Embed log toggle button in the outer splitter handle
    auto *logHandle = m_outerSplitter->handle(1);
    auto *logHandleLayout = new QHBoxLayout(logHandle);
    logHandleLayout->setContentsMargins(0, 0, 0, 0);
    m_logToggleBtn = new QToolButton(logHandle);
    m_logToggleBtn->setText("▲ Log");
    m_logToggleBtn->setAutoRaise(true);
    m_logToggleBtn->setCursor(Qt::ArrowCursor);
    logHandleLayout->addWidget(m_logToggleBtn);
    connect(m_logToggleBtn, &QToolButton::clicked, this, &MainWindow::onLogToggle);

    connect(&Logger::instance(), &Logger::messageLogged, this, [this](const QString &htmlLine) {
        m_logEdit->append(htmlLine);
        m_logEdit->moveCursor(QTextCursor::End);
    });
    connect(&Logger::instance(), &Logger::cleared, this, [this]() {
        m_logEdit->clear();
    });
    connect(logClearBtn, &QPushButton::clicked, this, []() {
        Logger::instance().clear();
    });

    layout->addWidget(m_outerSplitter, 1);

    // ── Allocate chart + table managers ──────────────────────────────────────
    m_chartManager = new ChartManager(chart, chartView, m_yScaleCombo, m_cacheManager, this);
    connect(chart, &QChart::plotAreaChanged, this, [this]() {
        m_chartManager->updateCrosshair();
        m_chartManager->updateBgImage();
    });
    connect(m_chartManager, &ChartManager::dateClicked, this, [this](const QDate &date) {
        m_tableManager->refresh(m_groupManager->selectedSymbols(), date);
    });

    m_tableManager = new TableManager(stockTable, vertSplitter,
                                      tableToggleBtn, displayModeBtn,
                                      m_cacheManager, this, this);
    connect(m_tableManager, &TableManager::periodsChanged,
            this, &MainWindow::rebuildPeriodButtons);
    connect(tableToggleBtn, &QToolButton::clicked, m_tableManager, &TableManager::onToggle);
    connect(displayModeBtn, &QPushButton::toggled, m_tableManager, &TableManager::onToggleDisplayMode);
    connect(periodsBtn, &QPushButton::clicked, m_tableManager, &TableManager::configurePeriods);

    // Save table height on splitter drag
    connect(vertSplitter, &QSplitter::splitterMoved,
            m_tableManager, &TableManager::onSplitterMoved);

    // ApiCallTracker is created in loadSettings() once the left panel layout is accessible.
}

void MainWindow::onLogToggle()
{
    m_logExpanded = !m_logExpanded;
    m_logToggleBtn->setText(m_logExpanded ? "▲ Log" : "▼ Log");
    const int total = m_outerSplitter->height();
    if (total > 0) {
        if (m_logExpanded)
            m_outerSplitter->setSizes({ total - total / 5, total / 5 }); // 80/20 split
        else
            m_outerSplitter->setSizes({ total, 0 });
    }
    saveSettings();
}

void MainWindow::setupMenu()
{
    QMenu *fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction("Export Groups...", this, [this]() {
        m_csvPorter->exportGroups(m_statusLabel);
    });
    fileMenu->addAction("Import Groups...", this, [this]() {
        m_csvPorter->importGroups(m_statusLabel);
    });

    QMenu *provMenu = menuBar()->addMenu("Providers");

    m_providerActionGroup = new QActionGroup(this);
    m_providerActionGroup->setExclusive(true);

    for (StockDataProvider *p : m_providers) {
        QAction *action = new QAction(p->displayName(), this);
        action->setCheckable(true);
        action->setData(p->id());
        m_providerActionGroup->addAction(action);
        provMenu->addAction(action);
        connect(action, &QAction::triggered, this, [this, p]() {
            setActiveProvider(p->id());
        });
    }

    provMenu->addSeparator();
    QAction *configAction = new QAction("Configure API Keys...", this);
    provMenu->addAction(configAction);
    connect(configAction, &QAction::triggered, this, &MainWindow::openSettings);
}

// ── Event filter ──────────────────────────────────────────────────────────────

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::ContextMenu) {
        for (StockDataProvider *p : m_providers) {
            if (m_apiTracker && m_apiTracker->rowWidget(p->id()) == obj) {
                QMenu menu(this);
                menu.addAction("Configure API Keys...", this, &MainWindow::openSettings);
                menu.exec(static_cast<QContextMenuEvent*>(event)->globalPos());
                return true;
            }
        }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            // Provider row clicks
            for (StockDataProvider *p : m_providers) {
                if (m_apiTracker && m_apiTracker->rowWidget(p->id()) == obj) {
                    if (p->hasCredentials())
                        setActiveProvider(p->id());
                    else
                        openSettings();
                    return true;
                }
            }
            // Chart crosshair
            if (m_chartManager && m_chartManager->isChartViewport(obj))
                m_chartManager->handleViewportMousePress(me->pos());
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ── Provider management ───────────────────────────────────────────────────────

void MainWindow::setActiveProvider(const QString &id)
{
    // Save any in-session data before clearing, so it survives the reload below.
    m_cacheManager->saveCache();
    m_activeProviderId = id;
    m_cacheManager->cache().clear();
    // Restore from QSettings immediately — saveCache() only writes entries, never
    // deletes keys, so the data written above (and from prior sessions) is still there.
    m_cacheManager->loadCache();

    for (QAction *action : m_providerActionGroup->actions())
        action->setChecked(action->data().toString() == id);

    StockDataProvider *p = activeProvider();
    if (!p) return;

    setWindowTitle("StockChart v" + kAppVersion + " — " + p->displayName());

    if (!p->hasCredentials())
        m_statusLabel->setText(p->displayName() + ": API key not set. Use Providers > Configure API Keys...");
    else
        m_statusLabel->setText("Provider: " + p->displayName() + " — Select stocks to load data.");

    if (m_apiTracker) m_apiTracker->updatePanel(id);
    saveSettings();

    // Refresh the tree so Age/Price/Background remain visible after a provider switch.
    // Guard against being called during loadSettings() before loadGroups() has run.
    if (m_stockTree->topLevelItemCount() > 0)
        m_groupManager->refreshAllStockCacheVisuals();
}

StockDataProvider *MainWindow::activeProvider() const
{
    for (StockDataProvider *p : m_providers)
        if (p->id() == m_activeProviderId) return p;
    return m_providers.isEmpty() ? nullptr : m_providers.first();
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(m_providers, m_activeProviderId, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto allCreds = dlg.allCredentials();
    for (StockDataProvider *p : m_providers)
        if (allCreds.contains(p->id()))
            p->setCredentials(allCreds[p->id()]);

    setActiveProvider(dlg.selectedProviderId());
    saveSettings();
}

// ── Selection & data callbacks ────────────────────────────────────────────────

void MainWindow::onStockSelectionChanged()
{
    const QStringList selected = m_groupManager->selectedSymbols();

    if (selected.isEmpty()) {
        m_chartManager->updateChart({});
        m_tableManager->refresh({}, {});
        m_statusLabel->setText("No stocks selected.");
        return;
    }

    StockDataProvider *p = activeProvider();
    if (!p || !p->hasCredentials()) {
        m_statusLabel->setText("API key not configured. Use Providers > Configure API Keys...");
        return;
    }

    QStringList loading;
    for (const QString &sym : selected) {
        if (!m_cacheManager->isDataFresh(sym) && !m_inFlightSymbols.contains(sym)) {
            m_inFlightSymbols.insert(sym);
            m_apiTracker->incrementCallCount(p->id());
            m_apiTracker->updatePanel(m_activeProviderId);
            p->fetchData(sym, "3mo");
            loading << sym;
        }
    }

    m_chartManager->updateChart(selected);
    m_tableManager->setSeriesColors(m_chartManager->seriesColors());
    m_tableManager->refresh(selected, m_chartManager->clickedDate());

    int ready = 0;
    for (const QString &sym : selected) if (m_cacheManager->cache().contains(sym)) ++ready;

    if (loading.isEmpty())
        m_statusLabel->setText(QString("%1 stock(s) — normalized % change").arg(ready));
    else
        m_statusLabel->setText(
            QString("Loading %1...  (%2/%3 ready)")
                .arg(loading.join(", ")).arg(ready).arg(selected.size()));
}

void MainWindow::onDataReady(const QString &symbol, const QVector<StockDataPoint> &data)
{
    m_inFlightSymbols.remove(symbol);
    // Track whether this arrival is from a fetchLatestQuote call (not a regular fetchData).
    // We only trigger a quote fetch from regular data — never from quote data — to prevent loops.
    const bool wasQuoteFetch = m_quoteInFlight.remove(symbol);

    if (data.isEmpty()) return;
    auto &existing = m_cacheManager->cache()[symbol];
    if (existing.isEmpty()) {
        existing = data;
    } else {
        QMap<QDateTime, double> merged;
        for (const StockDataPoint &pt : std::as_const(existing))
            merged[pt.timestamp] = pt.price;
        for (const StockDataPoint &pt : data)
            merged[pt.timestamp] = pt.price;
        existing.clear();
        existing.reserve(merged.size());
        for (auto it = merged.cbegin(); it != merged.cend(); ++it)
            existing.append({it.key(), it.value()});
    }
    StockCacheManager::normalizeCache(existing);
    m_groupManager->symbolErrors().remove(symbol);
    m_groupManager->updateTreeItemIcon(symbol);

    if (!m_cacheManager->symbolTypes().contains(symbol)) {
        if (StockDataProvider *p = activeProvider())
            p->fetchSymbolType(symbol);
    }

    const QStringList selected = m_groupManager->selectedSymbols();
    if (!selected.contains(symbol)) return;

    // After regular data arrives, check if today's close is still missing.
    // If so, fire a lightweight quote endpoint to fill in the current day.
    if (!wasQuoteFetch) {
        const QDate today = QDate::currentDate();
        const int dow = today.dayOfWeek(); // 1=Mon…5=Fri, 6=Sat, 7=Sun
        if (dow >= 1 && dow <= 5 && !existing.isEmpty()) {
            if (existing.last().timestamp.date() < today && !m_quoteInFlight.contains(symbol)) {
                m_quoteInFlight.insert(symbol);
                if (StockDataProvider *p = activeProvider())
                    p->fetchLatestQuote(symbol);
            }
        }
    }

    m_chartManager->updateChart(selected);
    m_tableManager->setSeriesColors(m_chartManager->seriesColors());
    m_tableManager->refresh(selected, m_chartManager->clickedDate());

    int ready = 0;
    for (const QString &sym : selected) if (m_cacheManager->cache().contains(sym)) ++ready;

    if (ready == selected.size())
        m_statusLabel->setText(QString("%1 stock(s) — normalized % change").arg(ready));
    else
        m_statusLabel->setText(QString("Loaded %1/%2 stocks...").arg(ready).arg(selected.size()));
}

void MainWindow::onForceReload(const QString &symbol)
{
    StockDataProvider *p = activeProvider();
    if (!p || !p->hasCredentials()) {
        m_statusLabel->setText("API key not configured. Use Providers > Configure API Keys...");
        return;
    }
    m_inFlightSymbols.insert(symbol);
    m_apiTracker->incrementCallCount(p->id());
    m_apiTracker->updatePanel(m_activeProviderId);
    p->fetchData(symbol, "3mo");
    m_statusLabel->setText("Reloading " + symbol + "...");
}

void MainWindow::onError(const QString &symbol, const QString &message)
{
    m_inFlightSymbols.remove(symbol);
    m_quoteInFlight.remove(symbol);
    QApplication::beep();
    m_statusLabel->setText("Error: " + message);
    const QString logMsg = symbol.isEmpty() ? message : symbol + ": " + message;
    Logger::instance().append(logMsg);
    if (!symbol.isEmpty()) {
        m_groupManager->symbolErrors().insert(symbol);
        m_groupManager->updateTreeItemIcon(symbol);
    }
    const QStringList selected = m_groupManager->selectedSymbols();
    if (!selected.isEmpty()) {
        m_chartManager->updateChart(selected);
        m_tableManager->setSeriesColors(m_chartManager->seriesColors());
        m_tableManager->refresh(selected, m_chartManager->clickedDate());
    }
}

void MainWindow::onSymbolTypeReady(const QString &symbol, SymbolType type)
{
    if (m_cacheManager->symbolTypes().value(symbol) == type) return;
    m_cacheManager->symbolTypes()[symbol] = type;
    m_cacheManager->saveSymbolType(symbol, type);
    m_groupManager->updateTreeItemIcon(symbol);
}

// ── Period buttons ────────────────────────────────────────────────────────────

void MainWindow::rebuildPeriodButtons(const QList<int> &periods)
{
    // Remove and delete all existing buttons
    for (QAbstractButton *btn : m_chartRangeBtnGroup->buttons()) {
        m_chartRangeBtnGroup->removeButton(btn);
        delete btn;
    }

    // Restore the previously selected range (default 60 days)
    const int savedId = QSettings("StockChart", "StockChart")
                            .value("lastChartRangeDays", 60).toInt();

    auto periodLabel = [](int p) -> QString {
        if (p == 0) return "Today";
        const int d = qAbs(p);
        if (d >= 365 && d % 365 == 0) return QString("%1y").arg(d / 365);
        return QString("%1d").arg(d);
    };

    const QString btnStyle =
        "QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 1px 4px; }"
        "QToolButton:checked { background-color: palette(highlight); "
        "color: palette(highlighted-text); border-radius: 3px; }";

    auto *layout = qobject_cast<QHBoxLayout *>(m_periodBtnsContainer->layout());

    for (int p : periods) {
        const int id = qAbs(p);
        auto *btn = new QToolButton(m_periodBtnsContainer);
        btn->setText(periodLabel(p));
        btn->setCheckable(true);
        btn->setAutoRaise(false);
        btn->setStyleSheet(btnStyle);
        m_chartRangeBtnGroup->addButton(btn, id);
        layout->addWidget(btn);
    }

    // Select the button whose id is closest to the saved range
    if (!m_chartRangeBtnGroup->buttons().isEmpty()) {
        QAbstractButton *best = m_chartRangeBtnGroup->buttons().first();
        int bestDiff = std::abs(m_chartRangeBtnGroup->id(best) - savedId);
        for (QAbstractButton *btn : m_chartRangeBtnGroup->buttons()) {
            const int diff = std::abs(m_chartRangeBtnGroup->id(btn) - savedId);
            if (diff < bestDiff) { bestDiff = diff; best = btn; }
        }
        best->setChecked(true);
    }

    const int selectedId = m_chartRangeBtnGroup->checkedId();
    if (selectedId >= 0 && m_chartManager) {
        m_chartManager->setRangeDays(selectedId);
        const QStringList sel = m_groupManager ? m_groupManager->selectedSymbols() : QStringList{};
        if (!sel.isEmpty())
            m_chartManager->updateChart(sel);
    }
    if (selectedId >= 0 && m_tableManager)
        m_tableManager->setActivePeriodDays(selectedId);
}

// ── Settings ──────────────────────────────────────────────────────────────────

void MainWindow::loadSettings()
{
    QSettings s("StockChart", "StockChart");

    for (StockDataProvider *p : m_providers) {
        s.beginGroup(p->id());
        QMap<QString,QString> creds;
        for (const auto &field : p->credentialFields())
            creds[field.first] = s.value(field.first).toString();
        p->setCredentials(creds);
        s.endGroup();
    }

    // Build the API info panel now that the left layout is complete.
    // We need to reach the left panel's layout — find it via the splitter.
    QWidget *leftPanel = m_splitter->widget(0);
    auto *leftLayout   = qobject_cast<QVBoxLayout*>(leftPanel->layout());
    m_apiTracker = new ApiCallTracker(m_providers, leftPanel, leftLayout, this);
    m_apiTracker->loadDailyCallCounts();

    // Install event filter on provider rows so clicks are caught in eventFilter()
    for (StockDataProvider *p : m_providers)
        if (QWidget *row = m_apiTracker->rowWidget(p->id()))
            row->installEventFilter(this);

    m_tableManager->loadSettings();

    m_autoRefreshCheck->setChecked(s.value("autoRefresh", true).toBool());

    m_logExpanded = s.value("logExpanded", true).toBool();
    if (m_logToggleBtn)
        m_logToggleBtn->setText(m_logExpanded ? "▲ Log" : "▼ Log");

    if (s.contains("mainSplitterState"))
        m_splitter->restoreState(s.value("mainSplitterState").toByteArray());
    if (s.contains("outerSplitterState"))
        m_outerSplitter->restoreState(s.value("outerSplitterState").toByteArray());

    // setActiveProvider clears m_cache but immediately reloads it from QSettings,
    // so the cache is populated before loadGroups() runs below.
    setActiveProvider(s.value("activeProvider", m_providers.first()->id()).toString());
    m_cacheManager->loadCache();         // second load is harmless; ensures cache is fresh
    m_cacheManager->loadSymbolTypeCache();

    // Load groups AFTER the cache is populated so addStockToGroup() can display
    // Age, Price, and background colours from the very first paint.
    m_groupManager->loadGroups();

    // Restore previously selected symbols (signals are blocked inside selectSymbols,
    // so we trigger onStockSelectionChanged manually afterward).
    const QStringList lastSelected = s.value("selectedSymbols").toStringList();
    if (!lastSelected.isEmpty()) {
        m_groupManager->selectSymbols(lastSelected);
        onStockSelectionChanged();
    }

    // Build CSV porter now that group manager is ready
    m_csvPorter = new CsvPorter(m_stockTree, m_groupManager, this);
}

void MainWindow::saveSettings()
{
    QSettings s("StockChart", "StockChart");
    s.setValue("autoRefresh",           m_autoRefreshCheck->isChecked());
    s.setValue("activeProvider",        m_activeProviderId);
    s.setValue("logExpanded",           m_logExpanded);
    s.setValue("mainSplitterState",     m_splitter->saveState());
    s.setValue("outerSplitterState",    m_outerSplitter->saveState());
    if (m_groupManager)
        s.setValue("selectedSymbols", m_groupManager->selectedSymbols());
    m_cacheManager->saveCache();
    if (m_tableManager) m_tableManager->saveSettings();
    for (StockDataProvider *p : m_providers) {
        s.beginGroup(p->id());
        for (const auto &field : p->credentialFields())
            s.setValue(field.first, p->credentials().value(field.first));
        s.endGroup();
    }
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // Restore the table splitter once, after the first real layout pass.
    // showEvent fires during show() before children finish layout, so we defer
    // one tick; by the time the timer fires, show() has completed and the
    // splitter has its correct height.
    if (!m_tableRestored) {
        m_tableRestored = true;
        QTimer::singleShot(0, this, [this]() {
            if (m_tableManager) m_tableManager->restoreTableSplitter();
            // If log was saved as collapsed, apply after layout is complete
            if (!m_logExpanded)
                m_outerSplitter->setSizes({ m_outerSplitter->height(), 0 });
        });
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

// ── Help dialog ───────────────────────────────────────────────────────────────

void MainWindow::showHelp()
{
    QDialog dlg(this);
    dlg.setWindowTitle("About StockChart");
    dlg.setMinimumWidth(680);

    auto *layout = new QVBoxLayout(&dlg);
    layout->setSpacing(12);

    auto *aboutFrame = new QFrame(&dlg);
    aboutFrame->setFrameShape(QFrame::StyledPanel);
    auto *aboutLayout = new QVBoxLayout(aboutFrame);

    auto *appName = new QLabel("<b style='font-size:14pt'>StockChart</b>", aboutFrame);
    appName->setAlignment(Qt::AlignCenter);
    auto *version = new QLabel("Version 1.0", aboutFrame);
    version->setAlignment(Qt::AlignCenter);
    auto *desc = new QLabel(
        "A Qt6 desktop application for viewing and comparing historical stock performance.\n"
        "Supports multiple data providers, normalized % change charting,\n"
        "and a configurable performance table.", aboutFrame);
    desc->setAlignment(Qt::AlignCenter);
    desc->setWordWrap(true);
    auto *author = new QLabel("Author: Dennis Lang", aboutFrame);
    author->setAlignment(Qt::AlignCenter);

    aboutLayout->addWidget(appName);
    aboutLayout->addWidget(version);
    aboutLayout->addWidget(desc);
    aboutLayout->addWidget(author);
    layout->addWidget(aboutFrame);

    layout->addWidget(new QLabel("<b>Stock Market API Providers</b>", &dlg));

    struct ProviderInfo { QString name, freeTier, limits, notes; };
    const QList<ProviderInfo> providers = {
        { "Alpha Vantage",  "Yes",           "25 req/day",             "Good historical data; slow on free tier" },
        { "Finnhub",        "Yes",           "60 req/min",             "Real-time US quotes; solid free tier" },
        { "Polygon.io",     "Yes (delayed)", "5 req/min",              "15-min delayed on free; real-time needs $29/mo" },
        { "Twelve Data",    "Yes",           "800 req/day, 8 req/min", "Generous free tier; good historical data" },
        { "Tiingo",         "Yes",           "500 req/hour",           "End-of-day historical; very good for casual use" },
        { "Yahoo Finance",  "Yes (no key)",  "—",                      "Unofficial API only; no key required but can break without warning" },
    };

    auto *table = new QTableWidget(providers.size(), 4, &dlg);
    table->setHorizontalHeaderLabels({ "Provider", "Free Tier", "Limits", "Notes" });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->setShowGrid(true);
    table->setAlternatingRowColors(true);

    const QStringList integrated = { "Alpha Vantage", "Finnhub", "Polygon.io", "Twelve Data" };
    const QColor integratedBg(230, 245, 230);

    for (int r = 0; r < providers.size(); ++r) {
        const auto &p = providers[r];
        const bool isIntegrated = integrated.contains(p.name);
        const QList<QString> cells = { p.name, p.freeTier, p.limits, p.notes };
        for (int c = 0; c < 4; ++c) {
            auto *item = new QTableWidgetItem(cells[c]);
            item->setFlags(Qt::ItemIsEnabled);
            if (isIntegrated) item->setBackground(integratedBg);
            table->setItem(r, c, item);
        }
    }
    table->resizeRowsToContents();

    auto *legend = new QLabel("<i>Green rows are integrated in this application.</i>", &dlg);
    legend->setAlignment(Qt::AlignRight);
    layout->addWidget(table);
    layout->addWidget(legend);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);
    layout->addWidget(buttons);

    dlg.exec();
}
