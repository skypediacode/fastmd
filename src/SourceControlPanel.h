#pragma once

#include "GitItemRoles.h"
#include "GitRunner.h"
#include "GitRepository.h"

#include <QFutureWatcher>
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QSet>

#include <optional>
#include <functional>
#include <QVector>
#include <QWidget>

class GitBranchButton;
class GitBranchPopup;
class GitFileDelegate;
class GitGraphDelegate;
class ThemedSplitter;

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Everything one refresh reads, gathered on a worker thread so a slow
// repository never blocks the UI.
struct GitSnapshot {
    bool                   valid = false;
    QString                root;
    QString                error;
    GitBranchInfo          branch;
    QVector<GitFileEntry>  files;
    QVector<GitCommit>     commits;
    QVector<GitRemote>     remotes;
    QVector<GitBranchRef>  branches;
    bool                   merging = false;
    bool                   moreCommits = false;
    bool                   gitMissing = false;
    int                    generation = 0;
};

// A reusable strip of small buttons parked over whichever row the mouse is
// on -- the same affordance the Workspace panel's hover Reveal button uses.
class GitRowActions final : public QWidget {
    Q_OBJECT
public:
    explicit GitRowActions(QWidget* parent = nullptr);

    struct Action {
        int     id = 0;
        QString tooltip;
        ushort  iconCode = 0;
    };

    // ids are opaque to this widget; the owner decides what they mean.
    void setActions(const QVector<Action>& actions, bool dark);

private:
    QVector<QToolButton*> m_buttons;
};

// The Source Control mode of the Workspace side panel: its commit/changes
// content and Graph stacked in one column. Constructed lazily by
// WorkspacePanelController on the first switch into this mode -- nothing here
// runs at startup. Diffs are not shown here; they open as document tabs in the
// main area (see MainWindow::openGitDiff).
class SourceControlPanel final : public QWidget {
    Q_OBJECT
public:
    explicit SourceControlPanel(QWidget* parent = nullptr);
    ~SourceControlPanel() override;

    void setRepositoryFromWorkspace(const QString& workspaceRoot);
    void setDarkMode(bool dark);
    // Called by MainWindow when the window is activated (returning from the
    // editor is exactly when the working tree is most likely to have moved),
    // by the panel's own F5 shortcut, and by its "Refresh" context-menu entry
    // (right-click the Changes list or the Graph) -- added so the F5 binding
    // has a visible, discoverable affordance instead of being a hidden key.
    void refresh();
    // Called after FastMD successfully saves a document. Avoid refreshing the
    // workspace's repository for a file saved elsewhere.
    void refreshForSavedPath(const QString& path);
    void focusCommitMessage();
    void persistState();

signals:
    void openFileRequested(const QString& absolutePath);
    // A pull, stash creation or discard can change files the user has open.
    void worktreeChanged();
    // A file or commit row was chosen: MainWindow opens it as a diff document.
    void diffRequested(const GitDiffSpec& spec);
    // One refresh finished, so open working-tree diff tabs may be stale.
    void refreshed();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum RowKind { GroupRow, FileRow, CommitRow, CommitFileRow };
    enum FileGroup { ConflictGroup, StagedGroup, UnstagedGroup };
    enum ActionId {
        ActOpenFile, ActStage, ActUnstage, ActDiscard, ActResolve,
        ActStageAll, ActUnstageAll, ActDiscardAll
    };
    // What the split commit button currently does -- VS Code collapses
    // Commit/Stage All+Commit and Sync Changes/Publish Branch into one
    // primary action rather than showing Pull/Push separately.
    enum class PrimaryGitAction { Commit, Sync, Publish };
    // Which network command is in flight, purely to pick the button's running
    // label/icon -- VS Code shows this inline on the button, not in a
    // separate progress area.
    enum class NetworkOpKind { Pull, Push, Sync, Publish };
    // How the Changes list orders files within each status group (Merge
    // Conflicts / Staged / Changes -- those groups themselves are not
    // affected by this). Persisted; see showFileListContextMenu().
    using ChangesSortMode = GitChangesSortMode;

    // --- construction (SourceControlPanel.cpp) ---
    void buildUi();
    QWidget* buildNoRepoPage();
    QWidget* buildNoGitPage();
    QWidget* buildSourceControlSection();
    void restoreState();

    // --- refresh pipeline (SourceControlPanel.cpp) ---
    void scheduleRefresh();
    void startRefresh();
    void applySnapshot(const GitSnapshot& snapshot);
    void populateFiles(const GitSnapshot& snapshot);
    void updateHeader(const GitSnapshot& snapshot);
    void updateActionStates();
    void showRepositoryMessage(const QString& message);
    void showNoRepositoryState(const QString& workspaceRoot);
    void showNoGitState();
    void applyRepositoryState();
    void initializeRepository();

