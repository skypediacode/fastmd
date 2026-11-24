// Focused regression tests for the integrated terminal's pure profile layer.
// Deliberately links only Qt6::Core + src/TerminalProfiles.cpp -- no widgets,
// no ConPTY, no shell actually launched.

#include "TerminalProfiles.h"

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

#define CHECK(cond) check((cond), #cond, __LINE__)

TerminalProfile makeProfile(const QString& id, const QString& name)
{
    TerminalProfile profile;
    profile.id = id;
    profile.name = name;
    profile.program = QStringLiteral("C:/Windows/System32/") + id + QStringLiteral(".exe");
    return profile;
}

void testDefaultPrefersPwshThenPowerShellThenCmd()
{
    const QList<TerminalProfile> all{
        makeProfile(QStringLiteral("cmd"), QStringLiteral("Command Prompt")),
        makeProfile(QStringLiteral("powershell"), QStringLiteral("Windows PowerShell")),
        makeProfile(QStringLiteral("pwsh"), QStringLiteral("PowerShell")),
    };
    // Preference order must be independent of detection order.
    CHECK(TerminalProfiles::defaultProfile(all).id == QStringLiteral("pwsh"));

    const QList<TerminalProfile> noPwsh{
        makeProfile(QStringLiteral("cmd"), QStringLiteral("Command Prompt")),
        makeProfile(QStringLiteral("powershell"), QStringLiteral("Windows PowerShell")),
    };
    CHECK(TerminalProfiles::defaultProfile(noPwsh).id == QStringLiteral("powershell"));

    const QList<TerminalProfile> cmdOnly{
        makeProfile(QStringLiteral("cmd"), QStringLiteral("Command Prompt")),
    };
    CHECK(TerminalProfiles::defaultProfile(cmdOnly).id == QStringLiteral("cmd"));
}

void testDefaultFallsBackToFirstAndToNothing()
{
    const QList<TerminalProfile> exotic{
        makeProfile(QStringLiteral("wsl:Ubuntu"), QStringLiteral("Ubuntu (WSL)")),
    };
    CHECK(TerminalProfiles::defaultProfile(exotic).id == QStringLiteral("wsl:Ubuntu"));
    CHECK(!TerminalProfiles::defaultProfile({}).isValid());
}

void testResolvePreferredHonoursPersistedChoice()
{
    const QList<TerminalProfile> all{
        makeProfile(QStringLiteral("pwsh"), QStringLiteral("PowerShell")),
        makeProfile(QStringLiteral("cmd"), QStringLiteral("Command Prompt")),
    };
    CHECK(TerminalProfiles::resolvePreferred(all, QStringLiteral("cmd")).id
          == QStringLiteral("cmd"));
}

void testResolvePreferredRecoversFromUninstalledShell()
{
    // The persisted default names a shell that has since been uninstalled: the
    // panel must still open a terminal rather than refusing to start one.
    const QList<TerminalProfile> all{
        makeProfile(QStringLiteral("powershell"), QStringLiteral("Windows PowerShell")),
        makeProfile(QStringLiteral("cmd"), QStringLiteral("Command Prompt")),
    };
    const TerminalProfile resolved =
        TerminalProfiles::resolvePreferred(all, QStringLiteral("wsl:Ubuntu"));
    CHECK(resolved.isValid());
    CHECK(resolved.id == QStringLiteral("powershell"));

    CHECK(TerminalProfiles::resolvePreferred(all, QString()).id
          == QStringLiteral("powershell"));
}

void testFindById()
{
    const QList<TerminalProfile> all{
        makeProfile(QStringLiteral("pwsh"), QStringLiteral("PowerShell")),
    };
    CHECK(TerminalProfiles::findById(all, QStringLiteral("pwsh")).isValid());
    CHECK(!TerminalProfiles::findById(all, QStringLiteral("nope")).isValid());
    CHECK(!TerminalProfiles::findById(all, QString()).isValid());
}

} // namespace

int main()
{
    testDefaultPrefersPwshThenPowerShellThenCmd();
    testDefaultFallsBackToFirstAndToNothing();
    testResolvePreferredHonoursPersistedChoice();
    testResolvePreferredRecoversFromUninstalledShell();
    testFindById();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
