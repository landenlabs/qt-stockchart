#include "MorningstarProvider.h"
#include "Logger.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrl>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>

// Exchanges tried in order when the cached exchange is unknown for a symbol.
// Each corresponds to a Morningstar exchange code used in the URL path.
const QStringList MorningstarProvider::kExchanges = {
    "xnys",  // NYSE
    "xnas",  // NASDAQ
    "xase",  // AMEX
    "arcx",  // NYSE Arca (ETFs)
};

static const QByteArray kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/135.0.0.0 Safari/537.36";

MorningstarProvider::MorningstarProvider(QObject *parent)
    : StockDataProvider(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    connect(m_manager, &QNetworkAccessManager::finished,
            this, &MorningstarProvider::onReplyFinished);
}

// ── Public API ────────────────────────────────────────────────────────────────

void MorningstarProvider::fetchData(const QString &symbol, const QString &)
{
    // Page scraping returns only the current price bar — no historical range.
    // The single point merges into cached historical data in MainWindow::onDataReady.
    doFetch(symbol);
}

void MorningstarProvider::fetchLatestQuote(const QString &symbol)
{
    doFetch(symbol);
}

// ── Network ───────────────────────────────────────────────────────────────────

void MorningstarProvider::doFetch(const QString &symbol)
{
    // Use cached exchange if we found a working one before, else start from the top.
    const QString exchange = m_symbolExchange.value(symbol, kExchanges.first());
    fetchQuotePage(symbol, exchange);
}

