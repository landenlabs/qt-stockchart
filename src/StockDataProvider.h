#pragma once
#include <QObject>
#include <QVector>
#include <QDateTime>
#include <QMap>
#include <QPair>
#include <QList>
#include <QNetworkReply>

struct StockDataPoint {
    QDateTime timestamp;
    double price;
};

enum class SymbolType { Unknown, Stock, ETF, Index, MutualFund, Crypto };

class StockDataProvider : public QObject
{
    Q_OBJECT
public:
    explicit StockDataProvider(QObject *parent = nullptr);
    virtual ~StockDataProvider() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    // Returns list of {fieldKey, displayLabel} pairs
    virtual QList<QPair<QString,QString>> credentialFields() const = 0;

    void setCredentials(const QMap<QString,QString> &creds) { m_credentials = creds; }
    QMap<QString,QString> credentials() const { return m_credentials; }
    bool hasCredentials() const;

    virtual void fetchData(const QString &symbol, const QString &range = "3mo") = 0;
    virtual void fetchSymbolType(const QString &symbol) { Q_UNUSED(symbol); }
    // Fetches the latest available price (today's close or intraday) and emits dataReady.
    virtual void fetchLatestQuote(const QString &symbol) { Q_UNUSED(symbol); }

signals:
    void dataReady(const QString &symbol, const QVector<StockDataPoint> &data);
    void errorOccurred(const QString &symbol, const QString &message);
    void symbolTypeReady(const QString &symbol, SymbolType type);

protected:
    // Tracks in-flight data replies: reply -> {symbol, range}
    QMap<QNetworkReply*, QPair<QString,QString>> m_pending;
    QMap<QString,QString> m_credentials;

    static int rangeToDays(const QString &range);
};
