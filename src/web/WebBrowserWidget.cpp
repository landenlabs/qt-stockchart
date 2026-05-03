#include "WebBrowserWidget.h"
#include "RequestInterceptor.h"
#include "AppSettings.h"
#include "Logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineLoadingInfo>
#include <QUrl>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>

WebBrowserWidget::WebBrowserWidget(QWidget *parent)
    : QWidget(parent)
{
    loadTabDefinitions();

    // ── Profile + interceptor ─────────────────────────────────────────────────
    m_profile     = new QWebEngineProfile("StockChartBrowser", this);
    m_interceptor = new RequestInterceptor(this);
    m_profile->setUrlRequestInterceptor(m_interceptor);

    // ── Web view ──────────────────────────────────────────────────────────────
    m_webView = new QWebEngineView(this);
    auto *page = new QWebEnginePage(m_profile, m_webView);
    m_webView->setPage(page);

    // ── Tab bar row (tabs + "+" button) ───────────────────────────────────────
    auto *tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->setSpacing(0);

    m_tabBar = new QTabBar(this);
    m_tabBar->setExpanding(false);
    m_tabBar->setTabsClosable(true);
    for (const auto &tab : m_tabs) {
        const int idx = m_tabBar->addTab(tab.name);
        if (!tab.comment.isEmpty())
            m_tabBar->setTabToolTip(idx, tab.comment);
        // Fixed (configured) tabs have no close button.
        if (tab.fixed)
            m_tabBar->setTabButton(idx, QTabBar::RightSide, nullptr);
    }

    m_addTabBtn = new QToolButton(this);
    m_addTabBtn->setText("+");
    m_addTabBtn->setToolTip("Open new browser tab");
    m_addTabBtn->setAutoRaise(true);
    m_addTabBtn->setFixedSize(24, m_tabBar->sizeHint().height());

    tabRow->addWidget(m_tabBar);
    tabRow->addWidget(m_addTabBtn);
    tabRow->addStretch();

    // ── URL bar ───────────────────────────────────────────────────────────────
    m_urlBar = new QLineEdit(this);
    m_urlBar->setPlaceholderText("Enter URL or search…");
    m_urlBar->setClearButtonEnabled(true);

    // ── Assemble layout ───────────────────────────────────────────────────────
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(tabRow);
    layout->addWidget(m_urlBar);
    layout->addWidget(m_webView, 1);

    // ── Signals ───────────────────────────────────────────────────────────────
    connect(m_tabBar,    &QTabBar::currentChanged,    this, &WebBrowserWidget::onTabChanged);
    connect(m_tabBar,    &QTabBar::tabCloseRequested, this, &WebBrowserWidget::onCloseTab);
    connect(m_addTabBtn, &QToolButton::clicked,       this, &WebBrowserWidget::onAddTab);
    connect(m_urlBar,    &QLineEdit::returnPressed,   this, &WebBrowserWidget::onUrlBarReturnPressed);

    // Keep URL bar in sync with actual page URL (redirects, JS navigation, etc.)
    connect(m_webView, &QWebEngineView::urlChanged, this, [this](const QUrl &url) {
        if (url.isEmpty() || url == QUrl("about:blank")) return;
        m_urlBar->setText(url.toString());
        // Persist the current URL for generic tabs so we can restore it on tab switch.
        const int idx = m_tabBar->currentIndex();
        if (idx >= 0 && idx < m_tabs.size() && !m_tabs[idx].fixed)
            m_tabs[idx].lastUrl = url.toString();
    });

    // After each page load, repopulate the ad-block dialog's active domain list.
    connect(page, &QWebEnginePage::loadFinished, this, [this](bool) {
        if (m_adBlockDialog)
            m_adBlockDialog->refreshActiveList();
    });

    // ── Loading log ───────────────────────────────────────────────────────────
    connect(page, &QWebEnginePage::loadingChanged,
            this, [this](const QWebEngineLoadingInfo &info) {
        switch (info.status()) {
        case QWebEngineLoadingInfo::LoadStartedStatus:
            Logger::instance().append("Browser: loading " + info.url().toString());
            break;
        case QWebEngineLoadingInfo::LoadSucceededStatus:
            Logger::instance().append("Browser: OK \xe2\x80\x94 " + info.url().toString());
            break;
        case QWebEngineLoadingInfo::LoadFailedStatus:
            Logger::instance().append(
                QString("Browser: failed [%1] \xe2\x80\x94 %2")
                    .arg(info.errorString(), info.url().toString()));
            break;
        default:
            break;
        }
    });
}

// ── Public API ────────────────────────────────────────────────────────────────

void WebBrowserWidget::setSymbol(const QString &symbol)
{
    if (m_symbol == symbol) return;
    m_symbol = symbol;
    loadCurrentTab();
}

