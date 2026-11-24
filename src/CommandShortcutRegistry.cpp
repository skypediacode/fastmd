#include "CommandShortcutRegistry.h"

#include <unordered_map>

static const QVector<CommandShortcutDefinition>& definitionsTable()
{
    static const QVector<CommandShortcutDefinition> table = {
        // File
        {
            CommandId::FileNew,
            "file.new",
            "File",
            "New",
            QKeySequence(QKeySequence::New),
            { QKeySequence(Qt::CTRL | Qt::Key_T) }
        },
        {
            CommandId::FileNewWindow,
            "file.newWindow",
            "File",
            "New Window",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
            {}
        },
        {
            CommandId::FileOpen,
            "file.open",
            "File",
            "Open...",
            QKeySequence(QKeySequence::Open),
            {}
        },
        {
            CommandId::FileOpenFolder,
            "file.openFolder",
            "File",
            "Open Folder...",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
            {}
        },
        {
            CommandId::FileSave,
            "file.save",
            "File",
            "Save",
            QKeySequence(QKeySequence::Save),
            {}
        },
        {
            CommandId::FileSaveAs,
            "file.saveAs",
            "File",
            "Save As…",
            QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S),
            {}
        },
        {
            CommandId::FileSaveAll,
            "file.saveAll",
            "File",
            "Save All",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
            {}
        },
        {
            CommandId::FileCloseTab,
            "file.closeTab",
            "File",
            "Close Tab",
            QKeySequence(Qt::CTRL | Qt::Key_W),
            {}
        },
        {
            CommandId::FileReopenClosedTab,
            "file.reopenClosedTab",
            "File",
            "Reopen Closed Tab",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),
            {}
        },
        {
            CommandId::FilePreviewBrowser,
            "file.previewBrowser",
            "File",
            "Preview in Browser",
            QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P),
            {}
        },
        {
            CommandId::FileExportPdf,
            "file.exportPdf",
            "File",
            "Export PDF…",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P),
            {}
        },
        {
            CommandId::FileExportDocx,
            "file.exportDocx",
            "File",
            "Export DOCX…",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D),
            {}
        },
        {
            CommandId::FileExportHtml,
            "file.exportHtml",
            "File",
            "Export HTML…",
            // Ctrl+Shift+H moved to Replace in Files, matching the convention
            // every code editor uses; Export HTML stays on the File menu and
            // the Command Palette.
            QKeySequence(),
            {}
        },
        {
            CommandId::FilePrint,
            "file.print",
            "File",
            "Print",
            QKeySequence(Qt::CTRL | Qt::Key_P),
            {}
        },
        {
            CommandId::FilePreferences,
            "file.preferences",
            "File",
            "Preferences...",
            QKeySequence(Qt::CTRL | Qt::Key_Comma),
            {}
        },
        {
            CommandId::FileExit,
            "file.exit",
            "File",
            "Exit",
            QKeySequence(QKeySequence::Quit),
            {}
        },

        // Edit
        {
            CommandId::EditUndo,
            "edit.undo",
            "Edit",
            "Undo",
            QKeySequence(QKeySequence::Undo),
            {}
        },
        {
            CommandId::EditRedo,
            "edit.redo",
            "Edit",
            "Redo",
            QKeySequence(QKeySequence::Redo),
            {}
        },
        {
            CommandId::EditCut,
            "edit.cut",
            "Edit",
            "Cut",
            QKeySequence(QKeySequence::Cut),
            {}
        },
        {
            CommandId::EditCopy,
            "edit.copy",
            "Edit",
            "Copy",
            QKeySequence(QKeySequence::Copy),
            {}
        },
        {
            CommandId::EditPaste,
            "edit.paste",
            "Edit",
            "Paste",
            QKeySequence(QKeySequence::Paste),
            {}
        },
        {
            CommandId::EditDelete,
            "edit.delete",
            "Edit",
            "Delete",
            QKeySequence(QKeySequence::Delete),
            {}
        },
        {
            CommandId::EditSelectAll,
            "edit.selectAll",
            "Edit",
            "Select All",
            QKeySequence(QKeySequence::SelectAll),
            {}
        },
        {
            CommandId::EditBack,
            "edit.back",
            "Edit",
            "Back",
            QKeySequence(Qt::ALT | Qt::Key_Left),
            {}
        },
        {
            CommandId::EditForward,
            "edit.forward",
            "Edit",
            "Forward",
            QKeySequence(Qt::ALT | Qt::Key_Right),
            {}
        },
        {
            CommandId::EditFind,
            "edit.find",
            "Edit",
            "Find…",
            QKeySequence(QKeySequence::Find),
            {}
        },
        {
            CommandId::EditReplace,
            "edit.replace",
            "Edit",
            "Replace…",
            QKeySequence(QKeySequence::Replace),
            {}
        },
        {
            CommandId::EditFindNext,
            "edit.findNext",
            "Edit",
            "Find Next",
            QKeySequence(Qt::Key_F3),
            {}
        },
        {
            CommandId::EditFindPrevious,
            "edit.findPrevious",
            "Edit",
            "Find Previous",
            QKeySequence(Qt::SHIFT | Qt::Key_F3),
            {}
        },
        {
            CommandId::EditGoToLine,
            "edit.goToLine",
            "Edit",
            "Go To Line…",
            QKeySequence(Qt::CTRL | Qt::Key_G),
            {}
        },
        {
            CommandId::EditFindInFiles,
            "edit.findInFiles",
            "Edit",
            "Find in Files…",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F),
            {}
        },
        {
            CommandId::EditReplaceInFiles,
            "edit.replaceInFiles",
            "Edit",
            "Replace in Files…",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H),
            {}
        },
        {
            CommandId::EditDuplicateLine,
            "edit.duplicateLine",
            "Edit",
            "Duplicate Line",
            QKeySequence(Qt::CTRL | Qt::Key_D),
            {}
        },
        {
            CommandId::EditDeleteLine,
            "edit.deleteLine",
            "Edit",
            "Delete Line",
            QKeySequence(Qt::CTRL | Qt::Key_R),
            {}
        },
        {
            CommandId::EditMoveLineUp,
            "edit.moveLineUp",
            "Edit",
            "Move Line Up",
            QKeySequence(Qt::ALT | Qt::Key_Up),
            {}
        },
        {
            CommandId::EditMoveLineDown,
            "edit.moveLineDown",
            "Edit",
            "Move Line Down",
            QKeySequence(Qt::ALT | Qt::Key_Down),
            {}
        },

        // Format
        {
            CommandId::FormatBold,
            "format.bold",
            "Format",
            "Bold",
            QKeySequence(Qt::CTRL | Qt::Key_B),
            {}
        },
        {
            CommandId::FormatItalic,
            "format.italic",
            "Format",
            "Italic",
            QKeySequence(Qt::CTRL | Qt::Key_I),
            {}
        },
        {
            CommandId::FormatStrikethrough,
            "format.strikethrough",
            "Format",
            "Strikethrough",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus),
            {}
        },
        {
            CommandId::FormatHighlight,
            "format.highlight",
            "Format",
            "Highlight",
            QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_H),
            {}
        },
        {
            CommandId::FormatSubscript,
            "format.subscript",
            "Format",
            "Subscript",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Comma),
            {}
        },
        {
            CommandId::FormatSuperscript,
            "format.superscript",
            "Format",
            "Superscript",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Period),
            {}
        },
        {
            CommandId::FormatAutoFormatDocument,
            "format.autoFormatDocument",
            "Format",
            "Auto Format Document",
            QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_F),
            {}
        },

        // Insert
        {
            CommandId::InsertHeading1,
            "insert.heading1",
            "Insert",
            "Heading 1",
            QKeySequence(Qt::CTRL | Qt::Key_1),
            {}
        },
        {
            CommandId::InsertHeading2,
            "insert.heading2",
            "Insert",
            "Heading 2",
            QKeySequence(Qt::CTRL | Qt::Key_2),
            {}
        },
        {
            CommandId::InsertHeading3,
            "insert.heading3",
            "Insert",
            "Heading 3",
            QKeySequence(Qt::CTRL | Qt::Key_3),
            {}
        },
        {
            CommandId::InsertHeading4,
            "insert.heading4",
            "Insert",
            "Heading 4",
            QKeySequence(Qt::CTRL | Qt::Key_4),
            {}
        },
        {
            CommandId::InsertHeading5,
            "insert.heading5",
            "Insert",
            "Heading 5",
            QKeySequence(Qt::CTRL | Qt::Key_5),
            {}
        },
        {
            CommandId::InsertHeading6,
            "insert.heading6",
            "Insert",
            "Heading 6",
            QKeySequence(Qt::CTRL | Qt::Key_6),
            {}
        },
        {
            CommandId::InsertBulletList,
            "insert.bulletList",
            "Insert",
            "Bullet list",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_8),
            {}
        },
        {
            CommandId::InsertNumberList,
            "insert.numberList",
            "Insert",
            "Numbered list",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_7),
            {}
        },
        {
            CommandId::InsertChecklist,
            "insert.checklist",
            "Insert",
            "Checklist",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X),
            {}
        },
        {
            CommandId::InsertBlockquote,
            "insert.blockquote",
            "Insert",
            "Blockquote",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q),
            {}
        },
        {
            CommandId::InsertCodeBlock,
            "insert.codeBlock",
            "Insert",
            "Code Block",
            QKeySequence(Qt::CTRL | Qt::Key_QuoteLeft),
            {}
        },
        {
            CommandId::InsertInlineLink,
            "insert.inlineLink",
            "Insert",
            "Insert Inline Link...",
            QKeySequence(Qt::CTRL | Qt::Key_K),
            {}
        },
        {
            CommandId::InsertReferenceLink,
            "insert.referenceLink",
            "Insert",
            "Insert Reference Link...",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K),
            {}
        },
        {
            CommandId::InsertFootnote,
            "insert.footnote",
            "Insert",
            "Footnote",
            QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F),
            {}
        },
        {
            CommandId::InsertNamedFootnote,
            "insert.namedFootnote",
            "Insert",
            "Named Footnote...",
            QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F),
            {}
        },
        {
            CommandId::InsertHorizontalRule,
            "insert.horizontalRule",
            "Insert",
            "Horizontal Rule",
            QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_H),
            {}
        },
        {
            CommandId::InsertPageBreak,
            "insert.pageBreak",
            "Insert",
            "Page Break",
            QKeySequence(Qt::CTRL | Qt::Key_Return),
            { QKeySequence(Qt::CTRL | Qt::Key_Enter) }
        },
        {
            CommandId::InsertInlineMath,
            "insert.inlineMath",
            "Insert",
            "Inline math",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M),
            {}
        },
        {
            CommandId::InsertBlockMath,
            "insert.blockMath",
            "Insert",
            "Block math",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B),
            {}
        },
        {
            CommandId::InsertTable,
            "insert.table",
            "Insert",
            "Table",
            QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T),
            {}
        },
        {
            CommandId::InsertImage,
            "insert.image",
            "Insert",
            "Insert Markdown Image",
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I),
            {}
        },

        // View
        {
            CommandId::ViewWordWrap,
            "view.wordWrap",
            "View",
            "Word Wrap",
            QKeySequence(Qt::ALT | Qt::Key_Z),
            {}
        },
        {
            CommandId::ViewRefresh,
            "view.refresh",
            "View",
            "Refresh",
            QKeySequence(Qt::Key_F5),
            {}
        },
        {
            CommandId::ViewTerminal,
            "view.terminal",
            "View",
            "Terminal",
            QKeySequence(Qt::Key_F6),
            {}
        },
        {
            CommandId::ViewSourceControl,
            "view.sourceControl",
            "View",
            "Source Control",
            QKeySequence(Qt::Key_F7),
            {}
        },
        {
            CommandId::ViewWorkspacePanel,
            "view.workspacePanel",
            "View",
            "Workspace Panel",
            QKeySequence(Qt::Key_F8),
            {}
        },
        {
            CommandId::ViewFocusFileSearch,
            "view.focusFileSearch",
            "View",
            "Focus File Search",
            QKeySequence(Qt::SHIFT | Qt::Key_F1),
            {}
        },
        {
            CommandId::ViewTogglePreview,
            "view.togglePreview",
            "View",
            "Preview Panel",
            QKeySequence(Qt::Key_F9),
            {}
        },
        {
            CommandId::ViewMindMap,
            "view.mindMap",
            "View",
            "View as Mind Map",
            QKeySequence(Qt::Key_F10),
            {}
        },
        {
            CommandId::ViewReadingMode,
            "view.readingMode",
            "View",
            "Reading Mode",
            QKeySequence(Qt::Key_F11),
            {}
        },
        {
            CommandId::ViewNotepadMode,
            "view.notepadMode",
            "View",
            "Notepad Mode",
            QKeySequence(Qt::Key_F12),
            {}
        },
        {
            CommandId::ViewCommandPalette,
            "view.commandPalette",
            "View",
            "Command Palette…",
            QKeySequence(Qt::Key_F1),
            {}
        },
        {
            CommandId::ViewZoomIn,
            "view.zoomIn",
            "View",
            "Zoom In",
            QKeySequence(Qt::CTRL | Qt::Key_Equal),
            {
                QKeySequence(Qt::CTRL | Qt::KeypadModifier | Qt::Key_Plus),
                QKeySequence(Qt::CTRL | Qt::Key_Plus)
            }
        },
        {
            CommandId::ViewZoomOut,
            "view.zoomOut",
            "View",
            "Zoom Out",
            QKeySequence(Qt::CTRL | Qt::Key_Minus),
            { QKeySequence(Qt::CTRL | Qt::KeypadModifier | Qt::Key_Minus) }
        },
        {
            CommandId::ViewResetZoom,
            "view.resetZoom",
            "View",
            "Reset Zoom",
            QKeySequence(Qt::CTRL | Qt::Key_0),
            {}
        },
        {
            CommandId::ViewSwitchEditorMode,
            "view.switchEditorMode",
            "View",
            "Switch Editor Mode",
            QKeySequence(Qt::CTRL | Qt::Key_M),
            {}
        },

        // Contextual / shared commands
        {
            CommandId::FileRevealInExplorer,
            "file.revealInExplorer",
            "File",
            "Reveal in Explorer",
            QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_R),
            {}
        },
        {
            CommandId::PreviewRefresh,
            "preview.refresh",
            "Preview",
            "Refresh Preview",
            QKeySequence(Qt::Key_F5),
            {}
        },
        {
            CommandId::WorkspaceRename,
            "workspace.rename",
            "Workspace",
            "Rename",
            QKeySequence(Qt::Key_F2),
            {}
        },

        // Terminal local shortcuts
        {
            CommandId::TerminalIncreaseFont,
            "terminal.increaseFont",
            "Terminal",
            "Increase Font Size",
            QKeySequence(Qt::CTRL | Qt::Key_Equal),
            {
                QKeySequence(Qt::CTRL | Qt::Key_Plus),
                QKeySequence(Qt::CTRL | Qt::KeypadModifier | Qt::Key_Plus)
            }
        },
        {
            CommandId::TerminalDecreaseFont,
            "terminal.decreaseFont",
            "Terminal",
            "Decrease Font Size",
            QKeySequence(Qt::CTRL | Qt::Key_Minus),
            { QKeySequence(Qt::CTRL | Qt::KeypadModifier | Qt::Key_Minus) }
        },
        {
            CommandId::TerminalResetFont,
            "terminal.resetFont",
            "Terminal",
            "Reset Font Size",
            QKeySequence(Qt::CTRL | Qt::Key_0),
            {}
        },
        {
            CommandId::TerminalRenameSession,
            "terminal.renameSession",
            "Terminal",
            "Rename Terminal",
            QKeySequence(Qt::Key_F2),
            {}
        }
    };
    return table;
}

