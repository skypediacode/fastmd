#include "WorkspacePanelController.h"
#include "WorkspaceFileOps.h"
#include "WorkspaceTreeView.h"
#include "AppShutdown.h"

#include <QDir>
#include <QDirIterator>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QFileSystemModel>
#include <QHash>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QtConcurrent>
#include <QLineEdit>
#include <QListView>
#include <QSignalBlocker>
#include <QSize>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>

namespace
{
    constexpr int kSearchResultCap = 500;
    constexpr int kSearchPathRole = Qt::UserRole + 1;
    constexpr int kSearchDirectoryRole = Qt::UserRole + 2;
}

void WorkspacePanelController::clearSearchSelection()
{
    if (m_searchListView && m_searchListView->selectionModel())
        m_searchListView->selectionModel()->clear();
}

bool WorkspacePanelController::isKeyboardSearchResult(const QModelIndex& index) const
{
    // Use the worker's metadata: navigation must not stat files on network
    // roots. The shared activation path checks existence immediately before use.
    return index.isValid() && index.model() == m_searchModel
        && (index.flags() & Qt::ItemIsEnabled) && (index.flags() & Qt::ItemIsSelectable)
        && !index.data(kSearchPathRole).toString().isEmpty()
        && index.data(kSearchDirectoryRole).isValid()
        && !index.data(kSearchDirectoryRole).toBool();
}

