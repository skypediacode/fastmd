#include "PreferencesTargets.h"

#include "CommandRegistry.h"

#include <QCoreApplication>

namespace PreferencesTargets {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("PreferencesTargets", text);
}

QString commandId(const QString& title)
{
    QString id = category() + QLatin1Char('.') + title;
    id = id.toLower();
    id.remove(QStringLiteral(" /"));
    id.replace(QLatin1Char(' '), QLatin1Char('-'));
    return id;
}

} // namespace

QString category()
{
    return tr("Settings");
}

const QVector<Spec>& all()
{
    static const QVector<Spec> specs = {
        { Id::RestoreSession,           tr("Restore Session on Startup") },
        { Id::CloseAppOnLastTab,        tr("Close App When Last Tab Closes") },
        { Id::ConfirmCloseAllTerminals, tr("Confirm Before Closing All Terminals") },
        { Id::HomeFolder,               tr("Home Folder") },
        { Id::DefaultSaveFolder,        tr("Default Save Folder") },
        { Id::OpenPdfExternally,        tr("Open PDF Files Externally") },
        { Id::OpenHtmlExternally,       tr("Open HTML Files Externally") },
        { Id::OpenImagesExternally,     tr("Open Image Files Externally") },
        { Id::ShowExportSuccessDialog,  tr("Show Dialog After Export") },
        { Id::HighlightCurrentLine,     tr("Highlight Current Line") },
        { Id::RenderSingleLineBreaks,   tr("Render Single Line Breaks") },
        { Id::DefaultEditorMode,        tr("Default Editor Mode") },
        { Id::AutoSave,                 tr("Auto Save") },
        { Id::AutoSaveDelay,            tr("Auto Save Delay") },
        { Id::CrashRecovery,            tr("Crash Recovery") },
        { Id::EditorFont,               tr("Editor Font") },
        { Id::PreviewFont,              tr("Preview Font") },
        { Id::PrintFont,                tr("Print / PDF Font") },
        { Id::LineSpacing,              tr("Line Spacing") },
        { Id::ParagraphSpacing,         tr("Paragraph Spacing") },
        { Id::ListSpacing,              tr("List Spacing") },
        { Id::FileAssociations,         tr("File Associations") },
    };
    return specs;
}

void registerAll(CommandRegistry* registry,
                 std::function<void(Id)> open,
                 const std::function<QStringList(const QString&)>& keywords)
{
    if (!registry || !open)
        return;
    for (const Spec& spec : all()) {
        Command command;
        command.id = commandId(spec.title);
        command.title = spec.title;
        command.category = category();
        if (keywords)
            command.keywords = keywords(spec.title);
        const Id id = spec.id;
        command.run = [open, id]() { open(id); };
        registry->registerCommand(std::move(command));
    }
}

} // namespace PreferencesTargets
