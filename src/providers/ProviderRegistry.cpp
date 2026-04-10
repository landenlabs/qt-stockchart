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
        pi.id         = obj["id"].toString();
        pi.label      = obj["label"].toString();
        pi.comment    = obj["comment"].toString();
        pi.url        = obj["url"].toString();
        pi.accountUrl = obj["accountUrl"].toString();
        pi.enabled    = obj["enable"].toBool(true);

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

void ProviderRegistry::validate(const QList<StockDataProvider*> &providers) const
{
    // Check every code-registered provider has a JSON entry
    for (const StockDataProvider *p : providers) {
        if (!m_map.contains(p->id()))
            qWarning() << "ProviderRegistry: no JSON entry for provider id:" << p->id();
    }
    // Check every JSON entry has a corresponding code object
    QStringList codeIds;
    for (const StockDataProvider *p : providers) codeIds.append(p->id());
    for (const ProviderInfo &pi : m_ordered) {
        if (!codeIds.contains(pi.id))
            qWarning() << "ProviderRegistry: JSON provider not found in code:" << pi.id;
    }
}
