#include "TwelveDataProvider.h"
#include "Logger.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QTimeZone>

TwelveDataProvider::TwelveDataProvider(QObject *parent)
    : StockDataProvider(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    connect(m_manager, &QNetworkAccessManager::finished,
            this, &TwelveDataProvider::onReplyFinished);
}

QList<QPair<QString,QString>> TwelveDataProvider::credentialFields() const
{
    return {{"apikey", "API Key"}};
}

void TwelveDataProvider::fetchData(const QString &symbol, const QString &range)
{
    const int days = rangeToDays(range);

    QUrl url("https://api.twelvedata.com/time_series");
    QUrlQuery query;
    query.addQueryItem("symbol",     symbol);
    query.addQueryItem("interval",   "1day");
    query.addQueryItem("outputsize", QString::number(days));
    query.addQueryItem("apikey",     m_credentials.value("apikey").trimmed());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "StockChart/1.0");

    QNetworkReply *reply = m_manager->get(request);
    m_pending[reply] = {symbol, range};
    Logger::instance().append(QString("TwelveData [%1] GET %2").arg(symbol, url.toString()));
}

void TwelveDataProvider::onReplyFinished(QNetworkReply *reply)
{
    if (!m_pending.contains(reply)) return;
    reply->deleteLater();
    auto [symbol, range] = m_pending.take(reply);

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    Logger::instance().append(QString("TwelveData [%1] HTTP %2").arg(symbol).arg(httpStatus));

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(symbol, reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.isNull()) {
        emit errorOccurred(symbol, "Twelve Data: failed to parse JSON");
        return;
    }

    QJsonObject root = doc.object();
    if (root["status"].toString() != "ok") {
        QString msg = root["message"].toString();
        emit errorOccurred(symbol, "Twelve Data: " + (msg.isEmpty() ? "no data for " + symbol : msg));
        return;
    }

    QJsonArray values = root["values"].toArray();

    // API returns newest-first; reverse to ascending chronological order
    QVector<StockDataPoint> points;
    points.reserve(values.size());
    for (int i = values.size() - 1; i >= 0; --i) {
        QJsonObject entry = values[i].toObject();
        QDateTime dt = QDateTime::fromString(entry["datetime"].toString(), "yyyy-MM-dd");
        dt.setTimeZone(QTimeZone::utc());
        double close = entry["close"].toString().toDouble();
        if (dt.isValid() && close > 0.0)
            points.append({dt, close});
    }

    emit dataReady(symbol, points);
}

void TwelveDataProvider::fetchLatestQuote(const QString &symbol)
{
    QUrl url("https://api.twelvedata.com/quote");
    QUrlQuery query;
    query.addQueryItem("symbol", symbol);
    query.addQueryItem("apikey", m_credentials.value("apikey").trimmed());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "StockChart/1.0");

    QNetworkReply *reply = m_manager->get(request);
    Logger::instance().append(QString("TwelveData [%1] GET %2 (quote)").arg(symbol, url.toString()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        reply->deleteLater();
        const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Logger::instance().append(QString("TwelveData [%1] HTTP %2 (quote)").arg(symbol).arg(st));
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(symbol, "Twelve Data (quote): " + reply->errorString());
            return;
        }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        if (root["status"].toString() == "error") return; // silently skip (e.g., rate-limited)
        // "close" holds the most recent session close; "datetime" is "YYYY-MM-DD[ HH:mm:ss]"
        const double price    = root["close"].toString().toDouble();
        const QString dateStr = root["datetime"].toString().left(10); // YYYY-MM-DD
        if (price <= 0.0 || dateStr.isEmpty()) return;
        QDateTime dt = QDateTime::fromString(dateStr, "yyyy-MM-dd");
        dt.setTimeZone(QTimeZone::utc());
        if (!dt.isValid()) return;
        emit dataReady(symbol, QVector<StockDataPoint>{{dt, price}});
    });
}

void TwelveDataProvider::fetchSymbolType(const QString &symbol)
{
    QUrl url("https://api.twelvedata.com/quote");
    QUrlQuery query;
    query.addQueryItem("symbol", symbol);
    query.addQueryItem("apikey", m_credentials.value("apikey").trimmed());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "StockChart/1.0");

    QNetworkReply *reply = m_manager->get(request);
    Logger::instance().append(QString("TwelveData [%1] GET %2 (type)").arg(symbol, url.toString()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        reply->deleteLater();
        const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Logger::instance().append(QString("TwelveData [%1] HTTP %2 (type)").arg(symbol).arg(st));
        if (reply->error() != QNetworkReply::NoError) return;
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QString typeStr = root["type"].toString();
        SymbolType type = SymbolType::Unknown;
        if      (typeStr == "Common Stock")   type = SymbolType::Stock;
        else if (typeStr == "ETF")            type = SymbolType::ETF;
        else if (typeStr == "Index")          type = SymbolType::Index;
        else if (typeStr == "Mutual Fund")    type = SymbolType::MutualFund;
        else if (typeStr == "Digital Currency" || typeStr == "Physical Currency")
                                              type = SymbolType::Crypto;
        if (type != SymbolType::Unknown) emit symbolTypeReady(symbol, type);
    });
}
