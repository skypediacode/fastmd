// Guards the theme ownership of dialogs the Terminal opens: docked they follow
// the application theme, detached they follow the Terminal window's effective
// theme, and a ThemedMessageBox given a theme other than the application's is
// themed consistently (title bar flag, palette and stylesheet together)
// without changing the application's own theme.

#include "ApplicationTheme.h"
#include "TerminalPanel.h"
#include "ThemedMessageBox.h"

#include <QApplication>
#include <QCheckBox>
#include <QWidget>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

bool isDarkColor(const QColor& color) { return color.lightness() < 128; }

void testResolutionMatrix()
{
    // Docked: the app theme wins whatever the terminal is pinned to.
    CHECK(!TerminalPanel::dialogThemeDark(false, true, false));
    CHECK(TerminalPanel::dialogThemeDark(false, false, true));
    CHECK(!TerminalPanel::dialogThemeDark(false, false, false));
    CHECK(TerminalPanel::dialogThemeDark(false, true, true));
    // Detached: the Terminal window's effective theme wins.
    CHECK(TerminalPanel::dialogThemeDark(true, true, false));
    CHECK(!TerminalPanel::dialogThemeDark(true, false, true));
    CHECK(!TerminalPanel::dialogThemeDark(true, false, false));
    CHECK(TerminalPanel::dialogThemeDark(true, true, true));
}

void testParentStylesheetDoesNotLeakIntoTheTitleBar()
{
    ApplicationTheme::apply(QStringLiteral("light"));
    // What a Terminal pinned Dark inside a Light app looks like to a child.
    QWidget panel;
    panel.setObjectName(QStringLiteral("terminalPanel"));
    panel.setStyleSheet(QStringLiteral("QWidget#terminalPanel { background: #1e1e1e; }"));
    panel.ensurePolished();
    ThemedMessageBox box(&panel);
    CHECK(!box.isDarkTheme());
    CHECK(box.styleSheet().isEmpty());
}

void testExplicitThemeIsConsistent(const QString& appTheme, bool dialogDark)
{
    const bool appDark = ApplicationTheme::apply(appTheme);
    const QString appSheet = qApp->styleSheet();

    ThemedMessageBox box;
    box.setDarkTheme(dialogDark);
    auto* checkBox = new QCheckBox(QStringLiteral("Don't show this again"), &box);
    box.setCheckBox(checkBox);
    box.ensurePolished();

    CHECK(box.isDarkTheme() == dialogDark);
    CHECK(isDarkColor(box.palette().color(QPalette::Window)) == dialogDark);
    CHECK(isDarkColor(box.palette().color(QPalette::WindowText)) != dialogDark);
    if (dialogDark == appDark)
        CHECK(box.styleSheet().isEmpty());
    else
        CHECK(box.styleSheet() == ApplicationTheme::styleSheet(dialogDark));
    // The application theme itself is never touched.
    CHECK(ApplicationTheme::isDark() == appDark);
    CHECK(qApp->styleSheet() == appSheet);
    CHECK(isDarkColor(QApplication::palette().color(QPalette::Window)) == appDark);

    // Re-theming the same box back to the app theme drops the override.
    box.setDarkTheme(appDark);
    CHECK(box.styleSheet().isEmpty());
    CHECK(isDarkColor(box.palette().color(QPalette::Window)) == appDark);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    testResolutionMatrix();
    testParentStylesheetDoesNotLeakIntoTheTitleBar();
    testExplicitThemeIsConsistent(QStringLiteral("light"), true);
    testExplicitThemeIsConsistent(QStringLiteral("dark"), false);
    testExplicitThemeIsConsistent(QStringLiteral("light"), false);
    testExplicitThemeIsConsistent(QStringLiteral("dark"), true);

    if (g_failures == 0)
        std::printf("TerminalDialogThemeTest: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
