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
#include <QCheckBox>
#include <QGridLayout>

#include "Logger.h"

static const char ALL_HEADER[] = "#Group";

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

void CsvPorter::exportGroups(QLabel *statusLabel, const QStringList& labels)
{
    QFileDialog dialogEx(m_dialogParent, "Save File", QString(), "CSV (*.csv);;Text Files (*.txt);;All Files (*)");
    dialogEx.setAcceptMode(QFileDialog::AcceptSave);
    dialogEx.setFileMode(QFileDialog::AnyFile);

    // Crucial: Native dialogs cannot be modified this way
    dialogEx.setOption(QFileDialog::DontUseNativeDialog);

    // Create your custom checkbox
    QCheckBox *incAllCb = new QCheckBox("Include purchase data", &dialogEx);
    incAllCb->setToolTip("Include purchase price and date");

    // Add the checkbox to the dialog's layout
    // QFileDialog usually uses a QGridLayout
    QGridLayout *layout = qobject_cast<QGridLayout*>(dialogEx.layout());
    if (layout) {
        // Adding to the bottom of the grid
        int row = layout->rowCount();
        layout->addWidget(incAllCb, row, 0, 1, -1);
    }

    if (dialogEx.exec() != QDialog::Accepted)
        return;

    QString path = dialogEx.selectedFiles().first();
    bool includeAll = incAllCb->isChecked();

    /*
    const QString path = QFileDialog::getSaveFileName(
        m_dialogParent, "Export Stock Groups", "stock_groups.csv",
        "CSV files (*.csv);;All files (*)");
    if (path.isEmpty()) return;
    */

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(m_dialogParent, "Export Failed",
            "Could not open file for writing:\n" + path);
        return;
    }

    QTextStream out(&file);

    if (includeAll) {
        out << ALL_HEADER << labels.join(",") << "\n";
    }


    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *group = m_tree->topLevelItem(i);
        const QString groupName = group->data(0, Qt::UserRole).toString();

        for (int j = 0; j < group->childCount(); ++j) {
            QStringList row;
            row << csvQuote(groupName);
            if (includeAll) {
                int colCnt = group->childCount();
                for (unsigned colIdx = 0; colIdx < colCnt; colIdx++)
                    row << group->child(j)->text(colIdx);
            } else {
                row << group->child(j)->text(3);
            }
            out << row.join(", ") << "\n";
        }

    }

    if (statusLabel)
        statusLabel->setText(
            QString("Exported %1 group(s) to %2")
                .arg(m_tree->topLevelItemCount())
                .arg(QFileInfo(path).fileName()));
}

// ── Import ────────────────────────────────────────────────────────────────────

void CsvPorter::importGroups(QLabel *statusLabel, const QStringList& labels)
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

    bool checkAll = true;
    bool incAll = false;

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;

        if (checkAll) {
            checkAll = false;
            incAll = line.startsWith(ALL_HEADER);
            if (incAll) {
                QString csvHeader = ALL_HEADER + labels.join(",");
                incAll = (line == csvHeader);   // TODO - verify same order as "labels"
                if (!incAll) {
                    QString errMsg = QString("CSV import format does not match, file: %s").arg(QFileInfo(path).fileName());
                    Logger::instance().append(errMsg);
                    if (statusLabel) statusLabel->setText(errMsg);
                    return;
                }
                continue;
            }
        }

        QStringList fields = csvParseLine(line);
        if (fields.isEmpty() || fields.first().isEmpty()) continue;
        const QString groupName = fields.takeFirst();

        QSet<QString> groupItems;
        QTreeWidgetItem *groupItem = existingGroups.value(groupName, nullptr);
        if (!groupItem) {
            groupItem = m_groupManager->addGroup(groupName, true);
            existingGroups[groupName] = groupItem;
            ++groupsAdded;
        } else {

        for (int j = 0; j < groupItem->childCount(); ++j)
            groupItems.insert(groupItem->child(j)->text(3));
        }

        if (incAll) {
            // , 1=start, 2=Type, 3=symbol, 4=age, 5=price, 6=purPrice, 7=purDate
            int star = -1;
            QString symbol = fields.at(3).toUpper();  // col 3 = symbol
            double purPrice = fields.at(6).toDouble();
            QString purDate = fields.at(7);

            const QTreeWidgetItem* oldItem = m_groupManager->findSymbol(*groupItem, symbol);
            if (oldItem != nullptr)
                groupItem->removeChild((QTreeWidgetItem*)oldItem);

            m_groupManager->addStockToGroup(groupItem, symbol, star, purPrice, purDate);
        } else {
            for (const QString &sym : std::as_const(fields)) {
                const QString upper = sym.toUpper();
                if (!upper.isEmpty() && !groupItems.contains(upper)) {
                    m_groupManager->addStockToGroup(groupItem, upper);
                    groupItems.insert(upper);
                    ++stocksAdded;
                }
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