    // --- local write commands (SourceControlPanel.cpp) ---
    void runLocal(const QStringList& args, const QByteArray& stdinData,
                  std::function<void()> onSuccess);
    void runLocalAt(const QString& root, const QStringList& args, const QByteArray& stdinData,
                    std::function<void()> onSuccess);
    void runCommit(const QString& message, bool thenPush);
    void stagePaths(const QStringList& paths);
    void unstagePaths(const QStringList& paths);
    void discardPaths(const QStringList& paths, bool confirm);
    void commitChanges(bool thenPush);

    // --- Source Control interactions (SourceControlPanel.cpp) ---
    void onFileSelectionChanged();
    void onFileActivated(QTreeWidgetItem* item, int column);
    void showFileDiff(const QString& path, bool staged);
    void showFileListContextMenu(const QPoint& pos);
    void setSortMode(ChangesSortMode mode);
    void handleRowAction(int id, QTreeWidgetItem* item);
    QVector<GitFileEntry> selectedFileEntries() const;
    QStringList rowActionPaths(GitFileAction action, QTreeWidgetItem* clicked) const;
    void updateHoverActions(QTreeWidget* tree, const QPoint& viewportPos);
    void updateRowActionPointerFeedback(const QPoint& viewportPos);
    void positionRowActions(GitRowActions* actions, QTreeWidget* tree, QTreeWidgetItem* item);
    QVector<GitRowActions::Action> hoverActionsFor(QTreeWidgetItem* item) const;
    void refreshHoverActionsAppearance();
    void hideHoverActions();
    void updateSelectedActions(QTreeWidgetItem* item);
    void hideSelectedActions();
    void updateInlineActionRows();

    // --- Graph + stash creation (SourceControlPanelHistory.cpp) ---
    QWidget* buildGraphSection();
    void populateHistory(const GitSnapshot& snapshot);
    // GitGraphDelegate is only a complete type in SourceControlPanelHistory.cpp.
    void applyGraphDarkMode(bool dark);
    void onCommitSelectionChanged();
    void onCommitExpanded(QTreeWidgetItem* item);
    void populateCommitFiles(QTreeWidgetItem* item, const QVector<GitFileEntry>& files,
                             const QString& root);
    void onGraphItemClicked(QTreeWidgetItem* item, int column);
    void onGraphItemDoubleClicked(QTreeWidgetItem* item, int column);
    void showCommitDiff(const QString& hash, const QString& parentHash, const QString& path);
    void showHistoryContextMenu(const QPoint& pos);
    void loadMoreCommits();
    // --- branch selector (SourceControlPanelHistory.cpp) ---
    void openBranchPopup();
    void checkoutBranch(const QString& name, bool remote);
    void stashCurrentChanges(bool includeUntracked = false);

    // --- network + remotes (SourceControlPanelSync.cpp) ---
    void pullFromUpstream(NetworkOpKind kind = NetworkOpKind::Pull);
    void pushToUpstream(NetworkOpKind kind = NetworkOpKind::Push);
    void syncChanges();
    // Publish Branch's fallback when the repository has no remote at all
    // (e.g. right after Initialize Repository): prompts for one, adds it,
    // and returns its name, or an empty string if the user cancelled or the
    // add failed. Deliberately minimal -- this is not the removed Manage
    // Remotes dialog, just enough for Publish Branch to have somewhere to push.
    void promptAddRemote(NetworkOpKind kind);
    void startNetworkOperation(NetworkOpKind kind, const QStringList& args, bool touchesWorktree);
    void finishNetworkOperation();
    void setBusy(bool busy);
    QString defaultRemote() const;
    // Label shown on the primary action button while a network op runs, and
    // the rotating sync-icon frame drawn there for the duration.
    static QString networkOperationLabel(NetworkOpKind kind);
    QPixmap rotatedSyncPixmap(int angleDeg) const;

    void setStatusMessage(const QString& message, bool isError = false);
    void updateStatusGeometry();
    void updateStatusCloseIcon();

    // A modal dialog or a context menu opened from a row action spins a
    // nested event loop, during which an in-flight refresh would otherwise
    // land and delete the very rows -- and replace the very snapshot -- the
    // action is still working from. While one of these is alive the finished
    // refresh is held back instead of applied, and released afterwards.
    class RefreshHold {
    public:
        explicit RefreshHold(SourceControlPanel* panel);
        ~RefreshHold();
        RefreshHold(const RefreshHold&) = delete;
        RefreshHold& operator=(const RefreshHold&) = delete;
    private:
        QPointer<SourceControlPanel> m_panel;
    };
    friend class RefreshHold;
    void releaseRefreshHold();

