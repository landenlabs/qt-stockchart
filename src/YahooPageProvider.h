#pragma once
#include "StockDataProvider.h"
#include <QNetworkAccessManager>

// Scrapes the Yahoo Finance quote page for the current price.
// No API key required. Returns one data point (current price) which
// merges into cached historical data in MainWindow::onDataReady.
//
// Two extraction methods handle stocks and indices:
//   Method 1 (stocks/ETFs): embedded JSON  "symbol":"SYM" ... "regularMarketPrice":{"raw":N}
//   Method 2 (indices/fallback): fin-streamer element  data-symbol="SYM" data-field="regularMarketPrice" data-value="N"
class YahooPageProvider : public StockDataProvider
{
    Q_OBJECT
public:
    explicit YahooPageProvider(QObject *parent = nullptr);

    QString id() const override { return "yahoopage"; }
    QString displayName() const override { return "Yahoo Finance (page)"; }
    QList<QPair<QString,QString>> credentialFields() const override { return {}; }

    void fetchData(const QString &symbol, const QString &range = "3mo") override;
    void fetchLatestQuote(const QString &symbol) override;
    void fetchSymbolType(const QString &symbol) override;

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    void doFetch(const QString &symbol);

    QNetworkAccessManager *m_manager;
};
