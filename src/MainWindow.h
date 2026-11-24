#pragma once
#include <QPointer>
#include "ExportManager.h"
#include "MainWindowTextIO.h"
#include "DocumentModel.h"
#include "TabWidget.h"
#include "StartupSettings.h"
#include "NavigationHistory.h"
#include "FileChangeSuppressionTracker.h"

#include <QMainWindow>
#include <QDateTime>
#include <QStringList>
#include <QIcon>
#include <QHash>
#include <QSet>
#include <functional>
#include <memory>
#include <optional>

namespace Urls {
    inline const QString GitHubRepo = QStringLiteral("https://github.com/skypediacode/fastmd");
    inline const QString ReportIssue = QStringLiteral("https://github.com/skypediacode/fastmd/issues");
    inline const QString RateStore = QStringLiteral("ms-windows-store://review/?ProductId=9N4DSD4WNZ9D");
    inline const QString StoreFallback = QStringLiteral("https://apps.microsoft.com/detail/9N4DSD4WNZ9D");
    inline const QString StoreProtocol = QStringLiteral("ms-windows-store://pdp/?ProductId=9N4DSD4WNZ9D");
    inline const QString GitForWindowsDownload = QStringLiteral("https://git-scm.com/download/win");
}

class MarkdownEditor;
class PreviewWidget;
struct PreviewBodyUpdate;
class ImageViewer;
class GitDiffView;
class FindController;
class FindOverlay;
class ImportManager;
class SourceControlPanel;
class WorkspaceSearchPanel;
class TerminalPanel;
class DetachedTerminalWindow;
class GoToLineDialog;
class MatrixInsertDialog;
class DocumentSummaryDialog;
class QLabel;
class QActionGroup;
class QTimer;
class QPaintEvent;
class QFileSystemWatcher;
class QFileSystemModel;
class QTreeView;
class QToolBar;
class QToolButton;
class UpdateChecker;
class QMenuBar;
class WindowTitleBar;
class QMenu;
class QShortcut;
class QSettings;
class QWidget;
class QSplitter;
class QVBoxLayout;
class OutlinePopup;
class AlignedMenu;
class RecentFilesManager;
class WorkspacePanelController;
class TabDragController;
class MathToolbar;
class ChemistryToolbar;
class LanguageSelectorPopup;
class CommandRegistry;
namespace PreferencesTargets { enum class Id; }
class CommandPaletteLauncher;
class CommandPalettePopup;
struct RecoverySnapshot;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    struct OpenDocumentRef {
        MainWindow* window = nullptr;
        TabPage* page = nullptr;
        int index = -1;
    };
    enum class StartupMode { Normal, NewEmptyWindow, TransferTarget };
    explicit MainWindow(QWidget* parent = nullptr, StartupMode mode = StartupMode::Normal,
                        StartupSettings startupSettings = StartupSettings::load(),
                        bool forceNotepadMode = false);
    ~MainWindow() override;
    static void openUrl(const QString& urlStr);
    static MainWindow* createAuxiliaryWindow(StartupMode mode, const QRect& geometry = {});
    TabWidget* tabWidget() const { return m_tabs; }
    void enqueueStartupTask(std::function<void()> task, int priority);
    // Starts a replacement process only after this window has closed cleanly.
    // Used for application-settings reset.
    bool restartApplication();

    // Gives the user a chance to save every dirty document before FastMD
    // restarts to install an update. Returns false when the user cancels or
    // a save fails.
    bool confirmSaveBeforeUpdate();

    // Mirrors the process-wide update state into this window's Help menu
    // and status bar. Idempotent; called for every window that exists when
    // the checker is created and for each window created after it.
    void attachUpdateChecker(UpdateChecker* updater);

public slots:
    void openFilePaths(const QStringList& paths);
    void openStartupPaths(const QStringList& paths);

public:
    // Reproducible preview-render benchmark used by both the QTextBrowser
    // baseline (Phase 1) and the WebView2 migration (Phase 3). Loads `file`
    // into the current tab, then times `iters` full re-renders (markdown parse
    // + preview display) using the backend-agnostic PreviewWidget::previewRendered
    // signal so sync and async backends are measured identically. Prints a
    // timing summary to stderr and is a no-op unless invoked from the harness.
    void runPreviewBenchmark(const QString& file, int iters);

protected:
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    // Keeps the title bar's maximize/restore glyph in sync with window states
    // this window never initiated (Aero Snap, Win+Arrow, the system menu, a
    // caption double-click handled by DefWindowProc).
    void changeEvent(QEvent* event) override;
#ifdef Q_OS_WIN
    // Suppresses the OS caption (WM_NCCALCSIZE) and re-answers everything it
    // used to provide (WM_NCHITTEST, WM_SETCURSOR, and the non-client mouse
    // messages the Snap Layout maximize button receives). See WindowTitleBar.h
    // and NativeWindowFrame.h -- the window frame itself is never replaced, so
    // resize, Aero Snap, Win+Arrow, Alt+Space, the shadow, and taskbar
    // behavior all stay native.
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif
    // Window activation is the substitute for the Source Control panel's old
    // window-level WindowActivate refresh: returning from the editor is when
    // the working tree is most likely to have moved. There is deliberately no
    // file watcher and no timer.
    bool event(QEvent* event) override;

signals:
    void firstPainted();
    void documentLanguageChanged(DocumentModel* model);