void MorningstarProvider::fetchQuotePage(const QString &symbol, const QString &exchange)
{
    // Morningstar URLs use lowercase ticker symbols.
    const QUrl url(QString("https://www.morningstar.com/stocks/%1/%2/quote")
                   .arg(exchange, symbol.toLower()));

    QNetworkRequest request{url};
    // Do NOT set Accept-Encoding manually — Qt negotiates gzip/deflate itself and
    // auto-decompresses the response body. Setting it manually disables that
    // auto-decompression, causing raw compressed bytes to arrive as the body.
    request.setRawHeader("User-Agent",                kUserAgent);
    request.setRawHeader("Accept",                    "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    request.setRawHeader("Accept-Language",           "en-US,en;q=0.9");
    request.setRawHeader("Upgrade-Insecure-Requests", "1");
    request.setRawHeader("Sec-Fetch-Dest",            "document");
    request.setRawHeader("Sec-Fetch-Mode",            "navigate");
    request.setRawHeader("Sec-Fetch-Site",            "none");
    request.setRawHeader("Sec-Fetch-User",            "?1");
    request.setRawHeader("Cache-Control",             "max-age=0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_manager->get(request);
    m_pending[reply] = {symbol, exchange};
    Logger::instance().append(
        QString("Morningstar [%1/%2] GET %3").arg(symbol, exchange, url.toString()));
}

void MorningstarProvider::tryNextExchange(const QString &symbol, const QString &triedExchange)
{
    const int idx = kExchanges.indexOf(triedExchange);
    if (idx < 0 || idx + 1 >= kExchanges.size()) {
        emit errorOccurred(symbol,
            QString("Morningstar: price not found for %1 on any exchange").arg(symbol));
        return;
    }
    fetchQuotePage(symbol, kExchanges[idx + 1]);
}

// ── Parsing ───────────────────────────────────────────────────────────────────

bool MorningstarProvider::parsePrice(const QString &html, double &price, QDateTime &dt) const
{
    price = 0.0;
    dt    = QDateTime();

    // The page embeds intraday bars as unquoted JS object literals, e.g.:
    //   {datetime:"2026-04-13T14:45:00-04:00",volume:45580,lastPrice:237.8201,...}
    //
    // Match objects that contain both a datetime and lastPrice field.
    // [^}]* stays within the current object (stops at closing brace).
    // We iterate all matches and keep the last one (most recent bar).
    QRegularExpression barRx(
        R"rx(\{datetime:"([^"]+)"[^}]*lastPrice:([\d.]+))rx");

    double    latestPrice = 0.0;
    QString   latestDtStr;

    QRegularExpressionMatchIterator it = barRx.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        const double p = m.captured(2).toDouble();
        if (p > 0.0) {
            latestPrice = p;
            latestDtStr = m.captured(1);
        }
    }

    // Fallback: bare lastPrice value anywhere on the page (no datetime pairing)
    if (latestPrice <= 0.0) {
        QRegularExpression fallbackRx(R"rx(lastPrice["\s:]+(\d+\.?\d*))rx");
        const auto fm = fallbackRx.match(html);
        if (fm.hasMatch())
            latestPrice = fm.captured(1).toDouble();
    }

    if (latestPrice <= 0.0)
        return false;

    price = latestPrice;

    // Parse ISO 8601 with UTC offset, e.g. "2026-04-13T14:45:00-04:00"
    if (!latestDtStr.isEmpty())
        dt = QDateTime::fromString(latestDtStr, Qt::ISODate);

    if (!dt.isValid()) {
        static const QTimeZone kET("America/New_York");
        dt = QDateTime(QDate::currentDate(), QTime(16, 0), kET);
    }

    return true;
}

// ── Reply handler ─────────────────────────────────────────────────────────────

void MorningstarProvider::onReplyFinished(QNetworkReply *reply)
{
    if (!m_pending.contains(reply)) return;
    reply->deleteLater();
    const auto [symbol, exchange] = m_pending.take(reply);

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    Logger::instance().append(
        QString("Morningstar [%1/%2] HTTP %3").arg(symbol, exchange).arg(httpStatus));

    // 202 / 429 / 503 → bot-protection or rate-limit: a session-level block,
    // not an exchange mismatch. Retrying other exchanges would just burn more
    // requests into the same wall. Emit a clear error and stop.
    if (httpStatus == 202 || httpStatus == 429 || httpStatus == 503) {
        const QString msg = (httpStatus == 429)
            ? QString("Morningstar rate-limited (HTTP 429) — wait before retrying")
            : QString("Morningstar bot-check blocked request (HTTP %1) — "
                      "wait a few minutes before retrying").arg(httpStatus);
        Logger::instance().append(QString("Morningstar [%1] %2").arg(symbol, msg));
        emit errorOccurred(symbol, "Morningstar: " + msg);
        return;
    }

    // 404 or network error → this symbol isn't on this exchange, try the next.
    if (reply->error() != QNetworkReply::NoError || httpStatus == 404) {
        Logger::instance().append(
            QString("Morningstar [%1/%2] %3 — trying next exchange")
            .arg(symbol, exchange, reply->errorString()));
        tryNextExchange(symbol, exchange);
        return;
    }

    const QByteArray rawBody = reply->readAll();
    m_lastHistoryJson = rawBody;
    m_lastQuoteJson   = rawBody;
    emit historyResponseStored();
    emit quoteResponseStored();

    const QString html = QString::fromUtf8(rawBody);

    // Detect a Cloudflare challenge page returned as HTTP 200.
    // These contain JS that a plain QNetworkAccessManager cannot execute.
    if (html.contains("cf-chl-bypass") || html.contains("challenge-platform") ||
        html.contains("jschl_vc") ||
        (html.contains("Cloudflare") && !html.contains("lastPrice"))) {
        const QString msg = "Morningstar bot-check active (Cloudflare JS challenge) — "
                            "wait a few minutes before retrying";
        Logger::instance().append(QString("Morningstar [%1] %2").arg(symbol, msg));
        emit errorOccurred(symbol, "Morningstar: " + msg);
        return;
    }

    double    price = 0.0;
    QDateTime dt;

    if (!parsePrice(html, price, dt)) {
        Logger::instance().append(
            QString("Morningstar [%1/%2] price not found — trying next exchange")
            .arg(symbol, exchange));
        tryNextExchange(symbol, exchange);
        return;
    }

    // Cache the working exchange so next call goes directly to the right URL.
    m_symbolExchange[symbol] = exchange;

    Logger::instance().append(
        QString("Morningstar [%1/%2] price=%3  dt=%4")
        .arg(symbol, exchange)
        .arg(price)
        .arg(dt.toString(Qt::ISODate)));

    emit dataReady(symbol, {{dt, price}});
}
