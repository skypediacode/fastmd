#pragma once

#include <QSortFilterProxyModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QDateTime>
#include <memory>

class QAtomicInt;

class QFileSystemModel;

// Sits between the QFileSystemModel and the tree/header view. Directories
// always pass through; files are filtered by WorkspaceFileOps::visibleFileExtensions()
// unless showAll() is true. Kept as its own class (rather than
// QFileSystemModel::setNameFilters()) so the tree can keep directories
// visible while filtering files, and so search can reuse the same predicate.
class WorkspaceTreeFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit WorkspaceTreeFilterModel(QObject* parent = nullptr);

    void setSourceFsModel(QFileSystemModel* model);

    bool showAll() const { return m_showAll; }
    void setShowAll(bool showAll);
    bool hasVisibleDescendants(const QModelIndex& proxyParent) const;

signals:
    // A deferred descendant probe finished and changed its cached answer, so the
    // branch decorations painted from it are now wrong and need a repaint.
    void branchVisibilityChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    // Directories always sort before files, regardless of sort column/order
    // (matches Explorer), falling back to the base string/numeric compare
    // for same-kind rows.
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

private:
    // Queues the recursive walk for `path` on a worker instead of
    // running it here: hasVisibleDescendants() is reachable from
    // WorkspaceTreeView::drawBranches(), i.e. from inside a paint event.
    void scheduleVisibilityProbe(const QString& path) const;
    void startNextVisibilityProbe();
    static bool scanForVisibleDescendant(const QString& path, QAtomicInt* cancel);
    // Drops the cached answer for `path` and its ancestors only. A change
    // inside a directory can flip the answer for that directory and every
    // folder above it, but never for its descendants, so clearing the whole
    // cache (and re-scanning entire trees afterwards) is unnecessary.
    void invalidateVisibilityCacheFor(const QString& path);
    struct FileMetadata {
        bool isDirectory = false;
        QString name;
        QString type;
        QDateTime modified;
        qint64 size = 0;
    };
    const FileMetadata& metadataFor(const QModelIndex& sourceIndex) const;
    static QString friendlyFileType(const QString& suffix, bool isDirectory);
    static QString displaySize(qint64 bytes);
    static int naturalCompare(const QString& left, const QString& right);
    void invalidateMetadataFor(const QString& path);

    QFileSystemModel* m_fsModel = nullptr;
    bool m_showAll = false;
    mutable QHash<QString, bool> m_visibleDescendantCache;
    mutable QSet<QString> m_pendingVisibilityProbes;
    mutable QStringList m_visibilityProbeQueue;
    bool m_visibilityProbeRunning = false;
    // Lets an in-flight recursive walk be abandoned when its answer can no
    // longer matter (the tree changed underneath it, or we are quitting).
    std::shared_ptr<QAtomicInt> m_visibilityProbeCancel;
    quint64 m_visibilityProbeGeneration = 0;
    mutable QHash<QString, FileMetadata> m_metadataCache;
};
