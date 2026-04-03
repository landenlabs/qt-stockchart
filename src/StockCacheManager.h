#pragma once
#include <QMap>
#include <QVector>
#include <QDate>
#include <QDateTime>
#include <QTimeZone>
#include <QString>
#include "StockDataProvider.h"

// Manages the in-memory + persisted cache of historical price data and symbol types.
class StockCacheManager
{
public:
    void loadCache();
    void saveCache();

    void loadSymbolTypeCache();
    void saveSymbolType(const QString &symbol, SymbolType type);

    // Removes all cached data for symbol from memory and QSettings.
    void clearSymbolCache(const QString &symbol);

    // Seconds since the newest data-point timestamp for sym, or -1 if no data.
    qint64 dataSecs(const QString &sym) const;

    // True if cached data is recent enough that a new API call is not needed.
    // Uses NYSE market hours: open Mon–Fri 9:30am–4:00pm ET → 15-min threshold;
    // market closed → 17-hour threshold.
    bool isDataFresh(const QString &sym) const;

    // Human-readable age of the newest data point ("12d", "3h", "10m").
    QString ageString(const QString &sym) const;

    // Returns the last closing price on or before `target` in ascending-sorted data.
    // Returns NaN if no data point exists at or before that date.
    static double priceAt(const QVector<StockDataPoint> &data, const QDate &target);

    QMap<QString, QVector<StockDataPoint>> &cache()             { return m_cache; }
    const QMap<QString, QVector<StockDataPoint>> &cache() const { return m_cache; }

    QMap<QString, SymbolType> &symbolTypes()             { return m_symbolTypes; }
    const QMap<QString, SymbolType> &symbolTypes() const { return m_symbolTypes; }

private:
    QMap<QString, QVector<StockDataPoint>> m_cache;
    QMap<QString, SymbolType>              m_symbolTypes;
};