private slots:
    // File
    void newFile();
    void newWindow();
    void openFile();
    void importAsMarkdown();
    // Insert > Content from File: text files go in verbatim, converter formats
    // through the same AnyDoc pipeline as Import, but into the caret.
    void insertContentFromFile();
    void openFilePath(const QString& path, bool openUnsupportedAsSource = false);
    void openFileAsText(const QString& path);
    // Help > User Guide: opens the bundled guide as an ordinary Markdown tab.
    void openUserGuide();
    // Help > Release Notes: opens the complete bundled CHANGELOG.md, unfiltered.
    void openReleaseNotes();
    void saveFile();
    void saveFileAs();
    void saveAll();
    void doExportHtml();
    void doExportPdf();
    void doExportDocx();
    void doExportEpub();
    void doExportPlainText();
    void doExportRtf();
    void doExportOdt();
    void doExportLatex();
    // Shared implementation for the "Export As" submenu formats (all Pandoc-based).
    void doExportViaPandoc(const QString& extension, const QString& filter,
                           const QString& dialogTitle, const QString& pandocFormat,
                           const QString& formatLabel, const QStringList& extraArgs = {},
                           const QString& successNote = QString());
    // Suggested path for an export Save dialog. Uses the current document's
    // folder + base name when saved, otherwise the tab title in the default/last
    // export folder. `extension` includes the leading dot (e.g. ".pdf").
    QString exportInitialPath(const QString& extension) const;
    void exportMindMapHtml(const QString& markdown);
    void exportMindMapPdf(const QString& markdownSource);
    void exportMindMapDocx(const QString& markdownSource);
    // Runs a snapshot-based export off the UI thread. The destination key keeps
    // two commands from racing over the same output while independent exports
    // remain safe (their exporter temp files are unique).
    void runExportTask(const QString& formatLabel, const QString& filePath,
                       std::function<bool()> task, const QString& successNote = {});
    void editPdfPageSetup();
    void editHeaderFooter();
    void printDocument();
    void openPreferences();
    // Same dialog, opened on the Spell Check setting. Kept as a separate entry point
    // rather than a defaulted argument on openPreferences(): that one is
    // connected directly to QAction::triggered(bool), which would no longer
    // compile against a parameter.
    void openEditorPreferences();
    void onApplicationReset(bool restartNow);
    void doPreviewBrowser();

    // Edit
    void openFind();
    void openReplace();
    void findNext();
    void findPrevious();
    void openGoToLine();
    // Reveals the Workspace panel in Source Control mode (F7). It
    // never touches the document area.
    void showSourceControl();
    // Opens (or re-activates) a read-only side-by-side diff document. The git
    // command runs on a worker; the tab appears immediately with a placeholder.
    void openGitDiff(const GitDiffSpec& spec);
    void loadGitDiffInto(TabPage* page);
    // Re-runs every open working-tree diff after the panel refreshes. A
    // commit-to-commit diff is immutable and is never re-run.
    void refreshOpenDiffTabs();
    void connectSourceControlPanel(SourceControlPanel* panel);
    // Ctrl+Shift+F / Ctrl+Shift+H (also reached from the Preview and the
    // Terminal, which own keyboard focus natively): reveals the Workspace panel
    // in Search mode and focuses the query. It never touches the document area.
    void openWorkspaceSearch(bool expandReplace);
    void connectSearchPanel(WorkspaceSearchPanel* panel);
    // Opens (or re-activates, in whichever window holds it) the file and selects
    // the match. A file open as a viewer, or one that would open as one, is
    // shown as source, since a match cannot be selected in a rendered page.
    void openSearchMatch(const QString& path, int line, int column, int length);
    // The Search panel edited a document's buffer. Only the active tab's editor
    // reaches onTextChanged(), so a replacement in a background tab needs the
    // same dirty/revision bookkeeping done here.
    void noteDocumentEditedBySearch(TabPage* page);
    // Keeps the View menu's Source Control / Workspace Panel checkmarks in
    // sync with the sidebar's actual visibility and panel mode -- the sidebar
    // is the source of truth, never separate menu-only state.
    void syncSidebarModeActions();
    // Replays every open document path through onFileChanged(). A pull or
    // stash apply replaces files wholesale, which the watcher does not
    // always re-signal for.
    void recheckOpenFilesOnDisk();
    void ensureFindOverlay();  // lazily creates m_findOverlay on first use
    void syncFindTarget();     // points Find at the active tab's editor or PDF
    void reopenClosedTab();
    void openMatrixInsertDialog();
    MatrixInsertDialog* matrixDialog();  // lazily creates m_matrixDialog on first use

    // View
    void togglePreview();
    void toggleReadingMode();
    void toggleNotepadMode();
    void toggleWorkspacePanel();
    void toggleTerminal();
    void toggleMindMap();
    void goToPreview();
    void setWordWrap(bool enabled);
    void showOutlinePopup();
    void setEditorMode(bool markdown);

    // Centralized language operation (language-indicator-plan.md). All
    // editable Markdown/PlainText transitions must route through these --
    // never call DocumentModel::setEditorMode() directly for an editable tab.
    void setDocumentLanguage(TabPage* pg, const QString& stableDefinitionName, LanguageSelectionSource source);
    void requestAutoDetectLanguage(TabPage* pg);
    void refreshDocumentLanguageForPath(TabPage* pg);
    void switchDocumentMode(TabPage* pg, bool markdown);
    void initializeDocumentLanguage(TabPage* pg, LanguageSelectionSource source = LanguageSelectionSource::Automatic);
    void migrateLegacyDocumentLanguage(TabPage* pg, EditorMode savedMode);
    // Sole receiver of documentLanguageChanged(): projects the new state onto
    // the status button, toolbar, menus, open selector popup, and preview.
    void onDocumentLanguageChanged(DocumentModel* model);
    void setTheme(const QString& theme);
    void zoomIn();
    void zoomOut();
    void resetZoom();

    // Internal
    void onEditorActivated(MarkdownEditor* editor, PreviewWidget* preview);
    void onTabCloseRequested(int index);
    void onContentReady();
    void onCursorPositionChanged();
    // Collapses the cursorPositionChanged/selectionChanged pair into one
    // onCursorPositionChanged() per event-loop turn.
    void scheduleCursorPositionUpdate();
    void onTextChanged();
    void onFileChanged(const QString& path);
    void onDirectoryChanged(const QString& path);
    void processExternalFileChange(const QString& path);
    void renameTab(int index);
    void onWorkspaceFileRenamed(const QString& oldPath, const QString& newPath);
    void onWorkspacePathsDeleted(const QStringList& paths);
    void showTabContextMenu(int index, QPoint globalPos);
    void showTabBarContextMenu(QPoint globalPos);
    void showPreviewContextMenu(const QPoint& globalPos, bool hasSelection, int sourceLine,
                                const QString& mermaidSvg, const QString& mermaidPngDataUrl);
    // Shared by the preview's right-click "Refresh" entry and its forwarded
    // F5 (PreviewWidget::Command::RefreshPreview) -- see connectEditor().
    void refreshActivePreview();
    // The window-wide F5 fallback (View > Refresh). Preview and Source
    // Control each answer F5 themselves -- WebView2 accelerator forwarding
    // and a local WidgetWithChildrenShortcut respectively -- and this is
    // never reached while either has focus. It only routes Workspace-tree
    // focus to WorkspacePanelController::updateRoot(); every other context
    // (the editor, viewer tabs, ...) has no meaningful refresh and is a
    // deliberate no-op.
    void refreshActiveContext();

    void checkForUpdates();
    void syncUpdateStatus();
    void onUpdateStatusClicked();
    void onAutoSaveTimeout(DocumentModel* model);
    void inspectRecoveryFiles();
    void navigateBack();
    void navigateForward();

