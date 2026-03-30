#pragma once
#include <QString>
#include <QStringList>

class QTreeWidget;
class QLabel;
class StockGroupManager;
class QWidget;

// Handles CSV export and import of stock groups.
class CsvPorter
{
public:
    explicit CsvPorter(QTreeWidget *tree, StockGroupManager *groupManager, QWidget *dialogParent);

    void exportGroups(QLabel *statusLabel);
    void importGroups(QLabel *statusLabel);

    static QString     csvQuote(const QString &s);
    static QStringList csvParseLine(const QString &line);

private:
    QTreeWidget       *m_tree;
    StockGroupManager *m_groupManager;
    QWidget           *m_dialogParent;
};
