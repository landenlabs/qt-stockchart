#pragma once
#include "StockDataProvider.h"
#include <QList>
#include <QNetworkAccessManager>
#include <QSet>
#include <functional>

// Unofficial Yahoo Finance v8 chart API — no API key required.
// Yahoo requires a crumb token obtained via a two-step cookie handshake.
// This provider handles that automatically and retries once on auth failures.
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
    void fetchLatestQuote(const QString &symbol) override;

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    // Crumb management
    void ensureCrumb(std::function<void()> callback);
    void fetchCrumb();

    // Actual network dispatchers (called once crumb is ready)
    void doFetch(const QString &symbol, const QString &range);
    void doFetchSymbolType(const QString &symbol);

    QNetworkAccessManager *m_manager;

    enum class CrumbState { Unknown, Fetching, Ready };
    CrumbState m_crumbState = CrumbState::Unknown;
    QString    m_crumb;
    QList<std::function<void()>> m_crumbCallbacks;

    // Tracks symbols that have already been retried to prevent infinite loops
    QSet<QString> m_symbolsRetried;
};
