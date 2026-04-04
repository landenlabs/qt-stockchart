#pragma once
#include "StockDataProvider.h"
#include <QNetworkAccessManager>

class FinnhubProvider : public StockDataProvider
{
    Q_OBJECT
public:
    explicit FinnhubProvider(QObject *parent = nullptr);

    QString id() const override { return "finnhub"; }
    QString displayName() const override { return "Finnhub"; }
    QList<QPair<QString,QString>> credentialFields() const override;

    void fetchData(const QString &symbol, const QString &range = "3mo") override;
    void fetchSymbolType(const QString &symbol) override;
    void fetchLatestQuote(const QString &symbol) override;

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_manager;
};
