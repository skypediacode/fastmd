#include "GitTools.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace GitTools {

namespace {

QString gitExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("git.exe"));
}

QString gitExecPath(const QString& git)
{
    QProcess probe;
    probe.start(git, { QStringLiteral("--exec-path") });
    if (!probe.waitForStarted(500)
        || !probe.waitForFinished(1000)
        || probe.exitStatus() != QProcess::NormalExit
        || probe.exitCode() != 0) {
        return QString();
    }
    return QString::fromLocal8Bit(probe.readAllStandardOutput()).trimmed();
}

QString executableFromGitExecPath(const QString& git, const QString& name)
{
    const QString directory = gitExecPath(git);
    if (directory.isEmpty())
        return QString();
    const QDir execPath(directory);
    for (const QString& candidate : { name, name + QStringLiteral(".exe") }) {
        const QString path = execPath.filePath(candidate);
        if (QFileInfo(path).isFile())
            return path;
    }
    return QString();
}

QString gitToolExecutable(const QString& git, const QString& tool)
{
    // Prefer the standard Git for Windows `cmd` launchers on PATH. A custom
    // Git installation may expose only git.exe, in which case its configured
    // exec path is the authoritative fallback.
    const QString fromPath = QStandardPaths::findExecutable(tool + QStringLiteral(".exe"));
    return fromPath.isEmpty() ? executableFromGitExecPath(git, tool) : fromPath;
}

} // namespace

LaunchResult openChanges(const QString& workingDirectory)
{
    const QString git = gitExecutable();
    if (git.isEmpty())
        return LaunchResult::GitNotInstalled;
    if (gitToolExecutable(git, QStringLiteral("git-gui")).isEmpty())
        return LaunchResult::ToolUnavailable;

    return QProcess::startDetached(git, { QStringLiteral("gui") }, workingDirectory)
        ? LaunchResult::Launched : LaunchResult::ToolUnavailable;
}

LaunchResult openHistory(const QString& workingDirectory)
{
    const QString git = gitExecutable();
    if (git.isEmpty())
        return LaunchResult::GitNotInstalled;

    const QString gitk = gitToolExecutable(git, QStringLiteral("gitk"));
    if (gitk.isEmpty()
        || !QProcess::startDetached(gitk, {}, workingDirectory)) {
        return LaunchResult::ToolUnavailable;
    }
    return LaunchResult::Launched;
}

} // namespace GitTools
