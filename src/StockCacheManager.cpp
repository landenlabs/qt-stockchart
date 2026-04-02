#include "StockCacheManager.h"
#include <QSettings>
#include <QDataStream>
#include <QTime>
#include <limits>
#include <cmath>

double StockCacheManager::priceAt(const QVector<StockDataPoint> &data, const QDate &target)
{
    double result = std::numeric_limits<double>::quiet_NaN();
    for (const StockDataPoint &pt : data) {
        if (pt.timestamp.date() <= target)
            result = pt.price; // ascending order: keep updating
        else
            break;
    }
    return result;
}

void StockCacheManager::saveCache()
{
    QSettings s("StockChart", "StockChart");
    s.beginGroup("historyCache");
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        QByteArray data;
        QDataStream out(&data, QIODevice::WriteOnly);
        out << (qint32)it.value().size();
        for (const auto &pt : it.value())
            out << pt.timestamp << pt.price;
        s.setValue(it.key(), data);
    }
    s.endGroup();
}

void StockCacheManager::loadCache()
{
    QSettings s("StockChart", "StockChart");
    s.beginGroup("historyCache");
    for (const QString &sym : s.childKeys()) {
        QByteArray data = s.value(sym).toByteArray();
        QDataStream in(&data, QIODevice::ReadOnly);
        qint32 size;
        in >> size;
        QVector<StockDataPoint> points;
        points.reserve(size);
        for (int i = 0; i < size; ++i) {
            QDateTime dt;
            double p;
            in >> dt >> p;
            if (!dt.isNull())
                points.append({dt, p});
        }
        if (!points.isEmpty()) m_cache[sym] = points;
    }
    s.endGroup();
}

void StockCacheManager::loadSymbolTypeCache()
{
    QSettings s("StockChart", "StockChart");
    s.beginGroup("symbolTypes");
    for (const QString &sym : s.childKeys())
        m_symbolTypes[sym] = static_cast<SymbolType>(s.value(sym).toInt());
    s.endGroup();
}

void StockCacheManager::saveSymbolType(const QString &symbol, SymbolType type)
{
    QSettings s("StockChart", "StockChart");
    s.beginGroup("symbolTypes");
    s.setValue(symbol, static_cast<int>(type));
    s.endGroup();
}

qint64 StockCacheManager::dataSecs(const QString &sym) const
{
    if (!m_cache.contains(sym) || m_cache[sym].isEmpty()) return -1;
    qint64 s = m_cache[sym].last().timestamp.secsTo(QDateTime::currentDateTime());
    return s >= 0 ? s : 0;
}

bool StockCacheManager::isDataFresh(const QString &sym) const
{
    const qint64 secs = dataSecs(sym);
    if (secs < 0) return false;

    // Determine whether NYSE is currently open: Mon–Fri 9:30am–4:00pm US/Eastern
    const QDateTime nowET = QDateTime::currentDateTime().toTimeZone(QTimeZone("America/New_York"));
    const int  dow        = nowET.date().dayOfWeek(); // 1=Mon … 7=Sun
    const QTime t         = nowET.time();
    const bool marketOpen = (dow >= 1 && dow <= 5)
                            && t >= QTime(9, 30)
                            && t < QTime(16, 0);

    return secs <= (marketOpen ? 15 * 60 : 17 * 3600);
}

QString StockCacheManager::ageString(const QString &sym) const
{
    const qint64 secs = dataSecs(sym);
    if (secs < 0) return {};
    const qint64 days = secs / 86400;
    const qint64 hrs  = secs / 3600;
    const qint64 mins = secs / 60;
    if (days >= 1) return QString("%1d").arg(days);
    if (hrs  >= 1) return QString("%1h").arg(hrs);
    return QString("%1m").arg(qMax(qint64(1), mins));
}
