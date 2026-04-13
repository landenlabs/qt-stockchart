#include "MainWindow.h"
#include "SettingsDialog.h"
#include "ProviderRegistry.h"
#include "AlphaVantageProvider.h"
#include "FinnhubProvider.h"
#include "PolygonProvider.h"
#include "TwelveDataProvider.h"
#include "YahooFinanceProvider.h"
#include "FmpProvider.h"
#include "YahooPageProvider.h"
#include "Logger.h"
#include "AppSettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
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
#include <QStackedWidget>
#include <QListWidget>
#include <QMovie>
#include <QPixmap>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QSlider>
#include <QStyle>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QDesktopServices>

const QStringList SymbolHeaderLabels = {
        "", "*", "Type", "Symbol", "Age", "Price", "Pur. $", "Pur. Date"
};

// ── Construction ──────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_providers = {
        new AlphaVantageProvider(this),
        new FinnhubProvider(this),
        new PolygonProvider(this),
        new TwelveDataProvider(this),
        new YahooFinanceProvider(this),
        new FmpProvider(this),
        new YahooPageProvider(this)
    };

    m_providers = ProviderRegistry::instance().validate(m_providers);

    for (StockDataProvider *p : m_providers) {
        connect(p, &StockDataProvider::dataReady,       this, &MainWindow::onDataReady);
        connect(p, &StockDataProvider::errorOccurred,   this, &MainWindow::onError);
        connect(p, &StockDataProvider::symbolTypeReady, this, &MainWindow::onSymbolTypeReady);
    }

    m_cacheManager = new StockCacheManager();

    setupUI(); // creates widgets; helper managers allocated below after widgets exist
    setupMenu();
    loadSettings();
    // Allow the window to be resized freely — without this, QMainWindow enforces the
    // aggregate minimumSizeHint of all children, which grows as chart content changes.
    setMinimumSize(0, 0);
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
    topLayout->setSpacing(8);

    // ── Star filter button ───────────────────────────────────────────────────
    m_starFilterBtn = new QToolButton(topControls);
    m_starFilterBtn->setText("\u2605"); // ★
    m_starFilterBtn->setAutoRaise(true);
    m_starFilterBtn->setFixedSize(26, 24);
    m_starFilterBtn->setToolTip("Filter rows by star color");
    m_starFilterBtn->setPopupMode(QToolButton::InstantPopup);

    auto *starFilterMenu = new QMenu(m_starFilterBtn);
    const QStringList starFilterNames = {"None", "Gold", "Blue", "Green", "Red", "Purple"};
    QAction *noneAct = starFilterMenu->addAction(StockGroupManager::makeStarIcon(0), "None");
    noneAct->setCheckable(true);
    noneAct->setChecked(true);

    QList<QAction*> colorActs;
    for (int i = 1; i < starFilterNames.size(); ++i) {
        QAction *act = starFilterMenu->addAction(StockGroupManager::makeStarIcon(i), starFilterNames[i]);
        act->setCheckable(true);
        colorActs.append(act);
    }
    m_starFilterBtn->setMenu(starFilterMenu);

    connect(noneAct, &QAction::triggered, this, [this, noneAct, colorActs]() {
        m_starFilterIndices.clear();
        for (QAction *a : colorActs) a->setChecked(false);
        noneAct->setChecked(true);
        applyStarFilter();
    });
    for (int i = 0; i < colorActs.size(); ++i) {
        QAction *act = colorActs[i];
        const int starIdx = i + 1;
        connect(act, &QAction::triggered, this, [this, noneAct, act, starIdx](bool checked) {
            if (checked)
                m_starFilterIndices.insert(starIdx);
            else
                m_starFilterIndices.remove(starIdx);
            noneAct->setChecked(m_starFilterIndices.isEmpty());
            applyStarFilter();
        });
    }

    m_autoRefreshCheck = new QCheckBox("Auto", topControls);
    m_autoRefreshCheck->setChecked(true);
    m_autoRefreshCheck->setToolTip("Automatically fetch data once a minute when market is open");

    auto *exportBtn = new QToolButton(topControls);
    exportBtn->setText("\u2B06"); // ⬆ up = export out
    exportBtn->setAutoRaise(true);
    exportBtn->setFixedSize(26, 24);
    exportBtn->setToolTip("Export Stock groups to CSV file");

    auto *importBtn = new QToolButton(topControls);
    importBtn->setText("\u2B07"); // ⬇ down = import in
    importBtn->setAutoRaise(true);
    importBtn->setFixedSize(26, 24);
    importBtn->setToolTip("Import stocks and groups from CSV file");

    auto *addGroupBtn = new QToolButton(topControls);
    addGroupBtn->setText("+");
    addGroupBtn->setFixedSize(26, 24);
    addGroupBtn->setToolTip("Add a new group");

    topLayout->addWidget(m_starFilterBtn);
    topLayout->addWidget(m_autoRefreshCheck);
    topLayout->addWidget(exportBtn);
    topLayout->addWidget(importBtn);
    topLayout->addWidget(addGroupBtn);
    topLayout->addStretch();

    leftLayout->addWidget(topControls);

    m_stockTree = new QTreeWidget(leftPanel);
#ifdef Q_OS_MACOS
    m_stockTree->setStyle(QStyleFactory::create("Fusion"));
