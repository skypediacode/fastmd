#include "TerminalCommands.h"

#include "CommandRegistry.h"
#include "TerminalView.h"

#include <QCoreApplication>

namespace TerminalCommands {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("TerminalCommands", text);
}

QString commandId(const QString& title)
{
    QString id = category() + QLatin1Char('.') + title;
    id = id.toLower();
    while (id.endsWith(QLatin1Char('.')))
        id.chop(1);
    id.replace(QLatin1Char(' '), QLatin1Char('-'));
    return id;
}

} // namespace

QString category()
{
    return tr("Terminal");
}

const QVector<Spec>& all()
{
    static const QVector<Spec> specs = {
        { Id::NewTerminal,            tr("New Terminal") },
        { Id::NewTerminalWithProfile, tr("New Terminal With Profile...") },
        { Id::SelectDefaultProfile,   tr("Select Default Profile...") },
        { Id::CloseTerminal,          tr("Close Terminal") },
        { Id::NextTerminal,           tr("Next Terminal") },
        { Id::PreviousTerminal,       tr("Previous Terminal") },
        { Id::FocusTerminal,          tr("Focus Terminal") },
        { Id::Copy,                   tr("Copy") },
        { Id::Paste,                  tr("Paste") },
        { Id::SelectAll,              tr("Select All") },
        { Id::Clear,                  tr("Clear") },
        { Id::IncreaseFontSize,       tr("Increase Font Size") },
        { Id::DecreaseFontSize,       tr("Decrease Font Size") },
        { Id::ResetFontSize,          tr("Reset Font Size") },
        { Id::UseLightTheme,          tr("Use Light Theme") },
        { Id::UseDarkTheme,           tr("Use Dark Theme") },
        { Id::FollowAppTheme,         tr("Follow App Theme") },
    };
    return specs;
}

bool isEnabled(Id id, const State& state)
{
    using Mode = TerminalPanel::ThemeMode;
    switch (id) {
    // These open the Terminal themselves when it is not on screen yet.
    case Id::NewTerminal:
    case Id::NewTerminalWithProfile:
    case Id::SelectDefaultProfile:
    case Id::FocusTerminal:
        return true;
    case Id::CloseTerminal:
    case Id::Copy:
    case Id::Paste:
    case Id::SelectAll:
    case Id::Clear:
        return state.panelExists && state.hasActiveSession;
    case Id::NextTerminal:
    case Id::PreviousTerminal:
        return state.panelExists && state.sessionCount > 1;
    case Id::IncreaseFontSize:
        return state.panelExists && state.fontSize < TerminalView::MaximumFontSize;
    case Id::DecreaseFontSize:
        return state.panelExists && state.fontSize > TerminalView::MinimumFontSize;
    case Id::ResetFontSize:
        return state.panelExists && state.fontSize != TerminalView::DefaultFontSize;
    case Id::UseLightTheme:
        return state.panelExists && state.themeMode != Mode::Light;
    case Id::UseDarkTheme:
        return state.panelExists && state.themeMode != Mode::Dark;
    case Id::FollowAppTheme:
        return state.panelExists && state.themeMode != Mode::FollowApp;
    }
    return false;
}

void registerAll(CommandRegistry* registry,
                 std::function<State()> state,
                 std::function<void(Id)> run,
                 const std::function<QStringList(const QString&)>& keywords)
{
    if (!registry || !state || !run)
        return;
    for (const Spec& spec : all()) {
        Command command;
        command.id = commandId(spec.title);
        command.title = spec.title;
        command.category = category();
        if (keywords)
            command.keywords = keywords(spec.title);
        const Id id = spec.id;
        command.run = [run, id]() { run(id); };
        command.enabled = [state, id]() { return isEnabled(id, state()); };
        registry->registerCommand(std::move(command));
    }
}

} // namespace TerminalCommands
