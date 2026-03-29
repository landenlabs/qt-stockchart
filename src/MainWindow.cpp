#include "MainWindow.h"
#include "SettingsDialog.h"
#include "AlphaVantageProvider.h"
#include "FinnhubProvider.h"
#include "PolygonProvider.h"
#include "TwelveDataProvider.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QLineSeries>
#include <QDateTimeAxis>
#include <QValueAxis>
#include <QPainter>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QSettings>
#include <QInputDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QDate>
#include <QGraphicsLineItem>
#include <QMouseEvent>
#include <QPen>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QTimeZone>
#include <QPixmap>
#include <QPainter>
#include <QStyle>
#include <QApplication>
#include <QAreaSeries>
#include <QLegendMarker>
#include <cmath>
#include <limits>

static const QStringList kDefaultStocks = {
    "AAPL", "GOOGL", "MSFT", "AMZN", "TSLA",
    "META", "NVDA", "NFLX", "JPM",  "UBER"
};
static const QList<int> kDefaultPeriods = { -365, -90, -60, -30, -7, 0 };

// Returns the last closing price on or before `target` in ascending-sorted data.
// Returns NaN if no data point exists at or before that date.
static double priceAt(const QVector<StockDataPoint> &data, const QDate &target)
{
    double result = std::numeric_limits<double>::quiet_NaN();
    for (const StockDataPoint &pt : data) {
        if (pt.timestamp.date() <= target)
            result = pt.price; // ascending order: keep updating
        else
            break;
    }
    return result;
}

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

    setupUI();
    setupMenu();
    loadSettings();
}

// ── UI setup ──────────────────────────────────────────────────────────────────

void MainWindow::setupUI()
{
    setWindowTitle("Stock Chart");
    resize(1200, 720);

    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    m_splitter = new QSplitter(Qt::Horizontal, central);

    // ── Left panel ───────────────────────────────────────────────────────────
    QWidget *leftPanel = new QWidget(m_splitter);
    leftPanel->setMinimumWidth(150);
    leftPanel->setMaximumWidth(220);

    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);

    // Two-column tree: symbol | latest price
    m_stockTree = new QTreeWidget(leftPanel);
    m_stockTree->setColumnCount(2);
    m_stockTree->setHeaderHidden(true);
    m_stockTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_stockTree->setIndentation(16);
    m_stockTree->setContextMenuPolicy(Qt::CustomContextMenu);
    // Layout columns: name stretches, price sized to contents
    m_stockTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_stockTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(m_stockTree, &QTreeWidget::itemSelectionChanged,
            this, &MainWindow::onStockSelectionChanged);
    connect(m_stockTree, &QTreeWidget::customContextMenuRequested,
            this, &MainWindow::onTreeContextMenu);

    m_addGroupBtn = new QPushButton("+ Add Group", leftPanel);
    connect(m_addGroupBtn, &QPushButton::clicked, this, &MainWindow::onAddGroupClicked);

    leftLayout->addWidget(m_stockTree, 1);
    leftLayout->addWidget(m_addGroupBtn);
    setupApiInfoPanel(leftPanel, leftLayout);

    // ── Right panel (toolbar + vertical splitter) ─────────────────────────
    QWidget *rightPanel = new QWidget(m_splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    setupRightPanel(rightPanel, rightLayout);

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
    // ── Table toolbar (always visible) ────────────────────────────────────
    QWidget *toolbar = new QWidget(parent);
    toolbar->setFixedHeight(30);
    auto *tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(6, 2, 6, 2);
    tbLayout->setSpacing(4);

    // State button group: Collapse | Half | Full
    m_tableStateBtnGroup = new QButtonGroup(this);
    m_tableStateBtnGroup->setExclusive(true);

    const QStringList stateLabels = { "▲ Collapse", "⊡ Half", "▼ Full" };
    for (int i = 0; i < 3; ++i) {
        auto *btn = new QToolButton(toolbar);
        btn->setText(stateLabels[i]);
        btn->setCheckable(true);
        btn->setAutoRaise(true);
        m_tableStateBtnGroup->addButton(btn, i);
        tbLayout->addWidget(btn);
    }
    m_tableStateBtnGroup->button(0)->setChecked(true); // Collapsed default

    connect(m_tableStateBtnGroup, &QButtonGroup::idClicked,
            this, &MainWindow::onTableStateChanged);

    // Separator
    auto *sep = new QFrame(toolbar);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    tbLayout->addWidget(sep);

    // Display mode toggle
    m_displayModeBtn = new QPushButton("Price", toolbar);
    m_displayModeBtn->setCheckable(true);
    m_displayModeBtn->setFixedWidth(80);
    connect(m_displayModeBtn, &QPushButton::toggled,
            this, &MainWindow::onToggleDisplayMode);
    tbLayout->addWidget(m_displayModeBtn);

    // Periods config
    auto *periodsBtn = new QPushButton("⚙ Periods", toolbar);
    connect(periodsBtn, &QPushButton::clicked, this, &MainWindow::configurePeriods);
    tbLayout->addWidget(periodsBtn);

    // Separator
    auto *sep2 = new QFrame(toolbar);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFrameShadow(QFrame::Sunken);
    tbLayout->addWidget(sep2);

    // Chart range button group — order matches table periods: 60d | 30d | 7d | Today
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
    m_chartRangeBtnGroup->button(0)->setChecked(true); // Today = all data
    connect(m_chartRangeBtnGroup, &QButtonGroup::idClicked,
            this, &MainWindow::onChartRangeChanged);

    tbLayout->addStretch();

    // Help button — far right
    auto *helpBtn = new QToolButton(toolbar);
    helpBtn->setText("?");
    helpBtn->setToolTip("About / Help");
    helpBtn->setFixedWidth(24);
    connect(helpBtn, &QToolButton::clicked, this, &MainWindow::showHelp);
    tbLayout->addWidget(helpBtn);

    layout->addWidget(toolbar);

    // ── Thin separator ────────────────────────────────────────────────────
    auto *hline = new QFrame(parent);
    hline->setFrameShape(QFrame::HLine);
    hline->setFrameShadow(QFrame::Sunken);
    layout->addWidget(hline);

    // ── Vertical splitter: chart (top) | table (bottom) ───────────────────
    m_vertSplitter = new QSplitter(Qt::Vertical, parent);
    m_vertSplitter->setChildrenCollapsible(true);

    m_chart = new QChart();
    m_chart->setAnimationOptions(QChart::SeriesAnimations);
    m_chart->setMargins(QMargins(4, 4, 4, 4));
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chart->legend()->setVisible(true);
    m_chartView = new QChartView(m_chart, m_vertSplitter);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->viewport()->installEventFilter(this);
    connect(m_chart, &QChart::plotAreaChanged, this, &MainWindow::updateCrosshair);

    m_stockTable = new QTableWidget(m_vertSplitter);
    m_stockTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stockTable->setAlternatingRowColors(true);
    m_stockTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_stockTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_stockTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_stockTable->hide(); // start collapsed
    connect(m_stockTable->horizontalHeader(), &QHeaderView::sectionClicked,
            this, &MainWindow::onTableColumnClicked);

    m_vertSplitter->addWidget(m_chartView);
    m_vertSplitter->addWidget(m_stockTable);

    layout->addWidget(m_vertSplitter, 1);
}

