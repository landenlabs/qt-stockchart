#include "AppSettings.h"
#include <QStandardPaths>

static QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/settings.json";
}

AppSettings::AppSettings()
    : m_settings(settingsPath())
{}

QString AppSettings::settingsFilePath() const
{
    return settingsPath();
}

AppSettings &AppSettings::instance()
{
    static AppSettings s;
    return s;
}

// ── UI / layout ───────────────────────────────────────────────────────────────

bool AppSettings::autoRefresh() const
    { return m_settings.value("autoRefresh", false).toBool(); }
void AppSettings::setAutoRefresh(bool v)
    { m_settings.setValue("autoRefresh", v); }

bool AppSettings::logExpanded() const
    { return m_settings.value("logExpanded", true).toBool(); }
void AppSettings::setLogExpanded(bool v)
    { m_settings.setValue("logExpanded", v); }

int AppSettings::yScaleIndex() const
    { return m_settings.value("yScaleIndex", 0).toInt(); } // 0=auto,10%,20%,30%,40%,50%
void AppSettings::setYScaleIndex(int v)
    { m_settings.setValue("yScaleIndex", v); }

int AppSettings::mainSplitterPos() const
    { return m_settings.value("mainSplitterPos", 0).toInt(); }
void AppSettings::setMainSplitterPos(int v)
    { m_settings.setValue("mainSplitterPos", v); }

int AppSettings::outerSplitterPos() const
    { return m_settings.value("outerSplitterPos", 0).toInt(); }
void AppSettings::setOuterSplitterPos(int v)
    { m_settings.setValue("outerSplitterPos", v); }

// ── Provider ──────────────────────────────────────────────────────────────────

QString AppSettings::activeProvider() const
    { return m_settings.value("providers/activeProvider").toString(); }
void AppSettings::setActiveProvider(const QString &v)
    { m_settings.setValue("providers/activeProvider", v); }

QStringList AppSettings::selectedSymbols() const
    { return m_settings.value("providers/selectedSymbols").toStringList(); }
void AppSettings::setSelectedSymbols(const QStringList &v)
    { m_settings.setValue("providers/selectedSymbols", v); }

QString AppSettings::providerCredential(const QString &providerId,
                                         const QString &field) const
    { return m_settings.value("providers/" + providerId + "/" + field).toString(); }
void AppSettings::setProviderCredential(const QString &providerId,
                                         const QString &field,
                                         const QString &value)
    { m_settings.setValue("providers/" + providerId + "/" + field, value); }

bool AppSettings::providerLimited(const QString &providerId) const
    { return m_settings.value("providers/" + providerId + "/limited", false).toBool(); }
void AppSettings::setProviderLimited(const QString &providerId, bool limited)
    { m_settings.setValue("providers/" + providerId + "/limited", limited); }

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

/*
bool AppSettings::tableExpanded() const
    { return m_settings.value("tableExpanded", false).toBool(); }
void AppSettings::setTableExpanded(bool v)
    { m_settings.setValue("tableExpanded", v); }
*/

// ── Appearance ────────────────────────────────────────────────────────────────

int AppSettings::fontPointSize() const
    { return m_settings.value("fontPointSize", 0).toInt(); }
void AppSettings::setFontPointSize(int v)
    { m_settings.setValue("fontPointSize", v); }

// ── Cache storage ─────────────────────────────────────────────────────────────

QString AppSettings::cacheDirPath() const
{
    const QString def = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/cache";
    return m_settings.value("cacheDirPath", def).toString();
}
void AppSettings::setCacheDirPath(const QString &path)
    { m_settings.setValue("cacheDirPath", path); }

// ── Ad blocker ────────────────────────────────────────────────────────────────

QStringList AppSettings::adBlockBlacklist() const
    { return m_settings.value("adBlockBlacklist").toStringList(); }
void AppSettings::setAdBlockBlacklist(const QStringList &v)
    { m_settings.setValue("adBlockBlacklist", v); }

static const QString kDefaultAdRegex = ".*(ad|doubleclick|amazon).*";
QString AppSettings::adBlockAdRegex() const
    { return m_settings.value("adBlockAdRegex", kDefaultAdRegex).toString(); }
void AppSettings::setAdBlockAdRegex(const QString &v)
    { m_settings.setValue("adBlockAdRegex", v); }

// ── API call tracking ─────────────────────────────────────────────────────────

QString AppSettings::dailyCallDate() const
    { return m_settings.value("providers/dailyCalls/date").toString(); }
void AppSettings::setDailyCallDate(const QString &date)
    { m_settings.setValue("providers/dailyCalls/date", date); }

int AppSettings::dailyCallCount(const QString &providerId) const
    { return m_settings.value("providers/dailyCalls/" + providerId, 0).toInt(); }
void AppSettings::setDailyCallCount(const QString &providerId, int count)
    { m_settings.setValue("providers/dailyCalls/" + providerId, count); }

// ── Stock groups ──────────────────────────────────────────────────────────────

QJsonArray AppSettings::stockGroups() const
    { return m_settings.jsonValue("stockGroups").toArray(); }
void AppSettings::setStockGroups(const QJsonArray &v)
    { m_settings.setJsonValue("stockGroups", v); }
