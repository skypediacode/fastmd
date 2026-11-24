// Regression coverage for the terminal grid's sizing contract.
//
// xterm's fit addon derives the row/column count from
// getComputedStyle(parentElement).height -- the border-box height when that
// parent uses box-sizing: border-box -- and subtracts only the .xterm element's
// own padding, never the parent's. Padding on .terminal-host is therefore space
// xterm counts as usable: it lays out a grid that overflows the host by the
// vertical padding, and .terminal-host's overflow:hidden clips whatever part of
// the bottom row falls past the edge. The clipped fraction changes with every
// panel height, which is what made the symptom look intermittent.
//
// The gutter must be expressed as insets instead, so the box the addon measures
// is the box the grid may paint into.

#include <QFile>
#include <QString>

#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

QString ruleBody(const QString& css, const QString& selector)
{
    const int start = css.indexOf(selector + QStringLiteral(" {"));
    if (start < 0) return QString();
    const int open = css.indexOf(QLatin1Char('{'), start);
    const int close = css.indexOf(QLatin1Char('}'), open);
    if (open < 0 || close < 0) return QString();
    return css.mid(open + 1, close - open - 1);
}

QString declarations(const QString& body)
{
    // Strip comments so a word inside one cannot satisfy or fail a check.
    QString out;
    out.reserve(body.size());
    int i = 0;
    while (i < body.size()) {
        if (body.mid(i, 2) == QLatin1String("/*")) {
            const int end = body.indexOf(QStringLiteral("*/"), i + 2);
            if (end < 0) break;
            i = end + 2;
            continue;
        }
        out.append(body.at(i));
        ++i;
    }
    return out;
}

void testTerminalHostReservesItsGutterWithInsets()
{
    QFile file(QStringLiteral(FASTMD_TERMINAL_CSS_PATH));
    CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString css = QString::fromUtf8(file.readAll());

    const QString host = declarations(ruleBody(css, QStringLiteral(".terminal-host")));
    CHECK(!host.isEmpty());

    // The whole point: no padding, on any axis, under any shorthand.
    CHECK(!host.contains(QStringLiteral("padding")));

    // Every side is stated explicitly, carrying the gutters the padding used to
    // express. `inset: 0` plus one overriding side hides which sides are gutters.
    CHECK(host.contains(QStringLiteral("position: absolute")));
    CHECK(host.contains(QStringLiteral("top: 4px")));
    CHECK(host.contains(QStringLiteral("right: 8px")));
    CHECK(host.contains(QStringLiteral("bottom: 8px")));
    CHECK(host.contains(QStringLiteral("left: 8px")));

    // The custom scrollbar is positioned into the right inset; the host must
    // not clip that track.
    CHECK(host.contains(QStringLiteral("overflow: visible")));
}

void testScrollbarTrackMatchesTheViewportItMirrors()
{
    QFile file(QStringLiteral(FASTMD_TERMINAL_CSS_PATH));
    CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString css = QString::fromUtf8(file.readAll());

    const QString track = declarations(ruleBody(css, QStringLiteral(".term-scrollbar")));
    CHECK(!track.isEmpty());

    // updateScrollbar() sizes the thumb from the viewport's clientHeight, so the
    // track has to span the host exactly rather than a hand-tuned inset of it.
    CHECK(track.contains(QStringLiteral("top: 0")));
    CHECK(track.contains(QStringLiteral("bottom: 0")));

    // It paints in the gutter .terminal-host's right inset reserved, which is
    // outside the host -- the two numbers have to stay in step.
    CHECK(track.contains(QStringLiteral("right: -10px")));
    CHECK(track.contains(QStringLiteral("width: 10px")));
}

void testTerminalSessionListResetsGlobalRoundedListChrome()
{
    QFile file(QStringLiteral(FASTMD_STYLESHEET_PATH));
    CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString style = QString::fromUtf8(file.readAll());

    // The global QListWidget rule supplies a 4px radius. Removing its border
    // does not reset that property, so the terminal pane must explicitly make
    // both theme-specific rules square to prevent the light parent showing
    // through at the corners.
    const QString selector = QStringLiteral("QListWidget#terminalSessionList");
    int searchFrom = 0;
    int matches = 0;
    const QString ruleStart = selector + QStringLiteral(" {");
    while ((searchFrom = style.indexOf(ruleStart, searchFrom)) >= 0) {
        const int open = searchFrom + ruleStart.size() - 1;
        const int close = style.indexOf(QLatin1Char('}'), open);
        CHECK(open >= 0 && close >= 0);
        const QString body = style.mid(open + 1, close - open - 1);
        CHECK(body.contains(QStringLiteral("border: none")));
        CHECK(body.contains(QStringLiteral("border-radius: 0px")));
        ++matches;
        searchFrom = close + 1;
    }
    CHECK(matches == 2);
}

} // namespace

int main()
{
    testTerminalHostReservesItsGutterWithInsets();
    testScrollbarTrackMatchesTheViewportItMirrors();
    testTerminalSessionListResetsGlobalRoundedListChrome();

    if (g_failures == 0) {
        std::printf("TerminalHostLayoutTest: %d checks passed\n", g_checks);
        return 0;
    }
    std::printf("TerminalHostLayoutTest: %d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
}
