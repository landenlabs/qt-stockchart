#include "StockGroupManager.h"
#include "AppSettings.h"
#include <algorithm>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QModelIndex>
#include <QHeaderView>

static const QStringList kDefaultStocks = {
    "AAPL", "GOOGL", "MSFT", "AMZN", "TSLA",
    "META", "NVDA", "NFLX", "JPM",  "UBER"
};

// Returns the row background brush for a cached symbol based on cache age:
//   < 1 day  → light green   ≥ 1 day and < 1 week → light yellow
//   ≥ 1 week or no data      → default (no fill)
static QBrush ageBgBrush(const StockCacheManager *cache, const QString &sym)
{
    if (!cache->cache().contains(sym) || cache->cache()[sym].isEmpty()) return QBrush();
    const qint64 secs = cache->dataSecs(sym);
    if (secs < 0 || secs >= 7 * 86400) return QBrush();
    if (secs >= 86400) return QBrush(QColor(255, 255, 210));  // light yellow
    return QBrush(QColor(230, 245, 230));                      // light green
}

StockGroupManager::StockGroupManager(QTreeWidget *tree, StockCacheManager *cache,
                                     QWidget *dialogParent, QObject *parent)
    : QObject(parent)
    , m_tree(tree)
    , m_cache(cache)
    , m_dialogParent(dialogParent)
{
}

// ── Icon factories ────────────────────────────────────────────────────────────

QIcon StockGroupManager::makeTypeIcon(SymbolType type)
{
    QColor bg;
    QString letter;
    switch (type) {
    case SymbolType::Stock:      bg = QColor("#1565C0"); letter = "S"; break;
    case SymbolType::ETF:        bg = QColor("#E65100"); letter = "E"; break;
    case SymbolType::Index:      bg = QColor("#6A1B9A"); letter = "I"; break;
    case SymbolType::MutualFund: bg = QColor("#2E7D32"); letter = "F"; break;
    case SymbolType::Crypto:     bg = QColor("#F57F17"); letter = "C"; break;
    default:                     return QIcon();
    }
    QPixmap pm(24, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(bg);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(5, 0, 14, 14, 3, 3);
    p.setPen(Qt::white);
    QFont f;
    f.setPixelSize(9);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(5, 0, 14, 14), Qt::AlignCenter, letter);
    return QIcon(pm);
}

QIcon StockGroupManager::makeErrorIcon()
{
    return QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
}

QIcon StockGroupManager::makeStarIcon(int index)
{
    QPixmap pm(24, 16);
    pm.fill(Qt::transparent);
    if (index <= 0) return QIcon(pm);
    static const QColor colors[] = {
        Qt::transparent, QColor("#FFD700"), QColor("#448AFF"),
        QColor("#4CAF50"), QColor("#F44336"), QColor("#9C27B0")
    };
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(colors[index % 6]);
    p.setPen(QPen(p.brush().color().darker(), 1));
    static const QPointF points[] = {
        {12, 1}, {14, 6}, {19, 6}, {15, 9}, {17, 15}, {12, 12}, {7, 15}, {9, 9}, {5, 6}, {10, 6}
    };
    p.drawPolygon(points, 10);
    return QIcon(pm);
}

// ── Tree building ─────────────────────────────────────────────────────────────

QTreeWidgetItem *StockGroupManager::addGroup(const QString &name, bool expanded)
{
    QString displayName = name;

    auto *item = new QTreeWidgetItem(m_tree);
    item->setFlags(Qt::ItemIsEnabled);
    item->setData(0, Qt::UserRole, name);

    auto *container = new QWidget;
    container->setStyleSheet("background: #f0f0f0;");
    auto *hl = new QHBoxLayout(container);
    hl->setContentsMargins(0, 1, 4, 1);
    hl->setSpacing(2);

    auto *nameLabel = new QLabel(displayName, container);
    QFont f = nameLabel->font();
    f.setBold(true);
    nameLabel->setFont(f);

    auto *addBtn = new QPushButton("+", container);
    addBtn->setFixedSize(18, 18);
    addBtn->setFlat(true);
    addBtn->setCursor(Qt::PointingHandCursor);
    addBtn->setToolTip("Add stock to " + name);
    addBtn->setStyleSheet(
            "QPushButton { border: none; font-weight: bold; color: #333; }"
            "QPushButton:hover { background-color: #e0e0e0; border-radius: 2px; }"
            );

    hl->addWidget(nameLabel, 0);    // 0=use its width
    hl->addWidget(addBtn);
    hl->addStretch(1);              // fill remainder to push others to left.

    m_tree->setItemWidget(item, 0, container);
    // Must be called AFTER setItemWidget: Qt sizes the persistent widget during a
    // delayed layout pass, so spanning set before the widget is installed is ignored.
    m_tree->setFirstColumnSpanned(m_tree->indexOfTopLevelItem(item), QModelIndex(), true);

    connect(addBtn, &QPushButton::clicked, this, [this, item]() {
        showAddStockDialog(item);
    });

    item->setExpanded(expanded);
    return item;
}

