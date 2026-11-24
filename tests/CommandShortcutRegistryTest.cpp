#include "CommandShortcutRegistry.h"

#include <QGuiApplication>
#include <QSet>
#include <cassert>
#include <iostream>

int main(int argc, char* argv[])
{
    // QGuiApplication is needed for QKeySequence platform key resolution (e.g. StandardKey)
    QGuiApplication app(argc, argv);

    const auto& defs = CommandShortcutRegistry::allDefinitions();
    assert(!defs.isEmpty());

    QSet<QString> seenIds;
    QSet<int> seenCommandIds;

    for (const auto& def : defs) {
        // ID string must be non-empty and unique
        const QString idStr = QString::fromLatin1(def.idString);
        assert(!idStr.isEmpty());
        assert(!seenIds.contains(idStr));
        seenIds.insert(idStr);

        // CommandId must be unique
        const int cId = static_cast<int>(def.commandId);
        assert(!seenCommandIds.contains(cId));
        seenCommandIds.insert(cId);

        // Category and Label must be non-empty
        assert(def.category != nullptr && strlen(def.category) > 0);
        assert(def.label != nullptr && strlen(def.label) > 0);

        // Lookup by enum and by string must return this definition
        const auto* byId = CommandShortcutRegistry::find(def.commandId);
        assert(byId == &def);

        const auto* byStr = CommandShortcutRegistry::find(QStringView(idStr));
        assert(byStr == &def);

        // Default shortcut must not be empty. Export HTML is the one deliberate
        // exception: Ctrl+Shift+H belongs to Replace in Files.
        assert(!def.defaultShortcut.isEmpty() || def.commandId == CommandId::FileExportHtml);
    }

    // These use a real check rather than assert(): a Release build defines
    // NDEBUG, which would compile the assertions away and pass vacuously.
    bool searchShortcutsOk = true;
    const auto require = [&searchShortcutsOk](bool condition, const char* what) {
        if (!condition) {
            std::cerr << "FAIL: " << what << std::endl;
            searchShortcutsOk = false;
        }
    };
    require(CommandShortcutRegistry::defaultShortcut(CommandId::EditFindInFiles)
                == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), "Find in Files is Ctrl+Shift+F");
    require(CommandShortcutRegistry::defaultShortcut(CommandId::EditReplaceInFiles)
                == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), "Replace in Files is Ctrl+Shift+H");
    require(CommandShortcutRegistry::defaultShortcut(CommandId::FileExportHtml).isEmpty(),
            "Export HTML no longer owns Ctrl+Shift+H");
    require(CommandShortcutRegistry::defaultShortcuts(CommandId::FileExportHtml).isEmpty(),
            "Export HTML has no alias either");
    // Qt delivers an ambiguous shortcut to nobody, so neither chord may be
    // shared with any other command, primary or alias.
    for (const CommandId id : {CommandId::EditFindInFiles, CommandId::EditReplaceInFiles}) {
        const QKeySequence chord = CommandShortcutRegistry::defaultShortcut(id);
        for (const auto& def : defs) {
            if (def.commandId == id)
                continue;
            require(def.defaultShortcut != chord && !def.aliases.contains(chord),
                    "Find/Replace in Files chord is not shared");
        }
    }
    const CommandId headings[] = {
        CommandId::InsertHeading1, CommandId::InsertHeading2, CommandId::InsertHeading3,
        CommandId::InsertHeading4, CommandId::InsertHeading5, CommandId::InsertHeading6,
    };
    for (int i = 0; i < 6; ++i) {
        const auto id = headings[i];
        const QKeySequence chord(Qt::CTRL | (Qt::Key_1 + i));
        require(CommandShortcutRegistry::defaultShortcut(id) == chord, "Heading shortcut matches its level");
        for (const auto& def : defs) {
            if (def.commandId != id)
                require(!CommandShortcutRegistry::defaultShortcuts(def.commandId).contains(chord),
                        "Heading chord is not shared with another command or alias");
        }
#ifdef Q_OS_WIN
        require(CommandShortcutRegistry::matchesNativeAccelerator(id, true, false, false, '1' + i),
                "Heading native accelerator matches its level");
        require(!CommandShortcutRegistry::matchesNativeAccelerator(id, true, true, false, '1' + i),
                "Heading native accelerator rejects Shift");
#endif
    }
    if (!searchShortcutsOk)
        return 1;

    // Specific key sequence assertions
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::FileSave) == QKeySequence::Save);
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::FileOpen) == QKeySequence::Open);
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::FileNew) == QKeySequence::New);
    assert(CommandShortcutRegistry::defaultShortcuts(CommandId::FileNew).size() == 2);
    assert(CommandShortcutRegistry::defaultShortcuts(CommandId::FileNew).contains(QKeySequence(Qt::CTRL | Qt::Key_T)));

    assert(CommandShortcutRegistry::defaultShortcut(CommandId::EditMoveLineUp) == QKeySequence(Qt::ALT | Qt::Key_Up));
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::EditMoveLineDown) == QKeySequence(Qt::ALT | Qt::Key_Down));
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::InsertBulletList) == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_8));
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::InsertNumberList) == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_7));

    assert(CommandShortcutRegistry::defaultShortcut(CommandId::FormatAutoFormatDocument) == QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_F));
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::FileRevealInExplorer) == QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_R));

    assert(CommandShortcutRegistry::defaultShortcut(CommandId::ViewRefresh) == QKeySequence(Qt::Key_F5));
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::ViewTogglePreview) == QKeySequence(Qt::Key_F9));
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::ViewReadingMode) == QKeySequence(Qt::Key_F11));
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::ViewMindMap) == QKeySequence(Qt::Key_F10));
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::ViewCommandPalette) == QKeySequence(Qt::Key_F1));
    assert(CommandShortcutRegistry::defaultShortcut(CommandId::ViewFocusFileSearch) == QKeySequence(Qt::SHIFT | Qt::Key_F1));

    assert(CommandShortcutRegistry::defaultShortcut(CommandId::InsertPageBreak) == QKeySequence(Qt::CTRL | Qt::Key_Return));
    assert(CommandShortcutRegistry::defaultShortcuts(CommandId::InsertPageBreak).contains(QKeySequence(Qt::CTRL | Qt::Key_Enter)));

