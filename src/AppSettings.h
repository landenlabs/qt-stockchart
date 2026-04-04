#pragma once
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVariantList>

// Singleton that owns the single QSettings instance for the whole application.
// All persistent state must be read and written through this class — never
// construct QSettings("StockChart","StockChart") anywhere else.
//
// For operations that require QSettings group/array APIs (StockCacheManager,
// StockGroupManager), call raw() to obtain the shared instance.  Those callers
// are responsible for correctly pairing every beginGroup/beginArray with the
// matching end call.
class AppSettings
{
public:
    static AppSettings &instance();

    // Gives callers that need group/array operations direct access to the
    // underlying instance.  Never store the returned reference beyond the
    // immediately enclosing scope.
    QSettings &raw() { return m_settings; }

    // Force a synchronous write to disk (call before quit).
    void sync() { m_settings.sync(); }

    // ── UI / layout ───────────────────────────────────────────────────────────
    bool       autoRefresh() const;
    void       setAutoRefresh(bool v);

    bool       logExpanded() const;
    void       setLogExpanded(bool v);

    int        yScaleIndex() const;
    void       setYScaleIndex(int v);

    QByteArray mainSplitterState() const;
    void       setMainSplitterState(const QByteArray &v);

    QByteArray outerSplitterState() const;
    void       setOuterSplitterState(const QByteArray &v);

    // ── Provider ──────────────────────────────────────────────────────────────
    QString     activeProvider() const;
    void        setActiveProvider(const QString &v);

    QStringList selectedSymbols() const;
    void        setSelectedSymbols(const QStringList &v);

    // Credentials are stored at "<providerId>/<fieldName>" — equivalent to
    // beginGroup(providerId) + setValue(fieldName) used by the old code.
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

    QByteArray   vertSplitterState() const;
    void         setVertSplitterState(const QByteArray &v);

    bool         tableExpanded() const;
    void         setTableExpanded(bool v);

    // ── Ad blocker ────────────────────────────────────────────────────────────
    QStringList  adBlockBlacklist() const;
    void         setAdBlockBlacklist(const QStringList &v);

    // ── API call tracking ─────────────────────────────────────────────────────
    // Stored under the "dailyCalls" group.
    QString dailyCallDate() const;
    void    setDailyCallDate(const QString &date);

    int     dailyCallCount(const QString &providerId) const;
    void    setDailyCallCount(const QString &providerId, int count);

private:
    AppSettings();
    ~AppSettings() = default;
    AppSettings(const AppSettings &)            = delete;
    AppSettings &operator=(const AppSettings &) = delete;

    QSettings m_settings;
};
