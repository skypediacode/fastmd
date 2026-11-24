#pragma once

#include <QObject>
#include <QAtomicInt>
#include <QByteArray>
#include <QColor>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QHash>
#include <functional>
#include <memory>
#include <optional>

class QWidget;
class QVBoxLayout;
class QTreeView;
class QListView;
class QStackedWidget;
class QToolButton;
class QToolBar;
class QLineEdit;
class QLabel;
class QFileSystemModel;
class QStandardItemModel;
class QTimer;
class QModelIndex;
class QEvent;
class QKeyEvent;
class NativeTreeContainer;
class WorkspaceTreeFilterModel;
class WorkspaceNavigationModel;
class WorkspaceRenameDelegate;
class RecentFoldersManager;
class AlignedMenu;
class WorkspaceTreeView;
class SourceControlPanel;
class WorkspaceSearchPanel;
class ThemedSplitter;
class WorkspaceCardBackdrop;
class QTreeWidget;
class QTreeWidgetItem;
class MarkdownEditor;
namespace WorkspaceFileOps { class WorkspaceCopyCancellation; }

// Owns the Workspace side panel: a lightweight, Explorer-like file browser
// for the current workspace folder (header, toolbar, search box, tree, and
// a flat search-results list), plus its file operations (create, rename,
// delete, copy/cut/paste, drag-and-drop import) and persisted state
// (workspace root, recent folders, Show All, expanded paths).
//
// The workspace root is explicit and persisted -- it changes ONLY via
// setRoot()/navigateToParent() (Open Folder, Recent Folders, Parent Folder, or MainWindow restoring it from QSettings
// at startup). It does NOT follow whichever tab is active; MainWindow
// supplies currentFilePathProvider() purely so updateRoot() can
// auto-reveal (expand/select/scroll to) the active file when it happens to
// live under the current root, without ever changing the root itself.
class WorkspacePanelController : public QObject {
    Q_OBJECT
public:
    // The panel has three modes sharing one header row. Only the content below
    // the header changes; the folder label and its actions are identical in all
    // of them, and switching modes never touches the document area. Search is
    // never persisted: a relaunch restores Explorer or Source Control.
    enum class PanelMode { Workspace, SourceControl, Search };

    WorkspacePanelController(QWidget* panelParent,
                              std::function<QString()> currentFilePathProvider,
                              std::function<bool()> darkProvider,
                              QObject* parent = nullptr);
    ~WorkspacePanelController() override;

    QWidget* panelWidget() const;
    QTreeView* treeView() const;
    void ensureUi();

    bool isVisible() const { return m_workspaceTreeVisible; }
    void setVisible(bool visible, bool persist = true);

    // Startup split: records the
    // visibility flag and reserves the panel's geometry without building the
    // tree UI or the QFileSystemModel, so neither lands before first paint.
    void setVisibleDeferringBuild(bool visible);
    // Runs the build setVisibleDeferringBuild() skipped. Re-reads the
    // visibility flag rather than trusting the value it was scheduled with, and
    // is a no-op once the UI exists, so it is safe to call at any time.
    void materialize();
    // materialize() for the startup task: builds the panel now and the tree once
    // ShellWarmUp has finished, so the shell's first-use cost is not paid on the
    // GUI thread. Once the shell is warm it is exactly materialize().
    void materializeAfterFirstPaint();

    PanelMode panelMode() const { return m_panelMode; }
    // Builds the Source Control panel on the first switch into that mode --
    // nothing Git-related may be constructed on the startup path.
    void setPanelMode(PanelMode mode, bool persist = true);
    SourceControlPanel* sourceControlPanel() const { return m_sourceControlPanel; }
    WorkspaceSearchPanel* searchPanel() const { return m_searchPanel; }
    // Ctrl+Shift+F / Ctrl+Shift+H: builds the Search body on first use, switches
    // to it and focuses the query. Replace is only ever expanded, never collapsed.
    // When seedFromEditor is set, a non-empty seed from setSearchSeedProvider()
    // replaces the query; an empty seed keeps the current query.
    void openSearch(bool expandReplace, bool seedFromEditor = true);
    void setSearchSeedProvider(std::function<QString()> provider) { m_searchSeedProvider = std::move(provider); }

    QString rootPath() const { return m_workspaceRootPath; }
    // Switches the workspace root. No-op if `root` isn't a valid, readable
    // directory. With persist=true (the default -- every caller except the
    // initial restore-from-settings load), emits rootChanged() so MainWindow
    // can add it to Recent Folders and persist settings; MainWindow passes
    // persist=false when seeding the root from QSettings at startup, the
    // same convention setVisible() already uses, so that load doesn't
    // trigger a premature settings write before readSettings() finishes.
    void setRoot(const QString& root, bool persist = true,
                 std::optional<bool> showAllFallback = std::nullopt);
    void navigateToParent();