#ifdef Q_OS_WIN
    // Test native accelerator matching
    // Ctrl+S for Save
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::FileSave, true, false, false, 'S'));
    assert(!CommandShortcutRegistry::matchesNativeAccelerator(CommandId::FileSave, true, true, false, 'S')); // Ctrl+Shift+S is SaveAll
    assert(!CommandShortcutRegistry::matchesNativeAccelerator(CommandId::FileSave, true, false, true, 'S')); // Ctrl+Alt+S is SaveAs

    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::FileSaveAs, true, false, true, 'S'));
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::FileSaveAll, true, true, false, 'S'));
    assert(!CommandShortcutRegistry::matchesNativeAccelerator(CommandId::FileSaveAs, true, true, false, 'S'));
    assert(!CommandShortcutRegistry::matchesNativeAccelerator(CommandId::FileSaveAll, true, false, true, 'S'));

    // F9 for TogglePreview
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::ViewTogglePreview, false, false, false, VK_F9));
    assert(!CommandShortcutRegistry::matchesNativeAccelerator(CommandId::ViewTogglePreview, true, false, false, VK_F9));

    // Shift+Alt+F for AutoFormat
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::FormatAutoFormatDocument, false, true, true, 'F'));
    assert(!CommandShortcutRegistry::matchesNativeAccelerator(CommandId::FormatAutoFormatDocument, true, true, true, 'F'));

    // Alt+Left/Right for Back/Forward
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::EditBack, false, false, true, VK_LEFT));
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::EditForward, false, false, true, VK_RIGHT));

    // Zoom in: Ctrl+=, Ctrl+Plus (OEM_PLUS and VK_ADD)
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::ViewZoomIn, true, false, false, VK_OEM_PLUS));
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::ViewZoomIn, true, false, false, VK_ADD));
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::ViewZoomOut, true, false, false, VK_OEM_MINUS));
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::ViewZoomOut, true, false, false, VK_SUBTRACT));
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::ViewResetZoom, true, false, false, '0'));

    // Preferences: Ctrl+,
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::FilePreferences, true, false, false, VK_OEM_COMMA));

    // PreviewRefresh: F5 (native accelerator match used by PreviewWidget's
    // WebView2 forwarding table).
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::PreviewRefresh, false, false, false, VK_F5));

    // ViewRefresh: F5 (the canonical, context-aware Refresh command -- see
    // MainWindow::refreshActiveContext()).
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::ViewRefresh, false, false, false, VK_F5));

    // SelectAll (read-only viewer): Ctrl+A
    assert(CommandShortcutRegistry::matchesNativeAccelerator(CommandId::EditSelectAll, true, false, false, 'A'));
#endif

    std::cout << "CommandShortcutRegistryTest passed (" << defs.size() << " commands verified)." << std::endl;
    return 0;
}
