#include "WorkspaceTreeFilterModel.h"
#include "WorkspaceFileOps.h"
#include "AppShutdown.h"

#include <QFileSystemModel>
#include <QDir>
#include <QDirIterator>
#include <QAtomicInt>
#include <QFutureWatcher>
#include <QFileInfo>
#include <QLocale>
#include <QCollator>
#include <QtConcurrent>

WorkspaceTreeFilterModel::WorkspaceTreeFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void WorkspaceTreeFilterModel::setSourceFsModel(QFileSystemModel* model)
{
    m_fsModel = model;
    setSourceModel(model);

    if (!m_fsModel)
        return;

    connect(m_fsModel, &QFileSystemModel::directoryLoaded, this,
            [this](const QString& path) {
        invalidateVisibilityCacheFor(path);
        invalidateMetadataFor(path);
    });
    connect(m_fsModel, &QFileSystemModel::fileRenamed, this,
            [this](const QString& path, const QString&, const QString&) {
        invalidateVisibilityCacheFor(path);
        invalidateMetadataFor(path);
    });
    connect(m_fsModel, &QFileSystemModel::rowsInserted, this,
            [this](const QModelIndex& parent, int, int) {
        invalidateVisibilityCacheFor(m_fsModel->filePath(parent));
        invalidateMetadataFor(m_fsModel->filePath(parent));
    });
    connect(m_fsModel, &QFileSystemModel::rowsRemoved, this,
            [this](const QModelIndex& parent, int, int) {
        invalidateVisibilityCacheFor(m_fsModel->filePath(parent));
        invalidateMetadataFor(m_fsModel->filePath(parent));
    });
    connect(m_fsModel, &QFileSystemModel::dataChanged, this,
            [this](const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>&) {
        for (int row = topLeft.row(); row <= bottomRight.row(); ++row)
            invalidateMetadataFor(m_fsModel->filePath(topLeft.sibling(row, 0)));
    });
}

void WorkspaceTreeFilterModel::setShowAll(bool showAll)
{
    if (m_showAll == showAll)
        return;
    beginFilterChange();
    m_showAll = showAll;
    m_visibleDescendantCache.clear();
    m_pendingVisibilityProbes.clear();
    m_visibilityProbeQueue.clear();
    ++m_visibilityProbeGeneration;
    // The generation bump already discards a stale answer, but the worker
    // producing it can still be minutes from finishing. Tell it to stop so it
    // stops holding a pool thread as well.
    if (m_visibilityProbeCancel)
        m_visibilityProbeCancel->storeRelaxed(1);
    m_visibilityProbeCancel.reset();
    endFilterChange();
}

bool WorkspaceTreeFilterModel::hasVisibleDescendants(const QModelIndex& proxyParent) const
{
    if (!proxyParent.isValid() || !m_fsModel)
        return false;

    const QModelIndex sourceParent = mapToSource(proxyParent);
    if (!sourceParent.isValid() || !m_fsModel->isDir(sourceParent))
        return false;

    if (m_showAll)
        return m_fsModel->rowCount(sourceParent) > 0 ||
               m_fsModel->hasChildren(sourceParent) ||
               m_fsModel->canFetchMore(sourceParent);

    const QString path = m_fsModel->filePath(sourceParent);
    if (path.isEmpty())
        return false;
    const auto cached = m_visibleDescendantCache.constFind(path);
    if (cached != m_visibleDescendantCache.constEnd())
        return cached.value();

    // Cache miss. This is reachable from a paint event, so the recursive walk is
    // queued instead of run here, and the answer for this frame is
    // QFileSystemModel's own "does it have any children at all", which costs no
    // filesystem access. branchVisibilityChanged() repaints once the real answer
    // disagrees.
    scheduleVisibilityProbe(path);
    return m_fsModel->hasChildren(sourceParent);
}

bool WorkspaceTreeFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const FileMetadata& leftMeta = metadataFor(left.siblingAtColumn(0));
    const FileMetadata& rightMeta = metadataFor(right.siblingAtColumn(0));
    if (leftMeta.isDirectory != rightMeta.isDirectory)
        return sortOrder() == Qt::AscendingOrder
            ? leftMeta.isDirectory : rightMeta.isDirectory;

    int comparison = 0;
    switch (left.column()) {
    case 1:
        comparison = leftMeta.size == rightMeta.size ? 0 : (leftMeta.size < rightMeta.size ? -1 : 1);
        break;
    case 2:
        comparison = naturalCompare(leftMeta.type, rightMeta.type);
        break;
    case 3:
        comparison = leftMeta.modified == rightMeta.modified ? 0 : (leftMeta.modified < rightMeta.modified ? -1 : 1);
        break;
    default:
        comparison = naturalCompare(leftMeta.name, rightMeta.name);
        break;
    }
    return comparison == 0 ? naturalCompare(leftMeta.name, rightMeta.name) < 0 : comparison < 0;
}

QVariant WorkspaceTreeFilterModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || role != Qt::DisplayRole)
        return QSortFilterProxyModel::data(index, role);

    const FileMetadata& meta = metadataFor(mapToSource(index).siblingAtColumn(0));
    switch (index.column()) {
    case 1: return meta.isDirectory ? QVariant() : displaySize(meta.size);
    case 2: return meta.type;
    case 3: return meta.modified.isValid()
        ? QLocale().toString(meta.modified, QLocale::ShortFormat) : QVariant();
    default: return QSortFilterProxyModel::data(index, role);
    }
}

const WorkspaceTreeFilterModel::FileMetadata& WorkspaceTreeFilterModel::metadataFor(const QModelIndex& sourceIndex) const
{
    static const FileMetadata empty;
    if (!m_fsModel || !sourceIndex.isValid())
        return empty;
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(m_fsModel->filePath(sourceIndex)));
    auto it = m_metadataCache.find(path);
    if (it != m_metadataCache.end())
        return it.value();

    const QFileInfo info(path);
    FileMetadata metadata;
    metadata.isDirectory = info.isDir();
    metadata.name = info.fileName();
    metadata.type = friendlyFileType(info.suffix(), metadata.isDirectory);
    metadata.modified = info.lastModified();
    metadata.size = metadata.isDirectory ? 0 : info.size();
    return m_metadataCache.insert(path, std::move(metadata)).value();
}

QString WorkspaceTreeFilterModel::friendlyFileType(const QString& suffix, bool isDirectory)
{
    if (isDirectory)
        return tr("Folder");
    const QString ext = suffix.toCaseFolded();
    static const QHash<QString, QString> types {
        { QStringLiteral("md"), tr("Markdown File") }, { QStringLiteral("markdown"), tr("Markdown File") },
        { QStringLiteral("txt"), tr("Text File") }, { QStringLiteral("json"), tr("JSON File") },
        { QStringLiteral("cpp"), tr("C++ Source File") }, { QStringLiteral("cxx"), tr("C++ Source File") },
        { QStringLiteral("cc"), tr("C++ Source File") }, { QStringLiteral("h"), tr("C++ Header File") },
        { QStringLiteral("hpp"), tr("C++ Header File") }, { QStringLiteral("py"), tr("Python File") },
        { QStringLiteral("js"), tr("JavaScript File") }, { QStringLiteral("ts"), tr("TypeScript File") },
        { QStringLiteral("html"), tr("HTML File") }, { QStringLiteral("htm"), tr("HTML File") },
        { QStringLiteral("css"), tr("CSS File") }, { QStringLiteral("xml"), tr("XML File") },
        { QStringLiteral("yml"), tr("YAML File") }, { QStringLiteral("yaml"), tr("YAML File") }
    };
    return types.value(ext, ext.isEmpty() ? tr("File") : ext.toUpper());
}

QString WorkspaceTreeFilterModel::displaySize(qint64 bytes)
{
    static const QStringList units { QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB"), QStringLiteral("TB") };
    double value = bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) { value /= 1024.0; ++unit; }
    return unit == 0 ? QString::number(bytes) + QLatin1Char(' ') + units.at(unit)
                     : QLocale().toString(value, 'f', 1) + QLatin1Char(' ') + units.at(unit);
}

int WorkspaceTreeFilterModel::naturalCompare(const QString& left, const QString& right)
{
    static const QCollator collator = [] {
        QCollator value;
        value.setCaseSensitivity(Qt::CaseInsensitive);
        value.setNumericMode(true);
        return value;
    }();
    return collator.compare(left, right);
}

bool WorkspaceTreeFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (m_showAll || !m_fsModel)
        return true;

    const QModelIndex idx = m_fsModel->index(sourceRow, 0, sourceParent);
    if (!idx.isValid())
        return true;
    if (m_fsModel->isDir(idx))
        return true;
    return WorkspaceFileOps::isVisibleFile(m_fsModel->filePath(idx));
}

void WorkspaceTreeFilterModel::scheduleVisibilityProbe(const QString& path) const
{
    if (m_pendingVisibilityProbes.contains(path))
        return;
    m_pendingVisibilityProbes.insert(path);
    m_visibilityProbeQueue.append(path);
    auto* self = const_cast<WorkspaceTreeFilterModel*>(this);
    self->startNextVisibilityProbe();
}

