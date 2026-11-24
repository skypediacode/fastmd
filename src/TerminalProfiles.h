#pragma once

// Shell profile discovery for the integrated terminal.
//
// Deliberately conservative: a small correct list beats a long unreliable one,
// so a profile is only offered when its executable is actually on this machine.
// Nothing here touches Qt Widgets or spawns a shell, which keeps the parsing
// half unit-testable (TerminalProfilesTest) the way GitRepository is.

#include <QString>
#include <QStringList>
#include <QList>

struct TerminalProfile {
    // Stable across launches -- this is what gets persisted as the default.
    QString id;
    QString name;
    QString program;
    QStringList arguments;
    // Material Icons codepoint used by the session list.
    ushort iconCode = 0;

    bool isValid() const { return !id.isEmpty() && !program.isEmpty(); }
};

namespace TerminalProfiles {

// Detected once per process, then cached. Detection is limited to file and
// registry reads, so opening the panel never waits for a helper process.
QList<TerminalProfile> available();

// Ordered preference used when nothing is persisted, or when the persisted
// choice names a shell that has since been uninstalled.
TerminalProfile defaultProfile(const QList<TerminalProfile>& profiles);

TerminalProfile findById(const QList<TerminalProfile>& profiles, const QString& id);

// Resolves the persisted id against what is actually installed, falling back to
// the preference order when it no longer exists.
TerminalProfile resolvePreferred(const QList<TerminalProfile>& profiles,
                                 const QString& preferredId);

// Shell integration: arguments that make the shell announce its directory with
// OSC 9;9 at every prompt, which is where the Terminal resolves relative
// Ctrl+Click links from. PowerShell's location is not its process's current
// directory, so nothing outside the shell can read it. The user's own prompt
// is wrapped, not replaced.
QStringList powerShellArguments();
// `inheritedPrompt` is the PROMPT environment value cmd would otherwise use.
// Returns no arguments -- cmd's own prompt, no cwd reports -- when that value
// cannot pass through cmd's /k command line intact.
QStringList cmdArguments(const QString& inheritedPrompt);

} // namespace TerminalProfiles