void WebBrowserWidget::openAdBlockDialog()
{
    if (!m_adBlockDialog) {
        m_adBlockDialog = new AdBlockDialog(m_interceptor, window());
        m_adBlockDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_adBlockDialog, &QDialog::finished, this, [this]() { saveBlacklist(); });
        connect(m_adBlockDialog, &AdBlockDialog::reloadRequested, this, [this]() { m_webView->reload(); });
    }
    m_adBlockDialog->show();
    m_adBlockDialog->raise();
    m_adBlockDialog->activateWindow();
}

void WebBrowserWidget::saveBlacklist() const
{
    const QSet<QString> bl = m_interceptor->blacklist();
    AppSettings::instance().setAdBlockBlacklist(QStringList(bl.cbegin(), bl.cend()));
}

void WebBrowserWidget::loadBlacklist()
{
    const QStringList list = AppSettings::instance().adBlockBlacklist();
    m_interceptor->setBlacklist(QSet<QString>(list.cbegin(), list.cend()));
}

// ── Private slots ─────────────────────────────────────────────────────────────

void WebBrowserWidget::onTabChanged(int idx)
{
    if (idx < 0 || idx >= m_tabs.size()) return;

    if (m_tabs[idx].fixed) {
        // Financial tab: reload from current symbol.
        loadCurrentTab();
    } else {
        // Generic tab: restore last URL, or show blank.
        m_interceptor->clearTracking();
        if (!m_tabs[idx].lastUrl.isEmpty()) {
            m_urlBar->setText(m_tabs[idx].lastUrl);
            m_webView->load(QUrl(m_tabs[idx].lastUrl));
        } else {
            m_urlBar->clear();
            m_webView->load(QUrl("about:blank"));
            m_urlBar->setFocus();
        }
    }
}

void WebBrowserWidget::onAddTab()
{
    m_tabs.append({ "New Tab", "", "", false, "" });
    const int idx = m_tabBar->addTab("New Tab");
    m_tabBar->setCurrentIndex(idx);
    // onTabChanged fires and handles the blank-page / URL-bar-focus logic.
}

void WebBrowserWidget::onCloseTab(int idx)
{
    if (idx < 0 || idx >= m_tabs.size()) return;
    if (m_tabs[idx].fixed) return; // configured tabs cannot be removed
    m_tabs.removeAt(idx);
    m_tabBar->removeTab(idx);
}

void WebBrowserWidget::onUrlBarReturnPressed()
{
    const QString text = m_urlBar->text().trimmed();
    if (text.isEmpty()) return;
    m_interceptor->clearTracking();
    m_webView->load(QUrl::fromUserInput(text));
}

// ── Private helpers ───────────────────────────────────────────────────────────

void WebBrowserWidget::loadCurrentTab()
{
    const int idx = m_tabBar->currentIndex();
    if (idx < 0 || idx >= m_tabs.size()) return;
    if (!m_tabs[idx].fixed) return; // generic tab — URL bar drives navigation
    if (m_symbol.isEmpty()) return;
    m_interceptor->clearTracking();
    const QString url = buildUrl(idx);
    if (!url.isEmpty()) {
        m_urlBar->setText(url);
        m_webView->load(QUrl(url));
    }
}

QString WebBrowserWidget::buildUrl(int tabIndex) const
{
    if (tabIndex < 0 || tabIndex >= m_tabs.size()) return {};
    QString url = m_tabs[tabIndex].urlPattern;
    url.replace("{upper}", m_symbol.toUpper());
    url.replace("{lower}", m_symbol.toLower());
    return url;
}

void WebBrowserWidget::loadTabDefinitions()
{
    // User override: a web-pages.json next to the executable takes priority.
    const QString userPath = QCoreApplication::applicationDirPath() + "/web-pages.json";
    QByteArray raw;

    if (QFile::exists(userPath)) {
        QFile f(userPath);
        if (f.open(QIODevice::ReadOnly))
            raw = f.readAll();
    }

    if (raw.isEmpty()) {
        QFile f(":/web-pages.json");
        if (f.open(QIODevice::ReadOnly))
            raw = f.readAll();
    }

    if (!raw.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        const QJsonArray tabs = doc.object().value("tabs").toArray();
        for (const QJsonValue &v : tabs) {
            const QJsonObject obj = v.toObject();
            if (!obj.value("enabled").toBool(true)) continue;
            m_tabs.append({ obj.value("name").toString(),
                            obj.value("url").toString(),
                            obj.value("comment").toString(),
                            true, "" });
        }
    }

    if (m_tabs.isEmpty()) {
        m_tabs = {
            { "Morningstar", "https://www.morningstar.com/search?query={upper}",    "", true, "" },
            { "TradingView", "https://www.tradingview.com/symbols/{upper}/",        "", true, "" },
            { "StockCharts", "https://stockcharts.com/sc3/ui/?s={upper}",           "", true, "" },
            { "Zacks",       "https://www.zacks.com/stock/quote/{upper}?q={lower}", "", true, "" },
        };
    }
}
