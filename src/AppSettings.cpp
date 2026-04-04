#include "AppSettings.h"

static constexpr char kOrg[] = "StockChart";
static constexpr char kApp[] = "StockChart";

AppSettings::AppSettings()
    : m_settings(kOrg, kApp)
{}

AppSettings &AppSettings::instance()
{
    static AppSettings s;
    return s;
}

// ── UI / layout ───────────────────────────────────────────────────────────────

bool AppSettings::autoRefresh() const
    { return m_settings.value("autoRefresh", true).toBool(); }
void AppSettings::setAutoRefresh(bool v)
    { m_settings.setValue("autoRefresh", v); }

bool AppSettings::logExpanded() const
    { return m_settings.value("logExpanded", true).toBool(); }
void AppSettings::setLogExpanded(bool v)
    { m_settings.setValue("logExpanded", v); }

int AppSettings::yScaleIndex() const
    { return m_settings.value("yScaleIndex", 2).toInt(); }
void AppSettings::setYScaleIndex(int v)
    { m_settings.setValue("yScaleIndex", v); }

QByteArray AppSettings::mainSplitterState() const
    { return m_settings.value("mainSplitterState").toByteArray(); }
void AppSettings::setMainSplitterState(const QByteArray &v)
    { m_settings.setValue("mainSplitterState", v); }

QByteArray AppSettings::outerSplitterState() const
    { return m_settings.value("outerSplitterState").toByteArray(); }
void AppSettings::setOuterSplitterState(const QByteArray &v)
    { m_settings.setValue("outerSplitterState", v); }

// ── Provider ──────────────────────────────────────────────────────────────────

QString AppSettings::activeProvider() const
    { return m_settings.value("activeProvider").toString(); }
void AppSettings::setActiveProvider(const QString &v)
    { m_settings.setValue("activeProvider", v); }

QStringList AppSettings::selectedSymbols() const
    { return m_settings.value("selectedSymbols").toStringList(); }
void AppSettings::setSelectedSymbols(const QStringList &v)
    { m_settings.setValue("selectedSymbols", v); }

// Credentials use the path "<providerId>/<fieldName>" which is equivalent to
// beginGroup(providerId) + value/setValue(fieldName) — same storage location.
QString AppSettings::providerCredential(const QString &providerId,
                                         const QString &field) const
    { return m_settings.value(providerId + "/" + field).toString(); }
void AppSettings::setProviderCredential(const QString &providerId,
                                         const QString &field,
                                         const QString &value)
    { m_settings.setValue(providerId + "/" + field, value); }

// ── Chart ─────────────────────────────────────────────────────────────────────

int AppSettings::lastChartRangeDays() const
    { return m_settings.value("lastChartRangeDays", 60).toInt(); }
void AppSettings::setLastChartRangeDays(int v)
    { m_settings.setValue("lastChartRangeDays", v); }

// ── Table ─────────────────────────────────────────────────────────────────────

QVariantList AppSettings::tablePeriods() const
    { return m_settings.value("tablePeriods").toList(); }
void AppSettings::setTablePeriods(const QVariantList &v)
    { m_settings.setValue("tablePeriods", v); }

bool AppSettings::tableShowPercent() const
    { return m_settings.value("tableShowPercent", false).toBool(); }
void AppSettings::setTableShowPercent(bool v)
    { m_settings.setValue("tableShowPercent", v); }

int AppSettings::tableHeight() const
    { return m_settings.value("tableHeight", -1).toInt(); }
void AppSettings::setTableHeight(int v)
    { m_settings.setValue("tableHeight", v); }

QByteArray AppSettings::vertSplitterState() const
    { return m_settings.value("vertSplitterState").toByteArray(); }
void AppSettings::setVertSplitterState(const QByteArray &v)
    { m_settings.setValue("vertSplitterState", v); }

bool AppSettings::tableExpanded() const
    { return m_settings.value("tableExpanded", false).toBool(); }
void AppSettings::setTableExpanded(bool v)
    { m_settings.setValue("tableExpanded", v); }

// ── Ad blocker ────────────────────────────────────────────────────────────────

QStringList AppSettings::adBlockBlacklist() const
    { return m_settings.value("adBlockBlacklist").toStringList(); }
void AppSettings::setAdBlockBlacklist(const QStringList &v)
    { m_settings.setValue("adBlockBlacklist", v); }

// ── API call tracking ─────────────────────────────────────────────────────────
// Stored as "dailyCalls/date" and "dailyCalls/<providerId>" — equivalent to the
// old beginGroup("dailyCalls") + value("date") / value(providerId) pattern.

QString AppSettings::dailyCallDate() const
    { return m_settings.value("dailyCalls/date").toString(); }
void AppSettings::setDailyCallDate(const QString &date)
    { m_settings.setValue("dailyCalls/date", date); }

int AppSettings::dailyCallCount(const QString &providerId) const
    { return m_settings.value("dailyCalls/" + providerId, 0).toInt(); }
void AppSettings::setDailyCallCount(const QString &providerId, int count)
    { m_settings.setValue("dailyCalls/" + providerId, count); }