void StockGroupManager::addStockToGroup(QTreeWidgetItem *groupItem, const QString &symbol,
                                        int star, double purPrice, const QString &purDate)
{
    auto *item = new QTreeWidgetItem(groupItem);
    item->setData(1, StarRole, star);
    item->setIcon(1, makeStarIcon(star));

    if (m_symbolErrors.contains(symbol))
        item->setIcon(2, makeErrorIcon());
    else if (m_cache->symbolTypes().contains(symbol))
        item->setIcon(2, makeTypeIcon(m_cache->symbolTypes()[symbol]));

    item->setText(3, symbol);
    item->setText(4, m_cache->ageString(symbol));
    if (m_cache->cache().contains(symbol) && !m_cache->cache()[symbol].isEmpty())
        item->setText(5, QString("$%1").arg(m_cache->cache()[symbol].last().price, 0, 'f', 2));
    const QBrush ageBrush = ageBgBrush(m_cache, symbol);
    item->setBackground(3, ageBrush);
    item->setBackground(4, ageBrush);
    item->setBackground(5, ageBrush);
    item->setData(6, PurPriceRole, purPrice);
    if (purPrice > 0) item->setText(6, QString::number(purPrice, 'f', 2));
    item->setData(7, PurDateRole, purDate);
    item->setText(7, purDate);

    item->setTextAlignment(5, Qt::AlignRight | Qt::AlignVCenter);
    item->setTextAlignment(6, Qt::AlignRight | Qt::AlignVCenter);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
}

// ── Add stock dialog ──────────────────────────────────────────────────────────