#endif
    m_stockTree->setColumnCount(8);
    m_stockTree->setHeaderLabels(SymbolHeaderLabels);
    m_stockTree->setHeaderHidden(false);
    m_stockTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_stockTree->setIndentation(12);
    m_stockTree->setContextMenuPolicy(Qt::CustomContextMenu);

    m_stockTree->header()->setSectionResizeMode(QHeaderView::Fixed);
    m_stockTree->header()->setSectionsClickable(true);
    m_stockTree->header()->setSortIndicatorShown(true);
    m_stockTree->header()->resizeSection(0, 16);
    m_stockTree->header()->resizeSection(1, 32);
    m_stockTree->header()->resizeSection(2, 24);
    m_stockTree->header()->resizeSection(3, 80);
    m_stockTree->header()->resizeSection(4, 50);
    m_stockTree->header()->resizeSection(5, 70);
    m_stockTree->header()->resizeSection(6, 70);
    m_stockTree->header()->resizeSection(7, 100);

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

    // auto *addGroupBtn = new QPushButton("+ Add Group", leftPanel);

    m_leftVSplitter = new QSplitter(Qt::Vertical, leftPanel);
    m_leftVSplitter->setHandleWidth(8);
    m_leftVSplitter->setChildrenCollapsible(false);
    m_leftVSplitter->addWidget(m_stockTree);

    leftLayout->addWidget(m_leftVSplitter, 1);
    // leftLayout->addWidget(addGroupBtn);

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
        m_csvPorter->exportGroups(m_statusLabel, SymbolHeaderLabels);
    });
    connect(importBtn, &QPushButton::clicked, this, [this]() {
        m_csvPorter->importGroups(m_statusLabel, SymbolHeaderLabels);
    });
    connect(m_stockTree, &QTreeWidget::customContextMenuRequested,
            m_groupManager, &StockGroupManager::onTreeContextMenu);
    connect(m_groupManager, &StockGroupManager::forceReloadRequested,
            this, &MainWindow::onForceReload);
    connect(m_groupManager, &StockGroupManager::stockDetailsChanged,
            this, [this](const QString &) {
        const QStringList sel = m_groupManager->selectedSymbols();
        if (!sel.isEmpty()) {
            QMap<QString, double> purPrices;
            for (const QString &sym : sel) {
                const double p = m_groupManager->purchaseInfoForSymbol(sym).first;
                if (p > 0) purPrices[sym] = p;
            }
            m_tableManager->setPurchasePrices(purPrices);
            if (m_purPctBtn) {
                const bool shouldShow = (sel.size() == 1 && purPrices.size() == 1);
                if (!shouldShow && m_purPctBtn->isChecked()) {
                    QSignalBlocker blocker(m_purPctBtn);
                    m_purPctBtn->setChecked(false);
                    m_chartManager->setPurPctMode(false);
                    m_tableManager->setPurPctMode(false);
                }
                m_purPctBtn->setVisible(shouldShow);
            }
            refreshChart(sel);
            m_tableManager->setSeriesColors(m_chartManager->seriesColors());
            m_tableManager->refresh(sel, m_chartManager->clickedDate());
        }
    });
    connect(m_stockTree->header(), &QHeaderView::sectionClicked,
            this, [this](int col) {
                if (col >= 1) m_groupManager->sortByColumn(col);
            });

    // ── Right panel ──────────────────────────────────────────────────────────
    QWidget *rightPanel = new QWidget(m_splitter);
    rightPanel->setMinimumWidth(0); // don't let chart content floor-lock the splitter width
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
    m_statusLabel->setMinimumWidth(0);
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

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

    // Y scale — first on the left
    m_yScaleCombo = new QComboBox(toolbar);
    m_yScaleCombo->addItems({ "auto", "10%", "20%", "30%", "40%", "50%" });
    connect(m_yScaleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        refreshChart(m_groupManager->selectedSymbols());
    });
    tbLayout->addWidget(new QLabel("Y:"));
    tbLayout->addWidget(m_yScaleCombo);

    auto *sep2 = new QFrame(toolbar);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFrameShadow(QFrame::Sunken);
    tbLayout->addWidget(sep2);

    // Period buttons
    m_chartRangeBtnGroup = new QButtonGroup(this);
    m_chartRangeBtnGroup->setExclusive(true);

    m_periodBtnsContainer = new QWidget(toolbar);
    auto *periodBtnsLayout = new QHBoxLayout(m_periodBtnsContainer);
    periodBtnsLayout->setContentsMargins(0, 0, 0, 0);
    periodBtnsLayout->setSpacing(2);
    tbLayout->addWidget(m_periodBtnsContainer);

    connect(m_chartRangeBtnGroup, &QButtonGroup::idClicked, this, [this](int days) {
        AppSettings::instance().setLastChartRangeDays(days);
        m_chartManager->setRangeDays(days);
        const QStringList sel = m_groupManager->selectedSymbols();
        refreshChart(sel);
        m_tableManager->setActivePeriodDays(days);
        m_tableManager->setSeriesColors(m_chartManager->seriesColors());
        m_tableManager->refresh(sel, m_chartManager->clickedDate());
    });

    // % Pur button — after last period button
    static const char *kPeriodBtnStyle =
        "QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 1px 4px; }"
        "QToolButton:checked { background-color: palette(highlight); "
        "color: palette(highlighted-text); border-radius: 3px; }";
    m_purPctBtn = new QToolButton(toolbar);
    m_purPctBtn->setText("% Pur");
    m_purPctBtn->setToolTip("% of purchase price");
    m_purPctBtn->setCheckable(true);
    m_purPctBtn->setAutoRaise(false);
    m_purPctBtn->setStyleSheet(kPeriodBtnStyle);
    m_purPctBtn->setVisible(false); // shown only when 1 stock with purchase price is selected
    tbLayout->addWidget(m_purPctBtn);

    auto *sep3 = new QFrame(toolbar);
    sep3->setFrameShape(QFrame::VLine);
    sep3->setFrameShadow(QFrame::Sunken);
    tbLayout->addWidget(sep3);

    m_browserBtn = new QToolButton(toolbar);
    m_browserBtn->setText("\xF0\x9F\x8C\x90"); // 🌐 globe emoji
    m_browserBtn->setToolTip("Open financial website browser");
    m_browserBtn->setCheckable(true);
    m_browserBtn->setFixedWidth(28);
    connect(m_browserBtn, &QToolButton::clicked, this, &MainWindow::onBrowserToggle);
    tbLayout->addWidget(m_browserBtn);

    m_gearBtn = new QToolButton(toolbar);
    m_gearBtn->setText("\xE2\x9A\x99"); // ⚙ gear
    m_gearBtn->setToolTip("Domain filter / Ad blocker");
    m_gearBtn->setFixedWidth(24);
    m_gearBtn->setVisible(false); // only shown in browser mode
    connect(m_gearBtn, &QToolButton::clicked, this, [this]() {
        m_webBrowser->openAdBlockDialog();
    });
    tbLayout->addWidget(m_gearBtn);

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
    vertSplitter->setHandleWidth(16);
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
    // QChart (a QGraphicsWidget) computes a minimum size from its axis labels, legend, and
    // margins. Without this override it grows over time and locks the window at a large minimum.
    chart->setMinimumSize(QSizeF(1, 1));

    auto *chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->viewport()->installEventFilter(this);
    chartView->setMinimumSize(0, 0);

    m_webBrowser = new WebBrowserWidget();

    m_contentStack = new QStackedWidget(vertSplitter);
    m_contentStack->addWidget(chartView);   // index 0 — chart
    m_contentStack->addWidget(m_webBrowser); // index 1 — browser
    m_contentStack->setCurrentIndex(0);

    // ── Table panel: title + toggle + table ───────────────────────────────────
    auto *tablePanel = new QWidget(vertSplitter);
    tablePanel->setMinimumHeight(0);
    auto *tablePanelLayout = new QVBoxLayout(tablePanel);
    tablePanelLayout->setContentsMargins(4, 4, 4, 0);
    tablePanelLayout->setSpacing(2);

    auto *tableTitle = new QLabel("Stock Performance", tablePanel);
    QFont titleFont = tableTitle->font();
    titleFont.setBold(true);
    tableTitle->setFont(titleFont);
    tablePanelLayout->addWidget(tableTitle);

    auto *displayModeBtn = new QPushButton("% Change", tablePanel);
    displayModeBtn->setCheckable(true);
    tablePanelLayout->addWidget(displayModeBtn, 0, Qt::AlignLeft);

    auto *stockTable = new QTableWidget(tablePanel);
    stockTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    stockTable->setAlternatingRowColors(true);
    stockTable->setSelectionMode(QAbstractItemView::SingleSelection);
    stockTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    stockTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    stockTable->setMinimumHeight(0);
    tablePanelLayout->addWidget(stockTable, 1);

    vertSplitter->addWidget(m_contentStack);
    vertSplitter->addWidget(tablePanel);

    // Embed the toggle button in the splitter handle so it stays visible when collapsed
    auto *splitHandle = vertSplitter->handle(1);
    auto *handleLayout = new QHBoxLayout(splitHandle);
    handleLayout->setContentsMargins(0, 0, 0, 0);
    auto *tableToggleBtn = new QToolButton(splitHandle);
    tableToggleBtn->setText("▼ Table");
    tableToggleBtn->setAutoRaise(true);
    tableToggleBtn->setCursor(Qt::ArrowCursor); // keep arrow cursor over button, not resize
    handleLayout->addWidget(tableToggleBtn);
    // Bottom margin creates a visible gap between the Table handle and the Log handle when collapsed
    vertSplitter->setContentsMargins(0, 0, 0, 8);

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

    // Clear button on the right edge of the Log handle
    auto *logClearBtn = new QToolButton(logHandle);
    logClearBtn->setText("Clear");
    logClearBtn->setAutoRaise(true);
    logClearBtn->setCursor(Qt::ArrowCursor);
    logHandleLayout->addStretch();
    logHandleLayout->addWidget(logClearBtn);
    connect(logClearBtn, &QToolButton::clicked, this, []() { Logger::instance().clear(); });

    connect(&Logger::instance(), &Logger::messageLogged, this, [this](const QString &htmlLine) {
        m_logEdit->append(htmlLine);
        m_logEdit->moveCursor(QTextCursor::End);
    });
    connect(&Logger::instance(), &Logger::cleared, this, [this]() {
        m_logEdit->clear();
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

    connect(m_purPctBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_chartManager->setPurPctMode(checked);
        m_tableManager->setPurPctMode(checked);
        const QStringList sel = m_groupManager->selectedSymbols();
        refreshChart(sel);
        m_tableManager->setSeriesColors(m_chartManager->seriesColors());
        m_tableManager->refresh(sel, m_chartManager->clickedDate());
    });

    // Save table height on splitter drag
    connect(vertSplitter, &QSplitter::splitterMoved,
            m_tableManager, &TableManager::onSplitterMoved);

    // ApiCallTracker is created in loadSettings() once the left panel layout is accessible.
}

