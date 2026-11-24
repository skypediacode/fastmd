#pragma once

#include <QKeyCombination>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringView>
#include <QVector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Authoritative identifiers for all FastMD commands that carry shortcuts.
// These stable IDs serve as the single source of truth for command shortcut
// definitions across Qt actions, toolbar proxies, contextual actions, and
// native WebView2 accelerator handling.
enum class CommandId {
    // File
    FileNew,
    FileNewWindow,
    FileOpen,
    FileOpenFolder,
    FileSave,
    FileSaveAs,
    FileSaveAll,
    FileCloseTab,
    FileReopenClosedTab,
    FilePreviewBrowser,
    FileExportPdf,
    FileExportDocx,
    FileExportHtml,
    FilePrint,
    FilePreferences,
    FileExit,

    // Edit
    EditUndo,
    EditRedo,
    EditCut,
    EditCopy,
    EditPaste,
    EditDelete,
    EditSelectAll,
    EditBack,
    EditForward,
    EditFind,
    EditReplace,
    EditFindNext,
    EditFindPrevious,
    EditGoToLine,
    EditFindInFiles,
    EditReplaceInFiles,
    EditDuplicateLine,
    EditDeleteLine,
    EditMoveLineUp,
    EditMoveLineDown,

    // Format
    FormatBold,
    FormatItalic,
    FormatStrikethrough,
    FormatHighlight,
    FormatSubscript,
    FormatSuperscript,
    FormatAutoFormatDocument,

    // Insert
    InsertHeading1,
    InsertHeading2,
    InsertHeading3,
    InsertHeading4,
    InsertHeading5,
    InsertHeading6,
    InsertBulletList,
    InsertNumberList,
    InsertChecklist,
    InsertBlockquote,
    InsertCodeBlock,
    InsertInlineLink,
    InsertReferenceLink,
    InsertFootnote,
    InsertNamedFootnote,
    InsertHorizontalRule,
    InsertPageBreak,
    InsertInlineMath,
    InsertBlockMath,
    InsertTable,
    InsertImage,

    // View
    ViewWordWrap,
    ViewRefresh,
    ViewTerminal,
    ViewSourceControl,
    ViewWorkspacePanel,
    ViewFocusFileSearch,
    ViewTogglePreview,
    ViewMindMap,
    ViewReadingMode,
    ViewNotepadMode,
    ViewCommandPalette,
    ViewZoomIn,
    ViewZoomOut,
    ViewResetZoom,
    ViewSwitchEditorMode,

    // Contextual / shared commands with established shortcuts
    FileRevealInExplorer,
    PreviewRefresh,
    WorkspaceRename,

    // Terminal panel local shortcuts
    TerminalIncreaseFont,
    TerminalDecreaseFont,
    TerminalResetFont,
    TerminalRenameSession,

    Count
};

struct CommandShortcutDefinition {
    CommandId commandId;
    const char* idString;           // Stable string key, e.g. "file.save"
    const char* category;           // User-visible category, e.g. "File"
    const char* label;              // User-visible label, e.g. "Save"
    QKeySequence defaultShortcut;   // Authoritative primary shortcut
    QList<QKeySequence> aliases;    // Secondary aliases (e.g. Ctrl+T for FileNew, Ctrl+Enter for PageBreak)
};

class CommandShortcutRegistry {
public:
    // Returns the authoritative primary default shortcut for a command.
    static QKeySequence defaultShortcut(CommandId id);

    // Returns all default key sequences accepted by a command (primary + secondary aliases).
    static QList<QKeySequence> defaultShortcuts(CommandId id);

    // Metadata lookups.
    static const CommandShortcutDefinition* find(CommandId id);
    static const CommandShortcutDefinition* find(QStringView idString);

    // Returns all registered command definitions in fixed order.
    static const QVector<CommandShortcutDefinition>& allDefinitions();

#ifdef Q_OS_WIN
    // Matches Win32 keyboard accelerator state (ctrl, shift, alt, virtual-key code)
    // against a specific command's effective shortcut definitions.
    static bool matchesNativeAccelerator(CommandId id, bool ctrl, bool shift, bool alt, UINT vk);

    // Helper to translate Win32 accelerator state into candidate Qt key combinations.
    static QList<QKeyCombination> nativeAcceleratorToKeyCombinations(bool ctrl, bool shift, bool alt, UINT vk);
#endif
};
