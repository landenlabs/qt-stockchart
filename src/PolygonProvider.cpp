#include "PolygonProvider.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QDate>

PolygonProvider::PolygonProvider(QObject *parent)
    : StockDataProvider(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    connect(m_manager, &QNetworkAccessManager::finished,
            this, &PolygonProvider::onReplyFinished);
}

QList<QPair<QString,QString>> PolygonProvider::credentialFields() const
{
    return {{"apiKey", "API Key"}};
}

void PolygonProvider::fetchData(const QString &symbol, const QString &range)
{
    QDate toDate   = QDate::currentDate();
    QDate fromDate = toDate.addDays(-rangeToDays(range));

    QString path = QString("/v2/aggs/ticker/%1/range/1/day/%2/%3")
                       .arg(symbol,
                            fromDate.toString("yyyy-MM-dd"),
                            toDate.toString("yyyy-MM-dd"));

    QUrl url("https://api.polygon.io" + path);
    QUrlQuery query;
    query.addQueryItem("adjusted", "true");
    query.addQueryItem("sort",     "asc");
    query.addQueryItem("limit",    "365");
    query.addQueryItem("apiKey",   m_credentials.value("apiKey").trimmed());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "StockChart/1.0");
    // Send key both ways — Polygon supports both; some API versions prefer the header.
    request.setRawHeader("Authorization",
        ("Bearer " + m_credentials.value("apiKey").trimmed()).toUtf8());

    QNetworkReply *reply = m_manager->get(request);
    m_pending[reply] = {symbol, range};
}

void PolygonProvider::onReplyFinished(QNetworkReply *reply)
{
    if (!m_pending.contains(reply)) return;
    reply->deleteLater();
    auto [symbol, range] = m_pending.take(reply);

    // Always read the body — Polygon puts useful diagnostic JSON in error responses too.
    const QByteArray body = reply->readAll();

    if (reply->error() != QNetworkReply::NoError) {
        // Try to surface Polygon's own error text from the body.
        const QJsonObject errObj = QJsonDocument::fromJson(body).object();
        QString msg = errObj["error"].toString();
        if (msg.isEmpty()) msg = errObj["message"].toString();
        if (msg.isEmpty()) msg = reply->errorString();
        emit errorOccurred(symbol, "Polygon.io: " + msg);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isNull()) {
        emit errorOccurred(symbol, "Polygon.io: failed to parse JSON response");
        return;
    }

    QJsonObject root   = doc.object();
    QString     status = root["status"].toString();

    // Free tier returns "DELAYED"; paid returns "OK". Anything else is an error.
    if (status != "OK" && status != "DELAYED") {
        // Polygon uses "error" in some responses, "message" in others.
        QString msg = root["error"].toString();
        if (msg.isEmpty()) msg = root["message"].toString();
        if (msg.isEmpty()) msg = "status=" + status;
        emit errorOccurred(symbol, "Polygon.io: " + msg);
        return;
    }

    QJsonArray results = root["results"].toArray();
    if (results.isEmpty()) {
        // Include any extra context Polygon provides for empty result sets.
        QString extra = root["message"].toString();
        if (extra.isEmpty()) extra = root["error"].toString();
        const QString detail = extra.isEmpty() ? QString() : " (" + extra + ")";
        emit errorOccurred(symbol,
            "Polygon.io: no data for " + symbol + detail
            + " — verify symbol and subscription level");
        return;
    }

    QVector<StockDataPoint> points;
    points.reserve(results.size());
    for (const QJsonValue &val : results) {
        QJsonObject obj = val.toObject();
        // Polygon timestamps are in milliseconds
        points.append({
            QDateTime::fromMSecsSinceEpoch(obj["t"].toVariant().toLongLong()),
            obj["c"].toDouble()
        });
    }

    emit dataReady(symbol, points);
}

void PolygonProvider::fetchSymbolType(const QString &symbol)
{
    QUrl url("https://api.polygon.io/v3/reference/tickers/" + symbol);
    QUrlQuery query;
    query.addQueryItem("apiKey", m_credentials.value("apiKey").trimmed());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "StockChart/1.0");

    QNetworkReply *reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QJsonObject results = QJsonDocument::fromJson(reply->readAll()).object()["results"].toObject();
        const QString typeCode = results["type"].toString();
        SymbolType type = SymbolType::Unknown;
        if      (typeCode == "CS")                              type = SymbolType::Stock;
        else if (typeCode == "ETF" || typeCode == "ETV")        type = SymbolType::ETF;
        else if (typeCode == "INDEX")                           type = SymbolType::Index;
        else if (typeCode == "MF"  || typeCode == "FUND")       type = SymbolType::MutualFund;
        else if (typeCode == "CRYPTO")                          type = SymbolType::Crypto;
        if (type != SymbolType::Unknown) emit symbolTypeReady(symbol, type);
    });
}
