#include "YahooFinanceProvider.h"
#include "Logger.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QTimer>

static const QByteArray kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/135.0.0.0 Safari/537.36";

YahooFinanceProvider::YahooFinanceProvider(QObject *parent)
    : StockDataProvider(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    connect(m_manager, &QNetworkAccessManager::finished,
            this, &YahooFinanceProvider::onReplyFinished);
}

// ── Crumb management ──────────────────────────────────────────────────────────

void YahooFinanceProvider::ensureCrumb(std::function<void()> callback)
{
    if (m_crumbState == CrumbState::Ready) {
        callback();
        return;
    }
    m_crumbCallbacks.append(std::move(callback));
    if (m_crumbState == CrumbState::Unknown)
        fetchCrumb();
}

void YahooFinanceProvider::fetchCrumb()
{
    m_crumbState = CrumbState::Fetching;

    // Step 1: fetch the Yahoo Finance homepage to seed cookies AND extract the crumb.
    // Yahoo embeds the crumb token in the page HTML as "crumb":"VALUE", so we can
    // avoid the separate /v1/test/getcrumb endpoint (which is aggressively rate-limited).
    QNetworkRequest req{QUrl("https://finance.yahoo.com/")};
    req.setRawHeader("User-Agent",      kUserAgent);
    req.setRawHeader("Accept",          "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    Logger::instance().append("Yahoo: GET https://finance.yahoo.com/ (crumb step 1)");
    QNetworkReply *r = m_manager->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        const int st1 = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Logger::instance().append(QString("Yahoo: HTTP %1 (crumb step 1)").arg(st1));
        const QByteArray body = r->readAll(); // read before deleteLater
        r->deleteLater();

        // Extract crumb from embedded JS: "crumb":"<value>"
        // Yahoo sometimes encodes '/' as \u002F — unescape it.
        const int idx = body.indexOf("\"crumb\":\"");
        if (idx != -1) {
            const int start = idx + 9; // length of "\"crumb\":\""
            const int end   = body.indexOf('"', start);
            if (end > start) {
                m_crumb = QString::fromUtf8(body.mid(start, end - start));
                m_crumb.replace("\\u002F", "/");
            }
        }

        if (!m_crumb.isEmpty()) {
            Logger::instance().append("Yahoo: crumb extracted from HTML");
            m_crumbState = CrumbState::Ready;
            const auto callbacks = std::move(m_crumbCallbacks);
            m_crumbCallbacks.clear();
            for (const auto &cb : callbacks) cb();
        } else {
            // HTML extraction failed — fall back to the dedicated crumb endpoint.
            Logger::instance().append("Yahoo: crumb not in HTML, trying getcrumb endpoint");
            fetchCrumbStep2(/*retrying=*/false);
        }
    });
}

void YahooFinanceProvider::fetchCrumbStep2(bool retrying)
{
    // Fallback: dedicated crumb endpoint. Requires Referer to avoid 429.
    QNetworkRequest req2{QUrl("https://query1.finance.yahoo.com/v1/test/getcrumb")};
    req2.setRawHeader("User-Agent",      kUserAgent);
    req2.setRawHeader("Accept",          "text/plain,*/*");
    req2.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    req2.setRawHeader("Referer",         "https://finance.yahoo.com/");
    req2.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                      QNetworkRequest::NoLessSafeRedirectPolicy);

    Logger::instance().append("Yahoo: GET https://query1.finance.yahoo.com/v1/test/getcrumb (crumb step 2)");
    QNetworkReply *r2 = m_manager->get(req2);
    connect(r2, &QNetworkReply::finished, this, [this, r2, retrying]() {
        const int st2 = r2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Logger::instance().append(QString("Yahoo: HTTP %1 (crumb step 2)").arg(st2));
        r2->deleteLater();

        if (r2->error() == QNetworkReply::NoError)
            m_crumb = QString::fromUtf8(r2->readAll()).trimmed();

        // On 429: retry once after a short delay.
        if (m_crumb.isEmpty() && st2 == 429 && !retrying) {
            Logger::instance().append("Yahoo: 429 on crumb — retrying in 2s");
            QTimer::singleShot(2000, this, [this]() { fetchCrumbStep2(/*retrying=*/true); });
            return;
        }

        m_crumbState = m_crumb.isEmpty() ? CrumbState::Unknown : CrumbState::Ready;
        const auto callbacks = std::move(m_crumbCallbacks);
        m_crumbCallbacks.clear();
        for (const auto &cb : callbacks) cb();
    });
}

// ── Public API ────────────────────────────────────────────────────────────────

void YahooFinanceProvider::fetchData(const QString &symbol, const QString &range)
{
    // Call the chart API directly. Crumb is included only when one is already cached
    // from a prior successful session; Yahoo's v8 API sometimes works with just cookies.
    // Avoid the crumb-fetch preamble — those extra requests make rate-limiting worse.
    doFetch(symbol, range);
}

void YahooFinanceProvider::doFetch(const QString &symbol, const QString &range)
{
    QUrl url(QString("https://query1.finance.yahoo.com/v8/finance/chart/%1").arg(symbol));
    QUrlQuery query;
    query.addQueryItem("interval", "1d");
    query.addQueryItem("range",    range);
    if (!m_crumb.isEmpty())
        query.addQueryItem("crumb", m_crumb); // include only when available
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept",     "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_manager->get(request);
    m_pending[reply] = {symbol, range};
    Logger::instance().append(QString("Yahoo [%1] GET %2").arg(symbol, url.toString()));
}