void StockGroupManager::showAddStockDialog(QTreeWidgetItem *groupItem)
{
    const QString groupName = groupItem->data(0, Qt::UserRole).toString();

    QDialog dlg(m_dialogParent);
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
                if (groupItem->child(i)->text(3) == sym) { exists = true; break; }
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

    connect(addBtn,   &QPushButton::clicked, &dlg, doAdd);
    connect(edit,     &QLineEdit::returnPressed, &dlg, doAdd);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    edit->setFocus();
    dlg.exec();
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void StockGroupManager::onAddGroupClicked()
{
    bool ok;
    QString name = QInputDialog::getText(m_dialogParent, "Add Group",
                                         "Group name:", QLineEdit::Normal, "", &ok);
    name = name.trimmed();
    if (!ok || name.isEmpty()) return;
    addGroup(name, true);
    saveGroups();
}

void StockGroupManager::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    QMenu menu(m_dialogParent);

    if (!item) {
        menu.addAction("Add Group...", this, &StockGroupManager::onAddGroupClicked);
    } else if (!item->parent()) {
        const QString groupName = item->data(0, Qt::UserRole).toString();
        menu.addAction("Add Stock...", this, [this, item]() { showAddStockDialog(item); });
        menu.addSeparator();
        menu.addAction("Delete Group", this, [this, item, groupName]() {
            if (QMessageBox::question(m_dialogParent, "Delete Group",
                    QString("Delete group \"%1\" and all its stocks?").arg(groupName))
                    != QMessageBox::Yes) return;
            delete item;
            saveGroups();
        });
    } else {
        menu.addAction("Edit Details...", this, [this, item]() { onEditStockDetails(item); });

        QMenu *starMenu = menu.addMenu("Set Star");
        const QStringList starNames = {"None", "Gold", "Blue", "Green", "Red", "Purple"};
        for (int i = 0; i < starNames.size(); ++i) {
            QAction *act = starMenu->addAction(makeStarIcon(i), starNames[i]);
            connect(act, &QAction::triggered, this, [this, item, i]() { onSetStar(item, i); });
        }

        menu.addSeparator();
        menu.addAction("Clear Cache",    this, [this, item]() { onClearCache(item); });
        menu.addAction("Force Reload",   this, [this, item]() { onForceReload(item); });
        menu.addAction("List Historical",this, [this, item]() { onListHistorical(item); });

        menu.addSeparator();
        menu.addAction("Remove Stock", this, [this, item]() {
            delete item;
            saveGroups();
        });
    }

    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void StockGroupManager::onEditStockDetails(QTreeWidgetItem *item)
{
    QDialog dlg(m_dialogParent);
    dlg.setWindowTitle("Edit Details: " + item->text(3));
    QFormLayout *form = new QFormLayout(&dlg);

    QLineEdit *priceEdit = new QLineEdit(item->text(6), &dlg);
    QLineEdit *dateEdit  = new QLineEdit(item->text(7), &dlg);
    dateEdit->setPlaceholderText("mm/dd/yyyy");
    form->addRow("Purchase Price:", priceEdit);
    form->addRow("Purchase Date:", dateEdit);

    QDialogButtonBox *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        item->setText(6, priceEdit->text());
        item->setText(7, dateEdit->text());
        item->setData(6, PurPriceRole, priceEdit->text().toDouble());
        item->setData(7, PurDateRole, dateEdit->text());
        saveGroups();
        emit stockDetailsChanged(item->text(3));
    }
}

QPair<double, QDate> StockGroupManager::purchaseInfoForSymbol(const QString &symbol) const
{
    // Search all groups: prefer the entry that has purchase data over an empty
    // duplicate in another group (e.g. a stock listed in both "Favorites" and
    // a portfolio group where only the portfolio entry has price/date set).
    double bestPrice = 0.0;
    QDate  bestDate;

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            auto *child = group->child(j);
            if (child->text(3) != symbol) continue;

            double price = child->data(6, PurPriceRole).toDouble();
            QDate  date;
            const QString dateStr = child->data(7, PurDateRole).toString();
            for (const QString &fmt : {"MM/dd/yyyy", "M/d/yyyy", "MM/d/yyyy", "M/dd/yyyy"}) {
                date = QDate::fromString(dateStr, fmt);
                if (date.isValid()) break;
            }

            // Take this entry if it has more data than what we've seen so far
            if ((price > 0.0 && bestPrice == 0.0) ||
                (date.isValid() && !bestDate.isValid()) ||
                (price > 0.0 && date.isValid())) {
                bestPrice = price;
                bestDate  = date;
            }
        }
    }
    return {bestPrice, bestDate};
}

void StockGroupManager::onClearCache(QTreeWidgetItem *item)
{
    const QString sym = item->text(3);
    m_cache->clearSymbolCache(sym);
    updateTreeItemIcon(sym);
}

void StockGroupManager::onForceReload(QTreeWidgetItem *item)
{
    emit forceReloadRequested(item->text(3));
}

void StockGroupManager::onListHistorical(QTreeWidgetItem *item)
{
    const QString sym = item->text(3);
    const QVector<StockDataPoint> &data = m_cache->cache()[sym];

    QDialog dlg(m_dialogParent);
    dlg.setWindowTitle("Historical Data: " + sym);
    dlg.setMinimumSize(320, 450);

    auto *layout = new QVBoxLayout(&dlg);

    auto *table = new QTableWidget(data.size(), 2, &dlg);
    table->setHorizontalHeaderLabels({"Date / Time", "Price"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);
    table->setAlternatingRowColors(true);

    for (int i = 0; i < data.size(); ++i) {
        const StockDataPoint &pt = data[i];
        auto *dtItem    = new QTableWidgetItem(pt.timestamp.toString("dd-MMM-yy hh:mm"));
        auto *priceItem = new QTableWidgetItem(QString::number(pt.price, 'f', 2));
        priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(i, 0, dtItem);
        table->setItem(i, 1, priceItem);
    }

    if (!data.isEmpty())
        table->scrollToBottom();

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);

    layout->addWidget(table);
    layout->addWidget(btns);

    dlg.exec();
}