private:
    friend class TabDragController;
    struct SessionTabState;

    // Builds the custom window title bar (app icon + application menu bar +
    // caption buttons) and installs it with QMainWindow::setMenuWidget(), so
    // it occupies exactly the row the native menu bar used to. Must run before
    // createMenus(), which populates the menu bar it owns.
    void createTitleBar();
    // Maximize <-> restore, shared by the title bar's middle button and the
    // Snap Layout button's non-client click path.
    void toggleMaximizedState();
    // Lines the title bar's app icon up with the left edge of the first
    // toolbar button's icon below it. Measured from the real widgets rather
    // than hard-coded, because the toolbar's left inset comes from its
    // contents margins, the style, and the application style sheet together.
    void createTitleBarLayoutToggles();
    void syncTitleBarIconAlignment();
#ifdef Q_OS_WIN
    // Classifies a WM_NCHITTEST screen point (physical pixels) as a resize
    // border, a title-bar zone, or ordinary client content.
    qintptr titleBarHitTest(int screenX, int screenY);
    // Opens the window's own system menu. The QPoint overload takes a point in
    // this window's logical coordinates (the app icon's left click); the other
    // takes physical screen pixels (the caption's right click, whose message
    // already carries them). Qt's Windows plugin swallows the WM_CONTEXTMENU
    // DefWindowProc would have used, so this window has to open it itself.
    void showWindowSystemMenu(const QPoint& windowPos);
    void showSystemMenuAtScreenPos(int screenX, int screenY);
    // Whether TrackMouseEvent is currently armed for WM_NCMOUSELEAVE, which is
    // what lets the Snap Layout maximize button drop its hover highlight.
    bool m_trackingNcMouseLeave = false;
    // Whether the press that a WM_NCLBUTTONUP completes actually started on the
    // maximize button. Without it, the button-up that arrives after a caption
    // double-click has already maximized the window would toggle it straight
    // back if the cursor happened to land on the (now repositioned) button.
    bool m_maximizeButtonPressed = false;
    bool m_closeButtonPressed = false;
