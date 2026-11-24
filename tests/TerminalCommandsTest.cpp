// Regression coverage for the Command Palette's "Terminal: ..." commands: the
// full set is registered once under the Terminal category, every command is
// greyed out exactly when its TerminalPanel handler could not act, and running
// one dispatches its own id to the caller's handler.

#include "CommandRegistry.h"
#include "TerminalCommands.h"
#include "TerminalView.h"

#include <QCoreApplication>
#include <QSet>

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

using TerminalCommands::Id;
using Mode = TerminalPanel::ThemeMode;

const Command* findByTitle(const CommandRegistry& registry, const QString& title)
{
    for (const Command& command : registry.commands()) {
        if (command.title == title)
            return &command;
    }
    return nullptr;
}

bool enabled(Id id, const TerminalCommands::State& state)
{
    return TerminalCommands::isEnabled(id, state);
}

void testRegistration()
{
    CommandRegistry registry;
    TerminalCommands::State state;
    QList<Id> ran;
    TerminalCommands::registerAll(&registry, [&state]() { return state; },
        [&ran](Id id) { ran.append(id); },
        [](const QString& title) {
            return title == QStringLiteral("Clear")
                ? QStringList{ QStringLiteral("cls") } : QStringList();
        });

    const QStringList expected = {
        QStringLiteral("New Terminal"), QStringLiteral("New Terminal With Profile..."),
        QStringLiteral("Select Default Profile..."), QStringLiteral("Close Terminal"),
        QStringLiteral("Next Terminal"), QStringLiteral("Previous Terminal"),
        QStringLiteral("Focus Terminal"), QStringLiteral("Copy"), QStringLiteral("Paste"),
        QStringLiteral("Select All"), QStringLiteral("Clear"),
        QStringLiteral("Increase Font Size"), QStringLiteral("Decrease Font Size"),
        QStringLiteral("Reset Font Size"), QStringLiteral("Use Light Theme"),
        QStringLiteral("Use Dark Theme"), QStringLiteral("Follow App Theme"),
    };
    CHECK(registry.commands().size() == expected.size());

    QSet<QString> ids;
    for (const Command& command : registry.commands()) {
        CHECK(command.category == QStringLiteral("Terminal"));
        CHECK(command.id.startsWith(QStringLiteral("terminal.")));
        CHECK(!command.id.endsWith(QLatin1Char('.')));
        // The palette is extended without binding anything new.
        CHECK(command.shortcut.isEmpty());
        ids.insert(command.id);
    }
    CHECK(ids.size() == expected.size());
    // Does not collide with the existing View: Terminal / Detach / Attach ids.
    CHECK(!ids.contains(QStringLiteral("view.terminal")));
    CHECK(!ids.contains(QStringLiteral("view.detach-terminal")));
    CHECK(!ids.contains(QStringLiteral("view.attach-terminal")));

    for (const QString& title : expected)
        CHECK(findByTitle(registry, title) != nullptr);

    const Command* clear = findByTitle(registry, QStringLiteral("Clear"));
    CHECK(clear && clear->keywords.contains(QStringLiteral("cls")));

    // Each command reports its own id, so MainWindow's switch cannot be fed
    // a neighbour's.
    for (int i = 0; i < registry.commands().size(); ++i)
        registry.commands().at(i).run();
    CHECK(ran.size() == TerminalCommands::all().size());
    for (int i = 0; i < ran.size() && i < TerminalCommands::all().size(); ++i)
        CHECK(ran.at(i) == TerminalCommands::all().at(i).id);

    // Enabled state is read live, not captured at registration.
    const Command* close = findByTitle(registry, QStringLiteral("Close Terminal"));
    CHECK(close && !close->enabled());
    state.panelExists = true;
    state.hasActiveSession = true;
    state.sessionCount = 1;
    CHECK(close && close->enabled());

    const QVector<CommandMatch> matches = registry.search(QStringLiteral("light theme"));
    CHECK(!matches.isEmpty()
          && matches.first().command->title == QStringLiteral("Use Light Theme"));
}

void testNoTerminal()
{
    // The Terminal has not been opened in this window.
    const TerminalCommands::State none;
    CHECK(enabled(Id::NewTerminal, none));
    CHECK(enabled(Id::NewTerminalWithProfile, none));
    CHECK(enabled(Id::SelectDefaultProfile, none));
    CHECK(enabled(Id::FocusTerminal, none));
    for (Id id : { Id::CloseTerminal, Id::NextTerminal, Id::PreviousTerminal, Id::Copy,
                   Id::Paste, Id::SelectAll, Id::Clear, Id::IncreaseFontSize,
                   Id::DecreaseFontSize, Id::ResetFontSize, Id::UseLightTheme,
                   Id::UseDarkTheme, Id::FollowAppTheme })
        CHECK(!enabled(id, none));
}

void testSessions()
{
    TerminalCommands::State state;
    state.panelExists = true;
    state.fontSize = TerminalView::DefaultFontSize;

    // Panel built, but its last session was closed.
    for (Id id : { Id::CloseTerminal, Id::Copy, Id::Paste, Id::SelectAll, Id::Clear,
                   Id::NextTerminal, Id::PreviousTerminal })
        CHECK(!enabled(id, state));

    state.hasActiveSession = true;
    state.sessionCount = 1;
    for (Id id : { Id::CloseTerminal, Id::Copy, Id::Paste, Id::SelectAll, Id::Clear })
        CHECK(enabled(id, state));
    CHECK(!enabled(Id::NextTerminal, state));
    CHECK(!enabled(Id::PreviousTerminal, state));

    state.sessionCount = 2;
    CHECK(enabled(Id::NextTerminal, state));
    CHECK(enabled(Id::PreviousTerminal, state));
}

void testFontSize()
{
    TerminalCommands::State state;
    state.panelExists = true;

    state.fontSize = TerminalView::DefaultFontSize;
    CHECK(enabled(Id::IncreaseFontSize, state));
    CHECK(enabled(Id::DecreaseFontSize, state));
    CHECK(!enabled(Id::ResetFontSize, state));

    state.fontSize = TerminalView::MaximumFontSize;
    CHECK(!enabled(Id::IncreaseFontSize, state));
    CHECK(enabled(Id::ResetFontSize, state));

    state.fontSize = TerminalView::MinimumFontSize;
    CHECK(!enabled(Id::DecreaseFontSize, state));
    CHECK(enabled(Id::ResetFontSize, state));
}

void testTheme()
{
    TerminalCommands::State state;
    state.panelExists = true;

    // Each mode is one direct command; only the mode already in effect is
    // unavailable, so the other two always apply in one step.
    state.themeMode = Mode::FollowApp;
    CHECK(enabled(Id::UseLightTheme, state));
    CHECK(enabled(Id::UseDarkTheme, state));
    CHECK(!enabled(Id::FollowAppTheme, state));

    state.themeMode = Mode::Light;
    CHECK(!enabled(Id::UseLightTheme, state));
    CHECK(enabled(Id::UseDarkTheme, state));
    CHECK(enabled(Id::FollowAppTheme, state));

    state.themeMode = Mode::Dark;
    CHECK(enabled(Id::UseLightTheme, state));
    CHECK(!enabled(Id::UseDarkTheme, state));
    CHECK(enabled(Id::FollowAppTheme, state));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    testRegistration();
    testNoTerminal();
    testSessions();
    testFontSize();
    testTheme();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
