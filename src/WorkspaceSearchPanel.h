#pragma once

#include "WorkspaceTextSearch.h"

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>

class QAtomicInt;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStandardItem;
class QStandardItemModel;
class QTimer;
class QToolButton;
class QModelIndex;
class SearchResultsView;
class SearchResultDelegate;

// "Search in Files": the third body of the Workspace panel (beside Explorer and
// Source Control). It owns the query/replace controls and the grouped result
// tree; the matching itself is WorkspaceTextSearch. It knows nothing about
// MainWindow -- open editors arrive through providers and a click leaves as
// openMatchRequested(), the same shape SourceControlPanel uses.
class WorkspaceSearchPanel : public QWidget {
    Q_OBJECT
public:
    struct OpenEditor {
        QString path;
        QPlainTextEdit* editor = nullptr;
    };
    // Every editable, path-bearing document open in any window.
    using OpenEditorsProvider = std::function<QList<OpenEditor>()>;
    // Called after the panel edited an open document, so its owner can do the
    // dirty/revision bookkeeping an edit in a background tab does not get.
    using EditedHook = std::function<void(QPlainTextEdit*)>;

    struct Target {
        int line = 0;
        int column = 0;
        int length = 0;
    };
    struct ReplaceOutcome {
        int replaced = 0;
        int skipped = 0;
        int failedFiles = 0;
        int files = 0;
        void add(const ReplaceOutcome& o)
        {
            replaced += o.replaced;
            skipped += o.skipped;
            failedFiles += o.failedFiles;
            files += o.files;
        }
    };

    explicit WorkspaceSearchPanel(QWidget* parent = nullptr);
    ~WorkspaceSearchPanel() override;

    void setOpenEditorsProvider(OpenEditorsProvider provider) { m_openEditors = std::move(provider); }
    void setEditedHook(EditedHook hook) { m_editedHook = std::move(hook); }

    // A different root drops the old results; the typed query and the option
    // toggles stay, so the same search can be repeated in the new workspace.
    void setRootPath(const QString& root);
    void setDarkMode(bool dark);

    // Ctrl+Shift+F / Ctrl+Shift+H. Neither rebuilds nor resets anything;
    // focusing never collapses an expanded Replace row.
    void focusQuery();
    // Replaces the query; an unchanged query is not re-set, so it does not re-run the search.
    void setQueryText(const QString& text);
    QString queryText() const;
    void setReplaceExpanded(bool expanded);
    bool replaceExpanded() const { return m_replaceExpanded; }

    // An open document was edited: refresh just that file's results.
    void notifyDocumentEdited(const QString& path);

signals:
    void openMatchRequested(const QString& path, int line, int column, int length);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    friend class SearchResultsView;

    void buildUi();
    void applyFixedMetrics();
    void updateIcons();
    void updateFocusState();
    void updateInvalidState(const QString& error);
    void updateActionStates();
    void updateReplacePreview();

    using Query = WorkspaceTextSearch::Query;
    Query currentQuery() const;
    void scheduleSearch();
    void startSearch();
    void cancelSearch();
    void clearResults();
    void appendBatch(quint64 generation, const QList<WorkspaceTextSearch::FileResult>& batch);
    void finishSearch(quint64 generation, const WorkspaceTextSearch::RunSummary& summary);
    void setStatus(const QString& text);
    void updateSummaryStatus();

    QStandardItem* addFileGroup(const WorkspaceTextSearch::FileResult& result);
    QStandardItem* findFileItem(const QString& path) const;
    void refreshFile(const QString& path);
    void refreshDirtyFiles();
    QPlainTextEdit* editorFor(const QString& path) const;
    bool currentLineText(const QString& path, int line, QString* text) const;

    void activateIndex(const QModelIndex& index);
    void replaceAtIndex(const QModelIndex& index);
    void replaceAllInWorkspace();
    ReplaceOutcome replaceInEditor(QPlainTextEdit* editor, const QList<Target>& targets);
    ReplaceOutcome replaceOneMatch(const QString& path, const Target& target);
    ReplaceOutcome replaceAllInFile(const QString& path);
    // The executed query plus the live Preserve Case state: it only shapes the
    // replacement text, so it is read at replace time like the Replace field.
    Query replaceQuery() const;
    void showContextMenu(const QPoint& viewportPos);
    void reportOutcome(const ReplaceOutcome& outcome);

    OpenEditorsProvider m_openEditors;
    EditedHook m_editedHook;

    QToolButton* m_chevronButton = nullptr;
    QWidget* m_queryRow = nullptr;
    QWidget* m_replaceRow = nullptr;
    QLineEdit* m_queryEdit = nullptr;
    QLineEdit* m_replaceEdit = nullptr;
    QToolButton* m_queryClearButton = nullptr;
    QToolButton* m_matchCaseButton = nullptr;
    QToolButton* m_wholeWordButton = nullptr;
    QToolButton* m_regexButton = nullptr;
    QToolButton* m_preserveCaseButton = nullptr;
    QToolButton* m_replaceAllButton = nullptr;
    QLabel* m_statusLabel = nullptr;
    QGridLayout* m_grid = nullptr;
    QList<QWidget*> m_separators;
    SearchResultsView* m_results = nullptr;
    SearchResultDelegate* m_delegate = nullptr;
    QStandardItemModel* m_model = nullptr;
    QTimer* m_searchDebounce = nullptr;
    QTimer* m_refreshDebounce = nullptr;

    QString m_root;
    bool m_dark = false;
    bool m_replaceExpanded = false;

    // What the visible results answer. Replacement always uses this, never the
    // text currently typed into the field, so Replace cannot silently apply to a
    // different query than the one on screen.
    Query m_executed;
    QRegularExpression m_executedRegex;
    bool m_hasResults = false;

    quint64 m_generation = 0;
    std::shared_ptr<QAtomicInt> m_cancel;
    QFutureWatcher<WorkspaceTextSearch::RunSummary> m_watcher;
    // Only one worker ever runs: a newer query cancels it and starts once it has
    // returned, so the destructor has a single future to wait for.
    bool m_running = false;
    bool m_restartPending = false;
    QFutureWatcher<ReplaceOutcome> m_replaceWatcher;
    bool m_replacing = false;
    ReplaceOutcome m_uiOutcome;  // open-editor half of a running Replace All
    bool m_capped = false;
    int m_skipped = 0;
    QString m_error;
    QString m_transientNote;
    // A Replace All re-runs the search to show what is left; its summary must
    // outlive that one run instead of vanishing with the results it describes.
    bool m_keepNoteOnce = false;

    QSet<QString> m_collapsed;
    QSet<QString> m_dirtyPaths;
};