void WorkspaceTreeFilterModel::startNextVisibilityProbe()
{
    if (m_visibilityProbeRunning || m_visibilityProbeQueue.isEmpty())
        return;

    const QString path = m_visibilityProbeQueue.takeFirst();
    const quint64 generation = m_visibilityProbeGeneration;
    m_visibilityProbeRunning = true;

    // Only immutable path data enters the worker. QFileSystemModel and the
    // proxy caches remain confined to the GUI thread.
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, path, generation]() {
                const bool visible = watcher->result();
                watcher->deleteLater();
                m_visibilityProbeRunning = false;
                m_pendingVisibilityProbes.remove(path);

                if (generation == m_visibilityProbeGeneration) {
                    m_visibleDescendantCache.insert(path, visible);
                    // Only a disagreement with the provisional answer can have
                    // painted a wrong branch decoration.
                    bool repaint = true;
                    if (m_fsModel) {
                        const QModelIndex sourceIndex = m_fsModel->index(path);
                        repaint = !sourceIndex.isValid()
                            || m_fsModel->hasChildren(sourceIndex) != visible;
                    }
                    if (repaint)
                        emit branchVisibilityChanged();
                }
                startNextVisibilityProbe();
            });
    auto cancel = std::make_shared<QAtomicInt>(0);
    m_visibilityProbeCancel = cancel;
    watcher->setFuture(QtConcurrent::run([path, cancel]() {
        return scanForVisibleDescendant(path, cancel.get());
    }));
}

// Preserve the original exact answer now that the walk is off the paint/UI
// path. Arbitrary depth or directory-name limits make valid folders appear to
// have no expandable branch even though the proxy still exposes their children.
bool WorkspaceTreeFilterModel::scanForVisibleDescendant(const QString& path,
                                                        QAtomicInt* cancel)
{
    static const QStringList kFilters = WorkspaceFileOps::visibleFileNameFilters();

    // Same answer as
    //   QDirIterator(path, kFilters, QDir::Files, QDirIterator::Subdirectories).hasNext()
    // which is what this used to be, but interruptible. That single call does
    // the whole recursive descent inside hasNext(), so there is no point at
    // which it can be stopped -- and when the answer is "no" (a node_modules
    // tree, a photo library, a source checkout with no *.md in it) it has to
    // visit every directory below `path` before it can say so. Unbounded and
    // uninterruptible is what let one of these outlive the window and hang
    // process exit in QThreadPool's waitForDone().
    //
    // The traversal is deliberately identical in shape: files are matched with
    // the name filters, subdirectories are descended without QDir::Hidden (so
    // hidden trees are skipped, matching both QDirIterator's default and the
    // QFileSystemModel filter the tree itself uses) and without following
    // symlinks, so no cycle can trap the walk.
    QStringList pending { path };
    int scanned = 0;
    while (!pending.isEmpty()) {
        if ((cancel && cancel->loadRelaxed()) || AppShutdown::started())
            return false;

        const QString directory = pending.takeLast();

        QDirIterator files(directory, kFilters, QDir::Files);
        if (files.hasNext())
            return true;

        QDirIterator subdirectories(directory, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        while (subdirectories.hasNext()) {
            pending.append(subdirectories.next());
            // A very wide tree would otherwise only reach the cancel check
            // above once this directory is fully enumerated.
            if ((++scanned & 0xff) == 0
                && ((cancel && cancel->loadRelaxed()) || AppShutdown::started())) {
                return false;
            }
        }
    }
    return false;
}

void WorkspaceTreeFilterModel::invalidateVisibilityCacheFor(const QString& path)
{
    if (path.isEmpty())
        return;

    // A filesystem notification can arrive while a worker is scanning. Drop
    // queued work and invalidate the in-flight generation so an answer sampled
    // before the change can never repopulate the cache.
    m_pendingVisibilityProbes.clear();
    m_visibilityProbeQueue.clear();
    ++m_visibilityProbeGeneration;

    QString current = QDir::cleanPath(path);
    while (!current.isEmpty()) {
        m_visibleDescendantCache.remove(current);
        const qsizetype slash = current.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0)
            break;
        current.truncate(slash);
    }
}

void WorkspaceTreeFilterModel::invalidateMetadataFor(const QString& path)
{
    if (path.isEmpty())
        return;
    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    // dataChanged fires per row, and most rows are files. A file has no cached
    // descendants, so the linear startsWith() sweep below can be skipped entirely
    // whenever the cache itself tells us this path is not a directory.
    const auto cached = m_metadataCache.constFind(cleanPath);
    const bool knownFile = cached != m_metadataCache.constEnd() && !cached->isDirectory;
    m_metadataCache.remove(cleanPath);
    if (knownFile)
        return;
    const QString prefix = cleanPath + QLatin1Char('/');
    for (auto it = m_metadataCache.begin(); it != m_metadataCache.end(); )
        it = it.key().startsWith(prefix, Qt::CaseInsensitive) ? m_metadataCache.erase(it) : std::next(it);
}
