// End to end for the Terminal's working-directory reports, which relative
// Ctrl+Click links resolve against. Each installed shell is started through
// the real ConPtySession with the arguments TerminalProfiles gives it, told to
// change into a directory with spaces and non-ASCII characters, and must
// announce it with OSC 9;9 through the pseudoconsole -- PowerShell's location
// is invisible from outside the shell otherwise. The announced directory then
// resolves a relative link exactly as TerminalPanel does.
//
// Launches the user's real shells (profiles included), so it needs a desktop
// session; a shell that is not installed is skipped, not failed.

#include "ConPtySession.h"
#include "TerminalLinks.h"
#include "TerminalProfiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <windows.h>

#include <cstdio>
#include <functional>

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

bool waitFor(const std::function<bool()>& condition, int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    return true;
}

// Every OSC 9;9 payload in the stream, quotes stripped the way terminal.js's
// cwdFromOsc9 does. Either terminator: ConPTY may rewrite ST as BEL.
QStringList reportedDirectories(const QByteArray& output)
{
    static const QByteArray kIntroducer("\x1b]9;9;");
    QStringList directories;
    for (qsizetype at = output.indexOf(kIntroducer); at >= 0;
         at = output.indexOf(kIntroducer, at + 1)) {
        const qsizetype begin = at + kIntroducer.size();
        const qsizetype bel = output.indexOf('\x07', begin);
        const qsizetype st = output.indexOf("\x1b\\", begin);
        const qsizetype end = bel < 0 ? st : st < 0 ? bel : qMin(bel, st);
        if (end < 0)
            break;
        QString path = QString::fromUtf8(output.mid(begin, end - begin));
        if (path.size() >= 2 && path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')))
            path = path.mid(1, path.size() - 2);
        directories.append(path);
    }
    return directories;
}

bool samePath(const QString& a, const QString& b)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(a))
               .compare(QDir::cleanPath(QDir::fromNativeSeparators(b)), Qt::CaseInsensitive)
        == 0;
}

void exerciseShell(const TerminalProfile& profile, const QString& root)
{
    std::printf("shell: %s\n", qPrintable(profile.id));
    const QString start = root + QStringLiteral("/start");
    const QString target = root + QStringLiteral("/Link Test üạ/sub dir");
    QDir().mkpath(start);
    QDir().mkpath(target + QStringLiteral("/toan11"));
    {
        QFile file(target + QStringLiteral("/toan11/bai tap.md"));
        CHECK(file.open(QIODevice::WriteOnly));
        file.write("a\nb\n");
    }
    for (const char* name : { "/README.md", "/toan11/on-tap.md" }) {
        QFile file(target + QString::fromLatin1(name));
        CHECK(file.open(QIODevice::WriteOnly));
    }

    ConPtySession session;
    QByteArray output;
    QObject::connect(&session, &ConPtySession::outputReady,
                     [&output](const QByteArray& data) { output += data; });
    QString error;
    // FastMD.exe is a GUI process with no standard handles. A console parent's
    // would be copied into the shell and carry its output past the
    // pseudoconsole, so start the shell the way FastMD does.
    const DWORD kStdHandles[] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
    HANDLE saved[3];
    for (int i = 0; i < 3; ++i) {
        saved[i] = ::GetStdHandle(kStdHandles[i]);
        ::SetStdHandle(kStdHandles[i], nullptr);
    }
    const bool started = session.start(profile.program, profile.arguments, start, 120, 30, &error);
    for (int i = 0; i < 3; ++i)
        ::SetStdHandle(kStdHandles[i], saved[i]);
    CHECK(started);
    if (!error.isEmpty())
        std::printf("  start error: %s\n", qPrintable(error));

    const auto lastReport = [&output] {
        const QStringList all = reportedDirectories(output);
        return all.isEmpty() ? QString() : all.last();
    };
    const bool announcedStart = waitFor([&] { return samePath(lastReport(), start); }, 30000);
    CHECK(announcedStart);
    if (!announcedStart) {
        std::printf("  last report: %s\n", qPrintable(lastReport()));
        return;
    }

    const QString native = QDir::toNativeSeparators(target);
    const QString cd = profile.id == QLatin1String("cmd")
        ? QStringLiteral("cd /d \"%1\"\r").arg(native)
        : QStringLiteral("Set-Location -LiteralPath '%1'\r").arg(native);
    session.write(cd.toUtf8());
    const bool announcedTarget = waitFor([&] { return samePath(lastReport(), target); }, 20000);
    CHECK(announcedTarget);
    if (!announcedTarget) {
        std::printf("  last report: %s\n", qPrintable(lastReport()));
        return;
    }

    // What TerminalPanel does with a Ctrl+Click once the page has the report.
    const TerminalLinks::Target link =
        TerminalLinks::resolve(QStringLiteral("toan11/bai tap.md:2"), lastReport(), start);
    CHECK(link.kind == TerminalLinks::Target::Kind::File && link.line == 2);
    CHECK(samePath(link.path, target + QStringLiteral("/toan11/bai tap.md")));
    CHECK(TerminalLinks::resolve(QStringLiteral("toan11/"), lastReport(), start).kind
          == TerminalLinks::Target::Kind::Directory);
    // Bare and backslash-relative forms, as PowerShell and cmd print them.
    const TerminalLinks::Target bare =
        TerminalLinks::resolve(QStringLiteral("README.md:12"), lastReport(), start);
    CHECK(bare.kind == TerminalLinks::Target::Kind::File && bare.line == 12);
    CHECK(samePath(bare.path, target + QStringLiteral("/README.md")));
    const TerminalLinks::Target nested =
        TerminalLinks::resolve(QStringLiteral("toan11\\on-tap.md:24:5"), lastReport(), start);
    CHECK(nested.kind == TerminalLinks::Target::Kind::File && nested.line == 24
          && nested.column == 5);
    CHECK(samePath(nested.path, target + QStringLiteral("/toan11/on-tap.md")));
    const TerminalLinks::Target missing =
        TerminalLinks::resolve(QStringLiteral("toan11\\missing.md:24:5"), lastReport(), start);
    CHECK(missing.kind == TerminalLinks::Target::Kind::NotFound);
    CHECK(samePath(missing.path, target + QStringLiteral("/toan11/missing.md")));

    session.write(QByteArrayLiteral("exit\r"));
    waitFor([&session] { return !session.isRunning(); }, 5000);
    session.terminate();
}

