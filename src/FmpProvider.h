#pragma once
#include "StockDataProvider.h"
#include <QNetworkAccessManager>

// Financial Modeling Prep (FMP) — free tier: 250 req/day, 5 req/min.
// Provides full historical daily closes + current quote + symbol type.
// Free API key at: https://financialmodelingprep.com/developer/docs/
class FmpProvider : public StockDataProvider
{
    Q_OBJECT
public:
    explicit FmpProvider(QObject *parent = nullptr);

    QString id() const override { return "fmp"; }
    QString displayName() const override { return "Fin. Modeling Prep"; }
    QList<QPair<QString,QString>> credentialFields() const override {
        return {{"apiKey", "API Key"}};
    }

    void fetchData(const QString &symbol, const QString &range = "3mo") override;
    void fetchLatestQuote(const QString &symbol) override;
    void fetchSymbolType(const QString &symbol) override;

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_manager;
};
