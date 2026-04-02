#include "YahooFinanceProvider.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>

// Browser-like User-Agent reduces the chance of Yahoo rejecting the request.
static const QByteArray kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/120.0.0.0 Safari/537.36";

YahooFinanceProvider::YahooFinanceProvider(QObject *parent)
    : StockDataProvider(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    connect(m_manager, &QNetworkAccessManager::finished,
            this, &YahooFinanceProvider::onReplyFinished);
}

void YahooFinanceProvider::fetchData(const QString &symbol, const QString &range)
{
    // Yahoo Finance range strings happen to match the internal ones (1mo, 3mo, 6mo, 1y).
    QUrl url(QString("https://query1.finance.yahoo.com/v8/finance/chart/%1").arg(symbol));
    QUrlQuery query;
    query.addQueryItem("interval", "1d");
    query.addQueryItem("range",    range);
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_manager->get(request);
    m_pending[reply] = {symbol, range};
}

void YahooFinanceProvider::onReplyFinished(QNetworkReply *reply)
{
    if (!m_pending.contains(reply)) return;
    reply->deleteLater();
    auto [symbol, range] = m_pending.take(reply);

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(symbol, "Yahoo: " + reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.isNull()) {
        emit errorOccurred(symbol, "Yahoo: failed to parse JSON");
        return;
    }

    QJsonObject chart = doc.object()["chart"].toObject();

    QJsonValue errorVal = chart["error"];
    if (!errorVal.isNull() && !errorVal.toObject().isEmpty()) {
        const QString desc = errorVal.toObject()["description"].toString();
        emit errorOccurred(symbol, desc.isEmpty() ? "Yahoo: unknown error for " + symbol : "Yahoo: " + desc);
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
    QJsonArray adjCloseArr = result["indicators"].toObject()
                                   ["adjclose"].toArray();
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

void YahooFinanceProvider::fetchSymbolType(const QString &symbol)
{
    QUrl url(QString("https://query1.finance.yahoo.com/v10/finance/quoteSummary/%1")
                 .arg(symbol));
    QUrlQuery query;
    query.addQueryItem("modules", "quoteType");
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull()) return;

        QJsonArray res = doc.object()["quoteSummary"].toObject()["result"].toArray();
        if (res.isEmpty()) return;

        const QString qt = res[0].toObject()["quoteType"].toObject()["quoteType"].toString();
        SymbolType type = SymbolType::Unknown;
        if      (qt == "EQUITY")       type = SymbolType::Stock;
        else if (qt == "ETF")          type = SymbolType::ETF;
        else if (qt == "MUTUALFUND")   type = SymbolType::MutualFund;
        else if (qt == "CRYPTOCURRENCY") type = SymbolType::Crypto;
        else if (qt == "INDEX")        type = SymbolType::Index;

        if (type != SymbolType::Unknown)
            emit symbolTypeReady(symbol, type);
    });
}