void MainWindow::onBrowserToggle()
{
    const bool showBrowser = m_browserBtn->isChecked();
    m_contentStack->setCurrentIndex(showBrowser ? 1 : 0);
    m_gearBtn->setVisible(showBrowser);

    if (showBrowser) {
        const QStringList sel = m_groupManager->selectedSymbols();
        if (!sel.isEmpty())
            m_webBrowser->setSymbol(sel.first());
    }
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
        m_csvPorter->exportGroups(m_statusLabel, SymbolHeaderLabels);
    });
    fileMenu->addAction("Import Groups...", this, [this]() {
        m_csvPorter->importGroups(m_statusLabel, SymbolHeaderLabels);
    });

    QMenu *provMenu = menuBar()->addMenu("Providers");

    m_providerActionGroup = new QActionGroup(this);
    m_providerActionGroup->setExclusive(true);

    for (StockDataProvider *p : m_providers) {
        QAction *action = new QAction(ProviderRegistry::instance().label(p->id()), this);
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

    setWindowTitle("StockChart v" + kAppVersion + " — " + ProviderRegistry::instance().label(p->id()));

    if (!p->hasCredentials())
        m_statusLabel->setText(ProviderRegistry::instance().label(p->id()) + ": API key not set. Use Providers > Configure API Keys...");
    else
        m_statusLabel->setText("Provider: " + ProviderRegistry::instance().label(p->id()) + " — Select stocks to load data.");

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

    const auto flags = dlg.limitedFlags();
    for (auto it = flags.cbegin(); it != flags.cend(); ++it)
        AppSettings::instance().setProviderLimited(it.key(), it.value());

    setActiveProvider(dlg.selectedProviderId());
    saveSettings();
}