    // --- state ---
    QString       m_root;
    QString       m_lastCheckedFolder;
    GitSnapshot   m_snapshot;
    ChangesSortMode m_sortMode = ChangesSortMode::Path;
    bool          m_dark = false;
    int           m_generation = 0;
    int           m_commitLimit = 100;
    bool          m_refreshRunning = false;
    bool          m_moreCommitsRequested = false;
    bool          m_refreshDirty = false;
    int           m_refreshHold = 0;
    bool          m_busy = false;
    bool          m_localRunning = false;
    // A successful network operation leaves the button in its running state
    // until the refresh that observes its result has been applied. Otherwise
    // updateActionStates() would briefly render the old ahead/behind counts.
    bool          m_clearBusyAfterRefresh = false;
    bool          m_networkTouchesWorktree = false;
    // Set while a Sync Changes pull is in flight, so its completion chains
    // into the matching push instead of stopping halfway.
    bool          m_syncPendingPush = false;
    QString       m_selectedCommitHash;
    QString       m_selectedCommitParent;
    // Commit contents are immutable. Keeping the few commits the user expands
    // avoids re-running `git show` whenever a normal status refresh rebuilds
    // the graph tree.
    QHash<QString, QVector<GitFileEntry>> m_commitFilesCache;
    QSet<QString> m_commitFilesLoading;

    // Tri-state: unknown until the one-shot `git --version` probe returns.
    std::optional<bool>          m_gitAvailable;
    QFutureWatcher<bool>*        m_availabilityWatcher = nullptr;
    QFutureWatcher<GitSnapshot>* m_refreshWatcher = nullptr;
    QFutureWatcher<GitRunner::Result>* m_networkWatcher = nullptr;
    QFutureWatcher<GitRunner::Result>* m_localWatcher = nullptr;
    std::function<void()>       m_localSuccess;
    QString                     m_localRoot;
    bool                        m_localInitializing = false;
    // Published by the worker; cancelled on shutdown so a live git child is
    // never left behind (see ~SourceControlPanel). The handle's own mutex is
    // what makes that safe across the two threads.
    GitRunner::CancelHandle      m_networkCancel;
    NetworkOpKind                m_networkOpKind = NetworkOpKind::Push;
    QTimer*                      m_syncSpinTimer = nullptr;
    int                          m_syncSpinAngle = 0;

    // --- widgets ---
    QStackedWidget* m_stack = nullptr;
    QLabel*         m_emptyLabel = nullptr;
    QWidget*        m_noRepoPage = nullptr;
    QLabel*         m_noRepoText = nullptr;
    QPushButton*    m_btnInitRepo = nullptr;
    QWidget*        m_noGitPage = nullptr;
    QPushButton*    m_btnDownloadGit = nullptr;
    QWidget*        m_content = nullptr;

    // Lives in the Graph section header now (see buildGraphSection), not a
    // separate top header row.
    GitBranchButton* m_branchButton = nullptr;
    GitBranchPopup*  m_branchPopup = nullptr;
    // Qt dismisses a Qt::Popup before the anchor sees the outside click, so
    // without this the same click would close and immediately reopen it (the
    // status bar's language/EOL popups carry the same guard).
    bool             m_branchPopupDismissedByButton = false;
    QLabel*      m_mergeBanner = nullptr;

    ThemedSplitter* m_sidebarSplitter = nullptr;

    QPlainTextEdit* m_commitMessage = nullptr;
    QPushButton*    m_btnCommit = nullptr;
    QLabel*         m_syncLabel = nullptr;
    QToolButton*    m_btnCommitMenu = nullptr;
    PrimaryGitAction m_primaryAction = PrimaryGitAction::Commit;
    QTreeWidget*    m_fileList = nullptr;
    QTreeWidget*    m_historyList = nullptr;

    QWidget*       m_statusBar = nullptr;
    QScrollArea*   m_statusScroll = nullptr;
    QLabel*        m_statusLabel = nullptr;
    QToolButton*   m_statusClose = nullptr;
    // >0 once the user has dragged the status area's top edge; reset by the
    // next message.
    int            m_statusUserHeight = 0;

    // Held by concrete type: both delegates live in anonymous namespaces
    // in the .cpp files and have no Q_OBJECT, so qobject_cast is out.
    GitFileDelegate*  m_fileDelegate = nullptr;
    GitGraphDelegate* m_graphDelegate = nullptr;

    // Shown strips are reparented into the file list's viewport and are
    // destroyed with it, not with this panel.
    QPointer<GitRowActions> m_hoverActions;
    QPointer<QTreeWidget>   m_hoverTree;
    QTreeWidgetItem*        m_hoverItem = nullptr;
    QPointer<GitRowActions> m_selectedActions;
    QTreeWidgetItem*        m_selectedItem = nullptr;
};