void StockGroupManager::onSetStar(QTreeWidgetItem *item, int starIndex)
{
    item->setData(1, StarRole, starIndex);
    item->setIcon(1, makeStarIcon(starIndex));
    saveGroups();
}

void StockGroupManager::sortByColumn(int col)
{
    if (col == m_sortCol)
        m_sortAscending = !m_sortAscending;
    else {
        m_sortCol = col;
        m_sortAscending = true;
    }
    const bool asc = m_sortAscending;

    static const QStringList dateFmts = {"MM/dd/yyyy", "M/d/yyyy", "MM/d/yyyy", "M/dd/yyyy"};
    auto parseDate = [&](const QString &s) {
        for (const QString &fmt : dateFmts) {
            QDate d = QDate::fromString(s, fmt);
            if (d.isValid()) return d;
        }
        return QDate{};
    };

    auto comparator = [&](QTreeWidgetItem *a, QTreeWidgetItem *b) -> bool {
        const QString symA = a->text(3);
        const QString symB = b->text(3);
        switch (col) {
        case 1: { // Star
            int sa = a->data(1, StarRole).toInt();
            int sb = b->data(1, StarRole).toInt();
            if (sa != sb) return asc ? sa < sb : sa > sb;
            break;
        }
        case 2: { // Type
            int ta = static_cast<int>(m_cache->symbolTypes().value(symA));
            int tb = static_cast<int>(m_cache->symbolTypes().value(symB));
            if (ta != tb) return asc ? ta < tb : ta > tb;
            break;
        }
        case 3: // Symbol
            if (symA != symB) return asc ? symA < symB : symA > symB;
            break;
        case 4: { // Age (seconds since newest data; -1 = no data → always last)
            qint64 sa = m_cache->dataSecs(symA);
            qint64 sb = m_cache->dataSecs(symB);
            if (sa < 0 && sb < 0) break;
            if (sa < 0) return false;
            if (sb < 0) return true;
            if (sa != sb) return asc ? sa < sb : sa > sb;
            break;
        }
        case 5: { // Current price (text "$NNN.NN"; empty = no data → always last)
            QString ta = a->text(5); if (ta.startsWith('$')) ta.remove(0, 1);
            QString tb = b->text(5); if (tb.startsWith('$')) tb.remove(0, 1);
            bool okA, okB;
            double da = ta.toDouble(&okA);
            double db = tb.toDouble(&okB);
            if (!okA && !okB) break;
            if (!okA) return false;
            if (!okB) return true;
            if (da != db) return asc ? da < db : da > db;
            break;
        }
        case 6: { // Purchase price (0 = not set → always last)
            double da = a->data(6, PurPriceRole).toDouble();
            double db = b->data(6, PurPriceRole).toDouble();
            if (da == 0 && db == 0) break;
            if (da == 0) return false;
            if (db == 0) return true;
            if (da != db) return asc ? da < db : da > db;
            break;
        }
        case 7: { // Purchase date (invalid = not set → always last)
            QDate da = parseDate(a->data(7, PurDateRole).toString());
            QDate db = parseDate(b->data(7, PurDateRole).toString());
            if (!da.isValid() && !db.isValid()) break;
            if (!da.isValid()) return false;
            if (!db.isValid()) return true;
            if (da != db) return asc ? da < db : da > db;
            break;
        }
        default: break;
        }
        return symA < symB; // tiebreak by symbol
    };

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_tree->topLevelItem(i);
        QList<QTreeWidgetItem *> children;
        children.reserve(group->childCount());
        while (group->childCount() > 0)
            children.append(group->takeChild(0));
        std::sort(children.begin(), children.end(), comparator);
        for (auto *child : children)
            group->addChild(child);
    }
    m_tree->header()->setSortIndicator(col, asc ? Qt::AscendingOrder : Qt::DescendingOrder);
    saveGroups();
}

// ── Persistence ───────────────────────────────────────────────────────────────