void MainWindow::setupMenu()
{
    QMenu *fileMenu = menuBar()->addMenu("File");
    fileMenu->addAction("Export Groups...", this, &MainWindow::exportGroups);
    fileMenu->addAction("Import Groups...", this, &MainWindow::importGroups);

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

// ── Table state ───────────────────────────────────────────────────────────────

void MainWindow::onTableStateChanged(int id)
{
    setTableState(id);
}

void MainWindow::setTableState(int stateId)
{
    m_tableStateId = stateId;
    m_tableStateBtnGroup->button(stateId)->setChecked(true);

    switch (stateId) {
    case 0: // Collapsed
        m_stockTable->hide();
        m_chartView->show();
        break;
    case 1: { // Half
        m_chartView->show();
        m_stockTable->show();
        const int h = m_vertSplitter->height();
        m_vertSplitter->setSizes({ h / 2, h / 2 });
        refreshTable();
        break;
    }
    case 2: // Full
        m_chartView->hide();
        m_stockTable->show();
        refreshTable();
        break;
    }

    saveTableSettings();
}

void MainWindow::onToggleDisplayMode(bool checked)
{
    m_showPercentChange = checked;
    m_displayModeBtn->setText(checked ? "% Change" : "Price");
    refreshTable();
    saveTableSettings();
}

// ── Table content ─────────────────────────────────────────────────────────────

void MainWindow::refreshTable()
{
    if (m_tableStateId == 0) return; // collapsed — skip

    const QStringList syms     = selectedSymbols();
    const QDate       today    = QDate::currentDate();
    const int         nPeriods = m_periods.size();
    const bool        hasClick = m_clickedDate.isValid();
    const int         nCols    = nPeriods + (hasClick ? 1 : 0);
    const int         nRows    = syms.size();

    // Clamp reference column to valid range
    if (nCols > 0 && m_refColIndex >= nCols)
        m_refColIndex = 0;

    m_stockTable->setRowCount(nRows);
    m_stockTable->setColumnCount(nCols);

    const QColor refBg(210, 228, 255);

    // Helper: date for column c
    auto colDate = [&](int c) -> QDate {
        return c < nPeriods ? today.addDays(m_periods[c]) : m_clickedDate;
    };

    // Column headers
    for (int c = 0; c < nPeriods; ++c) {
        int days = m_periods[c];
        QString label = (days == 0) ? "Today" : QString("%1d").arg(days);
        auto *hdr = new QTableWidgetItem(label);
        if (m_showPercentChange && c == m_refColIndex)
            hdr->setBackground(refBg);
        m_stockTable->setHorizontalHeaderItem(c, hdr);
    }
    if (hasClick) {
        auto *hdr = new QTableWidgetItem(m_clickedDate.toString("MMM d"));
        if (m_showPercentChange && nPeriods == m_refColIndex)
            hdr->setBackground(refBg);
        m_stockTable->setHorizontalHeaderItem(nPeriods, hdr);
    }

    // Pointer-hand cursor on header in % mode (click to set reference column)
    m_stockTable->horizontalHeader()->setCursor(
        m_showPercentChange ? Qt::PointingHandCursor : Qt::ArrowCursor);

    // Row headers + cells
    for (int r = 0; r < nRows; ++r) {
        const QString &sym = syms[r];
        m_stockTable->setVerticalHeaderItem(r, new QTableWidgetItem(sym));

        const bool hasCached = m_cache.contains(sym) && !m_cache[sym].isEmpty();

        // Base price from reference column (% change mode only)
        double basePrice = std::numeric_limits<double>::quiet_NaN();
        if (hasCached && m_showPercentChange && nCols > 0)
            basePrice = priceAt(m_cache[sym], colDate(m_refColIndex));

        for (int c = 0; c < nCols; ++c) {
            QTableWidgetItem *cell;

            if (!hasCached) {
                cell = new QTableWidgetItem("…");
                cell->setForeground(Qt::gray);
            } else {
                double price = priceAt(m_cache[sym], colDate(c));

                if (std::isnan(price)) {
                    cell = new QTableWidgetItem("N/A");
                    cell->setForeground(Qt::gray);
                } else if (m_showPercentChange && c == m_refColIndex) {
                    // Reference column: show base price as anchor
                    cell = new QTableWidgetItem(QString("$%1").arg(price, 0, 'f', 2));
                } else if (m_showPercentChange) {
                    if (std::isnan(basePrice) || basePrice == 0.0) {
                        cell = new QTableWidgetItem("N/A");
                        cell->setForeground(Qt::gray);
                    } else {
                        double pct = (price / basePrice - 1.0) * 100.0;
                        QString text = QString("%1%2%")
                                           .arg(pct >= 0 ? "+" : "")
                                           .arg(pct, 0, 'f', 2);
                        cell = new QTableWidgetItem(text);
                        if      (pct > 0) cell->setForeground(QColor("#2e7d32"));
                        else if (pct < 0) cell->setForeground(QColor("#c62828"));
                    }
                } else {
                    cell = new QTableWidgetItem(QString("$%1").arg(price, 0, 'f', 2));
                }
            }

            cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            cell->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (m_showPercentChange && c == m_refColIndex)
                cell->setBackground(refBg);
            m_stockTable->setItem(r, c, cell);
        }
    }
}

// ── Period configuration ──────────────────────────────────────────────────────

void MainWindow::configurePeriods()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Configure Periods");
    dlg.setMinimumWidth(280);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel("Day offsets from today (0 = today, negative = past):", &dlg));

    auto *list = new QListWidget(&dlg);
    for (int p : m_periods)
        list->addItem(QString::number(p));
    layout->addWidget(list);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton("Add...", &dlg);
    auto *removeBtn = new QPushButton("Remove", &dlg);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(removeBtn);
    layout->addLayout(btnRow);

    connect(addBtn, &QPushButton::clicked, &dlg, [&]() {
        bool ok;
        int offset = QInputDialog::getInt(&dlg, "Add Period",
            "Day offset (e.g. -30 for 30 days ago, 0 for today):",
            -30, -3650, 0, 1, &ok);
        if (!ok) return;
        // Avoid duplicates
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->text().toInt() == offset) return;
        list->addItem(QString::number(offset));
    });

    connect(removeBtn, &QPushButton::clicked, &dlg, [&]() {
        delete list->takeItem(list->currentRow());
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    QList<int> newPeriods;
    for (int i = 0; i < list->count(); ++i)
        newPeriods << list->item(i)->text().toInt();
    std::sort(newPeriods.begin(), newPeriods.end());

    if (newPeriods.isEmpty()) return;
    m_periods = newPeriods;
    saveTableSettings();
    refreshTable();
}

// ── Crosshair ─────────────────────────────────────────────────────────────────

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_chartView->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            QPointF scenePos = m_chartView->mapToScene(me->pos());
            QPointF chartPos = m_chart->mapFromScene(scenePos);
            onChartClicked(chartPos);
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onChartClicked(const QPointF &chartPos)
{
    if (m_chart->series().isEmpty()) return;
    if (!m_chart->plotArea().contains(chartPos)) return;

    QPointF value        = m_chart->mapToValue(chartPos);
    qint64  clickedMsecs = static_cast<qint64>(value.x());

    // Snap to nearest cached data point
    qint64 bestMsecs = -1;
    qint64 bestDiff  = std::numeric_limits<qint64>::max();
    for (const auto &data : std::as_const(m_cache)) {
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
    refreshTable();
}

void MainWindow::updateZeroLine()
{
    const auto vertAxes = m_chart->axes(Qt::Vertical);
    const auto horizAxes = m_chart->axes(Qt::Horizontal);
    if (vertAxes.isEmpty() || horizAxes.isEmpty() || m_chart->series().isEmpty()) {
        if (m_zeroLine) m_zeroLine->setVisible(false);
        return;
    }

    auto *axisY = qobject_cast<QValueAxis*>(vertAxes.first());
    if (!axisY || axisY->min() > 0.0 || axisY->max() < 0.0) {
        if (m_zeroLine) m_zeroLine->setVisible(false);
        return;
    }

    if (!m_zeroLine) {
        m_zeroLine = new QGraphicsLineItem(m_chart);
        QPen pen(QColor(90, 90, 90, 210));
        pen.setWidthF(1.2);
        m_zeroLine->setPen(pen);
        m_zeroLine->setZValue(8); // above fill areas, below crosshair
    }

    auto *axisX = qobject_cast<QDateTimeAxis*>(horizAxes.first());
    const double midMsecs = axisX
        ? (axisX->min().toMSecsSinceEpoch() + axisX->max().toMSecsSinceEpoch()) / 2.0
        : 0.0;

    const QRectF  plotArea = m_chart->plotArea();
    const QPointF zeroPos  = m_chart->mapToPosition(QPointF(midMsecs, 0.0));
    m_zeroLine->setLine(plotArea.left(), zeroPos.y(), plotArea.right(), zeroPos.y());
    m_zeroLine->setVisible(true);
}

void MainWindow::updateCrosshair()
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

void MainWindow::onTableColumnClicked(int col)
{
    if (!m_showPercentChange) return;
    m_refColIndex = col;
    refreshTable();
}

void MainWindow::onChartRangeChanged(int days)
{
    m_chartRangeDays = days;
    updateChart(selectedSymbols());
}

// ── Group helpers ─────────────────────────────────────────────────────────────

QTreeWidgetItem *MainWindow::addGroup(const QString &name, bool expanded)
{
    auto *item = new QTreeWidgetItem(m_stockTree);
    item->setFlags(Qt::ItemIsEnabled);
    item->setData(0, Qt::UserRole, name);

    auto *container = new QWidget;
    container->setStyleSheet("background: transparent;");
    auto *hl = new QHBoxLayout(container);
    hl->setContentsMargins(0, 1, 4, 1);
    hl->setSpacing(2);

    auto *nameLabel = new QLabel(name, container);
    QFont f = nameLabel->font();
    f.setBold(true);
    nameLabel->setFont(f);

    auto *addBtn = new QPushButton("+", container);
    addBtn->setFixedSize(18, 18);
    addBtn->setFlat(true);
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setToolTip("Add stock to " + name);

    hl->addWidget(nameLabel, 1);
    hl->addWidget(addBtn);
    m_stockTree->setItemWidget(item, 0, container);

    connect(addBtn, &QPushButton::clicked, this, [this, item]() {
        showAddStockDialog(item);
    });

    item->setExpanded(expanded);
    return item;
}

void MainWindow::addStockToGroup(QTreeWidgetItem *groupItem, const QString &symbol)
{
    // Two-column item: column 0 = symbol, column 1 = latest price (if cached)
    auto *item = new QTreeWidgetItem(groupItem);
    item->setText(0, symbol);
    item->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    // Set price and tint if cached
    if (m_cache.contains(symbol) && !m_cache[symbol].isEmpty()) {
        const double latest = m_cache[symbol].last().price;
        item->setText(1, QString("$%1").arg(latest, 0, 'f', 2));
        const QColor cachedBg(230, 245, 230); // light green
        item->setBackground(0, QBrush(cachedBg));
        item->setBackground(1, QBrush(cachedBg));
    } else {
        item->setText(1, QString());
    }

    // Apply cached icon immediately if available
    if (m_symbolErrors.contains(symbol))
        item->setIcon(0, makeErrorIcon());
    else if (m_symbolTypes.contains(symbol))
        item->setIcon(0, makeTypeIcon(m_symbolTypes[symbol]));
}

// ── Add stock dialog ──────────────────────────────────────────────────────────

void MainWindow::showAddStockDialog(QTreeWidgetItem *groupItem)
{
    const QString groupName = groupItem->data(0, Qt::UserRole).toString();

    QDialog dlg(this);
    dlg.setWindowTitle("Add Stocks — " + groupName);
    dlg.setMinimumWidth(340);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel("Enter one or more symbols, comma-separated:", &dlg));

    auto *edit = new QLineEdit(&dlg);
    edit->setPlaceholderText("e.g.  AAPL, MSFT, TSLA");
    layout->addWidget(edit);

    auto *statusLbl = new QLabel(&dlg);
    statusLbl->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(statusLbl);

    auto *buttons = new QDialogButtonBox(&dlg);
    auto *addBtn   = buttons->addButton("Add",   QDialogButtonBox::ActionRole);
    auto *closeBtn = buttons->addButton("Close", QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);

    auto doAdd = [&]() {
        const QStringList parts = edit->text().split(',', Qt::SkipEmptyParts);
        QStringList added, skipped;
        for (const QString &part : parts) {
            const QString sym = part.trimmed().toUpper();
            if (sym.isEmpty()) continue;
            bool exists = false;
            for (int i = 0; i < groupItem->childCount(); ++i)
                if (groupItem->child(i)->text(0) == sym) { exists = true; break; }
            if (exists) skipped << sym;
            else { addStockToGroup(groupItem, sym); added << sym; }
        }
        if (!added.isEmpty()) { groupItem->setExpanded(true); saveGroups(); }
        QString msg;
        if (!added.isEmpty())   msg += "Added: " + added.join(", ");
        if (!skipped.isEmpty()) msg += QString(msg.isEmpty() ? "" : "  ·  ") + "Already in group: " + skipped.join(", ");
        statusLbl->setText(msg);
        edit->clear();
        edit->setFocus();
    };

    connect(addBtn,   &QPushButton::clicked, this, doAdd);
    connect(edit,     &QLineEdit::returnPressed, this, doAdd);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    edit->setFocus();
    dlg.exec();
}

// ── Group slots ───────────────────────────────────────────────────────────────

void MainWindow::onAddGroupClicked()
{
    bool ok;
    QString name = QInputDialog::getText(this, "Add Group",
                                         "Group name:", QLineEdit::Normal, "", &ok);
    name = name.trimmed();
    if (!ok || name.isEmpty()) return;
    addGroup(name, true);
    saveGroups();
}

void MainWindow::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_stockTree->itemAt(pos);
    QMenu menu(this);

    if (!item) {
        menu.addAction("Add Group...", this, &MainWindow::onAddGroupClicked);
    } else if (!item->parent()) {
        const QString groupName = item->data(0, Qt::UserRole).toString();
        menu.addAction("Add Stock...", this, [this, item]() { showAddStockDialog(item); });
        menu.addSeparator();
        menu.addAction("Delete Group", this, [this, item, groupName]() {
            if (QMessageBox::question(this, "Delete Group",
                    QString("Delete group \"%1\" and all its stocks?").arg(groupName))
                    != QMessageBox::Yes) return;
            delete item;
            saveGroups();
        });
    } else {
        menu.addAction("Remove Stock", this, [this, item]() {
            delete item;
            saveGroups();
        });
    }

    menu.exec(m_stockTree->viewport()->mapToGlobal(pos));
}