// ── Selection & data callbacks ────────────────────────────────────────────────

void MainWindow::onStockSelectionChanged()
{
    const QStringList selected = m_groupManager->selectedSymbols();

    if (selected.isEmpty()) {
        refreshChart({});
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

    // Build purchase-price map and update % Pur button visibility
    {
        QMap<QString, double> purPrices;
        for (const QString &sym : selected) {
            const double p = m_groupManager->purchaseInfoForSymbol(sym).first;
            if (p > 0) purPrices[sym] = p;
        }
        m_tableManager->setPurchasePrices(purPrices);
        if (m_purPctBtn) {
            const bool shouldShow = (selected.size() == 1 && purPrices.size() == 1);
            if (!shouldShow && m_purPctBtn->isChecked()) {
                // Reset mode without triggering a redundant refresh via toggled signal
                QSignalBlocker blocker(m_purPctBtn);
                m_purPctBtn->setChecked(false);
                m_chartManager->setPurPctMode(false);
                m_tableManager->setPurPctMode(false);
            }
            m_purPctBtn->setVisible(shouldShow);
        }
    }

    refreshChart(selected);
    m_tableManager->setSeriesColors(m_chartManager->seriesColors());
    m_tableManager->refresh(selected, m_chartManager->clickedDate());

    if (m_contentStack && m_contentStack->currentIndex() == 1)
        m_webBrowser->setSymbol(selected.first());

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
                if (StockDataProvider *active = activeProvider()) {
                    const QString quoteId = ProviderRegistry::instance().quoteFromId(active->id());
                    StockDataProvider *quoteProvider = active;
                    if (!quoteId.isEmpty() && quoteId != active->id()) {
                        for (StockDataProvider *p : m_providers) {
                            if (p->id() == quoteId) { quoteProvider = p; break; }
                        }
                    }
                    quoteProvider->fetchLatestQuote(symbol);
                }
            }
        }
    }

    refreshChart(selected);
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
        refreshChart(selected);
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

void MainWindow::refreshChart(const QStringList &symbols)
{
    QMap<QString, ChartManager::PurchaseInfo> purInfo;
    for (const QString &sym : symbols) {
        auto [price, date] = m_groupManager->purchaseInfoForSymbol(sym);
        if (price > 0.0 || date.isValid())
            purInfo[sym] = {price, date};
    }
    m_chartManager->setPurchaseInfo(purInfo);
    m_chartManager->updateChart(symbols);
}

void MainWindow::applyFontSize(int pointSize)
{
    QFont f = QApplication::font();
    f.setPointSize(pointSize);
    QApplication::setFont(f);

    if (m_logEdit)
        m_logEdit->setFont(QFont("Courier", pointSize));
}

void MainWindow::applyStarFilter()
{
    if (m_starFilterBtn) {
        if (m_starFilterIndices.isEmpty()) {
            m_starFilterBtn->setStyleSheet("");
        } else {
            m_starFilterBtn->setStyleSheet(
                "QToolButton { background-color: palette(highlight); "
                "color: palette(highlighted-text); border-radius: 3px; }");
        }
    }

    if (!m_stockTree) return;
    for (int g = 0; g < m_stockTree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_stockTree->topLevelItem(g);
        for (int s = 0; s < group->childCount(); ++s) {
            QTreeWidgetItem *stock = group->child(s);
            const int starIdx = stock->data(1, StockGroupManager::StarRole).toInt();
            stock->setHidden(!m_starFilterIndices.isEmpty() && !m_starFilterIndices.contains(starIdx));
        }
    }
}

void MainWindow::rebuildPeriodButtons(const QList<int> &periods)
{
    // Remove and delete all existing buttons
    for (QAbstractButton *btn : m_chartRangeBtnGroup->buttons()) {
        m_chartRangeBtnGroup->removeButton(btn);
        delete btn;
    }

    // Restore the previously selected range (default 60 days)
    const int savedId = AppSettings::instance().lastChartRangeDays();

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
            refreshChart(sel);
    }
    if (selectedId >= 0 && m_tableManager)
        m_tableManager->setActivePeriodDays(selectedId);
}