#endif
    void createMenus();
    void createToolbar();
    QString currentWorkspaceFolder() const;
    void copyCurrentFilePath();
    void revealCurrentFileInExplorer();
    void openCurrentFolderInTerminal();
    // Command Palette-only exception: launches the user's actual external
    // terminal (Windows Terminal, falling back to cmd.exe) instead of the
    // Internal Terminal. See openFolderInInternalTerminal().
    void openCurrentFolderInExternalTerminal();
    void openGitChanges();
    void viewGitHistory();

    // ---- Command Palette (MainWindowCommands.cpp) ----
    // The registry and the popup are built on first use: a launch that never
    // opens the palette pays only for the painted launcher box in the toolbar.
    void ensureCommandPalette();
    void openCommandPalette();
    void registerCommands();
    void setStatusBarVisible(bool visible);
    void goToSource();

    void createStatusBar();
    void createWorkspacePanel();
    void ensureOutlinePopup();

    void connectEditor(MarkdownEditor* editor, PreviewWidget* preview);
    void disconnectEditor();
    ImageViewer* currentImageViewer() const;
    GitDiffView* currentDiffView() const;
    void completeStartup();
    void runNextStartupTask();
    void applyRecoveryInspection(QList<RecoverySnapshot> usable, QStringList obsoleteKeys);

    // Built on first use by either AnyDoc workflow.
    void ensureImportManager();
    // Reads the converted scratch Markdown, drops it at the caret, and removes
    // the scratch file whatever happens.
    void finishInsertContentFromFile(const QString& markdownPath, int droppedImages);
    // Final step shared by the raw-text and AnyDoc Insert routes.
    void insertContentAtCursor(MarkdownEditor* editor, QString content, int droppedImages);

    bool confirmClose(int tabIndex);
    bool saveDocument(int tabIndex, bool forceDialog);
    void discardTabForDuplicate(int tabIndex);
    static QString documentPathKey(const QString& path);
    static OpenDocumentRef findOpenDocument(const QString& path, const TabPage* exclude = nullptr);
    // The User Guide, Release Notes and What's New tabs have no path, so they
    // cannot be found through the open-document path registry; each is
    // instead unique by its DocumentModel::bundledKind().
    static OpenDocumentRef findOpenBundledDocument(const QString& kind);
    // Diff documents have no path, so they cannot be found through the path
    // registry either; they are unique by their GitDiffSpec.
    static OpenDocumentRef findOpenDiffDocument(const GitDiffSpec& spec);
    static void activateOpenDocument(const OpenDocumentRef& document);

    void applyTheme(const QString& theme);
    void syncWorkspaceHeaderHeight();
    void updateWindowTitle();
    void updatePreview();
    void updateCurrentHtml();
    // Converts on a worker thread and pushes the result back on the UI thread.
    // Falls back to the synchronous path for documents small enough that the
    // round trip costs more than the conversion.
    void updateCurrentHtmlAsync(TabPage* page);
    // `target` is a QPointer: closing a tab destroys its PreviewWidget, which
    // can happen while a conversion is still in flight (Rule 12).
    void applyConvertedHtml(quint64 generation, const QPointer<PreviewWidget>& target,
                            const QString& source, const PreviewBodyUpdate& update);
    void markPreviewRendered(TabPage* page);
    bool previewUpToDate(TabPage* page) const;
    void invalidateRenderedPreviews()
    {
        ++m_renderEpoch;
        ++m_htmlGeneration;
    }
    void updateWordCount(const QString& text = {});
    void setWordCountLabel(int words);
    void scheduleWordCount();
    void showDocumentSummary();
    void watchFile(const QString& path);
    QFileSystemWatcher* ensureFileWatcher();
    void rememberFileContents(const QString& path, const QByteArray& contents);
    bool fileContentsAreKnown(const QString& path, const QByteArray& contents) const;
    void forgetFileContents(const QString& path);
    void rememberFileIdentity(const QString& path);
    void forgetFileIdentity(const QString& path);
    QString renamedFilePath(const QString& oldPath) const;
    void reloadDocumentSilently(int index, TabPage* page, const QByteArray& diskBytes);
    void clearExternalConflict(DocumentModel* model);
    void showExternalConflictDialog(DocumentModel* model, const QString& path);
    void updateToolbarIcons(bool dark);
    void setExportStatus(const QString& message, int timeoutMs = 0);
    void scheduleAutoSave(DocumentModel* model);
    void writeRecoverySnapshot(DocumentModel* model, const QString& text, quint64 revision);
    void autoSaveDocument(DocumentModel* model, const QString& text, quint64 revision);
    void cleanupRecoveryFor(DocumentModel* model);
    void transferTabTo(MainWindow* target, TabPage* page, int insertIndex);
    void closeLaterIfEmpty();
    void syncCurrentTabUi();
    void setEditorZoomLevel(int pixelSize);
    void setDiffZoomDelta(int delta);
    bool isPreviewVisible() const;
    bool isMindMapVisible() const;
    void setPreviewVisible(bool visible, bool persist = true);
    // Reading Mode only applies to editable Markdown tabs; on any other tab the
    // flag stays set but the layout (and the actions' checked state) fall back
    // to normal, so returning to a Markdown tab resumes reading.
    bool isReadingModeActive() const;
    // `clearTabOverride` also drops the current tab's own per-tab Reading Mode
    // (DocumentModel::readingModeOverride). Pass false when the Editor being
    // asked for is not this tab's -- File > New, for instance, must not cancel
    // the User Guide's reading layout on its way to a brand-new tab.
    void setReadingMode(bool enabled, bool clearTabOverride = true);
    // Non-null only while Find belongs to the rendered page instead of the
    // Editor, i.e. Reading Mode with a visible Preview.
    PreviewWidget* readingModeFindTarget() const;
    void stopReadingModeFind();
    // Reapplies m_readingModeSplitterSizes to the current tab's Editor/Preview
    // splitter as a ratio of its current width. No-op when nothing was captured.
    void restoreDocumentSplitterSizes();
    // Called by the commands that are meaningless while the Editor is hidden
    // (Find/Replace, Go To, Go to Source, New) so they never operate on an
    // invisible editor.
    void leaveReadingModeForEditing(bool clearTabOverride = true);
    // Single mutation point for the window-level flag: it also mirrors the value
    // into TabWidget, so a tab created while Reading Mode is on is built in the
    // reading layout instead of flashing the Editor and splitter handle first.
    void setReadingModeFlag(bool enabled);
    // Hands keyboard control to the current tab's Preview after the Editor was
    // hidden by Reading Mode; a hidden QPlainTextEdit cannot hold native focus.
    void focusReadingModePreview();
    // Notepad Mode is Reading Mode's mirror image: it hides the Preview (instead
    // of the Editor) and also hides the Workspace Panel, leaving only the Editor.
    // It reuses Reading Mode's splitter-size capture/restore rather than a second
    // copy, and the two modes are mutually exclusive.
    bool isNotepadModeActive() const { return m_notepadMode; }
    void setNotepadMode(bool enabled);
    void ensureTerminalPanel();
    // A resolved Terminal Ctrl+Click on a file: opened in FastMD when FastMD
    // can show it, otherwise only revealed in Explorer -- never launched.
    void openTerminalFileLink(const QString& path, int line, int column);
    // Saves the docked splitter height only after the panel has actually been
    // laid out in this window. A newly-created panel that goes straight into a
    // detached window has only a transient header-height allocation.
    void persistDockedTerminalHeight();
    // `takeFocus` false is for the startup restore only: focusing hands native
    // focus to the WebView2 child, which at startup arrives after the window is
    // already interactive and steals keystrokes meant for the editor.
    void setTerminalVisible(bool visible, bool takeFocus = true);
    // Whether opening the Terminal should put it in its own window. Detached
    // is the default; only attaching it records the docked preference.
    static bool terminalOpensDetached();
    // Restores the persisted Terminal layout after first paint. Separate from
    // setTerminalVisible() because a terminal left detached must be built
    // directly into its floating window rather than shown docked and moved.
    void restoreTerminalPanel();
    // Hides the terminal feature and closes its floating shell when detached.
    // Keep this as the common path for the panel's Close button and View/F6.
    void closeTerminalPanel();
    // Reveals the Terminal panel (docked or detached, whichever is current)
    // and opens a new Internal Terminal tab whose working directory is
    // exactly `folder`. Shared by every "Open Folder in Terminal" UI location
    // except the Command Palette's external-terminal command.
    void openFolderInInternalTerminal(const QString& folder);
    // Command Palette terminal commands: brings the Terminal to the front and
    // focuses it, opening it first if needed, without re-laying out a docked
    // panel that is already on screen.
    void revealTerminal();
    // Lists the installed shell profiles in the Command Palette. A pick either
    // opens a new terminal with that profile or makes it the default.
    void openTerminalProfilePicker(bool selectDefault);
    void openTerminalAtFile(const QString& filePath);
    void openTerminalAtWorkspace(const QString& workspacePath);
    // Moves the live TerminalPanel (sessions, tabs, and all) into its own
    // floating window; see DetachedTerminalWindow. Kept parameterless so it can
    // be connected directly to TerminalPanel::detachRequested.
    void detachTerminalPanel();
    // `takeFocus` false also suppresses activating the new window, so a
    // restored detached terminal never pulls activation off the main window
    // moments after it became interactive.
    void moveTerminalToDetachedWindow(bool takeFocus);
    // Reparents the panel back into the docked splitter. Safe to call whether
    // or not a terminal is currently detached.
    void reattachTerminalPanel();
    // Brings the floating terminal window back to the front, un-minimizing it
    // without disturbing a maximized state. show() alone does not: a minimized
    // window is already "visible" as far as QWidget is concerned.
    void raiseDetachedTerminal(bool takeFocus);
    void setNotepadModeFlag(bool enabled);
    void setMindMapVisible(bool visible);
    void togglePreviewTask(int startLine, int endLine, bool checked);
    NavigationHistory::Location currentNavigationLocation() const;
    void recordNavigationDeparture();
    void restoreNavigationLocation(const NavigationHistory::Location& location);
    void updateNavigationActions();

    void readSettings();
    RestoredTabSettings defaultTabSettingsForPath(const QString& path = {}) const;
    // Creates the very first tab of a fresh startup: an ordinary blank tab, or,
    // exactly once on the first-ever launch, an editable tab pre-seeded with the
    // bundled Welcome.md sample.
    void openInitialTab();
    // Shared plumbing behind the User Guide, Release Notes, and What's New
    // tabs: bundled, read-only, Qt-resource-backed content shown in the
    // reading layout without touching the window's own Reading Mode/Preview/
    // Workspace/splitter preferences. `activate` false leaves the tab in the
    // background -- used by the automatic startup trigger so it never steals
    // focus from an explicitly requested document.
    void openBundledReadOnlyDocument(const QString& kind, const QString& title,
                                     const QString& markdown, bool activate);
    // First-launch-after-update check: shows What's New at most once per
    // installed version, never on a fresh install. Active if (and only if)
    // nothing else has already claimed the startup active tab -- see
    // consumeStartupPlaceholderIfPristine().
    void maybeShowWhatsNew();
    // If the tracked startup placeholder is still pristine (untitled,
    // unedited, not bundled, not a diff) and `widget` matches, returns its
    // current tab index; otherwise -1. Shared by
    // discardStrayStartupPlaceholder() and consumeStartupPlaceholderIfPristine().
    int pristineStartupPlaceholderIndex(QWidget* widget) const;
    // Runs unconditionally (whether or not What's New has anything to show):
    // discards the auto-created blank startup tab tracked by
    // m_startupPlaceholderTab if it is still pristine but is *not* the
    // active tab -- meaning an explicitly opened file or a restored session
    // tab already claimed the active tab ahead of this. Leaves it (and its
    // tracking) untouched if it is still active, so
    // consumeStartupPlaceholderIfPristine() can still replace it below.
    void discardStrayStartupPlaceholder();
    // Discards the auto-created blank startup tab tracked by
    // m_startupPlaceholderTab if -- and only if -- it still exists, is still
    // that exact tab, and is still untouched (untitled, unedited, not
    // bundled, not a diff). Returns whether it was the active tab at the
    // time (i.e. whether the caller is now free to activate a replacement
    // without displacing anything the user or startup already chose).
    bool consumeStartupPlaceholderIfPristine();
    void writeSettings();
    // Coalesces signal-driven persistence: one writeSettings() is not cheap
    // (40+ setValue calls, a full session-tab array rewrite, and a WordWrap
    // prune sweep), and opening N files at once used to run it N times. Every
    // signal-driven caller goes through this; closeEvent and the Preferences OK
    // path stay synchronous, and closeEvent's synchronous write is also what
    // flushes a still-pending one before the tabs are torn down.
    void scheduleWriteSettings();
    void pruneWordWrapSettings();
    void restorePreviousSession();
    void restoreNextSessionTab();
    void loadSessionTab(int stateIndex, bool active);
    void restoreLoadedSessionTab(int stateIndex, bool active, EditableTextFileLoadResult result,
                                 QString content, QString lineEnding, TextEncoding encoding,
                                 qint64 fileSize, QDateTime lastModified);
    void attachRestoredFileWatchers();
    void restoreTabState(TabPage* page, const SessionTabState& state);
    // Single description of "everything worth remembering about a tab": shared
    // by the persisted session and by the closed-tab (Ctrl+Shift+T) history.
    SessionTabState captureTabState(const TabPage* page) const;
    void recordClosedTab(int index, const TabPage* page);
    void updateReopenClosedTabAction();
    bool loadFileText(const QString& path, QString* content, QString* lineEnding,
                      TextEncoding* encoding = nullptr) const;
    int addTabFromFile(const QString& path, bool activate, bool addToRecent, bool showError,
                       const SessionTabState* state = nullptr, const QString* loadedContent = nullptr,
                       const QString* loadedLineEnding = nullptr,
                       const TextEncoding* loadedEncoding = nullptr);

    // ---- toolbar icon registry ----
    struct TbIcon {
        QAction* action;
        bool     isText;    // true = text label, false = Material Icons codepoint
        QChar    code;
        QString  text;
    };
    QList<TbIcon> m_tbIcons;
    // QPointer, not a raw pointer: this is a cross-reference to a widget owned
    // by QMainWindow's layout (setMenuWidget takes ownership), and the native
    // event handler dereferences it from outside Qt's own event dispatch
    // (Rule 12).
    QPointer<WindowTitleBar> m_titleBar;
    // The application menu bar, owned by m_titleBar. QMainWindow::menuBar() is
    // deliberately never called once setMenuWidget() has been used: it would
    // build a second, empty QMenuBar in a row that no longer exists.
    QMenuBar*     m_menuBar      = nullptr;
    // View > Focus File Search (Shift+F1). Lives on the window rather than in
    // the View menu -- see createMenus() for why it is not merely hidden.
    QAction*      m_actFocusFileSearch = nullptr;
    // The toolbar's first button. Its icon's left edge is the alignment
    // reference for the title bar's app icon -- see syncTitleBarIconAlignment().
    QAction*      m_actToolbarNew = nullptr;
    QToolBar*     m_toolbar      = nullptr;
    QToolButton*  m_overflowBtn  = nullptr;
    QToolButton*  m_openButton   = nullptr;
    AlignedMenu*  m_toolbarOpenMenu = nullptr;
    QToolButton*  m_imageButton  = nullptr;
    AlignedMenu*  m_toolbarImageMenu = nullptr;
    OutlinePopup* m_outlinePopup = nullptr;
    QToolButton*  m_tableButton    = nullptr;
    CommandPaletteLauncher* m_commandLauncher = nullptr;
    CommandPalettePopup*    m_commandPalette  = nullptr;
    CommandRegistry*        m_commandRegistry = nullptr;
    // Markdown-syntax-inserting actions, disabled in Plain Text mode.
    QList<QAction*> m_formatActions;

    // ---- Math Toolbar (View -> Math Toolbar; hidden by default) ----
    // Constructed lazily by ensureMathToolbar()/ensureChemistryToolbar() -
    // hidden by default, so a normal launch never builds their icon
    // catalogs, raster variants, or popup coordination.
    void ensureMathToolbar();
    void ensureChemistryToolbar();
    QWidget* toolbarAt(const QPoint& globalPos) const;
    bool showToolbarContextMenu(const QPoint& globalPos);
    QWidget*      m_toolbarRootWidget = nullptr;
    QVBoxLayout*  m_toolbarRootLayout = nullptr;
    std::unique_ptr<MathToolbar> m_mathToolbar;
    QAction*      m_actMathToolbar = nullptr;
    bool          m_mathToolbarVisible = false;
    std::unique_ptr<ChemistryToolbar> m_chemistryToolbar;
    QAction*      m_actChemistryToolbar = nullptr;
    bool          m_chemistryToolbarVisible = false;
    QMenu*        m_toolbarContextMenu = nullptr;

    // ---- widgets ----
    TabWidget*          m_tabs;
    FindController*     m_findController = nullptr;
    FindOverlay*        m_findOverlay = nullptr;
    GoToLineDialog*     m_goToLineDialog = nullptr;
    QPointer<SourceControlPanel> m_sourceControl;
    MatrixInsertDialog* m_matrixDialog = nullptr;
    DocumentSummaryDialog* m_documentSummaryDialog = nullptr;
    QLabel*             m_lblPosition;
    QLabel*             m_lblExportStatus = nullptr;
    QLabel*             m_lblSelection;
    QLabel*             m_lblLinkTarget = nullptr;
    QLabel*             m_lblWords;
    QToolButton*        m_lblMode = nullptr;
    LanguageSelectorPopup* m_languagePopup = nullptr;
    void openLanguageSelector();
    void onLanguagePopupAutoDetect();
    void onLanguagePopupDefinitionChosen(const QString& stableName);
    // Byte-format indicators: both are clickable status-bar buttons whose
    // menus mutate only the current tab's DocumentModel (see
    // applyDocumentLineEnding/applyDocumentEncoding).
    QToolButton*        m_lblLineEnding = nullptr;
    QToolButton*        m_lblEncoding = nullptr;
    AlignedMenu*        m_lineEndingMenu = nullptr;
    AlignedMenu*        m_encodingMenu = nullptr;
    QAction*            m_actEolCrlf = nullptr;
    QAction*            m_actEolLf = nullptr;
    QAction*            m_actEolCr = nullptr;
    QList<QAction*>     m_encodingActions;
    void openLineEndingMenu();
    void openEncodingMenu();
    // Spell check: one clickable status-bar indicator, one Edit submenu, one
    // set of QActions shared by both plus the Command Palette.
    QToolButton*        m_lblSpell = nullptr;
    AlignedMenu*        m_spellMenu = nullptr;
    AlignedMenu*        m_spellLanguageMenu = nullptr;
    QActionGroup*       m_spellLanguageGroup = nullptr;
    QAction*            m_actSpellEnable = nullptr;
    QAction*            m_actSpellFixAll = nullptr;
    QAction*            m_actSpellSettings = nullptr;
    QMenu*              m_spellCheckMenu = nullptr;
    void openSpellCheckMenu();
    void rebuildSpellLanguageMenu();
    void syncSpellCheckUi();
    void applyDocumentLineEnding(const QString& lineEnding);
    void applyDocumentEncoding(TextEncoding encoding);
    void markDocumentFormatChanged(TabPage* page);
    void popupStatusBarMenu(AlignedMenu* menu, QWidget* anchor);
    // A Qt::Popup closes itself on a press outside its window before the
    // anchor receives the matching clicked() signal. Remember that specific
    // press so the click does not immediately reopen the same popup.
    void rememberStatusBarPopupDismissal(QWidget* anchor);
    bool consumeStatusBarPopupDismissal(QWidget* anchor);
    QPointer<QWidget>       m_statusBarPopupDismissalAnchor;
    QToolButton*        m_updateStatusButton = nullptr;
    bool                m_updateCheckerAttached = false;
    QSet<QString>       m_exportDestinations;
    int                 m_runningExportCount = 0;
    quint64             m_exportStatusGeneration = 0;
    QSplitter*          m_mainSplitter = nullptr;
    // Vertical splitter nested in m_mainSplitter's right side: the tab area on
    // top, the Terminal panel underneath. Built eagerly; the panel is not.
    QSplitter*          m_editorTerminalSplitter = nullptr;
    TerminalPanel*      m_terminalPanel = nullptr;
    // Non-null only while the Terminal panel is popped out into its own
    // window; see detachTerminalPanel()/reattachTerminalPanel().
    QPointer<DetachedTerminalWindow> m_detachedTerminalWindow;
    // The persisted intent is kept separately from the lazily-created panel:
    // before first paint there may be no TerminalPanel whose visibility can
    // represent the setting yet.
    bool                m_terminalVisible = false;
    // Becomes true only after setSizes() has given the docked panel a real
    // layout. It prevents a direct-to-detached open from overwriting the
    // user's saved docked height with TerminalPanel's 36px header height.
    bool                m_terminalPanelHasDockedLayout = false;
    WorkspacePanelController* m_workspacePanelCtrl = nullptr;

    // ---- menus / actions ----
    AlignedMenu*  m_recentMenu;
    RecentFilesManager* m_recentFilesMgr = nullptr;
    ImportManager* m_importManager = nullptr;
    // The editor Insert > Content from File was invoked on. A QPointer because
    // the conversion is asynchronous and its tab can close while it runs.
    QPointer<MarkdownEditor> m_insertContentTarget;
    QActionGroup* m_themeGroup;
    QAction*      m_actTogglePreview;
    QAction*      m_actReadingMode = nullptr;
    QAction*      m_actNotepadMode = nullptr;
    QAction*      m_actTerminal = nullptr;
    QAction*      m_actRefresh = nullptr;
    QAction*      m_actMindMap = nullptr;
    QAction*      m_actGoToPreview = nullptr;
    QAction*      m_actWordWrap = nullptr;
    QAction*      m_actWorkspacePanel = nullptr;
    QAction*      m_actToggleWorkspace = nullptr;
    QAction*      m_actStatusBar = nullptr;
    QAction*      m_actOutline = nullptr;
    QAction*      m_actCommandPalette = nullptr;
    QAction*      m_actDocumentSummary = nullptr;
    QAction*      m_actToolbarReveal = nullptr;
    QAction*      m_actToolbarCopyPath = nullptr;
    QAction*      m_actToolbarTerminal = nullptr;
    QAction*      m_actModeToggle = nullptr;
    QAction*      m_actFormatDocument = nullptr;
    QAction*      m_actHighlight = nullptr;
    // The alignment actions are shared by the Format menu and the main toolbar.
    // One QAction per command keeps menu, toolbar, shortcut and palette in sync.
    QAction*      m_actAlignLeft = nullptr;
    QAction*      m_actAlignCenter = nullptr;
    QAction*      m_actAlignRight = nullptr;
    QAction*      m_actAlignJustify = nullptr;
    // Insert-menu code actions are also reused by the toolbar and command
    // palette, keeping one behavior and one shortcut owner for each command.
    QAction*      m_actInsertInlineCode = nullptr;
    QAction*      m_actInsertCodeBlock = nullptr;
    QAction*      m_actInsertInlineLink = nullptr;
    QAction*      m_actBack = nullptr;
    QAction*      m_actForward = nullptr;
    // File > New, reused by the tab bar's empty-area context menu.
    QAction*      m_actNewTab = nullptr;
    QAction*      m_actReopenClosedTab = nullptr;
    QAction*      m_actHorizontalRule = nullptr;
    QAction*      m_actPageBreak = nullptr;

    // Reused as-is by the Preview's right-click context menu (see
    // showPreviewContextMenu) so enabled state, shortcuts, and translations
    // stay in sync with the File menu.
    QAction*      m_actExportHtml = nullptr;
    QAction*      m_actPreviewBrowser = nullptr;
    QAction*      m_actExportDocx = nullptr;
    QAction*      m_actExportPdf = nullptr;
    QAction*      m_actPrint = nullptr;

    // ---- Help actions ----
    QAction*      m_actCheckUpdates = nullptr;
    QAction*      m_actRateStore = nullptr;
    QAction*      m_actGithubRepo = nullptr;
    QAction*      m_actReportIssue = nullptr;
    QAction*      m_actLicenses = nullptr;
    QAction*      m_actShortcuts = nullptr;
    QAction*      m_actSourceControl = nullptr;
    QAction*      m_actUserGuide = nullptr;
    QAction*      m_actReleaseNotes = nullptr;
    QAction*      m_actAbout = nullptr;

    // ---- state ----
    QString            m_theme = QStringLiteral("system");
    StartupSettings    m_startupSettings;
    MarkdownEditor*    m_activeEditor  = nullptr;
    PreviewWidget*     m_activePreview = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
    FileChangeSuppressionTracker m_ignoredFileChanges;
    QHash<QString, QTimer*> m_fileChangeDebounce;
    QHash<QString, QByteArray> m_knownFileContents;
    // Windows file identities survive Explorer renames, unlike a path watch.
    QHash<QString, QByteArray> m_knownFileIdentities;
    QHash<DocumentModel*, QByteArray> m_conflictDiskBytes;
    ExportManager::PdfExportOptions m_pdfExportOptions;
    bool                m_workspaceTreeVisible = true;
    bool                m_statusBarVisible = true;
    bool                m_welcomeShown = false;
    // The app version this install last ran, as of before this launch --
    // read in readSettings(), then immediately advanced to the running
    // version there so a later failure to show What's New can never cause a
    // retry loop. Persisted as "app/lastRunVersion".
    QString             m_lastRunVersion;
    // Non-empty only when this launch is the first one after an upgrade to
    // this exact version (never on a fresh install): the version whose
    // CHANGELOG.md section maybeShowWhatsNew() should display, computed once
    // in readSettings() and consumed by the deferred startup task.
    QString             m_whatsNewVersionToShow;
    // The splitter widget of the blank tab openInitialTab() auto-creates
    // when there is nothing to restore/show (welcome already shown, no
    // session tab, no explicit file yet) -- tracked by identity, not title
    // or index, so maybeShowWhatsNew() can tell it apart from a blank tab
    // the user created themselves. Nulls itself if that tab is ever closed.
    // See consumeStartupPlaceholderIfPristine().
    QPointer<QWidget>   m_startupPlaceholderTab;
    bool                m_restoreSessionOnStartup = false;
    bool                m_closeAppOnLastTabClose = true;
    bool                m_autoSaveEnabled = true;
    int                 m_autoSaveDelayMs = 2000;
    bool                m_crashRecoveryEnabled = true;
    bool                m_openPdfExternally = false;
    bool                m_openHtmlExternally = false;
    bool                m_openImagesExternally = false;
    bool                m_showExportSuccessDialog = true;
    QHash<DocumentModel*, QTimer*> m_autoSaveTimers;
    QSet<DocumentModel*> m_autoSaveSuspended;
    QHash<DocumentModel*, quint64> m_editRevisions;
    // Per-tab (edit revision, render epoch) the tab's preview was last built
    // from. A tab switch only re-converts when this no longer matches, so
    // switching away and back costs zero Markdown->HTML conversions. The epoch
    // is bumped by anything that changes how a document renders without editing
    // it (theme, renderer options, preview font size).
    QHash<DocumentModel*, QPair<quint64, quint64>> m_renderedRevisions;
    quint64 m_renderEpoch = 1;
    QSet<DocumentModel*> m_savingDocuments;
    QString             m_defaultSaveFolder;
    EditorMode          m_defaultNewDocMode = EditorMode::Markdown;
    bool                m_highlightCurrentLineEnabled = true;
    // The single authority for spell-check state. Preferences, the Edit menu,
    // the status-bar popup and the Command Palette all mutate it through
    // applySpellCheckState() and never write these members directly.
    bool                m_spellCheckEnabled = false;
    QString             m_spellCheckLanguage;
    bool                m_spellCheckResolved = false;
    void                applySpellCheckState(bool enabled, const QString& languageTag);
    void                ensureSpellCheckResolved();
    bool                spellCheckEffectivelyOn() const;
    void                fixAllMisspellings();
    void                showPreferences(std::optional<PreferencesTargets::Id> target);
    bool                m_renderSingleLineBreaks = true;
    // Preview (and Browser Preview / HTML export) body font; empty is the
    // built-in one. The Editor font is process-wide in ApplicationTheme and the
    // Print/PDF font lives in m_pdfExportOptions.
    QString             m_previewFontFamily;
    void                applyFontPreferences(const QString& editorFamily, const QString& previewFamily,
                                             const QString& printFamily);
    // Document Spacing is one shared value for the live Preview, Browser Preview,
    // HTML export, Print and PDF; the Editor's own spacing is not part of it.
    // Applied to every window and persisted (default = key removed) only when it
    // changes.
    DocumentSpacing::Settings m_documentSpacing;
    void                applyDocumentSpacing(const DocumentSpacing::Settings& spacing);
    NavigationHistory  m_navigationHistory;
    bool               m_restoringNavigation = false;
    int                m_lastNavigationLine = -1;
    NavigationHistory::Location m_lastObservedNavigationLocation;
    int                 m_defaultEditorFontSize = FastMdDefaults::EditorFontSize;
    int                 m_diffZoomDelta = 0;
    // Read-through cache of the persisted per-path word-wrap choice (writes go
    // straight to QSettings in setWordWrap). Mutable + a negative cache so
    // defaultTabSettingsForPath() never re-queries the registry for a path it
    // has already looked up.
    mutable QHash<QString, bool> m_wordWrapByPath;
    mutable QSet<QString> m_wordWrapUnsetPaths;
    mutable QSettings* m_lazyWordWrapSettings = nullptr;
    QString             m_lastSaveFolder;
    QString             m_lastOpenFolder;
    QString             m_lastExportFolder;
    struct SessionTabState {
        QString path;
        int     cursorPos = 0;
        int     editorScroll = 0;
        int     previewScroll = 0;
        EditorMode mode = EditorMode::Markdown;
        bool    previewVisible = true;
        MarkdownPresentation presentation = MarkdownPresentation::Preview;

        // Language state (language-indicator-plan.md). hasLanguageState is
        // false for sessions written before this feature -- restoreTabState()
        // falls back to migrating the legacy `mode` field in that case.
        bool    hasLanguageState = false;
        QString selectedDefinitionName;
        QString automaticallyDetectedName;
        LanguageSelectionSource languageSource = LanguageSelectionSource::Automatic;
        bool    hasLastNonMarkdownSelection = false;
        QString lastNonMarkdownDefinitionName;
        LanguageSelectionSource lastNonMarkdownSource = LanguageSelectionSource::Automatic;
    };

    // One closed tab, in the form Reopen Closed Tab needs to bring it back:
    // its file (or, for an untitled document, its text) plus the position it
    // occupied and the editor state it had at the moment it was closed.
    struct ClosedTabRecord {
        SessionTabState state;
        int     index = 0;
        bool    untitled = false;
        QString text;
        QString lineEnding;
        TextEncoding encoding = TextEncoding::Utf8;
        bool    dirty = false;
    };
    QList<ClosedTabRecord> m_closedTabs;

    QList<SessionTabState> m_sessionTabs;
    QList<int>              m_pendingSessionTabIndices;
    struct RestoredWatchPath {
        QString path;
        qint64 fileSize = -1;
        QDateTime lastModified;
    };
    QList<RestoredWatchPath> m_pendingRestoredWatchPaths;
    QPointer<QWidget>       m_startupActiveTabWidget;
    QHash<QString, RestoredTabSettings> m_restoredTabSettings;
    int                    m_sessionActiveTab = -1;

    // current HTML body (kept for export)
    QString m_currentHtml;
    QString m_previewSourceSnapshot;
    bool m_htmlDirty = true;
    bool m_previewDirty = true;
    // Bumped whenever the document changes or the active tab switches, so a
    // conversion that finishes after either event is discarded instead of
    // pushing stale (or another tab's) HTML into the preview.
    quint64 m_htmlGeneration = 0;
    bool m_htmlConversionRunning = false;
    bool m_scrollSyncing = false;
    bool m_firstPainted = false;
    bool m_themeEverApplied = false;
    QTimer* m_writeSettingsTimer = nullptr;
    QTimer* m_wordCountTimer = nullptr;
    int m_lastWordCount = 0;
    bool m_cursorUpdatePending = false;
    int m_lastSelectionStart = -1;
    int m_lastSelectionEnd = -1;
    QList<std::function<void()>> m_startupTasks[5];
    bool m_startupTaskScheduled = false;
    bool m_markdownPreviewVisible = true;
    // Reading Mode is a temporary, window-level view override: it shows the
    // Preview across the whole document area and hides the Editor without ever
    // touching DocumentModel::markdownPreviewVisible(), so the user's real
    // Editor/Preview layout is neither overwritten nor persisted while it is on.
    bool m_readingMode = false;
    // The Editor/Preview splitter sizes captured when Reading Mode was entered,
    // reapplied proportionally on exit so a window resize in between cannot
    // distort the restored layout.
    QList<int> m_readingModeSplitterSizes;
    // Notepad Mode is the same kind of temporary, window-level view override:
    // it hides the Preview (and the Workspace Panel) so only the Editor shows,
    // again without touching either's persisted preference. It reuses
    // m_readingModeSplitterSizes for the same capture/restore purpose -- the
    // two modes are mutually exclusive, so only one is ever using it.
    bool m_notepadMode = false;
    // Read before tabs exist, then applied by completeStartup() once there is
    // an active tab whose Editor/Preview layout can be temporarily overridden.
    bool m_restoreNotepadModeOnStartup = false;
    // Whether the Workspace Panel was visible right before Notepad Mode hid it,
    // so leaving Notepad Mode restores it only if it was actually showing.
    bool m_notepadModeWorkspaceWasVisible = false;
    bool m_wordWrapEnabled = true;
    bool m_isResetting = false;
    bool m_startupReady = false;
    bool m_hasPendingStartupOpenRequest = false;
    QStringList m_pendingStartupPaths;
    StartupMode         m_startupMode = StartupMode::Normal;
};
