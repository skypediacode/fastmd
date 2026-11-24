#include "TerminalProfiles.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>

namespace {

// Material Icons codepoints, each verified to render in the bundled
// MaterialIcons-Regular.ttf -- an unmapped codepoint silently paints nothing.
constexpr ushort kIconPowerShell = 0xEB8E; // terminal (>_)
constexpr ushort kIconPrompt     = 0xEB8E; // terminal (>_)
constexpr ushort kIconLinux      = 0xE31E; // laptop
constexpr ushort kIconBash       = 0xE86F; // code (<>)

QString environmentPath(const char* name)
{
    return QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
}

bool executableExists(const QString& path)
{
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    return info.exists() && info.isFile();
}

QString systemRoot()
{
    QString root = environmentPath("SystemRoot");
    if (root.isEmpty())
        root = QStringLiteral("C:/Windows");
    return QDir::fromNativeSeparators(root);
}

// Git for Windows records its location in the registry; checking that as well
// as the two Program Files locations covers the non-default install prefixes
// without resorting to a PATH scan that could pick up an unrelated bash.
QString gitBashPath()
{
    QStringList candidates{
        QDir::fromNativeSeparators(environmentPath("ProgramFiles")) + QStringLiteral("/Git/bin/bash.exe"),
        QDir::fromNativeSeparators(environmentPath("ProgramFiles(x86)")) + QStringLiteral("/Git/bin/bash.exe"),
    };

    for (const char* key : { "HKEY_LOCAL_MACHINE\\SOFTWARE\\GitForWindows",
                             "HKEY_CURRENT_USER\\SOFTWARE\\GitForWindows" }) {
        QSettings registry(QString::fromLatin1(key), QSettings::NativeFormat);
        const QString installPath = registry.value(QStringLiteral("InstallPath")).toString();
        if (!installPath.isEmpty()) {
            candidates.append(QDir::fromNativeSeparators(installPath)
                              + QStringLiteral("/bin/bash.exe"));
        }
    }

    for (const QString& candidate : std::as_const(candidates)) {
        if (executableExists(candidate))
            return candidate;
    }
    return QString();
}

QString wslPath()
{
    const QString path = systemRoot() + QStringLiteral("/System32/wsl.exe");
    return executableExists(path) ? path : QString();
}

QStringList wslDistributions()
{
    // Inbox and Store WSL distributions register their display name here.
    // Registry enumeration avoids launching `wsl --list`, which can take
    // seconds when its service is cold or unhealthy.
    QSettings registry(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Lxss"),
        QSettings::NativeFormat);
    QStringList distributions;
    for (const QString& key : registry.childGroups()) {
        registry.beginGroup(key);
        const QString name = registry.value(QStringLiteral("DistributionName")).toString().trimmed();
        registry.endGroup();
        if (name.isEmpty() || name.startsWith(QStringLiteral("docker-desktop"),
                                               Qt::CaseInsensitive))
            continue;
        if (!distributions.contains(name))
            distributions.append(name);
    }
    return distributions;
}

QList<TerminalProfile> detect()
{
    QList<TerminalProfile> profiles;

    // PowerShell 7 first: it is the modern default where it exists.
    QString pwsh = QDir::fromNativeSeparators(environmentPath("ProgramFiles"))
                   + QStringLiteral("/PowerShell/7/pwsh.exe");
    if (!executableExists(pwsh))
        pwsh = QStandardPaths::findExecutable(QStringLiteral("pwsh"));
    if (executableExists(pwsh)) {
        profiles.append({ QStringLiteral("pwsh"), QStringLiteral("PowerShell"),
                          pwsh, TerminalProfiles::powerShellArguments(), kIconPowerShell });
    }

    const QString windowsPowerShell = systemRoot()
        + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe");
    if (executableExists(windowsPowerShell)) {
        profiles.append({ QStringLiteral("powershell"), QStringLiteral("Windows PowerShell"),
                          windowsPowerShell, TerminalProfiles::powerShellArguments(),
                          kIconPowerShell });
    }

    QString cmd = QDir::fromNativeSeparators(environmentPath("COMSPEC"));
    if (!executableExists(cmd))
        cmd = systemRoot() + QStringLiteral("/System32/cmd.exe");
    if (executableExists(cmd)) {
        profiles.append({ QStringLiteral("cmd"), QStringLiteral("Command Prompt"), cmd,
                          TerminalProfiles::cmdArguments(environmentPath("PROMPT")),
                          kIconPrompt });
    }

    if (const QString bash = gitBashPath(); !bash.isEmpty()) {
        profiles.append({ QStringLiteral("gitbash"), QStringLiteral("Git Bash"), bash,
                          { QStringLiteral("--login"), QStringLiteral("-i") }, kIconBash });
    }

    if (const QString wsl = wslPath(); !wsl.isEmpty()) {
        for (const QString& distribution : wslDistributions()) {
            TerminalProfile profile;
            profile.id = QStringLiteral("wsl:") + distribution;
            profile.name = distribution + QStringLiteral(" (WSL)");
            profile.program = wsl;
            profile.arguments = { QStringLiteral("-d"), distribution };
            profile.iconCode = kIconLinux;
            profiles.append(profile);
        }
    }

    return profiles;
}

} // namespace

