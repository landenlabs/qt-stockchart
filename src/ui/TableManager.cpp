#include "TableManager.h"
#include "AppSettings.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QInputDialog>
#include <QLineEdit>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <limits>
#include <cmath>

static QIcon makeColorIcon(const QColor &color, bool thick = false)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(thick ? QPen(Qt::white, 1.5) : Qt::NoPen);
    p.drawRoundedRect(1, 1, 12, 12, 2, 2);
    return QIcon(pm);
}

// Eye icon: open (visible) = filled colored eye shape; closed = gray with slash.
static QIcon makeEyeIcon(bool visible)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (visible) {
        // Filled green circle = visible
        p.setBrush(QColor(40, 160, 40));
        p.setPen(Qt::NoPen);
        p.drawEllipse(2, 2, 10, 10);
    } else {
        // Hollow gray circle with diagonal slash = hidden
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(160, 160, 160), 1.5));
        p.drawEllipse(2, 2, 10, 10);
        p.drawLine(4, 10, 10, 4);
    }
    return QIcon(pm);
}

static const QList<int> kDefaultPeriods = { -365, -90, -60, -30, -7, 0 };

TableManager::TableManager(QTableWidget *table, QSplitter *vertSplitter,
                           QToolButton *toggleBtn, QPushButton *displayModeBtn,
                           StockCacheManager *cache, QWidget *dialogParent,
                           QObject *parent)
    : QObject(parent)
    , m_table(table)
    , m_vertSplitter(vertSplitter)
    , m_toggleBtn(toggleBtn)
    , m_displayModeBtn(displayModeBtn)
    , m_cache(cache)
    , m_dialogParent(dialogParent)
    , m_periods(kDefaultPeriods)
{
}

// ── Settings ──────────────────────────────────────────────────────────────────

void TableManager::loadSettings()
{
    auto &as = AppSettings::instance();

    QVariantList vl = as.tablePeriods();
    if (vl.isEmpty()) {
        m_periods = kDefaultPeriods;
    } else {
        m_periods.clear();
        for (const QVariant &v : vl) m_periods << v.toInt();
    }

    emit periodsChanged(m_periods);

    m_showPercentChange = as.tableShowPercent();
    m_displayModeBtn->setChecked(m_showPercentChange);
    m_displayModeBtn->setText(m_showPercentChange ? "% Change" : "Price");
    m_displayModeBtn->setToolTip(m_showPercentChange
        ? "Graph shows normalized % change"
        : "Graph shows stock price");

    // Table open/closed state — sizes are applied by restoreTableSplitter() from showEvent,
    // once the window is fully laid out and the splitter has a real height.
    m_savedTableHeight = as.tableHeight();
    m_tableExpanded    = m_savedTableHeight > 0; // as.tableExpanded();
    // m_toggleBtn->setText(m_tableExpanded ? "▲ Table" : "▼ Table");
    // setExpanded(m_tableExpanded);
}

void TableManager::saveSettings()
{
    auto &as = AppSettings::instance();
    QVariantList vl;
    for (int p : m_periods) vl << p;
    as.setTablePeriods(vl);
    as.setTableShowPercent(m_showPercentChange);
    // as.setTableExpanded(m_tableExpanded);
    as.setTableHeight(m_savedTableHeight);
}

// ── Expand / collapse ─────────────────────────────────────────────────────────

void TableManager::onToggle()
{
    setExpanded(!m_tableExpanded);
    if (m_tableExpanded)
        refresh(m_lastSymbols, m_clickedDate);
}

void TableManager::setExpanded(bool expanded)
{
    m_tableExpanded = expanded;
    m_toggleBtn->setText(expanded ? "▲ Table" : "▼ Table");
    const int total = m_vertSplitter->height();
    if (total > 0) {
        if (expanded) {
            const int tableH = qBound(50, (m_savedTableHeight > 0) ? m_savedTableHeight : 150,
                                      total - 50);
            m_vertSplitter->setSizes({ total - tableH, tableH });
        } else {
            m_vertSplitter->setSizes({ total, 0 });
        }
    }
    m_savedTableHeight = m_vertSplitter->sizes()[1];
    saveSettings();
}

void TableManager::restoreTableSplitter()
{
    const int total = m_vertSplitter->height();
    if (!m_tableExpanded) {
        if (total > 0)
            m_vertSplitter->setSizes({ total, 0 }); // collapse table to 0, handle stays at edge
        return;
    }

    // Use saved height or default to 25% of available space.
    if (total > 0) {
        const int defaultH = qMax(50, total / 4);
        const int tableH   = qBound(50, (m_savedTableHeight > 0) ? m_savedTableHeight : defaultH, total - 50);
        m_vertSplitter->setSizes({ total - tableH, tableH });
    }
}

