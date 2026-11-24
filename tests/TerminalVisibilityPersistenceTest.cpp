// Regression coverage for terminal/visible persistence. This uses an INI
// backend, so it is independent of the developer's registry settings and does
// not construct the WebView2-backed terminal UI.

#include "TerminalVisibilityPreferences.h"
#include "TerminalCloseAllPreferences.h"

#include <QTemporaryDir>

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

void testVisibleChoiceSurvivesStartupAndUnrelatedSettingsWrites()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));

    {
        QSettings settings(path, QSettings::IniFormat);
        TerminalVisibilityPreferences::write(settings, true); // user shows Terminal
        settings.sync();
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        CHECK(TerminalVisibilityPreferences::read(settings)); // first restored launch
        // This models startup/session persistence unrelated to the terminal.
        settings.setValue(QStringLiteral("workspace/rootPath"), QStringLiteral("C:/work"));
        settings.sync();
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        CHECK(TerminalVisibilityPreferences::read(settings)); // second restored launch
        TerminalVisibilityPreferences::write(settings, false); // user hides Terminal
        settings.sync();
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        CHECK(!TerminalVisibilityPreferences::read(settings));
        TerminalVisibilityPreferences::write(settings, true); // user shows it again
        settings.sync();
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        CHECK(TerminalVisibilityPreferences::read(settings));
    }
}

void testCloseAllConfirmationIsOptOutAndPersists()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));

    {
        QSettings settings(path, QSettings::IniFormat);
        CHECK(TerminalCloseAllPreferences::confirmationEnabled(settings));
        // Cancelling must not write the opt-out preference.
        settings.sync();
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        CHECK(TerminalCloseAllPreferences::confirmationEnabled(settings));
        // This models checking "Don't show this again" and confirming Close All.
        TerminalCloseAllPreferences::setConfirmationEnabled(settings, false);
        settings.sync();
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        CHECK(!TerminalCloseAllPreferences::confirmationEnabled(settings));
        // Re-enabling confirmation from Preferences uses the same setting.
        TerminalCloseAllPreferences::setConfirmationEnabled(settings, true);
        settings.sync();
    }
    {
        QSettings settings(path, QSettings::IniFormat);
        CHECK(TerminalCloseAllPreferences::confirmationEnabled(settings));
    }
}

void testCloseAllConfirmationOnlyAppliesToMultipleSessions()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("settings.ini"));

    QSettings settings(path, QSettings::IniFormat);
    CHECK(!TerminalCloseAllPreferences::shouldConfirmCloseAll(0, settings));
    CHECK(!TerminalCloseAllPreferences::shouldConfirmCloseAll(1, settings));
    CHECK(TerminalCloseAllPreferences::shouldConfirmCloseAll(2, settings));
    CHECK(TerminalCloseAllPreferences::shouldConfirmCloseAll(3, settings));

    TerminalCloseAllPreferences::setConfirmationEnabled(settings, false);
    CHECK(!TerminalCloseAllPreferences::shouldConfirmCloseAll(2, settings));
    CHECK(!TerminalCloseAllPreferences::shouldConfirmCloseAll(3, settings));
}

} // namespace

int main()
{
    testVisibleChoiceSurvivesStartupAndUnrelatedSettingsWrites();
    testCloseAllConfirmationIsOptOutAndPersists();
    testCloseAllConfirmationOnlyAppliesToMultipleSessions();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
