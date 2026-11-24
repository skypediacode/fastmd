#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class CommandRegistry;

// The Command Palette's "Settings: ..." commands, and the Preferences controls
// they lead to.
//
// A command only opens Preferences on the control's tab and focuses it; it
// never changes a value, so the dialog's own OK/Cancel stays the one place a
// setting is applied. PreferencesDialog::targetWidgets() maps every Id to its
// controls, and PreferencesNavigationTest fails when one no longer resolves.
// Qt Widgets-free, so the list is testable without a dialog.
namespace PreferencesTargets {

enum class Id {
    RestoreSession,
    CloseAppOnLastTab,
    ConfirmCloseAllTerminals,
    HomeFolder,
    DefaultSaveFolder,
    OpenPdfExternally,
    OpenHtmlExternally,
    OpenImagesExternally,
    ShowExportSuccessDialog,
    HighlightCurrentLine,
    RenderSingleLineBreaks,
    DefaultEditorMode,
    AutoSave,
    AutoSaveDelay,
    CrashRecovery,
    // Reached through "Spell Check: Open Settings" and the Spell Check menu,
    // which already exist, so it has no "Settings:" command of its own.
    SpellCheck,
    EditorFont,
    PreviewFont,
    PrintFont,
    LineSpacing,
    ParagraphSpacing,
    ListSpacing,
    FileAssociations,
    Count
};

struct Spec {
    Id id;
    QString title;
};

QString category();
// The registered commands, in palette order. Every Id except SpellCheck.
const QVector<Spec>& all();

void registerAll(CommandRegistry* registry,
                 std::function<void(Id)> open,
                 const std::function<QStringList(const QString&)>& keywords = {});

} // namespace PreferencesTargets