namespace TerminalProfiles {

QList<TerminalProfile> available()
{
    static const QList<TerminalProfile> profiles = detect();
    return profiles;
}

TerminalProfile defaultProfile(const QList<TerminalProfile>& profiles)
{
    // Preference order, independent of detection order.
    for (const char* id : { "pwsh", "powershell", "cmd" }) {
        const TerminalProfile match = findById(profiles, QString::fromLatin1(id));
        if (match.isValid())
            return match;
    }
    return profiles.isEmpty() ? TerminalProfile{} : profiles.first();
}

TerminalProfile findById(const QList<TerminalProfile>& profiles, const QString& id)
{
    if (id.isEmpty())
        return {};
    for (const TerminalProfile& profile : profiles) {
        if (profile.id == id)
            return profile;
    }
    return {};
}

TerminalProfile resolvePreferred(const QList<TerminalProfile>& profiles,
                                 const QString& preferredId)
{
    const TerminalProfile preferred = findById(profiles, preferredId);
    if (preferred.isValid())
        return preferred;
    // The persisted shell was uninstalled since it was chosen: fall back rather
    // than failing to open a terminal at all.
    return defaultProfile(profiles);
}

QStringList powerShellArguments()
{
    // Runs after the user's profile, so a custom prompt (oh-my-posh, starship)
    // is the one wrapped. The original prompt is called first, while $? still
    // describes the user's last command. No double quotes: the script has to
    // survive Windows command-line quoting unchanged.
    static const QString script = QStringLiteral(
        "try{$global:__FastMDPrompt=$function:prompt;"
        "if(-not $global:__FastMDPrompt){$global:__FastMDPrompt={'PS '+$PWD+'> '}};"
        "function global:prompt{$p=& $global:__FastMDPrompt;"
        "$l=$executionContext.SessionState.Path.CurrentLocation;"
        "if($l.Provider.Name -ne 'FileSystem'){return $p};"
        "[string][char]27+']9;9;'+[char]34+$l.ProviderPath+[char]34+[char]27+'\\'+$p}}catch{}");
    return { QStringLiteral("-NoLogo"), QStringLiteral("-NoExit"),
             QStringLiteral("-Command"), script };
}

QStringList cmdArguments(const QString& inheritedPrompt)
{
    const QString prompt = inheritedPrompt.isEmpty() ? QStringLiteral("$P$G") : inheritedPrompt;
    // cmd strips only the outer quotes of a /k command; anything it would
    // re-interpret, and a trailing backslash that quoting would double, means
    // the user's prompt is left alone instead.
    for (const QChar c : prompt) {
        if (c.unicode() < 0x20 || QStringLiteral("\"%&<>()@^|!").contains(c))
            return {};
    }
    if (prompt.endsWith(QLatin1Char('\\')))
        return {};
    return { QStringLiteral("/k"), QStringLiteral("prompt $e]9;9;$P$e\\") + prompt };
}

} // namespace TerminalProfiles