void StockGroupManager::loadGroups()
{
    const QJsonArray groups = AppSettings::instance().stockGroups();
    if (groups.isEmpty()) {
        QTreeWidgetItem *fav = addGroup("Favorites", true);
        for (const QString &sym : kDefaultStocks) addStockToGroup(fav, sym);
        saveGroups();
        return;
    }
    for (const QJsonValue &gv : groups) {
        const QJsonObject g = gv.toObject();
        QTreeWidgetItem *group = addGroup(g["name"].toString(),
                                          g["expanded"].toBool(true));
        for (const QJsonValue &sv : g["stocks"].toArray()) {
            const QJsonObject s = sv.toObject();
            addStockToGroup(group,
                            s["sym"].toString(),
                            s["star"].toInt(),
                            s["purPrice"].toDouble(),
                            s["purDate"].toString());
        }
    }
}

void StockGroupManager::saveGroups()
{
    QJsonArray groups;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_tree->topLevelItem(i);
        QJsonObject g;
        g["name"]     = group->data(0, Qt::UserRole).toString();
        g["expanded"] = group->isExpanded();
        QJsonArray stocks;
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *child = group->child(j);
            QJsonObject s;
            s["sym"]      = child->text(3);
            s["star"]     = child->data(1, StarRole).toInt();
            s["purPrice"] = child->data(6, PurPriceRole).toDouble();
            s["purDate"]  = child->data(7, PurDateRole).toString();
            stocks.append(s);
        }
        g["stocks"] = stocks;
        groups.append(g);
    }
    AppSettings::instance().setStockGroups(groups);
}

// ── Visual updates ────────────────────────────────────────────────────────────

void StockGroupManager::updateTreeItemIcon(const QString &symbol)
{
    QIcon icon;
    if (m_symbolErrors.contains(symbol))
        icon = makeErrorIcon();
    else if (m_cache->symbolTypes().contains(symbol))
        icon = makeTypeIcon(m_cache->symbolTypes()[symbol]);

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *child = group->child(j);
            if (child->text(3) == symbol) {
                child->setIcon(2, icon);
                child->setText(4, m_cache->ageString(symbol));
                if (m_cache->cache().contains(symbol) && !m_cache->cache()[symbol].isEmpty())
                    child->setText(5, QString("$%1").arg(m_cache->cache()[symbol].last().price, 0, 'f', 2));
                else
                    child->setText(5, QString());
                const QBrush ageBrush = ageBgBrush(m_cache, symbol);
                child->setBackground(3, ageBrush);
                child->setBackground(4, ageBrush);
                child->setBackground(5, ageBrush);
            }
        }
    }
}

void StockGroupManager::refreshAllStockCacheVisuals()
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *child = group->child(j);
            const QString sym = child->text(3);
            child->setText(4, m_cache->ageString(sym));
            if (m_cache->cache().contains(sym) && !m_cache->cache()[sym].isEmpty())
                child->setText(5, QString("$%1").arg(m_cache->cache()[sym].last().price, 0, 'f', 2));
            else
                child->setText(5, QString());
            const QBrush ageBrush = ageBgBrush(m_cache, sym);
            child->setBackground(3, ageBrush);
            child->setBackground(4, ageBrush);
            child->setBackground(5, ageBrush);
        }
    }
}

// ── Selection ─────────────────────────────────────────────────────────────────

QStringList StockGroupManager::selectedSymbols() const
{
    QStringList syms;
    for (const QTreeWidgetItem *item : m_tree->selectedItems())
        if (item->parent())
            syms << item->text(3);
    return syms;
}

void StockGroupManager::selectSymbols(const QStringList &symbols)
{
    if (symbols.isEmpty()) return;
    QSignalBlocker blocker(m_tree);
    m_tree->clearSelection();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *child = group->child(j);
            if (symbols.contains(child->text(3)))
                child->setSelected(true);
        }
    }
}


const QTreeWidgetItem* StockGroupManager::findSymbol(const QTreeWidgetItem& group, const QString& symbol) const
{
    for (int j = 0; j < group.childCount(); ++j) {
        QTreeWidgetItem *child = group.child(j);
        if (symbol == child->text(3))
            return child;
    }
    return nullptr;
}
