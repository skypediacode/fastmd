#include "NavigationHistory.h"

#include <QDir>
#include <QFileInfo>

QString NavigationHistory::canonicalPath(const QString& path)
{
    if (path.isEmpty())
        return {};
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const QString canonical = QFileInfo(absolute).canonicalFilePath();
    return QDir::toNativeSeparators((canonical.isEmpty() ? absolute : canonical)).toCaseFolded();
}

bool NavigationHistory::equivalent(const Location& a, const Location& b)
{
    return canonicalPath(a.path) == canonicalPath(b.path)
        && (qMax(1, a.line) - 1) / 20 == (qMax(1, b.line) - 1) / 20;
}

void NavigationHistory::push(const Location& input)
{
    Location location = input;
    location.path = canonicalPath(location.path);
    if (location.path.isEmpty())
        return;
    if (!m_back.isEmpty() && equivalent(m_back.constLast(), location)) {
        m_back.last() = location;
        return;
    }
    m_back.append(location);
    while (m_back.size() > MaxEntries)
        m_back.removeFirst();
}

std::optional<NavigationHistory::Location> NavigationHistory::takeValid(
    QList<Location>& stack, const std::function<bool(const Location&)>& valid)
{
    while (!stack.isEmpty()) {
        Location result = stack.takeLast();
        if (!valid || valid(result))
            return result;
    }
    return std::nullopt;
}

std::optional<NavigationHistory::Location> NavigationHistory::goBack(
    const Location& current, const std::function<bool(const Location&)>& valid)
{
    auto result = takeValid(m_back, valid);
    if (result) {
        Location departure = current;
        departure.path = canonicalPath(departure.path);
        m_forward.append(departure);
        markBackNavigation();
    }
    return result;
}

std::optional<NavigationHistory::Location> NavigationHistory::goForward(
    const Location& current, const std::function<bool(const Location&)>& valid)
{
    auto result = takeValid(m_forward, valid);
    if (result) {
        Location departure = current;
        departure.path = canonicalPath(departure.path);
        m_back.append(departure);
    }
    return result;
}

void NavigationHistory::clearForward()
{
    m_forward.clear();
    m_forwardClearedOnEdit = true;
}

void NavigationHistory::markBackNavigation()
{
    m_forwardClearedOnEdit = false;
}

void NavigationHistory::onDocumentEdited()
{
    if (!m_forward.isEmpty() && !m_forwardClearedOnEdit)
        clearForward();
}