// ── Persistence ───────────────────────────────────────────────────────────────

void MainWindow::loadGroups()
{
    QSettings s("StockChart", "StockChart");
    int count = s.beginReadArray("stockGroups");

    if (count == 0) {
        s.endArray();
        QTreeWidgetItem *fav = addGroup("Favorites", true);
        for (const QString &sym : kDefaultStocks) addStockToGroup(fav, sym);
        saveGroups();
        return;
    }

    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        QTreeWidgetItem *group = addGroup(s.value("name").toString(),
                                          s.value("expanded", true).toBool());
        for (const QString &sym : s.value("stocks").toStringList())
            addStockToGroup(group, sym);
    }
    s.endArray();
}

void MainWindow::saveGroups()
{
    QSettings s("StockChart", "StockChart");
    s.remove("stockGroups");
    s.beginWriteArray("stockGroups");
    for (int i = 0; i < m_stockTree->topLevelItemCount(); ++i) {
        s.setArrayIndex(i);
        QTreeWidgetItem *group = m_stockTree->topLevelItem(i);
        s.setValue("name",     group->data(0, Qt::UserRole).toString());
        s.setValue("expanded", group->isExpanded());
        QStringList stocks;
        for (int j = 0; j < group->childCount(); ++j)
            stocks << group->child(j)->text(0);
        s.setValue("stocks", stocks);
    }
    s.endArray();
}

