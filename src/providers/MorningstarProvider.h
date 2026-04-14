#pragma once
#include "StockDataProvider.h"
#include <QNetworkAccessManager>
#include <QMap>

// Scrapes the Morningstar quote page for the current price and timestamp.
// No API key required. Returns one data point (current price).
//
// URL format: https://www.morningstar.com/stocks/{exchange}/{symbol}/quote
//
// The page embeds intraday bars as unquoted JS object literals, e.g.:
//   {datetime:"2026-04-13T14:45:00-04:00",volume:45580,lastPrice:237.8201,...}
// We extract the last (most recent) bar's lastPrice and datetime.
//
// Exchange auto-detection: tries xnys → xnas → xase → arcx in order until a
// price is found. Successful exchange is cached per symbol for subsequent calls.
class MorningstarProvider : public StockDataProvider
{
    Q_OBJECT
public:
    explicit MorningstarProvider(QObject *parent = nullptr);

    QString id() const override { return "morningstar"; }
    QList<QPair<QString,QString>> credentialFields() const override { return {}; }

    void fetchData(const QString &symbol, const QString &range = "3mo") override;
    void fetchLatestQuote(const QString &symbol) override;

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    void doFetch(const QString &symbol);
    void fetchQuotePage(const QString &symbol, const QString &exchange);
    void tryNextExchange(const QString &symbol, const QString &triedExchange);
    bool parsePrice(const QString &html, double &price, QDateTime &dt) const;

    QNetworkAccessManager *m_manager;
    QMap<QString, QString> m_symbolExchange; // cache: symbol -> known working exchange

    static const QStringList kExchanges;
};
