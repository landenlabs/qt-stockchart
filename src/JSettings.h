#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QVariant>
#include <QString>

// Lightweight JSON-backed settings store.
//
// Supports slash-separated key paths up to two levels deep
// (e.g. "dailyCalls/date", "AlphaVantage/apiKey").
// Loaded from and saved to a plain JSON file via sync().
class JSettings
{
public:
    explicit JSettings(const QString &filePath);

    QVariant   value(const QString &key, const QVariant &defaultValue = {}) const;
    void       setValue(const QString &key, const QVariant &v);

    // Direct JSON access for structured values (arrays, nested objects).
    QJsonValue jsonValue(const QString &key) const;
    void       setJsonValue(const QString &key, const QJsonValue &v);

    void sync();

private:
    static QJsonValue variantToJson(const QVariant &v);
    static QVariant   jsonToVariant(const QJsonValue &jv, const QVariant &defaultValue);

    QJsonObject m_data;
    QString     m_filePath;
};
