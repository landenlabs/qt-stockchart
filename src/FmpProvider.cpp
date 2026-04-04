#include "FmpProvider.h"
#include "Logger.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QDate>
#include <QDateTime>
#include <QTimeZone>

FmpProvider::FmpProvider(QObject *parent)
    : StockDataProvider(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    connect(m_manager, &QNetworkAccessManager::finished,
            this, &FmpProvider::onReplyFinished);
}

// ── Public API ────────────────────────────────────────────────────────────────

void FmpProvider::fetchData(const QString &symbol, const QString &range)
{
    const QString key = m_credentials.value("apiKey").trimmed();
    const QDate today = QDate::currentDate();
    const QDate from  = today.addDays(-rangeToDays(range));

    QUrl url(QString("https://financialmodelingprep.com/api/v3/historical-price-full/%1").arg(symbol));
    QUrlQuery q;
    q.addQueryItem("from",   from.toString("yyyy-MM-dd"));
    q.addQueryItem("to",     today.toString("yyyy-MM-dd"));
    q.addQueryItem("apikey", key);
    url.setQuery(q);

    QNetworkRequest request{url};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_manager->get(request);
    m_pending[reply] = {symbol, range};
    Logger::instance().append(QString("FMP [%1] GET %2").arg(symbol, url.toString()));
}

void FmpProvider::fetchLatestQuote(const QString &symbol)
{
    const QString key = m_credentials.value("apiKey").trimmed();

    QUrl url(QString("https://financialmodelingprep.com/api/v3/quote-short/%1").arg(symbol));
    QUrlQuery q;
    q.addQueryItem("apikey", key);
    url.setQuery(q);

    QNetworkRequest request{url};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_manager->get(request);
    m_pending[reply] = {symbol, "quote"};
    Logger::instance().append(QString("FMP [%1] GET %2 (quote)").arg(symbol, url.toString()));
}

void FmpProvider::fetchSymbolType(const QString &symbol)
{
    const QString key = m_credentials.value("apiKey").trimmed();

    QUrl url(QString("https://financialmodelingprep.com/api/v3/profile/%1").arg(symbol));
    QUrlQuery q;
    q.addQueryItem("apikey", key);
    url.setQuery(q);

    QNetworkRequest request{url};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_manager->get(request);
    Logger::instance().append(QString("FMP [%1] GET %2 (type)").arg(symbol, url.toString()));

    connect(reply, &QNetworkReply::finished, this, [this, reply, symbol]() {
        reply->deleteLater();
        const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Logger::instance().append(QString("FMP [%1] HTTP %2 (type)").arg(symbol).arg(st));
        if (reply->error() != QNetworkReply::NoError) return;

        const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
        if (arr.isEmpty()) return;

        const QJsonObject obj = arr[0].toObject();
        SymbolType type = SymbolType::Unknown;
        if      (obj["isEtf"].toBool())  type = SymbolType::ETF;
        else if (obj["isFund"].toBool()) type = SymbolType::MutualFund;
        else if (symbol.startsWith('^')) type = SymbolType::Index;
        else                             type = SymbolType::Stock;

        if (type != SymbolType::Unknown)
            emit symbolTypeReady(symbol, type);
    });
}

// ── Reply handler ─────────────────────────────────────────────────────────────

void FmpProvider::onReplyFinished(QNetworkReply *reply)
{
    if (!m_pending.contains(reply)) return;
    reply->deleteLater();
    auto [symbol, range] = m_pending.take(reply);

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    Logger::instance().append(QString("FMP [%1] HTTP %2").arg(symbol).arg(httpStatus));

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(symbol, "FMP: " + reply->errorString());
        return;
    }

    const QByteArray body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isNull()) {
        emit errorOccurred(symbol, "FMP: failed to parse JSON for " + symbol);
        return;
    }

    // Shared error detection: {"Error Message": "..."} or {"message": "..."}
    const QJsonObject root = doc.object();
    const QString errMsg = root["Error Message"].toString();
    if (!errMsg.isEmpty()) {
        emit errorOccurred(symbol, "FMP: " + errMsg);
        return;
    }
    const QString msg = root["message"].toString();
    if (!msg.isEmpty()) {
        emit errorOccurred(symbol, "FMP: " + msg);
        return;
    }

    // ── Latest quote ──────────────────────────────────────────────────────────
    if (range == "quote") {
        const QJsonArray arr = doc.array();
        if (arr.isEmpty()) {
            emit errorOccurred(symbol, "FMP: no quote data for " + symbol);
            return;
        }
        const QJsonObject q = arr[0].toObject();
        const double price = q["price"].toDouble();
        if (price <= 0.0) {
            emit errorOccurred(symbol, "FMP: invalid quote price for " + symbol);
            return;
        }
        // Use today at 16:00 ET as the timestamp for the current quote.
        static const QTimeZone kET("America/New_York");
        const QDateTime ts(QDate::currentDate(), QTime(16, 0), kET);
        emit dataReady(symbol, {{ts, price}});
        return;
    }

    // ── Historical data ───────────────────────────────────────────────────────
    const QJsonArray historical = root["historical"].toArray();
    if (historical.isEmpty()) {
        emit errorOccurred(symbol, "FMP: no historical data for " + symbol +
                           " — check symbol or upgrade plan");
        return;
    }

    static const QTimeZone kET("America/New_York");
    QVector<StockDataPoint> points;
    points.reserve(historical.size());
    for (const QJsonValue &v : historical) {
        const QJsonObject bar = v.toObject();
        const QDate date = QDate::fromString(bar["date"].toString(), "yyyy-MM-dd");
        if (!date.isValid()) continue;

        // Prefer adjusted close; fall back to regular close.
        double price = bar["adjClose"].toDouble();
        if (price <= 0.0) price = bar["close"].toDouble();
        if (price <= 0.0) continue;

        points.append({QDateTime(date, QTime(16, 0), kET), price});
    }

    // FMP returns newest-first; reverse to ascending.
    std::reverse(points.begin(), points.end());

    if (points.isEmpty()) {
        emit errorOccurred(symbol, "FMP: no valid price data for " + symbol);
        return;
    }

    emit dataReady(symbol, points);
}
