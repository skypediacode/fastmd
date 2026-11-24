#include "NavigationHistory.h"

#include <QCoreApplication>

#include <cstdio>

static bool require(bool value, const char* message)
{
    if (!value)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return value;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using Location = NavigationHistory::Location;
    const QString a = QStringLiteral("a.md");
    const QString b = QStringLiteral("b.md");
    NavigationHistory history;

    Location first; first.path = a; first.line = 1;
    Location nearby = first; nearby.line = 10;
    Location far = first; far.line = 21;
    history.push(first);
    history.push(nearby);
    bool ok = require(history.canGoBack(), "push creates back history");
    history.push(far);
    auto target = history.goBack(Location{b, 0, 0, 0, 0, 1});
    ok &= require(target && target->path == NavigationHistory::canonicalPath(a), "back reaches prior file");
    auto roundTrip = history.goForward(Location{a, 0, 0, 0, 0, 21});
    ok &= require(roundTrip && roundTrip->path == NavigationHistory::canonicalPath(b), "forward round trip");

    history.markBackNavigation();
    history.onDocumentEdited();
    ok &= require(!history.canGoForward(), "edit clears forward after back");

    history.push(first);
    auto invalid = history.goBack(Location{b, 0, 0, 0, 0, 1}, [](const Location&) {
        return false;
    });
    ok &= require(!invalid && !history.canGoBack(), "invalid entries are pruned");
    return ok ? 0 : 1;
}