void MainWindow::loadTableSettings()
{
    QSettings s("StockChart", "StockChart");

    // Periods
    QVariantList vl = s.value("tablePeriods").toList();
    if (vl.isEmpty()) {
        m_periods = kDefaultPeriods;
    } else {
        m_periods.clear();
        for (const QVariant &v : vl) m_periods << v.toInt();
    }

    m_showPercentChange = s.value("tableShowPercent", false).toBool();
    m_displayModeBtn->setChecked(m_showPercentChange);
    m_displayModeBtn->setText(m_showPercentChange ? "% Change" : "Price");

    // Table state — apply after widget is shown
    int stateId = s.value("tableState", 0).toInt();
    setTableState(stateId);
}

void MainWindow::saveTableSettings()
{
    QSettings s("StockChart", "StockChart");
    QVariantList vl;
    for (int p : m_periods) vl << p;
    s.setValue("tablePeriods",    vl);
    s.setValue("tableShowPercent", m_showPercentChange);
    s.setValue("tableState",      m_tableStateId);
}

// ── Provider management ───────────────────────────────────────────────────────

void MainWindow::setActiveProvider(const QString &id)
{
    m_activeProviderId = id;
    m_cache.clear();

    for (QAction *action : m_providerActionGroup->actions())
        action->setChecked(action->data().toString() == id);

    StockDataProvider *p = activeProvider();
    if (!p) return;

    setWindowTitle("Stock Chart — " + p->displayName());

    if (!p->hasCredentials())
        m_statusLabel->setText(p->displayName() + ": API key not set. Use Providers > Configure API Keys...");
    else
        m_statusLabel->setText("Provider: " + p->displayName() + " — Select stocks to load data.");

    updateApiInfoPanel();
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

// ── Selection & chart ─────────────────────────────────────────────────────────

QStringList MainWindow::selectedSymbols() const
{
    QStringList syms;
    for (const QTreeWidgetItem *item : m_stockTree->selectedItems())
        if (item->parent())
            syms << item->text(0);
    return syms;
}

void MainWindow::onStockSelectionChanged()
{
    const QStringList selected = selectedSymbols();

    if (selected.isEmpty()) {
        m_chart->removeAllSeries();
        for (QAbstractAxis *ax : m_chart->axes()) m_chart->removeAxis(ax);
        m_stockTable->setRowCount(0);
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
        if (!m_cache.contains(sym)) {
            incrementCallCount(p->id());
            p->fetchData(sym, "3mo");
            loading << sym;
        }
    }

    updateChart(selected);
    refreshTable();

    int ready = 0;
    for (const QString &sym : selected) if (m_cache.contains(sym)) ++ready;

    if (loading.isEmpty())
        m_statusLabel->setText(QString("%1 stock(s) — normalized % change").arg(ready));
    else
        m_statusLabel->setText(
            QString("Loading %1...  (%2/%3 ready)")
                .arg(loading.join(", ")).arg(ready).arg(selected.size()));
}

void MainWindow::onDataReady(const QString &symbol, const QVector<StockDataPoint> &data)
{
    m_cache[symbol] = data;

    // Update error/icon state
    m_symbolErrors.remove(symbol);
    updateTreeItemIcon(symbol);

    // Fetch type once per symbol (cached permanently)
    if (!m_symbolTypes.contains(symbol)) {
        if (StockDataProvider *p = activeProvider())
            p->fetchSymbolType(symbol);
    }

    const QStringList selected = selectedSymbols();
    if (!selected.contains(symbol)) return;

    updateChart(selected);
    refreshTable();

    int ready = 0;
    for (const QString &sym : selected) if (m_cache.contains(sym)) ++ready;

    if (ready == selected.size())
        m_statusLabel->setText(QString("%1 stock(s) — normalized % change").arg(ready));
    else
        m_statusLabel->setText(QString("Loaded %1/%2 stocks...").arg(ready).arg(selected.size()));
}

void MainWindow::onError(const QString &symbol, const QString &message)
{
    m_statusLabel->setText("Error: " + message);
    if (!symbol.isEmpty()) {
        m_symbolErrors.insert(symbol);
        updateTreeItemIcon(symbol);
    }
}

void MainWindow::updateChart(const QStringList &selectedSymbols)
{
    static volatile bool isUpdating = false;
    if (isUpdating) return;
    isUpdating = true;

    m_chart->removeAllSeries();
    for (QAbstractAxis *ax : m_chart->axes()) m_chart->removeAxis(ax);

    QStringList ready;
    for (const QString &sym : selectedSymbols)
        if (m_cache.contains(sym) && !m_cache[sym].isEmpty()) ready << sym;

    if (ready.isEmpty()) {
        updateCrosshair();
        isUpdating = false;
        return;
    }
    
    // Before adding new series
    for (auto* axis : m_chart->axes()) {
        m_chart->removeAxis(axis);
    }

    auto *axisX = new QDateTimeAxis();
    axisX->setFormat("MMM dd");
    m_chart->addAxis(axisX, Qt::AlignBottom);

    auto *axisY = new QValueAxis();
    axisY->setTitleText("% Change");
    axisY->setLabelFormat("%.1f%%");
    m_chart->addAxis(axisY, Qt::AlignLeft);

    // Clip series to the selected range; Y range computed only for visible points
    const QDateTime rangeStart = (m_chartRangeDays > 0)
        ? QDateTime(QDate::currentDate().addDays(-m_chartRangeDays), QTime(0, 0), QTimeZone::utc())
        : QDateTime();

    double    minPct = std::numeric_limits<double>::max();
    double    maxPct = std::numeric_limits<double>::lowest();
    QDateTime minTime, maxTime;

    const bool singleStock = (ready.size() == 1);

    for (const QString &sym : ready) {
        const auto &data = m_cache[sym];

        // Base price = first visible data point so the left edge always starts at 0%
        double basePrice = std::numeric_limits<double>::quiet_NaN();
        for (const StockDataPoint &pt : data) {
            if (!rangeStart.isValid() || pt.timestamp >= rangeStart) {
                basePrice = pt.price;
                break;
            }
        }
        if (std::isnan(basePrice) || basePrice == 0.0) continue;

        // Collect visible (msecs, pct) pairs, updating axis range
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
            // ── Area chart: green fill above 0%, red fill below 0% ──────────
            auto *upperPos  = new QLineSeries(); // max(pct, 0) — top of green area
            auto *zeroLine  = new QLineSeries(); // constant 0  — bottom of red area top
            auto *lowerNeg  = new QLineSeries(); // min(pct, 0) — bottom of red area
            auto *mainLine  = new QLineSeries();
            mainLine->setName(sym);

            for (const auto &[msecs, pct] : pts) {
                upperPos->append(msecs, qMax(0.0, pct));
                zeroLine->append(msecs, 0.0);
                lowerNeg->append(msecs, qMin(0.0, pct));
                mainLine->append(msecs, pct);
            }

            // Green area: between max(pct,0) and explicit y=0 lower boundary
            auto *zeroLineGreen = new QLineSeries();
            for (const auto &[msecs, pct] : pts)
                zeroLineGreen->append(msecs, 0.0);
            auto *greenArea = new QAreaSeries(upperPos, zeroLineGreen);
            greenArea->setBrush(QColor(56, 142, 60, 130));  // Material green, semi-transparent
            greenArea->setPen(QPen(Qt::transparent));
            //2 upperPos->setParent(greenArea);
            //2 zeroLineGreen->setParent(greenArea);

            // Red area: between y=0 and min(pct,0)
            auto *redArea = new QAreaSeries(zeroLine, lowerNeg);
            redArea->setBrush(QColor(198, 40, 40, 130));    // Material red, semi-transparent
            redArea->setPen(QPen(Qt::transparent));
            //2 zeroLine->setParent(redArea);
            //2 lowerNeg->setParent(redArea);

            QPen linePen(QColor(40, 40, 40));
            linePen.setWidthF(1.5);
            mainLine->setPen(linePen);

            m_chart->addSeries(greenArea);
            m_chart->addSeries(redArea);
            m_chart->addSeries(mainLine);
            greenArea->attachAxis(axisX);  greenArea->attachAxis(axisY);
            redArea->attachAxis(axisX);    redArea->attachAxis(axisY);
            mainLine->attachAxis(axisX);   mainLine->attachAxis(axisY);

            // Hide area series from legend — only show the main line
            for (QLegendMarker *mk : m_chart->legend()->markers(greenArea)) mk->setVisible(false);
            for (QLegendMarker *mk : m_chart->legend()->markers(redArea))   mk->setVisible(false);

        } else {
            // ── Multi-stock: standard line chart ─────────────────────────────
            auto *series = new QLineSeries();
            series->setName(sym);
            for (const auto &[msecs, pct] : pts)
                series->append(msecs, pct);
            m_chart->addSeries(series);
            series->attachAxis(axisX);
            series->attachAxis(axisY);
        }
    }

    if (minTime.isNull()) { updateCrosshair(); 
        isUpdating = false;
        return;
    } // no data in range

    axisX->setMin(minTime);
    axisX->setMax(maxTime);
    double pad = std::max(0.5, (maxPct - minPct) * 0.08);
    axisY->setMin(minPct - pad);
    axisY->setMax(maxPct + pad);
    m_chart->legend()->setVisible(true);
    updateCrosshair();
    isUpdating = false;
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

    loadDailyCallCounts();
    loadSymbolTypeCache();
    loadTableSettings();
    loadGroups();
    setActiveProvider(s.value("activeProvider", m_providers.first()->id()).toString());
}