void TableManager::onSplitterMoved()
{
    const QList<int> sizes = m_vertSplitter->sizes();
    if (sizes.size() < 2) return;

    const bool nowCollapsed = (sizes[1] == 0);
    if (nowCollapsed && m_tableExpanded) {
        m_tableExpanded = false;
        m_toggleBtn->setText("▼ Table");
        saveSettings();
    } else if (!nowCollapsed && !m_tableExpanded) {
        m_tableExpanded = true;
        m_toggleBtn->setText("▲ Table");
        m_savedTableHeight = sizes[1];
        saveSettings();
    } else if (m_tableExpanded && sizes[1] > 0) {
        m_savedTableHeight = sizes[1];
        saveSettings();
    }
}

// ── Display mode ──────────────────────────────────────────────────────────────

void TableManager::onToggleDisplayMode(bool checked)
{
    m_showPercentChange = checked;
    m_displayModeBtn->setText(checked ? "% Change" : "Price");
    m_displayModeBtn->setToolTip(checked
        ? "Graph shows normalized % change"
        : "Graph shows stock price");
    // saveSettings();
    if (m_tableExpanded)
        refresh(m_lastSymbols, m_clickedDate);
}

// ── Period configuration ──────────────────────────────────────────────────────

void TableManager::configurePeriods()
{
    QDialog dlg(m_dialogParent);
    dlg.setWindowTitle("Configure Periods");
    dlg.setMinimumWidth(280);

    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel("Day offsets from today (0 = today, negative = past):", &dlg));

    auto *list = new QListWidget(&dlg);
    for (int p : m_periods)
        list->addItem(QString::number(p));
    layout->addWidget(list);

    auto *btnRow  = new QHBoxLayout;
    auto *addBtn  = new QPushButton("Add...", &dlg);
    auto *remBtn  = new QPushButton("Remove", &dlg);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(remBtn);
    layout->addLayout(btnRow);

    connect(addBtn, &QPushButton::clicked, &dlg, [&]() {
        bool ok;
        const QString input = QInputDialog::getText(&dlg, "Add Period(s)",
            "Day offset(s), comma-separated\n(e.g. -30, -60 for 30 and 60 days ago, 0 for today):",
            QLineEdit::Normal, {}, &ok);
        if (!ok || input.trimmed().isEmpty()) return;
        for (const QString &part : input.split(',', Qt::SkipEmptyParts)) {
            bool numOk;
            const int offset = part.trimmed().toInt(&numOk);
            if (!numOk) continue;
            bool exists = false;
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->text().toInt() == offset) { exists = true; break; }
            if (!exists) list->addItem(QString::number(offset));
        }
    });

    connect(remBtn, &QPushButton::clicked, &dlg, [&]() {
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
    emit periodsChanged(m_periods);
    saveSettings();
    if (m_tableExpanded)
        refresh(m_lastSymbols, m_clickedDate);
}

// ── Refresh ───────────────────────────────────────────────────────────────────

void TableManager::refresh(const QStringList &syms, const QDate &clickedDate)
{
    m_clickedDate = clickedDate;
    m_lastSymbols = syms;

    if (!m_tableExpanded) return;

    const QDate today    = QDate::currentDate();
    const int   nPeriods = m_periods.size();
    const bool  hasClick = m_clickedDate.isValid();
    const int   nCols    = nPeriods + (hasClick ? 1 : 0);  // period + click columns
    const int   nRows    = syms.size();

    // Col 0: color swatch (thick toggle).
    // Col 1: eye icon (visibility toggle).
    // Col 2..nCols+1: period/click data.
    // Col nCols+2: Purchase.
    m_table->setRowCount(nRows);
    m_table->setColumnCount(nCols + 3);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 22);
    m_table->setColumnWidth(1, 22);

    // Find the period column (0-based period index) matching the active graph range.
    int activeOffset = 0;  // fallback to first period
    for (int c = 0; c < nPeriods; ++c) {
        if (qAbs(m_periods[c]) == m_activePeriodDays) { activeOffset = c; break; }
    }

    const QColor refBg(210, 228, 255);

    // colDate: takes a period-loop index c (0..nCols-1), returns the date for that column.
    auto colDate = [&](int c) -> QDate {
        return c < nPeriods ? today.addDays(m_periods[c]) : m_clickedDate;
    };

    // Column headers
    m_table->setHorizontalHeaderItem(0, new QTableWidgetItem(""));  // color swatch
    m_table->setHorizontalHeaderItem(1, new QTableWidgetItem(""));  // eye

    // Purchase column header: bold when in purPct mode (it's the reference)
    {
        auto *purchaseHdr = new QTableWidgetItem("Purchase");
        if (m_purPctMode) {
            QFont f = purchaseHdr->font();
            f.setBold(true);
            purchaseHdr->setFont(f);
        }
        m_table->setHorizontalHeaderItem(nCols + 2, purchaseHdr);
    }

    // Period column headers (table col = c + 2)
    // In purPct mode, no period column is the "active" reference — Purchase column is
    for (int c = 0; c < nPeriods; ++c) {
        const int days = m_periods[c];
        QString label = (days == 0) ? "Today" : QString("%1d").arg(days);
        auto *hdr = new QTableWidgetItem(label);
        const bool isActive = (c == activeOffset) && !m_purPctMode;
        if (isActive) {
            QFont f = hdr->font();
            f.setBold(true);
            hdr->setFont(f);
            if (m_showPercentChange)
                hdr->setBackground(refBg);
        }
        m_table->setHorizontalHeaderItem(c + 2, hdr);
    }
    if (hasClick) {
        auto *hdr = new QTableWidgetItem(m_clickedDate.toString("MMM d"));
        m_table->setHorizontalHeaderItem(nPeriods + 2, hdr);
    }

    m_table->horizontalHeader()->setCursor(Qt::ArrowCursor);

    for (int r = 0; r < nRows; ++r) {
        const QString &sym = syms[r];
        m_table->setVerticalHeaderItem(r, new QTableWidgetItem(sym));

        // Column 0: color swatch — white border when this symbol's line is thick
        auto *colorCell = new QTableWidgetItem();
        colorCell->setFlags(Qt::ItemIsEnabled);
        colorCell->setToolTip("Click to highlight line");
        if (m_seriesColors.contains(sym))
            colorCell->setIcon(makeColorIcon(m_seriesColors[sym], sym == m_thickSymbol));
        m_table->setItem(r, 0, colorCell);

        // Column 1: eye icon — green filled = visible, gray hollow = hidden
        auto *eyeCell = new QTableWidgetItem();
        eyeCell->setFlags(Qt::ItemIsEnabled);
        eyeCell->setToolTip("Click to show/hide line");
        eyeCell->setIcon(makeEyeIcon(!m_hiddenSymbols.contains(sym)));
        m_table->setItem(r, 1, eyeCell);

        const bool hasCached = m_cache->cache().contains(sym) && !m_cache->cache()[sym].isEmpty();

        double basePrice = std::numeric_limits<double>::quiet_NaN();
        if (hasCached && m_showPercentChange && nPeriods > 0) {
            if (m_purPctMode) {
                const double pp = m_purchasePrices.value(sym, 0.0);
                if (pp > 0.0) basePrice = pp;
            } else {
                basePrice = StockCacheManager::priceAt(m_cache->cache()[sym], colDate(activeOffset));
            }
        }

        // Period + click columns (loop index c → table column c + 2)
        for (int c = 0; c < nCols; ++c) {
            const int tableCol   = c + 2;
            // In purPct mode the purchase column is the reference; no period col is "active"
            const bool isActive  = (c == activeOffset) && (c < nPeriods) && !m_purPctMode;
            QTableWidgetItem *cell;

            if (!hasCached) {
                cell = new QTableWidgetItem("…");
                cell->setForeground(Qt::gray);
            } else {
                const double price = StockCacheManager::priceAt(m_cache->cache()[sym], colDate(c));

                if (std::isnan(price)) {
                    cell = new QTableWidgetItem("N/A");
                    cell->setForeground(Qt::gray);
                } else if (m_showPercentChange && isActive) {
                    // Active period column: show the base price
                    cell = new QTableWidgetItem(QString("$%1").arg(price, 0, 'f', 2));
                } else if (m_showPercentChange) {
                    if (std::isnan(basePrice) || basePrice == 0.0) {
                        cell = new QTableWidgetItem("N/A");
                        cell->setForeground(Qt::gray);
                    } else {
                        const double pct = (price / basePrice - 1.0) * 100.0;
                        cell = new QTableWidgetItem(
                            QString("%1%2%").arg(pct >= 0 ? "+" : "").arg(pct, 0, 'f', 2));
                        if      (pct > 0) cell->setForeground(QColor("#2e7d32"));
                        else if (pct < 0) cell->setForeground(QColor("#c62828"));
                    }
                } else {
                    cell = new QTableWidgetItem(QString("$%1").arg(price, 0, 'f', 2));
                }
            }

            cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            cell->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (m_showPercentChange && isActive)
                cell->setBackground(refBg);
            m_table->setItem(r, tableCol, cell);
        }

        // Purchase price column (always last); highlighted as reference in purPct mode
        const double purPrice = m_purchasePrices.value(sym, 0.0);
        auto *purCell = new QTableWidgetItem(purPrice > 0
            ? QString("$%1").arg(purPrice, 0, 'f', 2) : QString());
        purCell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        purCell->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (m_purPctMode && m_showPercentChange)
            purCell->setBackground(refBg);
        m_table->setItem(r, nCols + 2, purCell);
    }
}
