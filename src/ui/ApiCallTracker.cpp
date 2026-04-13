#include "ApiCallTracker.h"
#include "AppSettings.h"
#include "JsonViewerDialog.h"
#include "ProviderRegistry.h"
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>

// Returns a compact human-readable size string (e.g. "12K", "850B", "-")
static QString sizeStr(int bytes)
{
    if (bytes <= 0) return "-";
    if (bytes < 1024) return QString::number(bytes) + "B";
    return QString::number(bytes / 1024) + "K";
}

ApiCallTracker::ApiCallTracker(const QList<StockDataProvider*> &providers,
                               QWidget *panelParent,
                               QObject *parent)
    : QObject(parent)
    , m_providers(providers)
    , m_panelParent(panelParent)
{
    // ── Build the panel widget ────────────────────────────────────────────────

    m_scrollArea = new QScrollArea(panelParent);
    m_scrollArea->setWidgetResizable(true); // Crucial: allows panel to expand
    m_scrollArea->setFrameShape(QFrame::NoFrame); // Clean look

    auto *panel = new QFrame(panelParent);
    panel->setFrameShape(QFrame::StyledPanel);
    panel->setFrameShadow(QFrame::Sunken);
    panel->setMinimumWidth(300); // Forces scrollbar if viewport < 200

    auto *vl = new QVBoxLayout(panel);
    vl->setContentsMargins(6, 4, 6, 4);
    vl->setSpacing(0);

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

    const unsigned NAM_W = 80;
    const unsigned CALL_W = 26;
    const unsigned OTH_W = 32;

    // ── Column header row ────────────────────────────────────────────────────
    {
        auto *hdr = new QWidget(panel);
        auto *hl  = new QHBoxLayout(hdr);
        hl->setContentsMargins(2, 0, 2, 0);
        hl->setSpacing(4);

        auto *hName  = new QLabel("Provider", hdr);
        auto *hCalls = new QLabel("Calls", hdr);
        auto *hHist  = new QLabel("Hours", hdr);
        auto *hQuote = new QLabel("Quote", hdr);

        QFont hf = sf;
        hf.setItalic(true);
        for (auto *lbl : {hName, hCalls, hHist, hQuote}) lbl->setFont(hf);

        hCalls->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hHist->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hQuote->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // 1. Set the fixed width for the Provider column
        hName->setFixedWidth(NAM_W);

        // 2. Set the minimum width for the numeric columns
        hCalls->setMinimumWidth(CALL_W);
        hHist->setMinimumWidth(OTH_W);
        hQuote->setMinimumWidth(OTH_W);

        // 3. Add to layout with equal stretch factors
        hl->addWidget(hName,  0); // Stretch 0 = only take what is required (50px)
        hl->addWidget(hCalls, 1); // Stretch 1 = share remaining space equally
        hl->addWidget(hHist,  1);
        hl->addWidget(hQuote, 1);

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
        nameLabel->setFixedWidth(NAM_W);

        auto *countLabel = new QLabel("0", row);
        countLabel->setFont(sf);
        countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        countLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        // countLabel->setFixedWidth(30);
        countLabel->setMinimumWidth(CALL_W);

        // "History" and "Quote" clickable size buttons — fix height to match label rows
        const int btnH = QFontMetrics(sf).height();

        auto *histBtn = new QPushButton("", row);
        histBtn->setFlat(true); // Removes the border/background
        histBtn->setStyleSheet("border: none; color: blue; text-decoration: underline;");
        histBtn->setFont(sf);
        histBtn->setCursor(Qt::PointingHandCursor);
        // histBtn->setFixedSize(38, btnH);
        histBtn->setMinimumWidth(OTH_W);
        // histBtn->setFixedHeight(btnH);
        histBtn->setToolTip("Click to view history fetch response");

        auto *quoteBtn = new QPushButton("", row);
        quoteBtn->setFlat(true); // Removes the border/background
        quoteBtn->setStyleSheet("border: none; color: blue; text-decoration: underline;");
        quoteBtn->setFont(sf);
        quoteBtn->setCursor(Qt::PointingHandCursor);
        // quoteBtn->setFixedSize(38, btnH);
        quoteBtn->setMinimumWidth(OTH_W);
        // quoteBtn->setFixedHeight(btnH);
        quoteBtn->setToolTip("Click to view quote response");

        hl->addWidget(nameLabel,  0); // Stretch 0 = only take what is required (50px)
        hl->addWidget(countLabel, 1); // Stretch 1 = share remaining space equally
        hl->addWidget(histBtn,  1);
        hl->addWidget(quoteBtn, 1);


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

    vl->addStretch(1);  // fill bottom with blank causing rows to back tight at top.

    m_scrollArea->setWidget(panel);
}

QWidget *ApiCallTracker::rowWidget(const QString &providerId) const
{
    return m_rowWidgets.value(providerId, nullptr);
}

QWidget *ApiCallTracker::panelWidget() const
{
    return m_scrollArea;
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
