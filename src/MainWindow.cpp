#include "MainWindow.h"
#include "SettingsDialog.h"
#include "AlphaVantageProvider.h"
#include "FinnhubProvider.h"
#include "PolygonProvider.h"
#include "TwelveDataProvider.h"
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
#include <QMouseEvent>
#include <QCloseEvent>
#include <QLinearGradient>
#include <QStyleFactory>

// ── Construction ──────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_providers = {
        new AlphaVantageProvider(this),
        new FinnhubProvider(this),
        new PolygonProvider(this),
        new TwelveDataProvider(this)
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

    m_stockTree = new QTreeWidget(leftPanel);
#ifdef Q_OS_MACOS
    m_stockTree->setStyle(QStyleFactory::create("Fusion"));
#endif
    m_stockTree->setColumnCount(6);
    m_stockTree->setHeaderLabels({"*", "Type", "Symbol", "Price", "Pur. $", "Pur. Date"});
    m_stockTree->setHeaderHidden(false);
    m_stockTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_stockTree->setIndentation(12);
    m_stockTree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_stockTree->header()->setSectionResizeMode(QHeaderView::Fixed);
    m_stockTree->header()->resizeSection(0, 32);
    m_stockTree->header()->resizeSection(1, 24);
    m_stockTree->header()->resizeSection(2, 80);
    m_stockTree->header()->resizeSection(3, 70);
    m_stockTree->header()->resizeSection(4, 70);
    m_stockTree->header()->resizeSection(5, 100);

    connect(m_stockTree, &QTreeWidget::itemSelectionChanged,
            this, &MainWindow::onStockSelectionChanged);

    auto *addGroupBtn = new QPushButton("+ Add Group", leftPanel);

    leftLayout->addWidget(m_stockTree, 1);
    leftLayout->addWidget(addGroupBtn);

    // Create StockGroupManager before setupRightPanel: the yScaleCombo and range
    // buttons connect to lambdas that call m_groupManager->selectedSymbols(), so
    // m_groupManager must exist before setCurrentIndex(2) fires currentIndexChanged.
    m_groupManager = new StockGroupManager(m_stockTree, m_cacheManager, this, this);
    connect(addGroupBtn, &QPushButton::clicked, m_groupManager, &StockGroupManager::onAddGroupClicked);
    connect(m_stockTree, &QTreeWidget::customContextMenuRequested,
            m_groupManager, &StockGroupManager::onTreeContextMenu);

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

    auto *tableToggleBtn = new QToolButton(toolbar);
    tableToggleBtn->setText("▼ Table");
    tableToggleBtn->setAutoRaise(true);
    tbLayout->addWidget(tableToggleBtn);

    auto *sep = new QFrame(toolbar);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    tbLayout->addWidget(sep);

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
    const QList<QPair<QString,int>> ranges = {
        {"60d", 60}, {"30d", 30}, {"7d", 7}, {"Today", 0}
    };
    const QString rangeCheckedStyle =
        "QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 1px 4px; }"
        "QToolButton:checked { background-color: palette(highlight); color: palette(highlighted-text); border-radius: 3px; }";
    for (const auto &[label, days] : ranges) {
        auto *btn = new QToolButton(toolbar);
        btn->setText(label);
        btn->setCheckable(true);
        btn->setAutoRaise(false);
        btn->setStyleSheet(rangeCheckedStyle);
        m_chartRangeBtnGroup->addButton(btn, days);
        tbLayout->addWidget(btn);
    }
    m_chartRangeBtnGroup->button(0)->setChecked(true);
    connect(m_chartRangeBtnGroup, &QButtonGroup::idClicked, this, [this](int days) {
        m_chartManager->setRangeDays(days);
        m_chartManager->updateChart(m_groupManager->selectedSymbols());
    });

    m_yScaleCombo = new QComboBox(toolbar);
    m_yScaleCombo->addItems({ "Auto", "+/- 30%", "+/- 20%", "+/- 10%" });
    connect(m_yScaleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        m_chartManager->updateChart(m_groupManager->selectedSymbols());
    });

    tbLayout->addSpacing(10);
    tbLayout->addWidget(new QLabel("Y:"));
    tbLayout->addWidget(m_yScaleCombo);
    m_chartRangeBtnGroup->button(60)->setChecked(true);

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
    auto *vertSplitter = new QSplitter(Qt::Vertical, parent);
    vertSplitter->setChildrenCollapsible(true);

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
    stockTable->hide();

    vertSplitter->addWidget(chartView);
    vertSplitter->addWidget(stockTable);

    layout->addWidget(vertSplitter, 1);

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
    connect(tableToggleBtn, &QToolButton::clicked, m_tableManager, &TableManager::onToggle);
    connect(displayModeBtn, &QPushButton::toggled, m_tableManager, &TableManager::onToggleDisplayMode);
    connect(stockTable->horizontalHeader(), &QHeaderView::sectionClicked,
            m_tableManager, &TableManager::onColumnClicked);
    connect(periodsBtn, &QPushButton::clicked, m_tableManager, &TableManager::configurePeriods);

    // Save table height on splitter drag
    connect(vertSplitter, &QSplitter::splitterMoved,
            m_tableManager, &TableManager::onSplitterMoved);

    // ApiCallTracker is created in loadSettings() once the left panel layout is accessible.
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
    m_activeProviderId = id;
    m_cacheManager->cache().clear();

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
        if (!m_cacheManager->cache().contains(sym)) {
            m_apiTracker->incrementCallCount(p->id());
            m_apiTracker->updatePanel(m_activeProviderId);
            p->fetchData(sym, "3mo");
            loading << sym;
        }
    }

    m_chartManager->updateChart(selected);
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
    m_cacheManager->cache()[symbol] = data;

    m_groupManager->symbolErrors().remove(symbol);
    m_groupManager->updateTreeItemIcon(symbol);

    if (!m_cacheManager->symbolTypes().contains(symbol)) {
        if (StockDataProvider *p = activeProvider())
            p->fetchSymbolType(symbol);
    }

    const QStringList selected = m_groupManager->selectedSymbols();
    if (!selected.contains(symbol)) return;

    m_chartManager->updateChart(selected);
    m_tableManager->refresh(selected, m_chartManager->clickedDate());

    int ready = 0;
    for (const QString &sym : selected) if (m_cacheManager->cache().contains(sym)) ++ready;

    if (ready == selected.size())
        m_statusLabel->setText(QString("%1 stock(s) — normalized % change").arg(ready));
    else
        m_statusLabel->setText(QString("Loaded %1/%2 stocks...").arg(ready).arg(selected.size()));
}

