#pragma once

#include <QHash>
#include <QString>

class FileChangeSuppressionTracker {
public:
    void arm(const QString& path, quint64 ticket)
    {
        if (!path.isEmpty())
            m_tickets.insert(path, ticket);
    }

    bool consume(const QString& path)
    {
        return m_tickets.remove(path);
    }

    void clear(const QString& path)
    {
        m_tickets.remove(path);
    }

    void clear(const QString& path, quint64 ticket)
    {
        const auto it = m_tickets.constFind(path);
        if (it != m_tickets.cend() && it.value() == ticket)
            m_tickets.remove(path);
    }

    bool matches(const QString& path, quint64 ticket) const
    {
        return m_tickets.value(path, 0) == ticket;
    }

private:
    QHash<QString, quint64> m_tickets;
};
