#pragma once
#include <QObject>
#include <QDate>
#include <QMap>
#include <QList>
#include "StockDataProvider.h"

class QWidget;
class QBoxLayout;
class QLabel;

// Manages daily API call counts and renders the info panel at the bottom of the left sidebar.
class ApiCallTracker : public QObject
{
    Q_OBJECT
public:
    explicit ApiCallTracker(const QList<StockDataProvider*> &providers,
                            QWidget *panelParent, QBoxLayout *layout,
                            QObject *parent = nullptr);

    void loadDailyCallCounts();
    void saveDailyCallCounts();
    void incrementCallCount(const QString &providerId);
    void updatePanel(const QString &activeProviderId);

    // Exposed so MainWindow::eventFilter can identify provider row clicks
    QWidget *rowWidget(const QString &providerId) const;

private:
    QList<StockDataProvider*>  m_providers;
    QDate                      m_currentDay;
    QMap<QString, int>         m_dailyCallCounts;
    QMap<QString, QLabel*>     m_nameLabels;
    QMap<QString, QLabel*>     m_countLabels;
    QMap<QString, QWidget*>    m_rowWidgets;
};