void MainWindow::saveSettings()
{
    QSettings s("StockChart", "StockChart");
    s.setValue("activeProvider", m_activeProviderId);
    for (StockDataProvider *p : m_providers) {
        s.beginGroup(p->id());
        for (const auto &field : p->credentialFields())
            s.setValue(field.first, p->credentials().value(field.first));
        s.endGroup();
    }
}

// ── API info panel ────────────────────────────────────────────────────────────

void MainWindow::setupApiInfoPanel(QWidget *parent, QBoxLayout *layout)
{
    auto *panel = new QFrame(parent);
    panel->setFrameShape(QFrame::StyledPanel);
    panel->setFrameShadow(QFrame::Sunken);

    auto *vl = new QVBoxLayout(panel);
    vl->setContentsMargins(6, 4, 6, 4);
    vl->setSpacing(3);

    auto *title = new QLabel("API Calls Today", panel);
    QFont tf = title->font();
    tf.setPointSize(tf.pointSize() - 1);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    vl->addWidget(title);

    auto *sep = new QFrame(panel);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    vl->addWidget(sep);

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    grid->setColumnStretch(0, 1);

    QFont sf;
    sf.setPointSize(sf.pointSize() - 1);

    for (int i = 0; i < m_providers.size(); ++i) {
        StockDataProvider *p = m_providers[i];
        auto *nameLabel  = new QLabel(p->displayName(), panel);
        nameLabel->setFont(sf);
        auto *countLabel = new QLabel("0", panel);
        countLabel->setFont(sf);
        countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(nameLabel,  i, 0);
        grid->addWidget(countLabel, i, 1);
        m_providerNameLabels[p->id()]  = nameLabel;
        m_callCountLabels[p->id()]     = countLabel;
    }
    vl->addLayout(grid);

    layout->addWidget(panel);
}

