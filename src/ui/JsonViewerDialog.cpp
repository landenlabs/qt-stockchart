#include "JsonViewerDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFont>
#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QLabel>
#include <QRegularExpression>
#include <QTextDocument>
#include <QColor>
#include <QFileDialog>
#include <QFile>
#include <QMessageBox>

JsonViewerDialog::JsonViewerDialog(const QString &title, const QByteArray &data,
                                   QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    resize(720, 540);

    auto *layout = new QVBoxLayout(this);

    // Size info
    auto *sizeLabel = new QLabel(
        QString("Response size: %1 bytes").arg(data.size()), this);
    QFont sf = sizeLabel->font();
    sf.setPointSize(sf.pointSize() - 1);
    sizeLabel->setFont(sf);
    layout->addWidget(sizeLabel);

    // Search bar
    auto *searchRow = new QHBoxLayout;
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search (regex)...");
    auto *prevBtn = new QPushButton("Previous", this);
    auto *nextBtn = new QPushButton("Next", this);
    m_matchLabel  = new QLabel(this);
    m_matchLabel->setMinimumWidth(90);
    searchRow->addWidget(m_searchEdit);
    searchRow->addWidget(prevBtn);
    searchRow->addWidget(nextBtn);
    searchRow->addWidget(m_matchLabel);
    layout->addLayout(searchRow);

    // Viewer
    m_viewer = new QTextEdit(this);
    m_viewer->setReadOnly(true);
    QFont mono;
    mono.setFamilies({"Menlo", "Courier New", "Monospace"});
    mono.setPointSize(11);
    m_viewer->setFont(mono);

    // Try to pretty-print as JSON; fall back to raw UTF-8 text (e.g. HTML from YahooPage)
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isNull())
        m_viewer->setPlainText(doc.toJson(QJsonDocument::Indented));
    else
        m_viewer->setPlainText(QString::fromUtf8(data));

    layout->addWidget(m_viewer);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *copyBtn = btns->addButton("Copy", QDialogButtonBox::ActionRole);
    auto *saveBtn = btns->addButton("Save…", QDialogButtonBox::ActionRole);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_viewer->toPlainText());
    });
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, "Save Response", QString(),
            "JSON files (*.json);;Text files (*.txt);;All files (*)");
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, "Save Failed",
                                 "Could not open file for writing:\n" + f.errorString());
            return;
        }
        f.write(m_viewer->toPlainText().toUtf8());
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(btns);

    // Search connections
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &JsonViewerDialog::runSearch);
    connect(nextBtn,  &QPushButton::clicked, this, &JsonViewerDialog::nextMatch);
    connect(prevBtn,  &QPushButton::clicked, this, &JsonViewerDialog::prevMatch);
    // Clear stale results when the pattern is edited
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        m_matches.clear();
        m_currentMatch = -1;
        m_viewer->setExtraSelections({});
        m_matchLabel->setText("");
    });
}

void JsonViewerDialog::runSearch()
{
    m_matches.clear();
    m_currentMatch = -1;

    const QString pattern = m_searchEdit->text().trimmed();
    if (pattern.isEmpty()) {
        m_viewer->setExtraSelections({});
        m_matchLabel->setText("");
        return;
    }

    QRegularExpression re(pattern);
    if (!re.isValid()) {
        m_matchLabel->setText("invalid regex");
        return;
    }

    QTextDocument *doc = m_viewer->document();
    QTextCursor c = doc->find(re);
    while (!c.isNull()) {
        m_matches.append(c);
        c = doc->find(re, c);
    }

    if (m_matches.isEmpty()) {
        m_matchLabel->setText("0 matches");
        m_viewer->setExtraSelections({});
        return;
    }

    m_currentMatch = 0;
    applyHighlights();
}

void JsonViewerDialog::nextMatch()
{
    if (m_matches.isEmpty()) { runSearch(); return; }
    m_currentMatch = (m_currentMatch + 1) % m_matches.size();
    applyHighlights();
}

void JsonViewerDialog::prevMatch()
{
    if (m_matches.isEmpty()) { runSearch(); return; }
    m_currentMatch = (m_currentMatch - 1 + m_matches.size()) % m_matches.size();
    applyHighlights();
}

void JsonViewerDialog::applyHighlights()
{
    QList<QTextEdit::ExtraSelection> selections;

    QTextCharFormat allFmt;
    allFmt.setBackground(QColor("#FFFFAA"));   // pale yellow for all matches

    QTextCharFormat curFmt;
    curFmt.setBackground(QColor("#FFA500"));   // orange for the current match
    curFmt.setForeground(Qt::black);

    for (int i = 0; i < m_matches.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = m_matches[i];
        sel.format = (i == m_currentMatch) ? curFmt : allFmt;
        selections.append(sel);
    }

    m_viewer->setExtraSelections(selections);

    if (m_currentMatch >= 0) {
        m_viewer->setTextCursor(m_matches[m_currentMatch]);
        m_viewer->ensureCursorVisible();
        m_matchLabel->setText(QString("%1 / %2").arg(m_currentMatch + 1).arg(m_matches.size()));
    }
}