bool WorkspacePanelController::handleSearchKey(QKeyEvent* event)
{
    // Keypad Enter carries KeypadModifier, which is not a text-editing modifier.
    if ((event->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier
        || !m_searchListView || !m_searchModel || m_searchBox->text().isEmpty())
        return false;

    const int key = event->key();
    const auto* selection = m_searchListView->selectionModel();
    const QModelIndex current = selection->currentIndex();
    const bool selected = current.isValid() && selection->isSelected(current);
    if (key == Qt::Key_Escape) {
        if (!selected)
            return false;
        if (event->type() == QEvent::KeyPress)
            clearSearchSelection();
    } else if (key == Qt::Key_Down || key == Qt::Key_Up
               || key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (event->type() == QEvent::KeyPress && m_searchResultsCurrent) {
            const bool activate = key == Qt::Key_Return || key == Qt::Key_Enter;
            if (activate && selected) {
                if (isKeyboardSearchResult(current))
                    activateSearchResult(current);
            } else {
                const int step = key == Qt::Key_Up ? -1 : 1;
                int row = selected && !activate ? current.row() + step
                    : (step > 0 ? 0 : m_searchModel->rowCount() - 1);
                for (; row >= 0 && row < m_searchModel->rowCount(); row += step) {
                    const QModelIndex index = m_searchModel->index(row, 0);
                    if (!isKeyboardSearchResult(index))
                        continue;
                    if (activate) {
                        activateSearchResult(index);
                    } else {
                        m_searchListView->selectionModel()->setCurrentIndex(
                            index, QItemSelectionModel::ClearAndSelect);
                        m_searchListView->scrollTo(index, QAbstractItemView::EnsureVisible);
                    }
                    break;
                }
            }
        }
    } else {
        return false;
    }
    event->accept();
    return true;
}

void WorkspacePanelController::activateSearchResult(const QModelIndex& index)
{
    if (!m_searchResultsCurrent || !index.isValid() || index.model() != m_searchModel
        || !(index.flags() & Qt::ItemIsEnabled))
        return;
    const QString path = index.data(kSearchPathRole).toString();
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    if (info.isDir()) {
        // A file replaced by a directory after the search is no longer a file
        // activation target. Only actual folder results reveal the tree.
        if (!index.data(kSearchDirectoryRole).toBool())
            return;
        // Folder activation remains the existing mouse/list behavior.
        clearSearch();
        if (!m_workspaceModel)
            return;
        const QModelIndex srcIndex = m_workspaceModel->index(path);
        if (srcIndex.isValid()) {
            const QModelIndex proxyIndex = viewIndexForSource(srcIndex);
            if (proxyIndex.isValid()) {
                for (QModelIndex ancestor = proxyIndex.parent(); ancestor.isValid(); ancestor = ancestor.parent())
                    m_workspaceTree->expand(ancestor);
                m_workspaceTree->setCurrentIndex(proxyIndex);
                m_workspaceTree->scrollTo(proxyIndex);
            }
        }
    } else if (info.isFile()) {
        emit fileActivated(path);
    }
}

void WorkspacePanelController::setSearchModeActive(bool active)
{
    if (!m_contentStack)
        return;
    m_contentStack->setCurrentIndex(active ? 1 : 0);
}

void WorkspacePanelController::scheduleSearch()
{
    // Invalidate immediately, not when the debounce expires: an older worker
    // can finish while the input already contains the next query.
    if (m_searchCancel)
        m_searchCancel->storeRelaxed(1);
    m_searchCancel.reset();
    ++m_searchGeneration;
    m_searchResultsCurrent = false;
    clearSearchSelection();
    if (m_searchDebounce)
        m_searchDebounce->start();
}

void WorkspacePanelController::runSearch()
{
    if (!m_searchModel || !m_searchBox)
        return;

    // Abandon whatever the previous keystroke started before beginning a new one.
    if (m_searchCancel)
        m_searchCancel->storeRelaxed(1);
    m_searchCancel.reset();

    m_searchModel->clear();
    m_searchResultsCurrent = false;
    const QString query = m_searchBox->text().trimmed();
    const quint64 generation = ++m_searchGeneration;
    if (query.isEmpty() || m_workspaceRootPath.isEmpty())
        return;

    auto cancel = std::make_shared<QAtomicInt>(0);
    m_searchCancel = cancel;
    const QString root = m_workspaceRootPath;
    const bool showAll = m_showAll;

    auto* watcher = new QFutureWatcher<QList<SearchHit>>(this);
    connect(watcher, &QFutureWatcher<QList<SearchHit>>::finished, this,
            [this, watcher, generation]() {
                const QList<SearchHit> hits = watcher->result();
                watcher->deleteLater();
                applySearchResults(generation, hits);
            });
    watcher->setFuture(QtConcurrent::run([root, query, showAll, cancel]() {
        QList<SearchHit> hits;
        // QDirIterator builds its paths from the root we pass in, so the relative
        // path is a prefix strip -- no per-entry QDir::relativeFilePath() needed.
        const qsizetype rootPrefixLength = root.endsWith(QLatin1Char('/'))
                                               ? root.size()
                                               : root.size() + 1;
        QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        int scanned = 0;
        while (it.hasNext() && hits.size() < kSearchResultCap) {
            it.next();
            // Checked in batches: the load is cheap but not free, and a
            // superseded walk only has to stop promptly, not instantly.
            if ((++scanned & 0xff) == 0
                && (cancel->loadRelaxed() || AppShutdown::started())) {
                // AppShutdown as well as the per-keystroke token: nothing sets
                // the token on quit, and this walk is unbounded, so without it
                // a search over a large or network tree keeps a pool thread
                // busy past main() and hangs the exiting process inside
                // QThreadPool's waitForDone().
                return QList<SearchHit>();
            }

            const bool isDir = it.fileInfo().isDir();
            if (!isDir && !showAll && !WorkspaceFileOps::isVisibleFile(it.fileName()))
                continue;

            const QString absolutePath = it.filePath();
            const QString relPath = absolutePath.size() > rootPrefixLength
                                        ? absolutePath.mid(rootPrefixLength)
                                        : absolutePath;
            if (!relPath.contains(query, Qt::CaseInsensitive))
                continue;

            hits.append({ relPath, absolutePath, isDir });
        }
        return hits;
    }));
}

void WorkspacePanelController::applySearchResults(quint64 generation, const QList<SearchHit>& hits)
{
    if (generation != m_searchGeneration || !m_searchModel)
        return;

    // QListView can retain a native icon's larger size hint even when its
    // iconSize is smaller.  Use the visible Workspace tree's measured row
    // height, which already includes its active style, font, padding, and
    // DPI metrics, to keep the flat results visually identical.
    const int workspaceRowHeight = m_workspaceTree
        ? m_workspaceTree->sizeHintForRow(0)
        : -1;

    // Native icons are resolved through a plain provider rather than
    // m_workspaceModel->index(path): asking the QFileSystemModel for an index
    // forces it to materialize (and start watching) a node for every directory
    // on the way to every hit, which is far more expensive than the search
    // itself and pollutes the model with folders the user never opened.
    static QFileIconProvider iconProvider;
    // One shell lookup per distinct suffix instead of one per hit (up to 500).
    QHash<QString, QIcon> iconBySuffix;
    QIcon folderIcon;

    m_searchModel->clear();
    for (const SearchHit& hit : hits) {
        auto* item = new QStandardItem(hit.relativePath);
        item->setData(hit.absolutePath, kSearchPathRole);
        item->setData(hit.isDirectory, kSearchDirectoryRole);
        if (hit.isDirectory) {
            if (folderIcon.isNull())
                folderIcon = iconProvider.icon(QFileIconProvider::Folder);
            item->setIcon(folderIcon);
        } else {
            const QString suffix = QFileInfo(hit.absolutePath).suffix().toLower();
            auto cached = iconBySuffix.constFind(suffix);
            if (cached == iconBySuffix.constEnd())
                cached = iconBySuffix.insert(suffix, iconProvider.icon(QFileInfo(hit.absolutePath)));
            item->setIcon(cached.value());
        }
        item->setEditable(false);
        if (workspaceRowHeight > 0)
            item->setSizeHint(QSize(0, workspaceRowHeight));
        m_searchModel->appendRow(item);
    }

    clearSearchSelection();
    m_searchResultsCurrent = true;
    if (m_contentStack)
        m_contentStack->setCurrentIndex(hits.isEmpty() ? 2 : 1);
}

void WorkspacePanelController::clearSearch()
{
    // Clearing via Escape uses a signal blocker, so no empty-query runSearch()
    // follows to invalidate an in-flight worker. Cancel and advance the
    // generation here to prevent stale results from reopening search mode.
    if (m_searchCancel)
        m_searchCancel->storeRelaxed(1);
    m_searchCancel.reset();
    ++m_searchGeneration;
    m_searchResultsCurrent = false;
    if (m_searchClearButton)
        m_searchClearButton->setVisible(false);
    if (m_searchBox && !m_searchBox->text().isEmpty())
    {
        const QSignalBlocker blocker(m_searchBox);
        m_searchBox->clear();
    }
    if (m_searchDebounce)
        m_searchDebounce->stop();
    if (m_searchModel)
        m_searchModel->clear();
    setSearchModeActive(false);
}
