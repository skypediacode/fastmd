// Regression coverage for the terminal WebView cursor contract. xterm ships
// with cursor:text on its root; FastMD's later stylesheet must deliberately
// override that default without touching xterm's more-specific interaction
// cursors (links, mouse reporting, and column selection).

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

void testTerminalDefaultCursorIsArrow()
{
    QFile file(QStringLiteral(FASTMD_TERMINAL_CSS_PATH));
    CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString css = QString::fromUtf8(file.readAll());

    // terminal.html loads terminal.css after xterm.css, so this equal-specificity
    // rule overrides the vendor root rule while its specific interaction rules
    // still win when xterm applies them.
    CHECK(css.contains(QStringLiteral(".xterm {\n  cursor: default;\n}")));
}

} // namespace

int main()
{
    testTerminalDefaultCursorIsArrow();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