// ── Settings ──────────────────────────────────────────────────────────────────

void MainWindow::loadSettings()
{
    auto &as = AppSettings::instance();

    for (StockDataProvider *p : m_providers) {
        QMap<QString,QString> creds;
        for (const auto &field : p->credentialFields())
            creds[field.first] = as.providerCredential(p->id(), field.first);
        p->setCredentials(creds);
    }

    // Build the API info panel and add it as the second pane of the left splitter.
    QWidget *leftPanel = m_splitter->widget(0);
    m_apiTracker = new ApiCallTracker(m_providers, leftPanel, this);
    m_apiTracker->loadDailyCallCounts();
    m_leftVSplitter->addWidget(m_apiTracker->panelWidget());

    // Install event filter on provider rows so clicks are caught in eventFilter()
    for (StockDataProvider *p : m_providers)
        if (QWidget *row = m_apiTracker->rowWidget(p->id()))
            row->installEventFilter(this);

    m_tableManager->loadSettings();

    m_autoRefreshCheck->setChecked(as.autoRefresh());
    if (m_yScaleCombo)
        m_yScaleCombo->setCurrentIndex(as.yScaleIndex());

    const int savedFontSize = as.fontPointSize();
    if (savedFontSize > 0)
        applyFontSize(savedFontSize);

    m_logExpanded = as.logExpanded();
    if (m_logToggleBtn)
        m_logToggleBtn->setText(m_logExpanded ? "▲ Log" : "▼ Log");

    // Load the ad-block blacklist into the interceptor BEFORE setActiveProvider(),
    // because setActiveProvider() calls saveSettings() which would otherwise
    // overwrite the saved blacklist with an empty one.
    if (m_webBrowser) m_webBrowser->loadBlacklist();

    // setActiveProvider clears m_cache but immediately reloads it from QSettings,
    // so the cache is populated before loadGroups() runs below.
    const QString savedProvider = as.activeProvider();
    setActiveProvider(savedProvider.isEmpty() ? m_providers.first()->id() : savedProvider);
    m_cacheManager->loadCache();         // second load is harmless; ensures cache is fresh
    m_cacheManager->loadSymbolTypeCache();

    // Load groups AFTER the cache is populated so addStockToGroup() can display
    // Age, Price, and background colours from the very first paint.
    m_groupManager->loadGroups();

    // Restore previously selected symbols (signals are blocked inside selectSymbols,
    // so we trigger onStockSelectionChanged manually afterward).
    const QStringList lastSelected = as.selectedSymbols();
    if (!lastSelected.isEmpty()) {
        m_groupManager->selectSymbols(lastSelected);
        onStockSelectionChanged();
    }

    // Build CSV porter now that group manager is ready
    m_csvPorter = new CsvPorter(m_stockTree, m_groupManager, this);
}

void MainWindow::saveSettings()
{
    auto &as = AppSettings::instance();
    as.setAutoRefresh(m_autoRefreshCheck->isChecked());
    as.setFontPointSize(QApplication::font().pointSize());
    as.setActiveProvider(m_activeProviderId);
    as.setLogExpanded(m_logExpanded);
    as.setYScaleIndex(m_yScaleCombo ? m_yScaleCombo->currentIndex() : 2);
    as.setMainSplitterPos(m_splitter->sizes().value(0, 0));
    as.setOuterSplitterPos(m_outerSplitter->sizes().value(1, 0));
    if (m_leftVSplitter) as.setLeftSplitterPos(m_leftVSplitter->sizes().value(1, 0));
    if (m_groupManager)
        as.setSelectedSymbols(m_groupManager->selectedSymbols());
    m_cacheManager->saveCache();
    if (m_tableManager) m_tableManager->saveSettings();
    if (m_webBrowser)   m_webBrowser->saveBlacklist();
    for (StockDataProvider *p : m_providers) {
        for (const auto &field : p->credentialFields())
            as.setProviderCredential(p->id(), field.first,
                                     p->credentials().value(field.first));
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
            auto &as = AppSettings::instance();

            if (m_tableManager) m_tableManager->restoreTableSplitter();

            // Main horizontal splitter (left panel | chart+table).
            const int leftW = as.mainSplitterPos();
            if (leftW > 0) {
                const int total = m_splitter->width();
                if (total > 0)
                    m_splitter->setSizes({leftW, total - leftW});
            }

            // Left vertical splitter (stock tree | API tracker).
            if (m_leftVSplitter && m_leftVSplitter->count() == 2) {
                const int apiH = as.leftSplitterPos();
                const int total = m_leftVSplitter->height();
                if (apiH > 0 && total > 0)
                    m_leftVSplitter->setSizes({total - apiH, apiH});
            }

            // Outer vertical splitter (chart+table | log).
            if (!m_logExpanded) {
                m_outerSplitter->setSizes({ m_outerSplitter->height(), 0 });
            } else {
                const int logH = as.outerSplitterPos();
                if (logH > 0) {
                    const int total = m_outerSplitter->height();
                    if (total > 0)
                        m_outerSplitter->setSizes({total - logH, logH});
                }
            }
        });
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    AppSettings::instance().sync();
    QMainWindow::closeEvent(event);
}

// ── Help dialog ───────────────────────────────────────────────────────────────

