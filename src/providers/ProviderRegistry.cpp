#include "ProviderRegistry.h"
#include "StockDataProvider.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

ProviderRegistry::ProviderRegistry()
{
    QFile f(":/providers.json");
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "ProviderRegistry: could not open :/providers.json";
        return;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull()) {
        qWarning() << "ProviderRegistry: JSON parse error:" << err.errorString();
        return;
    }

    for (const QJsonValue &v : doc.array()) {
        const QJsonObject obj = v.toObject();
        ProviderInfo pi;
        pi.id          = obj["id"].toString();
        pi.label       = obj["label"].toString();
        pi.comment     = obj["comment"].toString();
        pi.url         = obj["url"].toString();
        pi.accountUrl  = obj["accountUrl"].toString();
        pi.quoteFromId = obj["quote_from_id"].toString();
        pi.enabled     = obj["enable"].toBool(true);

        if (pi.id.isEmpty()) {
            qWarning() << "ProviderRegistry: skipping entry with empty id";
            continue;
        }
        if (pi.label.isEmpty()) pi.label = pi.id; // safe fallback

        m_map[pi.id] = pi;
        m_ordered.append(pi);
    }
}

ProviderRegistry &ProviderRegistry::instance()
{
    static ProviderRegistry s;
    return s;
}

ProviderInfo ProviderRegistry::info(const QString &id) const
{
    if (m_map.contains(id))
        return m_map[id];

    // Unknown id — return a safe default so callers never crash
    ProviderInfo fallback;
    fallback.id    = id;
    fallback.label = id; // use the raw id as label
    return fallback;
}

QList<StockDataProvider*> ProviderRegistry::validate(const QList<StockDataProvider*> &providers) const
{
    QList<StockDataProvider*> validProviders;

    // 1. Map the incoming providers by ID for O(1) lookup.
    // This decouples their original order from the lookup process.
    QMap<QString, StockDataProvider*> providerMap;
    for (StockDataProvider *p : providers) {
        providerMap.insert(p->id(), p);
    }

    // 2. Iterate through m_ordered to build the final list.
    // This ensures the return list follows the JSON order.
    for (const ProviderInfo &pi : m_ordered) {
        // Condition: ID must exist in the code-provided list AND the JSON map
        if (providerMap.contains(pi.id) && m_map.contains(pi.id)) {
            if (m_map[pi.id].enabled)
                validProviders.append(providerMap.value(pi.id));
        } else {
            if (!providerMap.contains(pi.id)) {
                qWarning() << "ProviderRegistry: JSON ID" << pi.id << "not found in code.";
            }
            if (!m_map.contains(pi.id)) {
                qWarning() << "ProviderRegistry: JSON ID" << pi.id << "not found in m_map.";
            }
        }
    }

    // 3. Optional: Warn about "Orphan" code objects (Code exists but not in JSON)
    for (StockDataProvider *p : providers) {
        if (!m_map.contains(p->id())) {
            qWarning() << "ProviderRegistry: Code object" << p->id() << "discarded (no JSON entry).";
        }
    }

    return validProviders;
}
