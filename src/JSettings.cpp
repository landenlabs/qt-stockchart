#include "JSettings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStringList>

JSettings::JSettings(const QString &filePath)
    : m_filePath(filePath)
{
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    QFile f(filePath);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (!doc.isNull() && doc.isObject())
            m_data = doc.object();
    }
}

// ── Public ────────────────────────────────────────────────────────────────────

QVariant JSettings::value(const QString &key, const QVariant &defaultValue) const
{
    const QStringList parts = key.split('/');
    QJsonObject obj = m_data;
    for (int i = 0; i < parts.size() - 1; ++i) {
        const QJsonValue v = obj[parts[i]];
        if (!v.isObject()) return defaultValue;
        obj = v.toObject();
    }
    const QJsonValue jv = obj[parts.last()];
    if (jv.isUndefined() || jv.isNull())
        return defaultValue;
    return jsonToVariant(jv, defaultValue);
}

void JSettings::setValue(const QString &key, const QVariant &v)
{
    const QStringList parts = key.split('/');
    if (parts.size() == 1) {
        m_data[key] = variantToJson(v);
        return;
    }
    // Build a stack of objects navigating down the path, then propagate back up.
    QVector<QJsonObject> objs;
    objs.reserve(parts.size());
    objs.append(m_data);
    for (int i = 0; i < parts.size() - 1; ++i)
        objs.append(objs.last()[parts[i]].toObject());

    objs.last()[parts.last()] = variantToJson(v);

    for (int i = parts.size() - 2; i >= 0; --i)
        objs[i][parts[i]] = objs[i + 1];

    m_data = objs[0];
}

QJsonValue JSettings::jsonValue(const QString &key) const
{
    return m_data[key];
}

void JSettings::setJsonValue(const QString &key, const QJsonValue &v)
{
    m_data[key] = v;
}

void JSettings::sync()
{
    QFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(m_data).toJson(QJsonDocument::Indented));
}

// ── Private helpers ───────────────────────────────────────────────────────────

QJsonValue JSettings::variantToJson(const QVariant &v)
{
    switch (v.userType()) {
    case QMetaType::Bool:    return v.toBool();
    case QMetaType::Int:     return v.toInt();
    case QMetaType::Double:  return v.toDouble();
    case QMetaType::Float:   return static_cast<double>(v.toFloat());
    case QMetaType::QString: return v.toString();
    case QMetaType::QStringList: {
        QJsonArray arr;
        for (const QString &s : v.toStringList())
            arr.append(s);
        return arr;
    }
    default:
        if (v.canConvert<QVariantList>()) {
            QJsonArray arr;
            for (const QVariant &item : v.toList())
                arr.append(variantToJson(item));
            return arr;
        }
        return v.toString();
    }
}

QVariant JSettings::jsonToVariant(const QJsonValue &jv, const QVariant &defaultValue)
{
    // Use the default's type as a hint when available.
    if (!defaultValue.isNull()) {
        switch (defaultValue.userType()) {
        case QMetaType::Bool:    return jv.toBool();
        case QMetaType::Int:     return jv.toInt();
        case QMetaType::Double:  return jv.toDouble();
        case QMetaType::Float:   return static_cast<float>(jv.toDouble());
        case QMetaType::QString: return jv.toString();
        default: break;
        }
    }

    // Natural JSON type conversion.
    switch (jv.type()) {
    case QJsonValue::Bool:   return jv.toBool();
    case QJsonValue::Double: return jv.toDouble();
    case QJsonValue::String: return jv.toString();
    case QJsonValue::Array: {
        QVariantList list;
        for (const QJsonValue &item : jv.toArray())
            list << jsonToVariant(item, {});
        return list;
    }
    default:
        return defaultValue;
    }
}