void MainWindow::onError(const QString &symbol, const QString &message)
{
    m_statusLabel->setText("Error: " + message);
    if (!symbol.isEmpty()) {
        m_groupManager->symbolErrors().insert(symbol);
        m_groupManager->updateTreeItemIcon(symbol);
    }
}

void MainWindow::onSymbolTypeReady(const QString &symbol, SymbolType type)
{
    if (m_cacheManager->symbolTypes().value(symbol) == type) return;
    m_cacheManager->symbolTypes()[symbol] = type;
    m_cacheManager->saveSymbolType(symbol, type);
    m_groupManager->updateTreeItemIcon(symbol);
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

    m_cacheManager->loadCache();
    m_cacheManager->loadSymbolTypeCache();

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

    if (s.contains("mainSplitterState"))
        m_splitter->restoreState(s.value("mainSplitterState").toByteArray());

    m_groupManager->loadGroups();

    // Build CSV porter now that group manager is ready
    m_csvPorter = new CsvPorter(m_stockTree, m_groupManager, this);

    setActiveProvider(s.value("activeProvider", m_providers.first()->id()).toString());
}

void MainWindow::saveSettings()
{
    QSettings s("StockChart", "StockChart");
    s.setValue("activeProvider",     m_activeProviderId);
    s.setValue("mainSplitterState",  m_splitter->saveState());
    m_cacheManager->saveCache();
    for (StockDataProvider *p : m_providers) {
        s.beginGroup(p->id());
        for (const auto &field : p->credentialFields())
            s.setValue(field.first, p->credentials().value(field.first));
        s.endGroup();
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
