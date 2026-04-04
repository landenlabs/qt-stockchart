#include "AlphaVantageProvider.h"
#include "Logger.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <QTimeZone>
#include <algorithm>

AlphaVantageProvider::AlphaVantageProvider(QObject *parent)
    : StockDataProvider(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    connect(m_manager, &QNetworkAccessManager::finished,
            this, &AlphaVantageProvider::onReplyFinished);
}

QList<QPair<QString,QString>> AlphaVantageProvider::credentialFields() const
{
    return {{"apiKey", "API Key"}};
}

void AlphaVantageProvider::fetchData(const QString &symbol, const QString &range)
{
    // compact = last 100 trading days (~5 months); full needed for 1y
    QString outputSize = (range == "1y") ? "full" : "compact";

    QUrl url("https://www.alphavantage.co/query");
    QUrlQuery query;
    query.addQueryItem("function",   "TIME_SERIES_DAILY");
    query.addQueryItem("symbol",     symbol);
    query.addQueryItem("outputsize", outputSize);
    query.addQueryItem("apikey",     m_credentials.value("apiKey").trimmed());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "StockChart/1.0");

    QNetworkReply *reply = m_manager->get(request);
    m_pending[reply] = {symbol, range};
    Logger::instance().append(QString("AlphaVantage [%1] GET %2").arg(symbol, url.toString()));
}

void AlphaVantageProvider::onReplyFinished(QNetworkReply *reply)
{
    if (!m_pending.contains(reply)) return; // type-lookup replies handled by their own lambda
    reply->deleteLater();
    auto [symbol, range] = m_pending.take(reply);

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    Logger::instance().append(QString("AlphaVantage [%1] HTTP %2").arg(symbol).arg(httpStatus));

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(symbol, reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.isNull()) {
        emit errorOccurred(symbol, "Alpha Vantage: failed to parse JSON");
        return;
    }

    QJsonObject root = doc.object();

    // Rate-limit / invalid-key messages come as "Information" or "Note"
    if (root.contains("Information") || root.contains("Note")) {
        QString msg = root["Information"].toString();
        if (msg.isEmpty()) msg = root["Note"].toString();
        emit errorOccurred(symbol, "Alpha Vantage: " + msg);
        return;
    }

    QJsonObject timeSeries = root["Time Series (Daily)"].toObject();
    if (timeSeries.isEmpty()) {
        emit errorOccurred(symbol, "Alpha Vantage: no time series data for " + symbol);
        return;
    }

    QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-rangeToDays(range));

    QVector<StockDataPoint> points;
    for (auto it = timeSeries.begin(); it != timeSeries.end(); ++it) {
        QDateTime dt = QDateTime::fromString(it.key(), "yyyy-MM-dd");
        dt.setTimeZone(QTimeZone::utc());
        if (dt < cutoff) continue;
        double close = it.value().toObject()["4. close"].toString().toDouble();
        points.append({dt, close});
    }

    // Response is newest-first; sort ascending for the chart
    std::sort(points.begin(), points.end(), [](const StockDataPoint &a, const StockDataPoint &b) {
        return a.timestamp < b.timestamp;
    });

    emit dataReady(symbol, points);
}

void AlphaVantageProvider::fetchLatestQuote(const QString &symbol)
{
    QUrl url("https://www.alphavantage.co/query");
    QUrlQuery query;
    query.addQueryItem("function", "GLOBAL_QUOTE");
    query.addQueryItem("symbol",   symbol);
    query.addQueryItem("apikey",   m_credentials.value("apiKey").trimmed());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "StockChart/1.0");

    QNetworkReply *reply = m_manager->get(request);
    Logger::instance().append(QString("AlphaVantage [%1] GET %2 (quote)").arg(symbol, url.toString()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        reply->deleteLater();
        const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Logger::instance().append(QString("AlphaVantage [%1] HTTP %2 (quote)").arg(symbol).arg(st));
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(symbol, "Alpha Vantage (quote): " + reply->errorString());
            return;
        }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        if (root.contains("Information") || root.contains("Note")) return; // rate-limited; skip silently
        QJsonObject quote = root["Global Quote"].toObject();
        if (quote.isEmpty()) return;
        const double price    = quote["05. price"].toString().toDouble();
        const QString dateStr = quote["07. latest trading day"].toString();
        if (price <= 0.0 || dateStr.isEmpty()) return;
        QDateTime dt = QDateTime::fromString(dateStr, "yyyy-MM-dd");
        dt.setTimeZone(QTimeZone::utc());
        if (!dt.isValid()) return;
        emit dataReady(symbol, QVector<StockDataPoint>{{dt, price}});
    });
}

void AlphaVantageProvider::fetchSymbolType(const QString &symbol)
{
    QUrl url("https://www.alphavantage.co/query");
    QUrlQuery query;
    query.addQueryItem("function", "OVERVIEW");
    query.addQueryItem("symbol",   symbol);
    query.addQueryItem("apikey",   m_credentials.value("apiKey").trimmed());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "StockChart/1.0");

    QNetworkReply *reply = m_manager->get(request);
    Logger::instance().append(QString("AlphaVantage [%1] GET %2 (type)").arg(symbol, url.toString()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        reply->deleteLater();
        const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Logger::instance().append(QString("AlphaVantage [%1] HTTP %2 (type)").arg(symbol).arg(st));
        if (reply->error() != QNetworkReply::NoError) return;
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QString assetType = root["AssetType"].toString();
        SymbolType type = SymbolType::Unknown;
        if      (assetType == "Common Stock")          type = SymbolType::Stock;
        else if (assetType == "Exchange Traded Fund")  type = SymbolType::ETF;
        else if (assetType == "Mutual Fund")           type = SymbolType::MutualFund;
        else if (assetType.contains("Index", Qt::CaseInsensitive)) type = SymbolType::Index;
        if (type != SymbolType::Unknown) emit symbolTypeReady(symbol, type);
    });
}
