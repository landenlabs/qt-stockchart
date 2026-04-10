#include "ApiCallTracker.h"
#include "AppSettings.h"
#include "JsonViewerDialog.h"
#include "ProviderRegistry.h"
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QBoxLayout>

// Returns a compact human-readable size string (e.g. "12K", "850B", "-")
static QString sizeStr(int bytes)
{
    if (bytes <= 0) return "-";
    if (bytes < 1024) return QString::number(bytes) + "B";
    return QString::number(bytes / 1024) + "K";
}

ApiCallTracker::ApiCallTracker(const QList<StockDataProvider*> &providers,
                               QWidget *panelParent, QBoxLayout *layout,
                               QObject *parent)
    : QObject(parent)
    , m_providers(providers)
    , m_panelParent(panelParent)
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

    // ── Column header row ────────────────────────────────────────────────────
    {
        auto *hdr = new QWidget(panel);
        auto *hl  = new QHBoxLayout(hdr);
        hl->setContentsMargins(2, 0, 2, 0);
        hl->setSpacing(4);

        auto *hName  = new QLabel("Provider", hdr);
        auto *hCalls = new QLabel("Calls", hdr);
        auto *hHist  = new QLabel("History", hdr);
        auto *hQuote = new QLabel("Quote", hdr);

        QFont hf = sf;
        hf.setItalic(true);
        for (auto *lbl : {hName, hCalls, hHist, hQuote}) lbl->setFont(hf);

        hCalls->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hHist->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hQuote->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        hCalls->setFixedWidth(30);
        hHist->setFixedWidth(38);
        hQuote->setFixedWidth(38);

        hl->addWidget(hName);
        hl->addStretch();
        hl->addWidget(hCalls);
        hl->addWidget(hHist);
        hl->addWidget(hQuote);
        vl->addWidget(hdr);
    }

    for (StockDataProvider *p : m_providers) {
        auto *row = new QFrame(panel);
        row->setCursor(Qt::PointingHandCursor);

        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(2, 1, 2, 1);
        hl->setSpacing(4);

        auto *nameLabel = new QLabel(ProviderRegistry::instance().label(p->id()), row);
        nameLabel->setFont(sf);
        nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        auto *countLabel = new QLabel("0", row);
        countLabel->setFont(sf);
        countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        countLabel->setFixedWidth(30);
        countLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

        // "History" and "Quote" clickable size buttons — fix height to match label rows
        const int btnH = QFontMetrics(sf).height() + 2;

        auto *histBtn = new QPushButton("-", row);
        histBtn->setFlat(true);
        histBtn->setFont(sf);
        histBtn->setCursor(Qt::PointingHandCursor);
        histBtn->setFixedSize(38, btnH);
        histBtn->setToolTip("Click to view last fetchData JSON response");

        auto *quoteBtn = new QPushButton("-", row);
        quoteBtn->setFlat(true);
        quoteBtn->setFont(sf);
        quoteBtn->setCursor(Qt::PointingHandCursor);
        quoteBtn->setFixedSize(38, btnH);
        quoteBtn->setToolTip("Click to view last fetchLatestQuote JSON response");

        hl->addWidget(nameLabel);
        hl->addStretch();
        hl->addWidget(countLabel);
        hl->addWidget(histBtn);
        hl->addWidget(quoteBtn);

        vl->addWidget(row);
        m_nameLabels[p->id()]  = nameLabel;
        m_countLabels[p->id()] = countLabel;
        m_histBtns[p->id()]    = histBtn;
        m_quoteBtns[p->id()]   = quoteBtn;
        m_rowWidgets[p->id()]  = row;

        // Connect provider signals to update button text
        connect(p, &StockDataProvider::historyResponseStored, this, [this, p]() {
            if (auto *btn = m_histBtns.value(p->id()))
                btn->setText(sizeStr(p->lastHistoryJson().size()));
        });
        connect(p, &StockDataProvider::quoteResponseStored, this, [this, p]() {
            if (auto *btn = m_quoteBtns.value(p->id()))
                btn->setText(sizeStr(p->lastQuoteJson().size()));
        });

        // Open viewer dialog on button click
        connect(histBtn, &QPushButton::clicked, this, [this, p]() {
            const QByteArray data = p->lastHistoryJson();
            if (data.isEmpty()) return;
            auto *dlg = new JsonViewerDialog(
                ProviderRegistry::instance().label(p->id()) + " — History (fetchData) Response",
                data, m_panelParent ? m_panelParent->window() : nullptr);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });
        connect(quoteBtn, &QPushButton::clicked, this, [this, p]() {
            const QByteArray data = p->lastQuoteJson();
            if (data.isEmpty()) return;
            auto *dlg = new JsonViewerDialog(
                ProviderRegistry::instance().label(p->id()) + " — Quote (fetchLatestQuote) Response",
                data, m_panelParent ? m_panelParent->window() : nullptr);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });
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
    auto &as = AppSettings::instance();
    const QDate today    = QDate::currentDate();
    const QDate savedDay = QDate::fromString(as.dailyCallDate(), "yyyy-MM-dd");
    m_currentDay = today;
    if (savedDay != today) {
        for (StockDataProvider *p : m_providers) m_dailyCallCounts[p->id()] = 0;
    } else {
        for (StockDataProvider *p : m_providers)
            m_dailyCallCounts[p->id()] = as.dailyCallCount(p->id());
    }
}

void ApiCallTracker::saveDailyCallCounts()
{
    auto &as = AppSettings::instance();
    as.setDailyCallDate(m_currentDay.toString("yyyy-MM-dd"));
    for (StockDataProvider *p : m_providers)
        as.setDailyCallCount(p->id(), m_dailyCallCounts.value(p->id(), 0));
}
