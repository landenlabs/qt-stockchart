#pragma once
#include <QObject>
#include <QTreeWidget>
#include <QIcon>
#include <QSet>
#include "StockCacheManager.h"
#include "StockDataProvider.h"

class QWidget;
class QBoxLayout;

// Manages the left-panel tree widget: groups, stocks, icons, and QSettings persistence.
class StockGroupManager : public QObject
{
    Q_OBJECT
public:
    // Roles stored on QTreeWidgetItem data
    enum TreeRoles {
        StarRole     = Qt::UserRole + 10,
        PurPriceRole,
        PurDateRole
    };

    explicit StockGroupManager(QTreeWidget *tree, StockCacheManager *cache,
                               QWidget *dialogParent, QObject *parent = nullptr);

    // Tree building
    QTreeWidgetItem *addGroup(const QString &name, bool expanded = true);
    void addStockToGroup(QTreeWidgetItem *groupItem, const QString &symbol,
                         int star = 0, double purPrice = 0.0, const QString &purDate = "");

    // Persistence
    void loadGroups();
    void saveGroups();

    // Visual updates
    void updateTreeItemIcon(const QString &symbol);
    void refreshAllStockCacheVisuals();

    // Selection
    QStringList selectedSymbols() const;

    // Error tracking (maintained here, but MainWindow sets errors on onError)
    QSet<QString> &symbolErrors()             { return m_symbolErrors; }
    const QSet<QString> &symbolErrors() const { return m_symbolErrors; }

    // Icon factories (static, used by other code too)
    static QIcon makeStarIcon(int index);
    static QIcon makeTypeIcon(SymbolType type);
    static QIcon makeErrorIcon();

signals:
    void forceReloadRequested(const QString &symbol);

public slots:
    void onAddGroupClicked();
    void onTreeContextMenu(const QPoint &pos);
    void onEditStockDetails(QTreeWidgetItem *item);
    void onSetStar(QTreeWidgetItem *item, int starIndex);
    void showAddStockDialog(QTreeWidgetItem *groupItem);

private:
    void onClearCache(QTreeWidgetItem *item);
    void onForceReload(QTreeWidgetItem *item);
    void onListHistorical(QTreeWidgetItem *item);
    QTreeWidget      *m_tree;
    StockCacheManager *m_cache;
    QWidget          *m_dialogParent;
    QSet<QString>     m_symbolErrors;
};
