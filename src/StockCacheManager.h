#pragma once
#include <QMap>
#include <QVector>
#include <QDate>
#include "StockDataProvider.h"

// Manages the in-memory + persisted cache of historical price data and symbol types.
class StockCacheManager
{
public:
    void loadCache();
    void saveCache();

    void loadSymbolTypeCache();
    void saveSymbolType(const QString &symbol, SymbolType type);

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
