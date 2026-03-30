#include "CsvPorter.h"
#include "StockGroupManager.h"
#include <QTreeWidget>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QSet>
#include <QMap>

CsvPorter::CsvPorter(QTreeWidget *tree, StockGroupManager *groupManager, QWidget *dialogParent)
    : m_tree(tree)
    , m_groupManager(groupManager)
    , m_dialogParent(dialogParent)
{
}

// ── Static helpers ────────────────────────────────────────────────────────────

QString CsvPorter::csvQuote(const QString &s)
{
    QString escaped = s;
    escaped.replace('"', "\"\"");
    return '"' + escaped + '"';
}

QStringList CsvPorter::csvParseLine(const QString &line)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (inQuotes) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current += '"';
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

// ── Export ────────────────────────────────────────────────────────────────────

void CsvPorter::exportGroups(QLabel *statusLabel)
{
    const QString path = QFileDialog::getSaveFileName(
        m_dialogParent, "Export Stock Groups", "stock_groups.csv",
        "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(m_dialogParent, "Export Failed",
            "Could not open file for writing:\n" + path);
        return;
    }

    QTextStream out(&file);
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *group = m_tree->topLevelItem(i);
        const QString groupName = group->data(0, Qt::UserRole).toString();

        QStringList row;
        row << csvQuote(groupName);
        for (int j = 0; j < group->childCount(); ++j)
            row << group->child(j)->text(2);

        out << row.join(", ") << "\n";
    }

    if (statusLabel)
        statusLabel->setText(
            QString("Exported %1 group(s) to %2")
                .arg(m_tree->topLevelItemCount())
                .arg(QFileInfo(path).fileName()));
}

// ── Import ────────────────────────────────────────────────────────────────────

void CsvPorter::importGroups(QLabel *statusLabel)
{
    const QString path = QFileDialog::getOpenFileName(
        m_dialogParent, "Import Stock Groups", QString(),
        "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(m_dialogParent, "Import Failed",
            "Could not open file:\n" + path);
        return;
    }

    QMap<QString, QTreeWidgetItem*> existingGroups;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *g = m_tree->topLevelItem(i);
        existingGroups[g->data(0, Qt::UserRole).toString()] = g;
    }

    QTextStream in(&file);
    int groupsAdded = 0, stocksAdded = 0;

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        QStringList fields = csvParseLine(line);
        if (fields.isEmpty() || fields.first().isEmpty()) continue;

        const QString groupName = fields.takeFirst();

        QTreeWidgetItem *groupItem = existingGroups.value(groupName, nullptr);
        if (!groupItem) {
            groupItem = m_groupManager->addGroup(groupName, true);
            existingGroups[groupName] = groupItem;
            ++groupsAdded;
        }

        QSet<QString> existing;
        for (int j = 0; j < groupItem->childCount(); ++j)
            existing.insert(groupItem->child(j)->text(2));

        for (const QString &sym : std::as_const(fields)) {
            const QString upper = sym.toUpper();
            if (!upper.isEmpty() && !existing.contains(upper)) {
                m_groupManager->addStockToGroup(groupItem, upper);
                existing.insert(upper);
                ++stocksAdded;
            }
        }
    }

    m_groupManager->saveGroups();
    if (statusLabel)
        statusLabel->setText(
            QString("Imported from %1 — %2 new group(s), %3 new stock(s) added")
                .arg(QFileInfo(path).fileName())
                .arg(groupsAdded)
                .arg(stocksAdded));
}