QKeySequence CommandShortcutRegistry::defaultShortcut(CommandId id)
{
    const auto* def = find(id);
    return def ? def->defaultShortcut : QKeySequence();
}

QList<QKeySequence> CommandShortcutRegistry::defaultShortcuts(CommandId id)
{
    const auto* def = find(id);
    if (!def)
        return {};
    QList<QKeySequence> list;
    if (!def->defaultShortcut.isEmpty())
        list.append(def->defaultShortcut);
    list.append(def->aliases);
    return list;
}

const CommandShortcutDefinition* CommandShortcutRegistry::find(CommandId id)
{
    const auto& table = definitionsTable();
    for (const auto& def : table) {
        if (def.commandId == id)
            return &def;
    }
    return nullptr;
}

const CommandShortcutDefinition* CommandShortcutRegistry::find(QStringView idString)
{
    const auto& table = definitionsTable();
    for (const auto& def : table) {
        if (idString == QLatin1String(def.idString))
            return &def;
    }
    return nullptr;
}

const QVector<CommandShortcutDefinition>& CommandShortcutRegistry::allDefinitions()
{
    return definitionsTable();
}

#ifdef Q_OS_WIN
QList<QKeyCombination> CommandShortcutRegistry::nativeAcceleratorToKeyCombinations(bool ctrl, bool shift, bool alt, UINT vk)
{
    QList<QKeyCombination> candidates;
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    if (ctrl)  mods |= Qt::ControlModifier;
    if (shift) mods |= Qt::ShiftModifier;
    if (alt)   mods |= Qt::AltModifier;

    auto addKey = [&](Qt::Key k) {
        candidates.append(QKeyCombination(mods, k));
    };

    if (vk >= 'A' && vk <= 'Z') {
        addKey(static_cast<Qt::Key>(Qt::Key_A + (vk - 'A')));
    } else if (vk >= '0' && vk <= '9') {
        addKey(static_cast<Qt::Key>(Qt::Key_0 + (vk - '0')));
    } else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        addKey(static_cast<Qt::Key>(Qt::Key_0 + (vk - VK_NUMPAD0)));
    } else if (vk >= VK_F1 && vk <= VK_F24) {
        addKey(static_cast<Qt::Key>(Qt::Key_F1 + (vk - VK_F1)));
    } else {
        switch (vk) {
        case VK_OEM_COMMA:
            addKey(Qt::Key_Comma);
            break;
        case VK_OEM_PERIOD:
            addKey(Qt::Key_Period);
            break;
        case VK_OEM_MINUS:
        case VK_SUBTRACT:
            addKey(Qt::Key_Minus);
            break;
        case VK_OEM_PLUS:
            addKey(Qt::Key_Equal);
            addKey(Qt::Key_Plus);
            break;
        case VK_ADD:
            addKey(Qt::Key_Plus);
            addKey(Qt::Key_Equal);
            break;
        case VK_LEFT:
            addKey(Qt::Key_Left);
            break;
        case VK_RIGHT:
            addKey(Qt::Key_Right);
            break;
        case VK_UP:
            addKey(Qt::Key_Up);
            break;
        case VK_DOWN:
            addKey(Qt::Key_Down);
            break;
        case VK_ESCAPE:
            addKey(Qt::Key_Escape);
            break;
        case VK_RETURN:
            addKey(Qt::Key_Return);
            addKey(Qt::Key_Enter);
            break;
        case VK_TAB:
            addKey(Qt::Key_Tab);
            break;
        case VK_BACK:
            addKey(Qt::Key_Backspace);
            break;
        case VK_DELETE:
            addKey(Qt::Key_Delete);
            break;
        case VK_OEM_3:
            addKey(Qt::Key_QuoteLeft);
            break;
        default:
            break;
        }
    }

    return candidates;
}

bool CommandShortcutRegistry::matchesNativeAccelerator(CommandId id, bool ctrl, bool shift, bool alt, UINT vk)
{
    const CommandShortcutDefinition* def = find(id);
    if (!def)
        return false;

    const QList<QKeyCombination> candidates = nativeAcceleratorToKeyCombinations(ctrl, shift, alt, vk);
    if (candidates.isEmpty())
        return false;

    auto seqMatchesAnyCandidate = [&](const QKeySequence& seq) {
        if (seq.isEmpty())
            return false;
        const QKeyCombination target = seq[0];
        const Qt::KeyboardModifiers targetMods = target.keyboardModifiers() & ~Qt::KeypadModifier;
        const Qt::Key targetKey = target.key();
        for (const QKeyCombination& cand : candidates) {
            if (cand.keyboardModifiers() == targetMods && cand.key() == targetKey)
                return true;
        }
        return false;
    };

    if (seqMatchesAnyCandidate(def->defaultShortcut))
        return true;

    for (const QKeySequence& alias : def->aliases) {
        if (seqMatchesAnyCandidate(alias))
            return true;
    }

    return false;
}
#endif