void testCmdPromptIsPreservedOrLeftAlone()
{
    CHECK(TerminalProfiles::cmdArguments(QString())
          == QStringList({ QStringLiteral("/k"), QStringLiteral("prompt $e]9;9;$P$e\\$P$G") }));
    CHECK(TerminalProfiles::cmdArguments(QStringLiteral("[$T] $P$G")).last()
          == QStringLiteral("prompt $e]9;9;$P$e\\[$T] $P$G"));
    // Anything cmd would reinterpret on the /k line: keep the user's prompt.
    CHECK(TerminalProfiles::cmdArguments(QStringLiteral("%USERNAME% $P$G")).isEmpty());
    CHECK(TerminalProfiles::cmdArguments(QStringLiteral("a\"b")).isEmpty());
    CHECK(TerminalProfiles::cmdArguments(QStringLiteral("a & b")).isEmpty());
    CHECK(TerminalProfiles::cmdArguments(QStringLiteral("$P\\")).isEmpty());
}

void testPowerShellArgumentsSurviveQuoting()
{
    const QStringList arguments = TerminalProfiles::powerShellArguments();
    CHECK(arguments.size() == 4 && arguments.at(2) == QLatin1String("-Command"));
    // A double quote would be re-escaped by Windows command-line quoting.
    CHECK(!arguments.last().contains(QLatin1Char('"')));
    CHECK(arguments.contains(QStringLiteral("-NoExit")));
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    testCmdPromptIsPreservedOrLeftAlone();
    testPowerShellArgumentsSurviveQuoting();

    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("FAIL: could not create a temporary directory\n");
        return 1;
    }
    int shells = 0;
    for (const TerminalProfile& profile : TerminalProfiles::available()) {
        if (profile.id != QLatin1String("pwsh") && profile.id != QLatin1String("powershell")
            && profile.id != QLatin1String("cmd"))
            continue;
        if (profile.arguments.isEmpty()) {
            std::printf("shell: %s skipped (custom PROMPT left alone)\n", qPrintable(profile.id));
            continue;
        }
        ++shells;
        exerciseShell(profile, QDir(temp.path()).absoluteFilePath(profile.id));
    }
    CHECK(shells > 0);

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
