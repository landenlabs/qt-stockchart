#include "FinnhubProvider.h"
#include "Logger.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QTimeZone>

FinnhubProvider::FinnhubProvider(QObject *parent)
    : StockDataProvider(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    connect(m_manager, &QNetworkAccessManager::finished,
            this, &FinnhubProvider::onReplyFinished);
}

QList<QPair<QString,QString>> FinnhubProvider::credentialFields() const
{
    return {{"token", "API Token"}};
}

void FinnhubProvider::fetchData(const QString &symbol, const QString &range)
{
    qint64 to   = QDateTime::currentSecsSinceEpoch();
    qint64 from = to - static_cast<qint64>(rangeToDays(range)) * 86400LL;

    QUrl url("https://finnhub.io/api/v1/stock/candle");
    QUrlQuery query;
    query.addQueryItem("symbol",     symbol);
    query.addQueryItem("resolution", "D");
    query.addQueryItem("from",       QString::number(from));
    query.addQueryItem("to",         QString::number(to));
    query.addQueryItem("token",      m_credentials.value("token").trimmed());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "StockChart/1.0");

    QNetworkReply *reply = m_manager->get(request);
    m_pending[reply] = {symbol, range};
    Logger::instance().append(QString("Finnhub [%1] GET %2").arg(symbol, url.toString()));
}

void FinnhubProvider::onReplyFinished(QNetworkReply *reply)
{
    if (!m_pending.contains(reply)) return;
    reply->deleteLater();
    auto [symbol, range] = m_pending.take(reply);

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    Logger::instance().append(QString("Finnhub [%1] HTTP %2").arg(symbol).arg(httpStatus));

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(symbol, reply->errorString());
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (doc.isNull()) {
        emit errorOccurred(symbol, "Finnhub: failed to parse JSON");
        return;
    }

    QJsonObject root = doc.object();
    QString status = root["s"].toString();
    if (status != "ok") {
        emit errorOccurred(symbol, "Finnhub: no data for " + symbol + " (status: " + status + ")");
        return;
    }

    QJsonArray closes     = root["c"].toArray();
    QJsonArray timestamps = root["t"].toArray();

    QVector<StockDataPoint> points;
    points.reserve(timestamps.size());
    for (int i = 0; i < timestamps.size() && i < closes.size(); ++i) {
        points.append({
            QDateTime::fromSecsSinceEpoch(timestamps[i].toVariant().toLongLong()),
            closes[i].toDouble()
        });
    }

    emit dataReady(symbol, points);
}

void FinnhubProvider::fetchLatestQuote(const QString &symbol)
{
    QUrl url("https://finnhub.io/api/v1/quote");
    QUrlQuery query;
    query.addQueryItem("symbol", symbol);
    query.addQueryItem("token",  m_credentials.value("token").trimmed());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "StockChart/1.0");

    QNetworkReply *reply = m_manager->get(request);
    Logger::instance().append(QString("Finnhub [%1] GET %2 (quote)").arg(symbol, url.toString()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        reply->deleteLater();
        const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Logger::instance().append(QString("Finnhub [%1] HTTP %2 (quote)").arg(symbol).arg(st));
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(symbol, "Finnhub (quote): " + reply->errorString());
            return;
        }
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const double price = root["c"].toDouble(); // current price
        if (price <= 0.0) return;
        // "t" is the Unix timestamp of the last trade; fall back to today 16:00 ET
        const qint64 t = root["t"].toVariant().toLongLong();
        QDateTime dt;
        if (t > 0) {
            dt = QDateTime::fromSecsSinceEpoch(t);
        } else {
            dt = QDateTime(QDate::currentDate(), QTime(16, 0, 0),
                           QTimeZone("America/New_York")).toUTC();
        }
        emit dataReady(symbol, QVector<StockDataPoint>{{dt, price}});
    });
}

void FinnhubProvider::fetchSymbolType(const QString &symbol)
{
    QUrl url("https://finnhub.io/api/v1/stock/profile2");
    QUrlQuery query;
    query.addQueryItem("symbol", symbol);
    query.addQueryItem("token",  m_credentials.value("token").trimmed());
    url.setQuery(query);

    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", "StockChart/1.0");

    QNetworkReply *reply = m_manager->get(request);
    Logger::instance().append(QString("Finnhub [%1] GET %2 (type)").arg(symbol, url.toString()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        reply->deleteLater();
        const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Logger::instance().append(QString("Finnhub [%1] HTTP %2 (type)").arg(symbol).arg(st));
        if (reply->error() != QNetworkReply::NoError) return;
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        // profile2 only returns data for common stocks; ETFs/indices return empty object
        if (!root["name"].toString().isEmpty())
            emit symbolTypeReady(symbol, SymbolType::Stock);
    });
}