void MainWindow::showHelp()
{
    QDialog dlg(this);
    dlg.setWindowTitle("About StockChart");
    dlg.setMinimumSize(700, 580);

    auto *outerLayout = new QVBoxLayout(&dlg);
    outerLayout->setSpacing(0);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // ── Content row: left nav + right stack ────────────────────────────────
    auto *contentWidget = new QWidget(&dlg);
    auto *contentLayout = new QHBoxLayout(contentWidget);
    contentLayout->setSpacing(0);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // ── Left navigation list ────────────────────────────────────────────────
    auto *navList = new QListWidget(contentWidget);
    navList->setFixedWidth(160);
    navList->setFrameShape(QFrame::NoFrame);
    navList->setStyleSheet(
        "QListWidget {"
        "  background: #f5f5f5;"
        "  border: none;"
        "  border-right: 1px solid #e0e0e0;"
        "}"
        "QListWidget::item {"
        "  height: 38px;"
        "  padding-left: 14px;"
        "  font-size: 13px;"
        "}"
        "QListWidget::item:selected {"
        "  background: #dce8fb;"
        "  color: #1a73e8;"
        "  font-weight: bold;"
        "  border-left: 3px solid #1a73e8;"
        "}");
    navList->addItems({ "About", "Appearance / Data", "Licenses" });

    // ── Right stacked pages ─────────────────────────────────────────────────
    auto *stack = new QStackedWidget(contentWidget);

    // Page 0 ── About ────────────────────────────────────────────────────────
    {
        auto *page = new QWidget();
        auto *vl = new QVBoxLayout(page);
        vl->setContentsMargins(20, 20, 20, 20);
        vl->setSpacing(10);

        auto *appName = new QLabel("<b style='font-size:15pt'>StockChart</b>", page);
        appName->setAlignment(Qt::AlignCenter);
        vl->addWidget(appName);

        auto *logoLabel = new QLabel(page);
        logoLabel->setAlignment(Qt::AlignCenter);
        auto *logoMovie = new QMovie(":/landenlabs.webp", QByteArray(), logoLabel);
        if (logoMovie->isValid()) {
            logoMovie->setScaledSize(QSize(80, 80));
            logoLabel->setMovie(logoMovie);
            logoMovie->start();
        } else {
            QPixmap logo(":/landenlabs.png");
            if (!logo.isNull())
                logoLabel->setPixmap(logo.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        vl->addWidget(logoLabel);

        auto *ghLink = new QLabel(
            "<a href='https://github.com/landenlabs/qt-stockchart'>"
            "https://github.com/landenlabs/qt-stockchart</a>", page);
        ghLink->setAlignment(Qt::AlignCenter);
        ghLink->setOpenExternalLinks(true);
        vl->addWidget(ghLink);

        auto *desc = new QLabel(
            "A Qt6 desktop application for viewing and comparing historical stock performance.\n"
            "Supports multiple data providers, normalized % change charting,\n"
            "and a configurable performance table.", page);
        desc->setAlignment(Qt::AlignCenter);
        desc->setWordWrap(true);
        vl->addWidget(desc);

        auto *ver = new QLabel(
            QString("Version %1   Built: %2 %3").arg(kAppVersion, __DATE__, __TIME__), page);
        ver->setAlignment(Qt::AlignCenter);
        vl->addWidget(ver);

        vl->addStretch();
        stack->addWidget(page);
    }

    // Page 1 ── Appearance / Data ─────────────────────────────────────────────
    {
        auto *page = new QWidget();
        auto *vl = new QVBoxLayout(page);
        vl->setContentsMargins(20, 20, 20, 20);
        vl->setSpacing(12);

        // ── Period buttons ────────────────────────────────────────────────────
        auto *periodsDesc = new QLabel(
            "Add/Remove period buttons (days in the past) to appear over the graph.", page);
        periodsDesc->setWordWrap(true);
        vl->addWidget(periodsDesc);

        auto *periodsBtn = new QPushButton("\u2699 Configure Periods", page);
        periodsBtn->setToolTip("Configure the performance table period columns");
        connect(periodsBtn, &QPushButton::clicked, this, [this]() {
            m_tableManager->configurePeriods();
        });
        vl->addWidget(periodsBtn, 0, Qt::AlignLeft);

        // ── Font size ─────────────────────────────────────────────────────────
        auto *sep = new QFrame(page);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        vl->addWidget(sep);

        const int currentSize = QApplication::font().pointSize();
        auto *fontSizeRow = new QHBoxLayout();
        fontSizeRow->setSpacing(8);
        auto *fontLabel = new QLabel("Font Size", page);
        auto *slider = new QSlider(Qt::Horizontal, page);
        slider->setRange(8, 20);
        slider->setValue(currentSize);
        slider->setTickPosition(QSlider::TicksBelow);
        slider->setTickInterval(2);
        auto *sizeLabel = new QLabel(QString("%1 pt").arg(currentSize), page);
        sizeLabel->setFixedWidth(36);
        sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        fontSizeRow->addWidget(fontLabel);
        fontSizeRow->addWidget(slider, 1);
        fontSizeRow->addWidget(sizeLabel);
        vl->addLayout(fontSizeRow);

        connect(slider, &QSlider::valueChanged, this, [this, sizeLabel](int v) {
            sizeLabel->setText(QString("%1 pt").arg(v));
            applyFontSize(v);
            AppSettings::instance().setFontPointSize(v);
        });

        // ── Settings file ─────────────────────────────────────────────────────
        auto *sep2 = new QFrame(page);
        sep2->setFrameShape(QFrame::HLine);
        sep2->setFrameShadow(QFrame::Sunken);
        vl->addWidget(sep2);

        vl->addWidget(new QLabel("Settings File", page));

        {
            const QString settingsPath = AppSettings::instance().settingsFilePath();
            auto *settingsRow = new QHBoxLayout();
            settingsRow->setSpacing(8);
            auto *settingsEdit = new QLineEdit(settingsPath, page);
            settingsEdit->setReadOnly(true);
            auto *settingsExploreBtn = new QPushButton("Explore", page);
            settingsExploreBtn->setToolTip("Open settings directory in file manager");
            connect(settingsExploreBtn, &QPushButton::clicked, this, [settingsPath]() {
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(QFileInfo(settingsPath).absolutePath()));
            });
            settingsRow->addWidget(settingsEdit, 1);
            settingsRow->addWidget(settingsExploreBtn);
            vl->addLayout(settingsRow);
        }

        // ── Cache directory ───────────────────────────────────────────────────
        auto *sep2b = new QFrame(page);
        sep2b->setFrameShape(QFrame::HLine);
        sep2b->setFrameShadow(QFrame::Sunken);
        vl->addWidget(sep2b);

        vl->addWidget(new QLabel("Cache Directory", page));

        auto *cacheRow = new QHBoxLayout();
        cacheRow->setSpacing(8);
        auto *cacheEdit = new QLineEdit(AppSettings::instance().cacheDirPath(), page);
        auto *browseBtn = new QPushButton("Find...", page);
        cacheRow->addWidget(cacheEdit, 1);
        cacheRow->addWidget(browseBtn);
        vl->addLayout(cacheRow);

        connect(browseBtn, &QPushButton::clicked, this, [this, cacheEdit]() {
            const QString path = QFileDialog::getExistingDirectory(
                this, "Select Cache Directory", cacheEdit->text());
            if (!path.isEmpty()) {
                cacheEdit->setText(path);
                AppSettings::instance().setCacheDirPath(path);
            }
        });
        connect(cacheEdit, &QLineEdit::editingFinished, cacheEdit, [cacheEdit]() {
            AppSettings::instance().setCacheDirPath(cacheEdit->text());
        });

        // ── Cache summary table ───────────────────────────────────────────────
        auto *sep3 = new QFrame(page);
        sep3->setFrameShape(QFrame::HLine);
        sep3->setFrameShadow(QFrame::Sunken);
        vl->addWidget(sep3);

        auto *cacheHeaderRow = new QHBoxLayout();
        cacheHeaderRow->setSpacing(8);
        cacheHeaderRow->addWidget(new QLabel("Cache Files (up to 30)", page));
        auto *exploreBtn = new QPushButton("Explore", page);
        exploreBtn->setToolTip("Open cache directory in file manager");
        connect(exploreBtn, &QPushButton::clicked, this, [cacheEdit]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(cacheEdit->text()));
        });
        cacheHeaderRow->addWidget(exploreBtn);
        cacheHeaderRow->addStretch();
        vl->addLayout(cacheHeaderRow);

        auto *cacheTable = new QTableWidget(0, 4, page);
        cacheTable->setHorizontalHeaderLabels({ "File", "Start Date", "End Date", "#Rows" });
        cacheTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        cacheTable->setSelectionMode(QAbstractItemView::NoSelection);
        cacheTable->verticalHeader()->setVisible(false);
        cacheTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        cacheTable->setShowGrid(true);
        cacheTable->setAlternatingRowColors(true);

        {
            const QString cacheDir = AppSettings::instance().cacheDirPath();
            QDir dir(cacheDir);
            QStringList csvFiles = dir.entryList({ "*.csv" }, QDir::Files, QDir::Name);
            csvFiles.removeAll("index.csv");
            if (csvFiles.size() > 30)
                csvFiles = csvFiles.mid(0, 30);

            cacheTable->setRowCount(csvFiles.size());

            for (int r = 0; r < csvFiles.size(); ++r) {
                QFile f(cacheDir + "/" + csvFiles[r]);
                QString startStr, endStr;
                int rowCount = 0;

                if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QTextStream in(&f);
                    in.setLocale(QLocale::c());
                    QString firstLine, lastLine;
                    while (!in.atEnd()) {
                        const QString line = in.readLine().trimmed();
                        if (line.isEmpty()) continue;
                        if (firstLine.isEmpty()) firstLine = line;
                        lastLine = line;
                        ++rowCount;
                    }

                    auto epochFromLine = [](const QString &line) -> qint64 {
                        const int comma = line.indexOf(',');
                        if (comma < 0) return -1;
                        bool ok;
                        const qint64 v = line.left(comma).toLongLong(&ok);
                        return ok ? v : -1;
                    };

                    const qint64 s = epochFromLine(firstLine);
                    const qint64 e = epochFromLine(lastLine);
                    if (s >= 0)
                        startStr = QDateTime::fromSecsSinceEpoch(s).toLocalTime()
                                       .toString("dd-MMM-yyyy hh:mm");
                    if (e >= 0)
                        endStr = QDateTime::fromSecsSinceEpoch(e).toLocalTime()
                                     .toString("dd-MMM-yyyy hh:mm");
                }

                cacheTable->setItem(r, 0, new QTableWidgetItem(csvFiles[r]));
                cacheTable->setItem(r, 1, new QTableWidgetItem(startStr));
                cacheTable->setItem(r, 2, new QTableWidgetItem(endStr));
                auto *rowsItem = new QTableWidgetItem(QString::number(rowCount));
                rowsItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                cacheTable->setItem(r, 3, rowsItem);
            }
        }

        vl->addWidget(cacheTable, 1);
        stack->addWidget(page);
    }

    // Page 2 ── Licenses / Providers ─────────────────────────────────────────
    {
        auto *page = new QWidget();
        auto *vl = new QVBoxLayout(page);
        vl->setContentsMargins(20, 20, 20, 20);

        auto *table = new QTableWidget(m_providers.size(), 5, page);
        table->setHorizontalHeaderLabels({ "", "", "Provider", "API Key", "URL" });
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        table->horizontalHeader()->resizeSection(0, 28);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        table->horizontalHeader()->resizeSection(1, 28);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setShowGrid(true);
        table->setAlternatingRowColors(true);

        for (int r = 0; r < m_providers.size(); ++r) {
            StockDataProvider *p = m_providers[r];

            // Column 0: trash button — clears API key for this provider
            auto *trashBtn = new QToolButton(page);
            trashBtn->setIcon(QApplication::style()->standardIcon(QStyle::SP_TrashIcon));
            trashBtn->setAutoRaise(true);
            trashBtn->setToolTip("Clear API key for " + ProviderRegistry::instance().label(p->id()));
            connect(trashBtn, &QToolButton::clicked, this, [this, p, r, table]() {
                if (QMessageBox::question(this, "Clear Credentials",
                        QString("Clear API key for %1?").arg(ProviderRegistry::instance().label(p->id())))
                        != QMessageBox::Yes) return;
                QMap<QString,QString> creds = p->credentials();
                for (const auto &field : p->credentialFields())
                    creds[field.first] = QString();
                p->setCredentials(creds);
                saveSettings();
                if (auto *le = qobject_cast<QLineEdit*>(table->cellWidget(r, 3)))
                    le->setText(QString());
            });
            auto *trashCell = new QWidget(page);
            auto *trashLayout = new QHBoxLayout(trashCell);
            trashLayout->setContentsMargins(2, 0, 2, 0);
            trashLayout->setAlignment(Qt::AlignCenter);
            trashLayout->addWidget(trashBtn);
            table->setCellWidget(r, 0, trashCell);

            // Column 1: gear button — opens provider configuration dialog
            auto *gearBtn = new QToolButton(page);
            gearBtn->setText("\u2699");
            gearBtn->setAutoRaise(true);
            gearBtn->setToolTip("Configure " + ProviderRegistry::instance().label(p->id()));
            connect(gearBtn, &QToolButton::clicked, this, [this, p]() {
                SettingsDialog dlg(m_providers, p->id(), this);
                if (dlg.exec() != QDialog::Accepted) return;
                const auto allCreds = dlg.allCredentials();
                for (StockDataProvider *pr : m_providers)
                    if (allCreds.contains(pr->id()))
                        pr->setCredentials(allCreds[pr->id()]);
                const auto flags = dlg.limitedFlags();
                for (auto it = flags.cbegin(); it != flags.cend(); ++it)
                    AppSettings::instance().setProviderLimited(it.key(), it.value());
                setActiveProvider(dlg.selectedProviderId());
                saveSettings();
            });
            auto *gearCell = new QWidget(page);
            auto *gearLayout = new QHBoxLayout(gearCell);
            gearLayout->setContentsMargins(2, 0, 2, 0);
            gearLayout->setAlignment(Qt::AlignCenter);
            gearLayout->addWidget(gearBtn);
            table->setCellWidget(r, 1, gearCell);

            // Column 2: provider name (read-only)
            auto *nameItem = new QTableWidgetItem(ProviderRegistry::instance().label(p->id()));
            nameItem->setFlags(Qt::ItemIsEnabled);
            table->setItem(r, 2, nameItem);

            // Column 3: API key — editable QLineEdit
            const auto fields = p->credentialFields();
            QString keyVal;
            if (!fields.isEmpty())
                keyVal = p->credentials().value(fields.first().first);
            auto *keyEdit = new QLineEdit(keyVal, page);
            keyEdit->setPlaceholderText("Enter API key...");
            keyEdit->setFrame(false);
            connect(keyEdit, &QLineEdit::editingFinished, this, [this, p, keyEdit]() {
                const auto flds = p->credentialFields();
                if (flds.isEmpty()) return;
                QMap<QString,QString> creds = p->credentials();
                creds[flds.first().first] = keyEdit->text().trimmed();
                p->setCredentials(creds);
                saveSettings();
            });
            table->setCellWidget(r, 3, keyEdit);

            // Column 4: signup URL
            const QString url = ProviderRegistry::instance().url(p->id());
            if (!url.isEmpty()) {
                auto *urlLabel = new QLabel(
                    QString("<a href='%1'>%1</a>").arg(url), page);
                urlLabel->setOpenExternalLinks(true);
                urlLabel->setContentsMargins(4, 0, 4, 0);
                table->setCellWidget(r, 4, urlLabel);
            } else {
                auto *urlItem = new QTableWidgetItem(QString());
                urlItem->setFlags(Qt::ItemIsEnabled);
                table->setItem(r, 4, urlItem);
            }
        }
        table->resizeRowsToContents();

        vl->addWidget(table);
        stack->addWidget(page);
    }

    contentLayout->addWidget(navList);
    contentLayout->addWidget(stack, 1);
    outerLayout->addWidget(contentWidget, 1);

    // ── Close button ─────────────────────────────────────────────────────────
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    buttons->setContentsMargins(12, 4, 12, 8);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);
    outerLayout->addWidget(buttons);

    QObject::connect(navList, &QListWidget::currentRowChanged,
                     stack,   &QStackedWidget::setCurrentIndex);
    navList->setCurrentRow(0);

    dlg.exec();
}
