#include "FinnhubProvider.h"
#include "Logger.h"
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <QDateTime>
#include <QTimeZone>

FinnhubProvider::FinnhubProvider(QObject *parent)
    : StockDataProvider(parent)
    , m_manager(new QNetworkAccessManager(this))
{
}

QList<QPair<QString,QString>> FinnhubProvider::credentialFields() const
{
    return {{"token", "API Token"}};
}

void FinnhubProvider::fetchData(const QString &symbol, const QString &range)
{
    Q_UNUSED(range)
    // Historical candle data requires a paid Finnhub plan. Fall back to the
    // real-time quote endpoint so the caller still receives today's price.
    Logger::instance().append(QString("Finnhub [%1] historical unavailable on free tier — fetching quote instead").arg(symbol));
    fetchLatestQuote(symbol);
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
        const QByteArray body = reply->readAll();
        m_lastQuoteJson = body;
        emit quoteResponseStored();
        QJsonObject root = QJsonDocument::fromJson(body).object();
        const double price = root["c"].toDouble(); // current price
        if (price <= 0.0) return;
        Logger::instance().append(QString("Finnhub [%1] quote price: %2").arg(symbol).arg(price));
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
