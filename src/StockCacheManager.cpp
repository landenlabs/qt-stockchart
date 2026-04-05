#include "StockCacheManager.h"
#include "AppSettings.h"
#include "Logger.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QLocale>
#include <QTime>
#include <limits>
#include <cmath>
#include <algorithm>

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

void StockCacheManager::normalizeCache(QVector<StockDataPoint> &points)
{
    if (points.isEmpty()) return;

    // 1. Sort ascending by timestamp.
    std::sort(points.begin(), points.end(),
              [](const StockDataPoint &a, const StockDataPoint &b) {
                  return a.timestamp < b.timestamp;
              });

    // 1a. Remove duplicates sharing the same (date, hour, minute); keep the
    //     largest timestamp in each bucket (last entry after ascending sort).
    {
        QVector<StockDataPoint> deduped;
        deduped.reserve(points.size());
        int j = 0;
        while (j < points.size()) {
            const QDateTime &base = points[j].timestamp;
            int k = j + 1;
            while (k < points.size()) {
                const QDateTime &t = points[k].timestamp;
                if (t.date()        == base.date()        &&
                    t.time().hour() == base.time().hour() &&
                    t.time().minute() == base.time().minute())
                    ++k;
                else
                    break;
            }
            deduped.append(points[k - 1]); // largest timestamp in bucket
            j = k;
        }
        points = std::move(deduped);
    }

    const QDate today       = QDate::currentDate();
    const QDate twoYearsAgo = today.addYears(-2);
    const QDate sevenDaysAgo = today.addDays(-7);
    const QTimeZone etZone("America/New_York");
    const QTime closeTime(16, 0, 0);

    QVector<StockDataPoint> result;
    result.reserve(points.size());

    int i = 0;

    // 2. Drop data older than 2 years.
    while (i < points.size() && points[i].timestamp.date() < twoYearsAgo)
        ++i;

    // 3. For data older than 7 days: keep one point per day, closest to 16:00 ET.
    while (i < points.size() && points[i].timestamp.date() < sevenDaysAgo) {
        const QDate day = points[i].timestamp.date();
        int best = i;
        int bestDist = std::numeric_limits<int>::max();
        while (i < points.size() && points[i].timestamp.date() == day) {
            const int dist = qAbs(
                points[i].timestamp.toTimeZone(etZone).time().secsTo(closeTime));
            if (dist < bestDist) {
                bestDist = dist;
                best = i;
            }
            ++i;
        }
        result.append(points[best]);
    }

    // 4. Data within 7 days: keep all points.
    while (i < points.size())
        result.append(points[i++]);

    points = std::move(result);
}

// ── File helpers ──────────────────────────────────────────────────────────────

// Sanitize a symbol name into a valid filename (no extension).
// Replaces characters invalid on Windows or confusing on any platform.
static QString symbolToFilename(const QString &symbol)
{
    QString name = symbol;
    // Characters invalid in Windows filenames, plus ^ and . for clarity
    for (QChar c : QString(R"(^./\:*?"<>|)"))
        name.replace(c, '_');
    return name + ".csv";
}

static QString typeToString(SymbolType t)
{
    switch (t) {
    case SymbolType::Stock:      return "stock";
    case SymbolType::ETF:        return "etf";
    case SymbolType::Index:      return "index";
    case SymbolType::MutualFund: return "mutualfund";
    case SymbolType::Crypto:     return "crypto";
    default:                     return "unknown";
    }
}

static SymbolType stringToType(const QString &s)
{
    if (s == "stock")      return SymbolType::Stock;
    if (s == "etf")        return SymbolType::ETF;
    if (s == "index")      return SymbolType::Index;
    if (s == "mutualfund") return SymbolType::MutualFund;
    if (s == "crypto")     return SymbolType::Crypto;
    return SymbolType::Unknown;
}

// ── Persistence ───────────────────────────────────────────────────────────────

void StockCacheManager::saveCache()
{
    // Don't overwrite disk data before we've done a first load.
    // setActiveProvider() calls saveCache() at startup when m_cache is still empty.
    if (!m_cacheLoaded) {
        Logger::instance().append("Cache: saveCache() skipped — not yet loaded");
        return;
    }

    const QString dir = AppSettings::instance().cacheDirPath();
    if (!QDir().mkpath(dir)) {
        Logger::instance().append("Cache: ERROR — cannot create cache directory: " + dir);
        return;
    }

    Logger::instance().append(QString("Cache: saving %1 symbol(s) to %2")
                               .arg(m_cache.size()).arg(dir));

    // Write per-symbol CSV files (epoch seconds, price).
    // Use C locale so decimal point is always '.' regardless of system locale.
    int savedFiles = 0;
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        const QString path = dir + "/" + symbolToFilename(it.key());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            Logger::instance().append("Cache: ERROR writing " + path + " — " + f.errorString());
            continue;
        }
        QTextStream out(&f);
        out.setLocale(QLocale::c());
        for (const auto &pt : it.value())
            out << pt.timestamp.toSecsSinceEpoch() << ',' << pt.price << '\n';
        ++savedFiles;
    }

    // Write index.csv: symbol, filename, type.
    const QString idxPath = dir + "/index.csv";
    QFile idx(idxPath);
    if (!idx.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Logger::instance().append("Cache: ERROR writing index.csv — " + idx.errorString());
        return;
    }
    QTextStream out(&idx);
    out << "symbol,filename,type\n";
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        const QString &sym = it.key();
        out << sym << ','
            << symbolToFilename(sym) << ','
            << typeToString(m_symbolTypes.value(sym, SymbolType::Unknown)) << '\n';
    }
    Logger::instance().append(QString("Cache: saved %1 file(s) + index.csv").arg(savedFiles));
}

