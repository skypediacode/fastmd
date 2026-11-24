#pragma once

#include "TerminalPanel.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class CommandRegistry;

// The Command Palette's "Terminal: ..." commands.
//
// Registration only: every command runs through the `run` callback, which
// MainWindow routes to the existing TerminalPanel handlers, and its enabled
// state is derived from a live State snapshot, so nothing here can drift from
// the panel's own controls. Qt Widgets-free apart from the ThemeMode enum, so
// TerminalCommandsTest can exercise it without a terminal.
namespace TerminalCommands {

enum class Id {
    NewTerminal,
    NewTerminalWithProfile,
    SelectDefaultProfile,
    CloseTerminal,
    NextTerminal,
    PreviousTerminal,
    FocusTerminal,
    Copy,
    Paste,
    SelectAll,
    Clear,
    IncreaseFontSize,
    DecreaseFontSize,
    ResetFontSize,
    UseLightTheme,
    UseDarkTheme,
    FollowAppTheme,
};

// What the predicates need to know. `panelExists` is false until the Terminal
// has been opened in this window; font size and theme live on the panel.
struct State {
    bool panelExists = false;
    bool hasActiveSession = false;
    int sessionCount = 0;
    int fontSize = 0;
    TerminalPanel::ThemeMode themeMode = TerminalPanel::ThemeMode::FollowApp;
};

struct Spec {
    Id id;
    QString title;
};

QString category();
const QVector<Spec>& all();
bool isEnabled(Id id, const State& state);

void registerAll(CommandRegistry* registry,
                 std::function<State()> state,
                 std::function<void(Id)> run,
                 const std::function<QStringList(const QString&)>& keywords = {});

} // namespace TerminalCommands
