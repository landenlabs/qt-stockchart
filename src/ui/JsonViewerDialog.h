#pragma once
#include <QDialog>
#include <QByteArray>
#include <QString>
#include <QList>
#include <QTextCursor>

class QTextEdit;
class QLineEdit;
class QLabel;

// Modal dialog that displays a raw JSON (or HTML) response in a readable, formatted view.
// Attempts to pretty-print as JSON; falls back to plain UTF-8 text.
class JsonViewerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit JsonViewerDialog(const QString &title, const QByteArray &data,
                              QWidget *parent = nullptr);

private slots:
    void runSearch();
    void nextMatch();
    void prevMatch();

private:
    void applyHighlights();

    QTextEdit *m_viewer     = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel    *m_matchLabel = nullptr;
    QList<QTextCursor> m_matches;
    int m_currentMatch = -1;
};