void MainWindow::updateApiInfoPanel()
{
    QFont normalFont, boldFont;
    boldFont.setPointSize(normalFont.pointSize() - 1);
    boldFont.setBold(true);
    normalFont.setPointSize(normalFont.pointSize() - 1);

    for (StockDataProvider *p : m_providers) {
        const bool active = (p->id() == m_activeProviderId);
        if (auto *nl = m_providerNameLabels.value(p->id()))
            nl->setFont(active ? boldFont : normalFont);
        if (auto *cl = m_callCountLabels.value(p->id())) {
            cl->setFont(active ? boldFont : normalFont);
            cl->setText(QString::number(m_dailyCallCounts.value(p->id(), 0)));
        }
    }
}

void MainWindow::incrementCallCount(const QString &providerId)
{
    const QDate today = QDate::currentDate();
    if (m_currentDay != today) {
        m_currentDay = today;
        for (StockDataProvider *p : m_providers) m_dailyCallCounts[p->id()] = 0;
    }
    m_dailyCallCounts[providerId]++;
    saveDailyCallCounts();
    updateApiInfoPanel();
}

void MainWindow::loadDailyCallCounts()
{
    QSettings s("StockChart", "StockChart");
    s.beginGroup("dailyCalls");
    const QDate today    = QDate::currentDate();
    const QDate savedDay = QDate::fromString(s.value("date").toString(), "yyyy-MM-dd");
    m_currentDay = today;
    if (savedDay != today) {
        for (StockDataProvider *p : m_providers) m_dailyCallCounts[p->id()] = 0;
    } else {
        for (StockDataProvider *p : m_providers)
            m_dailyCallCounts[p->id()] = s.value(p->id(), 0).toInt();
    }
    s.endGroup();
}

