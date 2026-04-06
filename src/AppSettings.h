#pragma once
#include "JSettings.h"
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QStandardPaths>

// Singleton that owns the single JSettings instance for the whole application.
// All persistent state must be read and written through this class.
class AppSettings
{
public:
    static AppSettings &instance();

    // Force a synchronous write to disk (call before quit).
    void sync() { m_settings.sync(); }

    // Path of the JSON settings file on disk.
    QString settingsFilePath() const;


    // ── UI / layout ───────────────────────────────────────────────────────────
    bool       autoRefresh() const;
    void       setAutoRefresh(bool v);

    bool       logExpanded() const;
    void       setLogExpanded(bool v);

    int        yScaleIndex() const;
    void       setYScaleIndex(int v);

    // Splitter positions (single int per splitter; QByteArray state removed).
    int        mainSplitterPos() const;   // left-panel width
    void       setMainSplitterPos(int v);

    int        outerSplitterPos() const;  // log-pane height
    void       setOuterSplitterPos(int v);

    // ── Provider ──────────────────────────────────────────────────────────────
    QString     activeProvider() const;
    void        setActiveProvider(const QString &v);

    QStringList selectedSymbols() const;
    void        setSelectedSymbols(const QStringList &v);

    // Credentials are stored at "<providerId>/<fieldName>".
    QString     providerCredential(const QString &providerId,
                                   const QString &field) const;
    void        setProviderCredential(const QString &providerId,
                                      const QString &field,
                                      const QString &value);

    // ── Chart ─────────────────────────────────────────────────────────────────
    int  lastChartRangeDays() const;
    void setLastChartRangeDays(int v);

    // ── Table ─────────────────────────────────────────────────────────────────
    QVariantList tablePeriods() const;
    void         setTablePeriods(const QVariantList &v);

    bool         tableShowPercent() const;
    void         setTableShowPercent(bool v);

    int          tableHeight() const;
    void         setTableHeight(int v);

    /*
    bool         tableExpanded() const;
    void         setTableExpanded(bool v);
    */

    // ── Appearance ────────────────────────────────────────────────────────────
    int  fontPointSize() const;        // 0 = use system default
    void setFontPointSize(int v);

    // ── Cache storage ─────────────────────────────────────────────────────────
    QString cacheDirPath() const;   // defaults to AppDataLocation/cache
    void    setCacheDirPath(const QString &path);

    // ── Ad blocker ────────────────────────────────────────────────────────────
    QStringList  adBlockBlacklist() const;
    void         setAdBlockBlacklist(const QStringList &v);

    QString      adBlockAdRegex() const;
    void         setAdBlockAdRegex(const QString &v);

    // ── API call tracking ─────────────────────────────────────────────────────
    QString dailyCallDate() const;
    void    setDailyCallDate(const QString &date);

    int     dailyCallCount(const QString &providerId) const;
    void    setDailyCallCount(const QString &providerId, int count);

    // ── Stock groups (structured JSON) ────────────────────────────────────────
    QJsonArray stockGroups() const;
    void       setStockGroups(const QJsonArray &v);

private:
    AppSettings();
    ~AppSettings() = default;
    AppSettings(const AppSettings &)            = delete;
    AppSettings &operator=(const AppSettings &) = delete;

    JSettings m_settings;
};