void YahooFinanceProvider::onReplyFinished(QNetworkReply *reply)
{
    if (!m_pending.contains(reply)) return;
    reply->deleteLater();
    auto [symbol, range] = m_pending.take(reply);

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    Logger::instance().append(QString("Yahoo [%1] HTTP %2").arg(symbol).arg(httpStatus));

    if (reply->error() != QNetworkReply::NoError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // On rate-limit: retry once after a delay without fetching a crumb
        // (more crumb requests to the same host make rate-limiting worse).
        if (status == 429 && !m_symbolsRetried.contains(symbol)) {
            m_symbolsRetried.insert(symbol);
            Logger::instance().append(QString("Yahoo [%1] 429 — retrying in 5s").arg(symbol));
            QTimer::singleShot(5000, this, [this, symbol, range]() { doFetch(symbol, range); });
            return;
        }
        m_symbolsRetried.remove(symbol);
        const QString hint = (status == 401 || status == 403 || status == 429)
            ? " (Yahoo rate-limited — try another provider or wait a few minutes)"
            : "";
        emit errorOccurred(symbol, "Yahoo: " + reply->errorString() + hint);
        return;
    }
    m_symbolsRetried.remove(symbol);

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.isNull()) {
        emit errorOccurred(symbol, "Yahoo: failed to parse JSON");
        return;
    }

    QJsonObject chart = doc.object()["chart"].toObject();

    QJsonValue errorVal = chart["error"];
    if (!errorVal.isNull() && !errorVal.toObject().isEmpty()) {
        const QString desc = errorVal.toObject()["description"].toString();
        emit errorOccurred(symbol, desc.isEmpty() ? "Yahoo: unknown error for " + symbol
                                                   : "Yahoo: " + desc);
        return;
    }

    QJsonArray results = chart["result"].toArray();
    if (results.isEmpty()) {
        emit errorOccurred(symbol, "Yahoo: no data returned for " + symbol);
        return;
    }

    QJsonObject result     = results[0].toObject();
    QJsonArray  timestamps = result["timestamp"].toArray();

    // Prefer adjusted-close prices (accounts for splits/dividends); fall back to close.
    QJsonArray closes;
    QJsonArray adjCloseArr = result["indicators"].toObject()["adjclose"].toArray();
    if (!adjCloseArr.isEmpty())
        closes = adjCloseArr[0].toObject()["adjclose"].toArray();

    if (closes.isEmpty())
        closes = result["indicators"].toObject()
                       ["quote"].toArray()[0].toObject()["close"].toArray();

    if (timestamps.isEmpty() || closes.isEmpty()) {
        emit errorOccurred(symbol, "Yahoo: empty price data for " + symbol);
        return;
    }

    QVector<StockDataPoint> points;
    points.reserve(timestamps.size());
    for (int i = 0; i < timestamps.size() && i < closes.size(); ++i) {
        if (closes[i].isNull()) continue;   // Yahoo includes nulls for non-trading days
        const double price = closes[i].toDouble();
        if (price <= 0.0) continue;
        points.append({
            QDateTime::fromSecsSinceEpoch(timestamps[i].toVariant().toLongLong()),
            price
        });
    }

    if (points.isEmpty()) {
        emit errorOccurred(symbol, "Yahoo: no valid price data for " + symbol);
        return;
    }

    emit dataReady(symbol, points);
}

// ── Latest quote ──────────────────────────────────────────────────────────────

void YahooFinanceProvider::fetchLatestQuote(const QString &symbol)
{
    doFetch(symbol, "5d");
}

// ── Symbol type ───────────────────────────────────────────────────────────────

void YahooFinanceProvider::fetchSymbolType(const QString &symbol)
{
    doFetchSymbolType(symbol);
}

void YahooFinanceProvider::doFetchSymbolType(const QString &symbol)
{
    QUrl url(QString("https://query2.finance.yahoo.com/v10/finance/quoteSummary/%1").arg(symbol));
    QUrlQuery query;
    query.addQueryItem("modules", "quoteType");
    if (!m_crumb.isEmpty())
        query.addQueryItem("crumb", m_crumb); // include only when available
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept",     "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_manager->get(request);
    Logger::instance().append(QString("Yahoo [%1] GET %2 (type)").arg(symbol, url.toString()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        reply->deleteLater();
        const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Logger::instance().append(QString("Yahoo [%1] HTTP %2 (type)").arg(symbol).arg(st));
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull()) return;

        QJsonArray res = doc.object()["quoteSummary"].toObject()["result"].toArray();
        if (res.isEmpty()) return;

        const QString qt = res[0].toObject()["quoteType"].toObject()["quoteType"].toString();
        SymbolType type = SymbolType::Unknown;
        if      (qt == "EQUITY")         type = SymbolType::Stock;
        else if (qt == "ETF")            type = SymbolType::ETF;
        else if (qt == "MUTUALFUND")     type = SymbolType::MutualFund;
        else if (qt == "CRYPTOCURRENCY") type = SymbolType::Crypto;
        else if (qt == "INDEX")          type = SymbolType::Index;

        if (type != SymbolType::Unknown)
            emit symbolTypeReady(symbol, type);
    });
}
