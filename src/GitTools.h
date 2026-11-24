#pragma once

#include <QString>

// Minimal bridge to Git for Windows' own graphical tools. FastMD never reads
// repository state; these functions only locate and launch external programs.
namespace GitTools {

enum class LaunchResult {
    Launched,
    GitNotInstalled,
    ToolUnavailable
};

LaunchResult openChanges(const QString& workingDirectory);
LaunchResult openHistory(const QString& workingDirectory);

} // namespace GitTools
