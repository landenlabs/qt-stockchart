#pragma once
#include "StockDataProvider.h"
#include <QNetworkAccessManager>

class TwelveDataProvider : public StockDataProvider
{
    Q_OBJECT
public:
    explicit TwelveDataProvider(QObject *parent = nullptr);

    QString id() const override { return "twelvedata"; }
    QString displayName() const override { return "Twelve Data"; }
    QList<QPair<QString,QString>> credentialFields() const override;

    void fetchData(const QString &symbol, const QString &range = "3mo") override;
    void fetchSymbolType(const QString &symbol) override;
    void fetchLatestQuote(const QString &symbol) override;

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_manager;
};
