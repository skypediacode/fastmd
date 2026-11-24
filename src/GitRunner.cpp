#include "GitRunner.h"

#include "AppShutdown.h"

#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>

namespace {

constexpr int kStartTimeoutMs   = 5000;
constexpr int kLocalTimeoutMs   = 15000;
constexpr int kNetworkTimeoutMs = 120000;
constexpr int kPollMs           = 100;

// The single most important line in this file. Without it a `git push` that
// wants a username blocks on a console FastMD never gave it, and the window
// appears frozen until the timeout expires. Git Credential Manager is a GUI
// helper and is unaffected -- it still shows its own window.
QProcessEnvironment gitEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    // Keeps read-only commands from taking index.lock, so the Git window
    // never fights a `git` the user is running in a terminal.
    env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    env.insert(QStringLiteral("PAGER"), QStringLiteral("cat"));
    return env;
}

QStringList commonArgs()
{
    return { QStringLiteral("--no-pager"),
             QStringLiteral("-c"), QStringLiteral("color.ui=false"),
             QStringLiteral("-c"), QStringLiteral("core.quotepath=false") };
}

bool waitBounded(QProcess& process, int finishMs)
{
    if (!process.waitForStarted(kStartTimeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        return false;
    }
    int remaining = finishMs;
    while (remaining > 0) {
        if (process.waitForFinished(qMin(kPollMs, remaining)))
            return true;
        if (AppShutdown::started())
            break;
        remaining -= kPollMs;
    }
    process.kill();
    process.waitForFinished(2000);
    return false;
}

GitRunner::Result finish(QProcess& process, bool completed)
{
    GitRunner::Result result;
    result.out = process.readAllStandardOutput();
    result.err = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (!completed) {
        result.exitCode = -1;
        if (result.err.isEmpty())
            result.err = QStringLiteral("The git command did not finish in time.");
        return result;
    }
    result.exitCode = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    if (process.exitStatus() != QProcess::NormalExit && result.err.isEmpty())
        result.err = QStringLiteral("The git command was terminated.");
    return result;
}

}

namespace GitRunner {

Result run(const QString& repoRoot, const QStringList& args,
           const QByteArray& stdinData, Kind kind)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setProcessEnvironment(gitEnvironment());
    if (!repoRoot.isEmpty())
        process.setWorkingDirectory(repoRoot);
    process.start(QStringLiteral("git"), commonArgs() + args);

    if (!process.waitForStarted(kStartTimeoutMs)) {
        Result failed;
        failed.err = QStringLiteral("git could not be started. Is it installed and on PATH?");
        process.kill();
        process.waitForFinished(2000);
        return failed;
    }

    // Always close the write channel: a command reading stdin (commit -F -)
    // otherwise waits forever for an EOF that never arrives.
    if (!stdinData.isEmpty())
        process.write(stdinData);
    process.closeWriteChannel();

    const int timeout = kind == Kind::Network ? kNetworkTimeoutMs : kLocalTimeoutMs;
    int remaining = timeout;
    bool completed = false;
    while (remaining > 0) {
        if (process.waitForFinished(qMin(kPollMs, remaining))) {
            completed = true;
            break;
        }
        if (AppShutdown::started())
            break;
        remaining -= kPollMs;
    }
    if (!completed) {
        process.kill();
        process.waitForFinished(2000);
    }
    return finish(process, completed);
}

void CancelHandle::publish(QProcess* process)
{
    QMutexLocker locker(&m_mutex);
    m_process = process;
    // Cancellation can race the worker's startup. Remember an early request
    // and apply it as soon as the process is published; otherwise a panel
    // destroyed immediately after queueing a network task could miss cancel
    // and wait for the full network timeout.
    if (m_process && m_cancelled)
        m_process->kill();
}

void CancelHandle::reset()
{
    QMutexLocker locker(&m_mutex);
    Q_ASSERT(!m_process);
    m_cancelled = false;
}

void CancelHandle::cancel()
{
    QMutexLocker locker(&m_mutex);
    m_cancelled = true;
    if (m_process)
        m_process->kill();
}

Result runCancellable(const QString& repoRoot, const QStringList& args,
                      CancelHandle* handle)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setProcessEnvironment(gitEnvironment());
    if (!repoRoot.isEmpty())
        process.setWorkingDirectory(repoRoot);
    process.start(QStringLiteral("git"), commonArgs() + args);

    if (!process.waitForStarted(kStartTimeoutMs)) {
        Result failed;
        failed.err = QStringLiteral("git could not be started. Is it installed and on PATH?");
        process.kill();
        process.waitForFinished(2000);
        return failed;
    }
    process.closeWriteChannel();

    // Declared after `process`, so it runs first on the way out: the handle is
    // cleared while the QProcess is still alive, closing the window in which a
    // canceller could act on a pointer to a destroyed object.
    struct Unpublish {
        CancelHandle* handle;
        ~Unpublish() { if (handle) handle->publish(nullptr); }
    } unpublish { handle };

    if (handle)
        handle->publish(&process);

    const bool completed = waitBounded(process, kNetworkTimeoutMs);
    return finish(process, completed);
}

bool isAvailable()
{
    // Resolved once per process. A machine that installs git while FastMD is
    // running is not worth re-probing on every refresh.
    static const bool available = [] {
        QProcess process;
        process.setProcessEnvironment(gitEnvironment());
        process.start(QStringLiteral("git"), { QStringLiteral("--version") });
        if (!process.waitForStarted(kStartTimeoutMs)) {
            process.kill();
            process.waitForFinished(2000);
            return false;
        }
        process.closeWriteChannel();
        if (!process.waitForFinished(kLocalTimeoutMs)) {
            process.kill();
            process.waitForFinished(2000);
            return false;
        }
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }();
    return available;
}

}