    // Re-syncs the tree to the current state (builds the model lazily on
    // first reveal) and auto-reveals the active file if it's under the
    // current root. Does NOT change the root. Safe to call on every
    // tab add/switch/close/rename.
    void updateRoot();

    void updateToggleIcon(bool dark);
    void focusSearch();
    void copySelection();
    void pasteSelection();
    bool hasWorkspaceFocus() const;

    // Keeps the standalone tree-header row height (and its section text's
    // vertical centering) in sync with the actual rendered height of the
    // tab bar row it sits beside. Call after the stylesheet is (re)applied,
    // since QSS-driven padding/border changes the tab bar's real height.
    void setHeaderHeight(int px);

    bool showAll() const { return m_showAll; }
    void setShowAll(bool showAll, bool persist = true);

    bool dateModifiedColumnVisible() const { return m_dateModifiedColumnVisible; }
    bool sizeColumnVisible() const { return m_sizeColumnVisible; }
    bool fileTypeColumnVisible() const { return m_fileTypeColumnVisible; }
    void setOptionalColumnsVisible(bool fileTypeVisible, bool dateModifiedVisible, bool sizeVisible, bool persist = true);
    int sortColumn() const { return m_sortColumn; }
    Qt::SortOrder sortOrder() const { return m_sortOrder; }
    void setSort(int column, Qt::SortOrder order, bool persist = true);
    QByteArray columnLayoutState() const;
    void setColumnLayoutState(const QByteArray& state);

    QStringList expandedPaths() const { return m_workspaceExpandedPaths; }
    void setExpandedPaths(const QStringList& paths);

    RecentFoldersManager* recentFoldersManager() const { return m_recentFoldersMgr; }

    // Outline section at the bottom of the Workspace body: a collapsible
    // heading-hierarchy view built on the same OutlineModel the Preview
    // panel's floating Outline popup and the Go To dialog's Outline tab
    // already use. This controller holds no DocumentModel/TabWidget
    // reference by design, so MainWindow feeds the active MarkdownEditor
    // (mirroring FindController::setEditor()/the math+chemistry toolbars)
    // and reacts to outlineHeadingActivated() to move the caret.
    bool outlineExpanded() const { return m_outlineExpanded; }
    void setOutlineExpanded(bool expanded, bool persist = true);
    void setOutlineEditor(MarkdownEditor* editor);
    QByteArray outlineSplitterState() const;
    void setOutlineSplitterState(const QByteArray& state);
    void setOutlineReferenceWidget(QWidget* reference);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void fileActivated(const QString& path);
    void textEditorRequested(const QString& path);
    void externalViewerRequested(const QString& path);
    // A folder-terminal action (header button or context menu) asked to open
    // `folder` in FastMD's Internal Terminal. MainWindow owns the terminal
    // panel, so it -- not this controller -- creates the tab.
    void terminalRequested(const QString& folder);
    void fileRenamed(const QString& oldPath, const QString& newPath);
    void pathsDeleted(const QStringList& paths);
    // Emitted whenever visibility changes with persist=true, so MainWindow
    // can call writeSettings().
    void visibilityChanged(bool visible);
    // Emitted whenever the workspace root changes, so MainWindow can
    // persist it and update the window/header title.
    void rootChanged(const QString& newRoot);
    void panelModeChanged(PanelMode mode);
    // Forwarded from the Source Control panel so MainWindow does not need to
    // know when that panel comes into existence.
    void sourceControlPanelCreated(SourceControlPanel* panel);
    void searchPanelCreated(WorkspaceSearchPanel* panel);
    void showAllChanged(bool showAll);
    void optionalColumnsChanged();
    void columnLayoutChanged();
    void sortChanged();
    void outlineExpandedChanged(bool expanded);
    // Emitted with the 0-based source line of the clicked heading; MainWindow
    // moves the active editor's caret there the same way the Preview
    // Outline popup and the Go To dialog's Outline tab already do.
    void outlineHeadingActivated(int line);

private:
    friend class WorkspaceFileSearchNavigationTest;
    QColor iconColor() const;
    void updateHoverRevealIcon();
    void ensureSourceControlPanel();
    void ensureSearchPanel();
    void applyPanelMode();
    void buildToolbar();
    void buildSearchRow();
    void buildPanel();
    void restorePanelMode();
    void ensureModel();
    void showDriveList();
    void navigateToHomeFolder();
    void navigateToCurrentFileFolder();
    void restoreExpandedState();
    void rememberExpansion(const QString& path, bool expanded);
    void refreshHeaderLabel();
    void updateHeaderActions();
    QModelIndex sourceIndexForView(const QModelIndex& viewIndex) const;
    QModelIndex viewIndexForSource(const QModelIndex& sourceIndex) const;