void MainWindow::saveDailyCallCounts()
{
    QSettings s("StockChart", "StockChart");
    s.beginGroup("dailyCalls");
    s.setValue("date", m_currentDay.toString("yyyy-MM-dd"));
    for (StockDataProvider *p : m_providers)
        s.setValue(p->id(), m_dailyCallCounts.value(p->id(), 0));
    s.endGroup();
}

// ── Symbol type icons & error badges ─────────────────────────────────────────

QIcon MainWindow::makeTypeIcon(SymbolType type)
{
    QColor bg;
    QString letter;
    switch (type) {
    case SymbolType::Stock:      bg = QColor("#1565C0"); letter = "S"; break; // blue
    case SymbolType::ETF:        bg = QColor("#E65100"); letter = "E"; break; // orange
    case SymbolType::Index:      bg = QColor("#6A1B9A"); letter = "I"; break; // purple
    case SymbolType::MutualFund: bg = QColor("#2E7D32"); letter = "F"; break; // green
    case SymbolType::Crypto:     bg = QColor("#F57F17"); letter = "C"; break; // amber
    default:                     return QIcon();
    }

    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(0, 0, 14, 14, 3, 3);
    p.setPen(Qt::white);
    QFont f;
    f.setPixelSize(9);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(0, 0, 14, 14), Qt::AlignCenter, letter);
    return QIcon(pm);
}

QIcon MainWindow::makeErrorIcon()
{
    return QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
}

void MainWindow::onSymbolTypeReady(const QString &symbol, SymbolType type)
{
    if (m_symbolTypes.value(symbol) == type) return; // no change
    m_symbolTypes[symbol] = type;
    saveSymbolType(symbol, type);
    updateTreeItemIcon(symbol);
}

void MainWindow::updateTreeItemIcon(const QString& symbol) {
    QIcon icon;
    if (m_symbolErrors.contains(symbol))
        icon = makeErrorIcon();
    else if (m_symbolTypes.contains(symbol))
        icon = makeTypeIcon(m_symbolTypes[symbol]);

    const QColor cachedBg(230, 245, 230); // light green

    for (int i = 0; i < m_stockTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* group = m_stockTree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem* child = group->child(j);
            if (child->text(0) == symbol) {
                // update icon
                child->setIcon(0, icon);
                // update price and background based on cache
                if (m_cache.contains(symbol) && !m_cache[symbol].isEmpty()) {
                    const double latest = m_cache[symbol].last().price;
                    child->setText(1, QString("$%1").arg(latest, 0, 'f', 2));
                    child->setBackground(0, QBrush(cachedBg));
                    child->setBackground(1, QBrush(cachedBg));
                }
                else {
                    child->setText(1, QString());
                    child->setBackground(0, QBrush());
                    child->setBackground(1, QBrush());
                }
            }
        }
    }
}

void MainWindow::refreshAllStockCacheVisuals() {
    const QColor cachedBg(230, 245, 230); // light green 
    for (int i = 0; i < m_stockTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_stockTree->topLevelItem(i); 
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *child = group->child(j); 
            const QString sym = child->text(0); 
            if (m_cache.contains(sym) && !m_cache[sym].isEmpty()) { 
                const double latest = m_cache[sym].last().price; 
                child->setText(1, QString("$%1").arg(latest, 0, 'f', 2));
                child->setBackground(0, QBrush(cachedBg));
                child->setBackground(1, QBrush(cachedBg)); 
            } 
            else { child->setText(1, QString()); 
            child->setBackground(0, QBrush()); 
            child->setBackground(1, QBrush()); 
            }
        }
    } 
}

/* 
void MainWindow::updateTreeItemIcon(const QString &symbol)
{
    QIcon icon;
    if (m_symbolErrors.contains(symbol))
        icon = makeErrorIcon();
    else if (m_symbolTypes.contains(symbol))
        icon = makeTypeIcon(m_symbolTypes[symbol]);

 
    for (int i = 0; i < m_stockTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* group = m_stockTree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            if (group->child(j)->text(0) == symbol)
                group->child(j)->setIcon(0, icon);
        }
    }
}
*/

void MainWindow::loadSymbolTypeCache()
{
    QSettings s("StockChart", "StockChart");
    s.beginGroup("symbolTypes");
    for (const QString &sym : s.childKeys())
        m_symbolTypes[sym] = static_cast<SymbolType>(s.value(sym).toInt());
    s.endGroup();
}

