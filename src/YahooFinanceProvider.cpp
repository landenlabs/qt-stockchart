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

static const QByteArray kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0.0.0 Safari/537.36";

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

    // Step 1: visit Yahoo Finance to seed the cookie jar (.yahoo.com cookies)
    QNetworkRequest req{QUrl("https://finance.yahoo.com/")};
    req.setRawHeader("User-Agent", kUserAgent);
    req.setRawHeader("Accept", "text/html,application/xhtml+xml");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    Logger::instance().append("Yahoo: GET https://finance.yahoo.com/ (crumb step 1)");
    QNetworkReply *r = m_manager->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        const int st1 = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Logger::instance().append(QString("Yahoo: HTTP %1 (crumb step 1)").arg(st1));
        r->deleteLater();

        // Step 2: exchange cookies for a crumb token
        QNetworkRequest req2{QUrl("https://query2.finance.yahoo.com/v1/test/getcrumb")};
        req2.setRawHeader("User-Agent", kUserAgent);
        req2.setRawHeader("Accept",     "*/*");
        req2.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                          QNetworkRequest::NoLessSafeRedirectPolicy);

        Logger::instance().append("Yahoo: GET https://query2.finance.yahoo.com/v1/test/getcrumb (crumb step 2)");
        QNetworkReply *r2 = m_manager->get(req2);
        connect(r2, &QNetworkReply::finished, this, [this, r2]() {
            const int st2 = r2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            Logger::instance().append(QString("Yahoo: HTTP %1 (crumb step 2)").arg(st2));
            r2->deleteLater();

            if (r2->error() == QNetworkReply::NoError)
                m_crumb = QString::fromUtf8(r2->readAll()).trimmed();

            m_crumbState = m_crumb.isEmpty() ? CrumbState::Unknown : CrumbState::Ready;

            // Flush all queued requests (they will error on their own if crumb is empty)
            const auto callbacks = std::move(m_crumbCallbacks);
            m_crumbCallbacks.clear();
            for (const auto &cb : callbacks) cb();
        });
    });
}

// ── Public API ────────────────────────────────────────────────────────────────

void YahooFinanceProvider::fetchData(const QString &symbol, const QString &range)
{
    ensureCrumb([this, symbol, range]() { doFetch(symbol, range); });
}

void YahooFinanceProvider::doFetch(const QString &symbol, const QString &range)
{
    if (m_crumb.isEmpty()) {
        emit errorOccurred(symbol, "Yahoo: failed to obtain authentication token");
        return;
    }

    QUrl url(QString("https://query1.finance.yahoo.com/v8/finance/chart/%1").arg(symbol));
    QUrlQuery query;
    query.addQueryItem("interval", "1d");
    query.addQueryItem("range",    range);
    query.addQueryItem("crumb",    m_crumb);
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
        // On auth/rate-limit failure: reset the crumb and retry once per symbol
        if ((status == 401 || status == 403 || status == 429) &&
            !m_symbolsRetried.contains(symbol)) {
            m_symbolsRetried.insert(symbol);
            m_crumb.clear();
            m_crumbState = CrumbState::Unknown;
            ensureCrumb([this, symbol, range]() { doFetch(symbol, range); });
            return;
        }
        m_symbolsRetried.remove(symbol);
        emit errorOccurred(symbol, "Yahoo: " + reply->errorString());
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
    // Reuse the existing chart API with a short range — "5d" is sufficient to
    // include today's bar and avoid extra crumb/endpoint complexity.
    ensureCrumb([this, symbol]() { doFetch(symbol, "5d"); });
}

// ── Symbol type ───────────────────────────────────────────────────────────────

void YahooFinanceProvider::fetchSymbolType(const QString &symbol)
{
    ensureCrumb([this, symbol]() { doFetchSymbolType(symbol); });
}

void YahooFinanceProvider::doFetchSymbolType(const QString &symbol)
{
    if (m_crumb.isEmpty()) return;  // best-effort; silently skip if no crumb

    QUrl url(QString("https://query2.finance.yahoo.com/v10/finance/quoteSummary/%1").arg(symbol));
    QUrlQuery query;
    query.addQueryItem("modules", "quoteType");
    query.addQueryItem("crumb",   m_crumb);
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
