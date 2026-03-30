#include "ApiCallTracker.h"
#include <QSettings>
#include <QFrame>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QBoxLayout>

ApiCallTracker::ApiCallTracker(const QList<StockDataProvider*> &providers,
                               QWidget *panelParent, QBoxLayout *layout,
                               QObject *parent)
    : QObject(parent)
    , m_providers(providers)
{
    // ── Build the panel widget ────────────────────────────────────────────────
    auto *panel = new QFrame(panelParent);
    panel->setFrameShape(QFrame::StyledPanel);
    panel->setFrameShadow(QFrame::Sunken);

    auto *vl = new QVBoxLayout(panel);
    vl->setContentsMargins(6, 4, 6, 4);
    vl->setSpacing(3);

    auto *title = new QLabel("API Calls Today", panel);
    QFont tf = title->font();
    tf.setPointSize(tf.pointSize() - 1);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    vl->addWidget(title);

    auto *sep = new QFrame(panel);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    vl->addWidget(sep);

    QFont sf;
    sf.setPointSize(sf.pointSize() - 1);

    for (StockDataProvider *p : m_providers) {
        auto *row = new QFrame(panel);
        row->setCursor(Qt::PointingHandCursor);

        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(2, 1, 2, 1);
        hl->setSpacing(4);

        auto *nameLabel = new QLabel(p->displayName(), row);
        nameLabel->setFont(sf);
        nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        auto *countLabel = new QLabel("0", row);
        countLabel->setFont(sf);
        countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        countLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        hl->addWidget(nameLabel);
        hl->addStretch();
        hl->addWidget(countLabel);

        vl->addWidget(row);
        m_nameLabels[p->id()]  = nameLabel;
        m_countLabels[p->id()] = countLabel;
        m_rowWidgets[p->id()]  = row;
    }

    layout->addWidget(panel);
}

QWidget *ApiCallTracker::rowWidget(const QString &providerId) const
{
    return m_rowWidgets.value(providerId, nullptr);
}

void ApiCallTracker::updatePanel(const QString &activeProviderId)
{
    QFont normalFont, boldFont;
    boldFont.setPointSize(normalFont.pointSize() - 1);
    boldFont.setBold(true);
    normalFont.setPointSize(normalFont.pointSize() - 1);

    for (StockDataProvider *p : m_providers) {
        const bool active     = (p->id() == activeProviderId);
        const bool configured = p->hasCredentials();

        if (auto *nl = m_nameLabels.value(p->id()))
            nl->setFont(active ? boldFont : normalFont);

        if (auto *cl = m_countLabels.value(p->id())) {
            cl->setFont(active ? boldFont : normalFont);
            cl->setText(QString::number(m_dailyCallCounts.value(p->id(), 0)));
        }

        if (auto *row = m_rowWidgets.value(p->id())) {
            row->setStyleSheet(configured
                ? ""
                : "QFrame { background-color: rgba(200, 60, 60, 0.12); border-radius: 3px; }");
        }
    }
}

void ApiCallTracker::incrementCallCount(const QString &providerId)
{
    const QDate today = QDate::currentDate();
    if (m_currentDay != today) {
        m_currentDay = today;
        for (StockDataProvider *p : m_providers) m_dailyCallCounts[p->id()] = 0;
    }
    m_dailyCallCounts[providerId]++;
    saveDailyCallCounts();
}

void ApiCallTracker::loadDailyCallCounts()
{
    QSettings s("StockChart", "StockChart");
    s.beginGroup("dailyCalls");
    const QDate today    = QDate::currentDate();
    const QDate savedDay = QDate::fromString(s.value("date").toString(), "yyyy-MM-dd");
    m_currentDay = today;
    if (savedDay != today) {
        for (StockDataProvider *p : m_providers) m_dailyCallCounts[p->id()] = 0;
    } else {
        for (StockDataProvider *p : m_providers)
            m_dailyCallCounts[p->id()] = s.value(p->id(), 0).toInt();
    }
    s.endGroup();
}

void ApiCallTracker::saveDailyCallCounts()
{
    QSettings s("StockChart", "StockChart");
    s.beginGroup("dailyCalls");
    s.setValue("date", m_currentDay.toString("yyyy-MM-dd"));
    for (StockDataProvider *p : m_providers)
        s.setValue(p->id(), m_dailyCallCounts.value(p->id(), 0));
    s.endGroup();
}
