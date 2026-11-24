#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QVariant>

class StartupSettings {
public:
    static StartupSettings load();
    // Whether load() snapshots `key`; a persisted key it omits reads back as
    // its default on the next launch.
    static bool preloads(const QString& key);

    QVariant value(const QString& key, const QVariant& defaultValue = {}) const
    {
        const auto it = m_values.constFind(key);
        return it == m_values.cend() || !it.value().isValid() ? defaultValue : it.value();
    }

    const QList<QVariantMap>& sessionTabs() const { return m_sessionTabs; }

private:
    QHash<QString, QVariant> m_values;
    QList<QVariantMap> m_sessionTabs;
};