    // Resolves the directory that New/Paste should target: the selected
    // folder, the parent of the selected file, or the workspace root if
    // nothing is selected.
    QString targetDirForNewItem() const;

    void showContextMenu(const QPoint& globalPos, const QModelIndex& index);
    void beginRenameSelected();
    void duplicateSelected();
    void deleteSelected();
    void copySelected(bool cut);
    void pasteIntoTarget(const QString& targetDir);
    void createNewFile();
    void createNewTextFile();
    void createNewFolder();
    void beginInlineEditFor(const QString& path, int retriesLeft = 10);
    void revealPath(const QString& path, int retriesLeft = 50);
    void openInExplorer(const QString& path);
    void onItemRenamed(const QString& oldPath, const QString& newPath);
    QStringList selectedPaths() const;

    // Coalesces the column-layout persistence signal: a single header drag
    // emits sectionResized for every pixel, and each emission would otherwise
    // trigger a full settings write.
    void scheduleColumnLayoutPersist();
    void updateSortUi();

    // Restores the fixed default Name-column width until the user resizes it.
    void rebalanceNameColumn();

    // Outline section (see the public API above for the rationale).
    QWidget* buildOutlineSection();
    void applyOutlineLayout();
    void updateOutlineHeaderIcon();
    void refreshOutlineFromEditor();
    void captureOutlineReferenceRatio();
    bool hasValidOutlineGeometry() const;
    void captureOutlineSplitterState();
    void applyOutlineReferenceRatio();
    void scheduleOutlineReferenceLayout();

    // Search. The recursive walk runs on a QtConcurrent worker: it only stops at
    // kSearchResultCap *matches*, so a query that matches nothing walks the whole
    // tree, and on a large root that froze the UI. The worker produces plain
    // paths; icons and every model mutation stay on the GUI thread.
    struct SearchHit {
        QString relativePath;
        QString absolutePath;
        bool isDirectory = false;
    };
    void scheduleSearch();
    void runSearch();
    void applySearchResults(quint64 generation, const QList<SearchHit>& hits);
    void clearSearch();
    void setSearchModeActive(bool active);
    void clearSearchSelection();
    bool handleSearchKey(QKeyEvent* event);
    bool isKeyboardSearchResult(const QModelIndex& index) const;
    void activateSearchResult(const QModelIndex& index);

    // Drag & drop from Explorer
    void importExternalPaths(const QStringList& sourcePaths, const QString& destDir);
    void copyPathsAsync(const QStringList& sourcePaths, const QString& destDir,
                        const QString& failureTitle);
    // Surfaces items a copy/move/paste could not process (permissions, a
    // vanished source, a read-only destination) instead of failing silently.
    void reportFailedTransfers(const QString& title, const QStringList& failedNames);

    QWidget*     m_workspacePanel  = nullptr;
    WorkspaceCardBackdrop* m_cardBackdrop = nullptr;
    QVBoxLayout* m_panelLayout = nullptr;
    QWidget*     m_headerBar       = nullptr;
    QLabel*      m_headerLabel     = nullptr;
    QToolButton* m_modeToggle      = nullptr;
    QStackedWidget* m_bodyStack    = nullptr;
    QWidget*     m_workspaceBody   = nullptr;
    SourceControlPanel* m_sourceControlPanel = nullptr;
    WorkspaceSearchPanel* m_searchPanel = nullptr;
    QToolButton* m_searchModeButton = nullptr;
    PanelMode    m_panelMode       = PanelMode::Workspace;
    bool         m_panelModeRestored = false;
    QToolBar*    m_toolbar         = nullptr;
    QWidget*     m_searchRow       = nullptr;
    QLineEdit*   m_searchBox       = nullptr;
    QTimer*      m_searchDebounce  = nullptr;
    // Results from a superseded generation are discarded; the flag lets the
    // in-flight walk abandon its traversal instead of finishing it.
    quint64      m_searchGeneration = 0;
    bool         m_searchResultsCurrent = false;
    bool         m_searchInputComposing = false;
    std::shared_ptr<QAtomicInt> m_searchCancel;
    QTimer*      m_columnPersistTimer = nullptr;
    QTimer*      m_treeRefreshTimer   = nullptr;
    // Running only between materializeAfterFirstPaint() and the tree build it
    // put off; updateRoot() waits for it, and ensureModel() ends it.
    QTimer*      m_deferredTreeTimer  = nullptr;
    WorkspaceTreeView* m_workspaceTree = nullptr;
    QListView*         m_searchListView = nullptr;
    QStackedWidget*    m_contentStack = nullptr;
    QStandardItemModel* m_searchModel = nullptr;
    QLabel*            m_searchEmptyLabel = nullptr;
    QFileSystemModel*         m_workspaceModel = nullptr;
    WorkspaceTreeFilterModel* m_filterProxy    = nullptr;
    WorkspaceNavigationModel* m_navigationModel = nullptr;
    WorkspaceRenameDelegate*  m_renameDelegate = nullptr;
    NativeTreeContainer* m_treeContainer = nullptr;
    RecentFoldersManager* m_recentFoldersMgr = nullptr;
    AlignedMenu* m_newMenu = nullptr;

