#include "StockGroupManager.h"
#include <QSettings>
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
    if (displayName.length() > 15)
        displayName = displayName.left(12) + "...";

    auto *item = new QTreeWidgetItem(m_tree);
    item->setFlags(Qt::ItemIsEnabled);
    item->setData(0, Qt::UserRole, name);
    m_tree->setFirstColumnSpanned(m_tree->indexOfTopLevelItem(item), QModelIndex(), true);

    auto *container = new QWidget;
    container->setStyleSheet("background: transparent;");
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

    hl->addWidget(nameLabel);
    hl->addWidget(addBtn);
    hl->addStretch();

    m_tree->setItemWidget(item, 0, container);

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
    item->setData(0, StarRole, star);
    item->setIcon(0, makeStarIcon(star));

    if (m_symbolErrors.contains(symbol))
        item->setIcon(1, makeErrorIcon());
    else if (m_cache->symbolTypes().contains(symbol))
        item->setIcon(1, makeTypeIcon(m_cache->symbolTypes()[symbol]));

    item->setText(2, symbol);
    item->setText(3, m_cache->ageString(symbol));
    if (m_cache->cache().contains(symbol) && !m_cache->cache()[symbol].isEmpty())
        item->setText(4, QString("$%1").arg(m_cache->cache()[symbol].last().price, 0, 'f', 2));
    const QBrush ageBrush = ageBgBrush(m_cache, symbol);
    item->setBackground(2, ageBrush);
    item->setBackground(3, ageBrush);
    item->setBackground(4, ageBrush);
    item->setData(5, PurPriceRole, purPrice);
    if (purPrice > 0) item->setText(5, QString::number(purPrice, 'f', 2));
    item->setData(6, PurDateRole, purDate);
    item->setText(6, purDate);

    item->setTextAlignment(4, Qt::AlignRight | Qt::AlignVCenter);
    item->setTextAlignment(5, Qt::AlignRight | Qt::AlignVCenter);
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
                if (groupItem->child(i)->text(2) == sym) { exists = true; break; }
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
    dlg.setWindowTitle("Edit Details: " + item->text(2));
    QFormLayout *form = new QFormLayout(&dlg);

    QLineEdit *priceEdit = new QLineEdit(item->text(5), &dlg);
    QLineEdit *dateEdit  = new QLineEdit(item->text(6), &dlg);
    form->addRow("Purchase Price:", priceEdit);
    form->addRow("Purchase Date:", dateEdit);

    QDialogButtonBox *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted) {
        item->setText(5, priceEdit->text());
        item->setText(6, dateEdit->text());
        item->setData(5, PurPriceRole, priceEdit->text().toDouble());
        item->setData(6, PurDateRole, dateEdit->text());
        saveGroups();
    }
}

void StockGroupManager::onClearCache(QTreeWidgetItem *item)
{
    const QString sym = item->text(2);
    m_cache->clearSymbolCache(sym);
    updateTreeItemIcon(sym);
}

void StockGroupManager::onForceReload(QTreeWidgetItem *item)
{
    emit forceReloadRequested(item->text(2));
}

void StockGroupManager::onListHistorical(QTreeWidgetItem *item)
{
    const QString sym = item->text(2);
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
    item->setData(0, StarRole, starIndex);
    item->setIcon(0, makeStarIcon(starIndex));
    saveGroups();
}

void StockGroupManager::sortBySymbol()
{
    const Qt::SortOrder order = m_sortAscending ? Qt::AscendingOrder : Qt::DescendingOrder;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_tree->topLevelItem(i);
        QList<QTreeWidgetItem *> children;
        children.reserve(group->childCount());
        while (group->childCount() > 0)
            children.append(group->takeChild(0));
        std::sort(children.begin(), children.end(),
                  [this](QTreeWidgetItem *a, QTreeWidgetItem *b) {
                      return m_sortAscending ? a->text(2) < b->text(2)
                                             : a->text(2) > b->text(2);
                  });
        for (auto *child : children)
            group->addChild(child);
    }
    m_tree->header()->setSortIndicator(2, order);
    m_sortAscending = !m_sortAscending;
    saveGroups();
}

// ── Persistence ───────────────────────────────────────────────────────────────

void StockGroupManager::loadGroups()
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
        int stockCount = s.beginReadArray("stocks");
        for (int j = 0; j < stockCount; ++j) {
            s.setArrayIndex(j);
            addStockToGroup(group,
                            s.value("sym").toString(),
                            s.value("star").toInt(),
                            s.value("purPrice").toDouble(),
                            s.value("purDate").toString());
        }
        s.endArray();
    }
    s.endArray();
}

void StockGroupManager::saveGroups()
{
    QSettings s("StockChart", "StockChart");
    s.remove("stockGroups");
    s.beginWriteArray("stockGroups");
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        s.setArrayIndex(i);
        QTreeWidgetItem *group = m_tree->topLevelItem(i);
        s.setValue("name",     group->data(0, Qt::UserRole).toString());
        s.setValue("expanded", group->isExpanded());
        s.beginWriteArray("stocks");
        for (int j = 0; j < group->childCount(); ++j) {
            s.setArrayIndex(j);
            QTreeWidgetItem *child = group->child(j);
            s.setValue("sym",      child->text(2));
            s.setValue("star",     child->data(0, StarRole).toInt());
            s.setValue("purPrice", child->data(5, PurPriceRole).toDouble());
            s.setValue("purDate",  child->data(6, PurDateRole).toString());
        }
        s.endArray();
    }
    s.endArray();
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
            if (child->text(2) == symbol) {
                child->setIcon(1, icon);
                child->setText(3, m_cache->ageString(symbol));
                if (m_cache->cache().contains(symbol) && !m_cache->cache()[symbol].isEmpty())
                    child->setText(4, QString("$%1").arg(m_cache->cache()[symbol].last().price, 0, 'f', 2));
                else
                    child->setText(4, QString());
                const QBrush ageBrush = ageBgBrush(m_cache, symbol);
                child->setBackground(2, ageBrush);
                child->setBackground(3, ageBrush);
                child->setBackground(4, ageBrush);
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
            const QString sym = child->text(2);
            child->setText(3, m_cache->ageString(sym));
            if (m_cache->cache().contains(sym) && !m_cache->cache()[sym].isEmpty())
                child->setText(4, QString("$%1").arg(m_cache->cache()[sym].last().price, 0, 'f', 2));
            else
                child->setText(4, QString());
            const QBrush ageBrush = ageBgBrush(m_cache, sym);
            child->setBackground(2, ageBrush);
            child->setBackground(3, ageBrush);
            child->setBackground(4, ageBrush);
        }
    }
}

// ── Selection ─────────────────────────────────────────────────────────────────

QStringList StockGroupManager::selectedSymbols() const
{
    QStringList syms;
    for (const QTreeWidgetItem *item : m_tree->selectedItems())
        if (item->parent())
            syms << item->text(2);
    return syms;
}
