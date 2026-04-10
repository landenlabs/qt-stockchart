#pragma once
#include <QString>
#include <QList>
#include <QMap>

class StockDataProvider;

// Metadata for a single provider, loaded from providers.json
struct ProviderInfo {
    QString id;
    QString label;      // display name shown in all UI
    QString comment;    // tooltip / hover text
    QString url;        // signup / API-key URL
    QString accountUrl; // account dashboard URL (may be empty)
    bool    enabled = true;
};

// Singleton that loads providers.json (bundled as a Qt resource) and
// provides fast lookup of provider metadata by id.
//
// Usage:
//   ProviderRegistry::instance().label("finnhub")   -> "Finnhub"
//   ProviderRegistry::instance().info("fmp").comment -> "Free tier: ..."
class ProviderRegistry
{
public:
    static ProviderRegistry &instance();

    // Returns metadata for the given id, or a default-constructed ProviderInfo
    // (with label == id) if the id is not found in the JSON.
    ProviderInfo info(const QString &id) const;

    // Convenience single-field accessors
    QString label(const QString &id) const      { return info(id).label; }
    QString comment(const QString &id) const    { return info(id).comment; }
    QString url(const QString &id) const        { return info(id).url; }
    QString accountUrl(const QString &id) const { return info(id).accountUrl; }
    bool    enabled(const QString &id) const    { return info(id).enabled; }

    // All provider infos in the order they appear in providers.json
    QList<ProviderInfo> all() const { return m_ordered; }

    // Logs a qWarning() for every provider id that appears in code but not in
    // providers.json, and vice-versa. Call once at startup after building m_providers.
    void validate(const QList<StockDataProvider*> &providers) const;

private:
    ProviderRegistry();

    QMap<QString, ProviderInfo> m_map;     // id -> info (fast lookup)
    QList<ProviderInfo>         m_ordered; // preserves JSON declaration order
};
