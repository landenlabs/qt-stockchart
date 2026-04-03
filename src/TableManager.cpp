#include "TableManager.h"
#include <QSettings>
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

static QIcon makeColorIcon(const QColor &color)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(1, 1, 12, 12, 2, 2);
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
    QSettings s("StockChart", "StockChart");

    QVariantList vl = s.value("tablePeriods").toList();
    if (vl.isEmpty()) {
        m_periods = kDefaultPeriods;
    } else {
        m_periods.clear();
        for (const QVariant &v : vl) m_periods << v.toInt();
    }

    emit periodsChanged(m_periods);

    m_showPercentChange = s.value("tableShowPercent", false).toBool();
    m_displayModeBtn->setChecked(m_showPercentChange);
    m_displayModeBtn->setText(m_showPercentChange ? "% Change" : "Price");

    m_savedTableHeight = s.value("tableHeight", -1).toInt();
    bool expanded = s.value("tableExpanded", false).toBool();
    setExpanded(expanded);
}

void TableManager::saveSettings()
{
    QSettings s("StockChart", "StockChart");
    QVariantList vl;
    for (int p : m_periods) vl << p;
    s.setValue("tablePeriods",    vl);
    s.setValue("tableShowPercent", m_showPercentChange);
    s.setValue("tableExpanded",   m_tableExpanded);
    s.setValue("tableHeight",     m_savedTableHeight);
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
    if (!expanded) {
        m_table->hide();
        m_toggleBtn->setText("▼ Table");
    } else {
        m_table->show();
        const int total  = m_vertSplitter->height();
        const int tableH = (m_savedTableHeight > 0) ? m_savedTableHeight : 100;
        m_vertSplitter->setSizes({ total - tableH, tableH });
        m_toggleBtn->setText("▲ Table");
    }
    saveSettings();
}

void TableManager::onSplitterMoved()
{
    if (m_tableExpanded) {
        const QList<int> sizes = m_vertSplitter->sizes();
        if (sizes.size() >= 2 && sizes[1] > 0) {
            m_savedTableHeight = sizes[1];
            saveSettings();
        }
    }
}

// ── Display mode ──────────────────────────────────────────────────────────────

void TableManager::onToggleDisplayMode(bool checked)
{
    m_showPercentChange = checked;
    m_displayModeBtn->setText(checked ? "% Change" : "Price");
    saveSettings();
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

    // Column 0 is the color swatch; period/click columns start at index 1.
    m_table->setRowCount(nRows);
    m_table->setColumnCount(nCols + 1);

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

    // Color-swatch column header (narrow, no label)
    m_table->setHorizontalHeaderItem(0, new QTableWidgetItem(""));

    // Period column headers (table col = c + 1)
    for (int c = 0; c < nPeriods; ++c) {
        const int days = m_periods[c];
        QString label = (days == 0) ? "Today" : QString("%1d").arg(days);
        auto *hdr = new QTableWidgetItem(label);
        const bool isActive = (c == activeOffset);
        if (isActive) {
            QFont f = hdr->font();
            f.setBold(true);
            hdr->setFont(f);
            if (m_showPercentChange)
                hdr->setBackground(refBg);
        }
        m_table->setHorizontalHeaderItem(c + 1, hdr);
    }
    if (hasClick) {
        auto *hdr = new QTableWidgetItem(m_clickedDate.toString("MMM d"));
        m_table->setHorizontalHeaderItem(nPeriods + 1, hdr);
    }

    m_table->horizontalHeader()->setCursor(Qt::ArrowCursor);

    for (int r = 0; r < nRows; ++r) {
        const QString &sym = syms[r];
        m_table->setVerticalHeaderItem(r, new QTableWidgetItem(sym));

        // Column 0: series color swatch
        auto *colorCell = new QTableWidgetItem();
        colorCell->setFlags(Qt::ItemIsEnabled);
        if (m_seriesColors.contains(sym))
            colorCell->setIcon(makeColorIcon(m_seriesColors[sym]));
        m_table->setItem(r, 0, colorCell);

        const bool hasCached = m_cache->cache().contains(sym) && !m_cache->cache()[sym].isEmpty();

        double basePrice = std::numeric_limits<double>::quiet_NaN();
        if (hasCached && m_showPercentChange && nPeriods > 0)
            basePrice = StockCacheManager::priceAt(m_cache->cache()[sym], colDate(activeOffset));

        // Period + click columns (loop index c → table column c + 1)
        for (int c = 0; c < nCols; ++c) {
            const int tableCol   = c + 1;
            const bool isActive  = (c == activeOffset) && (c < nPeriods);
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
    }
}