void MainWindow::saveSymbolType(const QString &symbol, SymbolType type)
{
    QSettings s("StockChart", "StockChart");
    s.beginGroup("symbolTypes");
    s.setValue(symbol, static_cast<int>(type));
    s.endGroup();
}

// ── CSV Export / Import ───────────────────────────────────────────────────────

// Quotes a CSV field: wraps in double-quotes and escapes internal double-quotes.
static QString csvQuote(const QString &s)
{
    QString escaped = s;
    escaped.replace('"', "\"\"");
    return '"' + escaped + '"';
}

// Parses one CSV line into fields, handling double-quoted fields.
static QStringList csvParseLine(const QString &line)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (inQuotes) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current += '"'; // escaped quote
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                current += ch;
            }
        } else {
            if (ch == '"') {
                inQuotes = true;
            } else if (ch == ',') {
                fields << current.trimmed();
                current.clear();
            } else {
                current += ch;
            }
        }
    }
    fields << current.trimmed();
    return fields;
}

void MainWindow::exportGroups()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Stock Groups", "stock_groups.csv",
        "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export Failed",
            "Could not open file for writing:\n" + path);
        return;
    }

    QTextStream out(&file);
    for (int i = 0; i < m_stockTree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *group = m_stockTree->topLevelItem(i);
        const QString groupName = group->data(0, Qt::UserRole).toString();

        QStringList row;
        row << csvQuote(groupName);
        for (int j = 0; j < group->childCount(); ++j)
            row << group->child(j)->text(0);

        out << row.join(", ") << "\n";
    }

    m_statusLabel->setText(
        QString("Exported %1 group(s) to %2")
            .arg(m_stockTree->topLevelItemCount())
            .arg(QFileInfo(path).fileName()));
}

void MainWindow::importGroups()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Import Stock Groups", QString(),
        "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Import Failed",
            "Could not open file:\n" + path);
        return;
    }

    // Build a map of existing groups for merge: group name -> QTreeWidgetItem*
    QMap<QString, QTreeWidgetItem*> existingGroups;
    for (int i = 0; i < m_stockTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *g = m_stockTree->topLevelItem(i);
        existingGroups[g->data(0, Qt::UserRole).toString()] = g;
    }

    QTextStream in(&file);
    int groupsAdded = 0, stocksAdded = 0;

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        QStringList fields = csvParseLine(line);
        if (fields.isEmpty() || fields.first().isEmpty()) continue;

        const QString groupName = fields.takeFirst(); // group name is first field

        QTreeWidgetItem *groupItem = existingGroups.value(groupName, nullptr);
        if (!groupItem) {
            groupItem = addGroup(groupName, true);
            existingGroups[groupName] = groupItem;
            ++groupsAdded;
        }

        // Collect existing stocks in this group to avoid duplicates
        QSet<QString> existing;
        for (int j = 0; j < groupItem->childCount(); ++j)
            existing.insert(groupItem->child(j)->text(0));

        for (const QString &sym : std::as_const(fields)) {
            const QString upper = sym.toUpper();
            if (!upper.isEmpty() && !existing.contains(upper)) {
                addStockToGroup(groupItem, upper);
                existing.insert(upper);
                ++stocksAdded;
            }
        }
    }

    saveGroups();
    m_statusLabel->setText(
        QString("Imported from %1 — %2 new group(s), %3 new stock(s) added")
            .arg(QFileInfo(path).fileName())
            .arg(groupsAdded)
            .arg(stocksAdded));
}

// ── Help dialog ───────────────────────────────────────────────────────────────

void MainWindow::showHelp()
{
    QDialog dlg(this);
    dlg.setWindowTitle("About StockChart");
    dlg.setMinimumWidth(680);

    auto *layout = new QVBoxLayout(&dlg);
    layout->setSpacing(12);

    // ── About section ────────────────────────────────────────────────────────
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
        "and a configurable performance table.",
        aboutFrame);
    desc->setAlignment(Qt::AlignCenter);
    desc->setWordWrap(true);

    auto *author = new QLabel("Author: Dennis Lang", aboutFrame);
    author->setAlignment(Qt::AlignCenter);

    aboutLayout->addWidget(appName);
    aboutLayout->addWidget(version);
    aboutLayout->addWidget(desc);
    aboutLayout->addWidget(author);
    layout->addWidget(aboutFrame);

    // ── API providers section ────────────────────────────────────────────────
    layout->addWidget(new QLabel("<b>Stock Market API Providers</b>", &dlg));

    struct ProviderInfo {
        QString name;
        QString freeTier;
        QString limits;
        QString notes;
    };

    const QList<ProviderInfo> providers = {
        { "Alpha Vantage",          "Yes",              "25 req/day",            "Good historical data; slow on free tier" },
        { "Finnhub",                "Yes",              "60 req/min",            "Real-time US quotes; solid free tier" },
        { "Polygon.io",             "Yes (delayed)",    "5 req/min",             "15-min delayed on free; real-time needs $29/mo" },
        { "Twelve Data",            "Yes",              "800 req/day, 8 req/min","Generous free tier; good historical data" },
        { "Tiingo",                 "Yes",              "500 req/hour",          "End-of-day historical; very good for casual use" },
        { "Yahoo Finance",          "Yes (no key)",     "—",                     "Unofficial API only; no key required but can break without warning" },
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

    // Highlight rows for providers that are integrated in the app
    const QStringList integrated = { "Alpha Vantage", "Finnhub", "Polygon.io", "Twelve Data" };
    const QColor integratedBg(230, 245, 230); // light green

    for (int r = 0; r < providers.size(); ++r) {
        const auto &p = providers[r];
        const bool isIntegrated = integrated.contains(p.name);
        const QList<QString> cells = { p.name, p.freeTier, p.limits, p.notes };
        for (int c = 0; c < 4; ++c) {
            auto *item = new QTableWidgetItem(cells[c]);
            item->setFlags(Qt::ItemIsEnabled);
            if (isIntegrated)
                item->setBackground(integratedBg);
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
