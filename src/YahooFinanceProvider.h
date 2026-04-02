#pragma once
#include "StockDataProvider.h"
#include <QNetworkAccessManager>

// Unofficial Yahoo Finance v8 chart API — no API key required.
// This is a best-effort "legacy" provider; Yahoo can change or block
// the endpoint at any time without notice.
class YahooFinanceProvider : public StockDataProvider
{
    Q_OBJECT
public:
    explicit YahooFinanceProvider(QObject *parent = nullptr);

    QString id() const override { return "yahoo"; }
    QString displayName() const override { return "Yahoo Finance (legacy)"; }

    // No credentials needed — hasCredentials() returns true for empty lists.
    QList<QPair<QString,QString>> credentialFields() const override { return {}; }

    void fetchData(const QString &symbol, const QString &range = "3mo") override;
    void fetchSymbolType(const QString &symbol) override;

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_manager;
};
