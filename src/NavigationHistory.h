#pragma once

#include <QList>
#include <QString>
#include <functional>
#include <optional>

class NavigationHistory
{
public:
    struct Location {
        QString path;
        int anchor = 0;
        int position = 0;
        int verticalScroll = 0;
        int horizontalScroll = 0;
        int line = 1;
    };

    static QString canonicalPath(const QString& path);

    void push(const Location& location);
    std::optional<Location> goBack(const Location& current,
                                   const std::function<bool(const Location&)>& valid = {});
    std::optional<Location> goForward(const Location& current,
                                      const std::function<bool(const Location&)>& valid = {});
    void clearForward();
    void markBackNavigation();
    void onDocumentEdited();

    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }
    bool forwardClearedOnEdit() const { return m_forwardClearedOnEdit; }

private:
    static constexpr int MaxEntries = 100;
    static bool equivalent(const Location& a, const Location& b);
    static std::optional<Location> takeValid(QList<Location>& stack,
                                             const std::function<bool(const Location&)>& valid);

    QList<Location> m_back;
    QList<Location> m_forward;
    bool m_forwardClearedOnEdit = false;
};