void StockCacheManager::loadCache()
{
    const QString dir = AppSettings::instance().cacheDirPath();
    Logger::instance().append("Cache: loadCache() from " + dir);

    // Mark as loaded regardless — even a missing index.csv is a valid empty cache.
    m_cacheLoaded = true;

    const QString idxPath = dir + "/index.csv";
    QFile idx(idxPath);
    if (!idx.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Logger::instance().append("Cache: index.csv not found — " + idxPath);
        return;
    }

    QTextStream idxIn(&idx);
    idxIn.readLine(); // skip header: "symbol,filename,type"

    QMap<QString, QString> symbolFiles; // symbol → filename
    while (!idxIn.atEnd()) {
        const QString line = idxIn.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QStringList parts = line.split(',');
        if (parts.size() < 3) continue;
        symbolFiles[parts[0]]  = parts[1];
        m_symbolTypes[parts[0]] = stringToType(parts[2]);
    }
    Logger::instance().append(QString("Cache: index has %1 symbol(s)").arg(symbolFiles.size()));

    // Load each per-symbol CSV file.
    int loadedSymbols = 0;
    for (auto it = symbolFiles.cbegin(); it != symbolFiles.cend(); ++it) {
        const QString filePath = dir + "/" + it.value();
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            Logger::instance().append("Cache: missing data file — " + filePath);
            continue;
        }
        QTextStream fin(&f);
        fin.setLocale(QLocale::c());
        QVector<StockDataPoint> points;
        int parseErrors = 0;
        while (!fin.atEnd()) {
            const QString line = fin.readLine().trimmed();
            if (line.isEmpty()) continue;
            const int comma = line.indexOf(',');
            if (comma < 0) { ++parseErrors; continue; }
            bool epochOk, priceOk;
            const qint64 epoch = line.left(comma).toLongLong(&epochOk);
            const double price = line.mid(comma + 1).toDouble(&priceOk);
            if (epochOk && priceOk)
                points.append({QDateTime::fromSecsSinceEpoch(epoch), price});
            else
                ++parseErrors;
        }
        if (parseErrors > 0)
            Logger::instance().append(QString("Cache: %1 — %2 parse error(s)")
                                       .arg(it.key()).arg(parseErrors));
        if (!points.isEmpty()) {
            normalizeCache(points);
            m_cache[it.key()] = points;
            ++loadedSymbols;
        } else {
            Logger::instance().append("Cache: " + it.key() + " — no valid points after normalize");
        }
    }
    Logger::instance().append(QString("Cache: loaded %1/%2 symbol(s)")
                               .arg(loadedSymbols).arg(symbolFiles.size()));
}

void StockCacheManager::loadSymbolTypeCache()
{
    // Types are loaded as part of loadCache() via index.csv — no-op here.
}

void StockCacheManager::saveSymbolType(const QString &symbol, SymbolType type)
{
    // Update in-memory only; index.csv is written at app close via saveCache().
    m_symbolTypes[symbol] = type;
}

void StockCacheManager::clearSymbolCache(const QString &symbol)
{
    m_cache.remove(symbol);
    m_symbolTypes.remove(symbol);
    const QString path = AppSettings::instance().cacheDirPath() + "/" + symbolToFilename(symbol);
    if (QFile::remove(path))
        Logger::instance().append("Cache: removed " + path);
    // index.csv is rebuilt on the next saveCache() call.
}

// ── Freshness ─────────────────────────────────────────────────────────────────

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
    if (days > 1) return QString("%1d").arg(days);
    if (hrs  >= 1) return QString("%1h").arg(hrs);
    return QString("%1m").arg(qMax(qint64(1), mins));
}