    // Outline section: a vertical splitter below the tree/search stack, its
    // second pane a collapsible header + heading tree.
    ThemedSplitter* m_workspaceSplitter = nullptr;
    QWidget*     m_outlineSection       = nullptr;
    QToolButton* m_outlineHeaderButton  = nullptr;
    QTreeWidget* m_outlineTree          = nullptr;
    MarkdownEditor* m_outlineEditor     = nullptr;
    bool         m_outlineExpanded      = false;
    QByteArray   m_savedOutlineSplitterState;
    // Only a laid-out, visible, expanded Workspace may replace the cached
    // state. Hidden stack pages and the collapsed header are never persisted.
    bool         m_outlineRestorePending = true;
    QPointer<QWidget> m_outlineReferenceWidget;
    std::optional<double> m_outlineReferenceRatio;
    bool         m_outlineReferenceLayoutPending = false;

    QToolButton* m_showAllButton = nullptr;
    QToolButton* m_sortButton = nullptr;
    QToolButton* m_currentFileFolderButton = nullptr;
    QToolButton* m_homeFolderButton = nullptr;
    QToolButton* m_newSplitButton = nullptr;
    QToolButton* m_openFolderSplitButton = nullptr;
    QToolButton* m_expandButton = nullptr;
    QToolButton* m_collapseButton = nullptr;
    QToolButton* m_copyFolderPathButton = nullptr;
    QToolButton* m_revealFolderButton = nullptr;
    QToolButton* m_terminalButton = nullptr;
    QLabel*      m_searchIconLabel    = nullptr;
    QToolButton* m_searchClearButton  = nullptr;

    std::function<QString()> m_currentFilePathProvider;
    std::function<bool()> m_darkProvider;
    std::function<QString()> m_searchSeedProvider;

    QString     m_workspaceRootPath;
    QHash<QString, QString> m_normalizedRootCache;
    QStringList m_workspaceExpandedPaths;
    QString     m_pendingRevealPath;
    bool        m_workspaceTreeVisible = true;
    bool        m_uiBuilt = false;
    bool        m_workspaceInteractionActive = false;
    bool        m_showAll = false;
    bool        m_showingDriveList = false;
    bool        m_columnsInitialized = false;
    bool        m_nameColumnUserResized = false;
    bool        m_adjustingColumns = false;
    QByteArray  m_savedColumnLayoutState;
    bool        m_dateModifiedColumnVisible = false;
    bool        m_sizeColumnVisible = false;
    bool        m_fileTypeColumnVisible = false;
    // QFileSystemModel column 2 is the file type. Keep this aligned with the
    // session-restoration fallback for installations without saved settings.
    int         m_sortColumn = 2;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    // Cut state is local because Qt's generic URL clipboard format has no
    // portable cut/move marker. Copies are also published as file URLs, so
    // they can be pasted into Explorer and files copied there can be pasted
    // into the workspace.
    QStringList m_clipboardPaths;
    bool        m_clipboardIsCut = false;
    bool        m_settingSystemClipboard = false;
    bool        m_transferRunning = false;
    std::shared_ptr<WorkspaceFileOps::WorkspaceCopyCancellation> m_transferCancel;

    // Hover-only "Reveal in Explorer" affordance (spec: appears only while
    // hovering a row, to avoid visual clutter).
    QToolButton* m_hoverRevealButton = nullptr;
    QString      m_hoverPath;
    bool         m_hoverRowSelected = false;
    // Color the button's icon is currently rendered in, so re-tinting can be
    // skipped when nothing about the row's foreground changed.
    QColor       m_hoverRevealIconColor;
};
