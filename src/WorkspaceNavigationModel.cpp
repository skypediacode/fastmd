#include "WorkspaceNavigationModel.h"

#include <QAbstractItemModel>
#include <QTimer>

#include <utility>

WorkspaceNavigationModel::WorkspaceNavigationModel(QObject* parent)
    : QAbstractProxyModel(parent)
{
}

void WorkspaceNavigationModel::setSourceModel(QAbstractItemModel* sourceModel)
{
    if (sourceModel == this->sourceModel())
        return;
    beginResetModel();
    QAbstractProxyModel::setSourceModel(sourceModel);
    m_workspaceRoot = {};
    m_workspaceAtModelRoot = false;
    endResetModel();

    if (!sourceModel)
        return;
    connect(sourceModel, &QAbstractItemModel::rowsAboutToBeInserted, this,
            [this](const QModelIndex& sourceParent, int first, int last) {
        const QModelIndex proxyParent = mapFromSource(sourceParent);
        const int offset = m_parentEntryEnabled && isNavigationRoot(proxyParent) ? 1 : 0;
        beginInsertRows(proxyParent, first + offset, last + offset);
    });
    connect(sourceModel, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex&, int, int) { endInsertRows(); });
    connect(sourceModel, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex& sourceParent, int first, int last) {
        const QModelIndex proxyParent = mapFromSource(sourceParent);
        const int offset = m_parentEntryEnabled && isNavigationRoot(proxyParent) ? 1 : 0;
        beginRemoveRows(proxyParent, first + offset, last + offset);
    });
    connect(sourceModel, &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex&, int, int) { endRemoveRows(); });
    connect(sourceModel, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& topLeft, const QModelIndex& bottomRight,
                   const QList<int>& roles) {
        const QModelIndex proxyTopLeft = mapFromSource(topLeft);
        const QModelIndex proxyBottomRight = mapFromSource(bottomRight);
        if (proxyTopLeft.isValid() && proxyBottomRight.isValid())
            emit dataChanged(proxyTopLeft, proxyBottomRight, roles);
    });
    connect(sourceModel, &QAbstractItemModel::modelReset, this, &WorkspaceNavigationModel::resetFromSource);

    // Sorting the wrapped model moves rows without emitting row signals. The
    // proxy rows must follow, otherwise every cached index (and therefore the
    // view) keeps pointing at the pre-sort row numbers.
    connect(sourceModel, &QAbstractItemModel::layoutAboutToBeChanged, this,
            [this](const QList<QPersistentModelIndex>& sourceParents, LayoutChangeHint hint) {
        m_layoutChangeProxyIndexes = persistentIndexList();
        emit layoutAboutToBeChanged(mapParentsFromSource(sourceParents), hint);
    });
    connect(sourceModel, &QAbstractItemModel::layoutChanged, this,
            [this](const QList<QPersistentModelIndex>& sourceParents, LayoutChangeHint hint) {
        for (const QModelIndex& oldIndex : std::as_const(m_layoutChangeProxyIndexes)) {
            const auto* node = static_cast<const Node*>(oldIndex.internalPointer());
            if (!node)
                continue;
            // The virtual ".." row is not part of the source model, so it
            // keeps its position across a source re-sort.
            if (!node->isParentEntry)
                changePersistentIndex(oldIndex, mapFromSource(node->sourceIndex));
        }
        m_layoutChangeProxyIndexes.clear();
        emit layoutChanged(mapParentsFromSource(sourceParents), hint);
    });
}

QList<QPersistentModelIndex> WorkspaceNavigationModel::mapParentsFromSource(
    const QList<QPersistentModelIndex>& sourceParents) const
{
    QList<QPersistentModelIndex> proxyParents;
    proxyParents.reserve(sourceParents.size());
    for (const QPersistentModelIndex& sourceParent : sourceParents) {
        if (sourceParent.isValid())
            proxyParents.append(mapFromSource(sourceParent));
    }
    return proxyParents;
}

void WorkspaceNavigationModel::setWorkspaceRootIndex(const QModelIndex& sourceRoot)
{
    const bool atModelRoot = !sourceRoot.isValid();
    if (sourceRoot == m_workspaceRoot && atModelRoot == m_workspaceAtModelRoot)
        return;
    beginResetModel();
    m_workspaceRoot = sourceRoot;
    m_workspaceAtModelRoot = atModelRoot;
    endResetModel();
}

void WorkspaceNavigationModel::setParentEntryEnabled(bool enabled)
{
    if (m_parentEntryEnabled == enabled)
        return;
    beginResetModel();
    m_parentEntryEnabled = enabled;
    endResetModel();
}

void WorkspaceNavigationModel::setParentEntryIcon(const QIcon& icon)
{
    m_parentEntryIcon = icon;
    if (m_parentEntryEnabled && (m_workspaceRoot.isValid() || m_workspaceAtModelRoot)) {
        const QModelIndex root = m_workspaceAtModelRoot ? QModelIndex() : mapFromSource(m_workspaceRoot);
        emit dataChanged(index(0, 0, root), index(0, columnCount() - 1, root),
                         {Qt::DecorationRole});
    }
}

bool WorkspaceNavigationModel::isParentEntry(const QModelIndex& index) const
{
    // The virtual item exists only as the first child of the navigation
    // root. Checking its structural position prevents a stale filesystem
    // index from being mistaken for this row during an asynchronous refresh.
    if (!index.isValid() || index.model() != this || index.row() != 0)
        return false;
    const auto* node = static_cast<const Node*>(index.internalPointer());
    return node && node->isParentEntry;
}

QModelIndex WorkspaceNavigationModel::sourceIndexForEntry(const QModelIndex& index) const
{
    if (!index.isValid() || index.model() != this)
        return {};
    const auto* node = static_cast<const Node*>(index.internalPointer());
    return node ? QModelIndex(node->sourceIndex) : QModelIndex();
}

bool WorkspaceNavigationModel::isWorkspaceRoot(const QModelIndex& index) const
{
    // m_workspaceRoot belongs to the wrapped source model, while `index`
    // belongs to this proxy. Compare in one model's coordinate system.
    return m_workspaceRoot.isValid() && !isParentEntry(index) &&
           mapToSource(index) == QModelIndex(m_workspaceRoot);
}

bool WorkspaceNavigationModel::isNavigationRoot(const QModelIndex& index) const
{
    return m_workspaceAtModelRoot ? !index.isValid() : isWorkspaceRoot(index);
}

QModelIndex WorkspaceNavigationModel::mapToSource(const QModelIndex& proxyIndex) const
{
    return isParentEntry(proxyIndex) ? QModelIndex() : sourceIndexForEntry(proxyIndex);
}

QModelIndex WorkspaceNavigationModel::mapFromSource(const QModelIndex& sourceIndex) const
{
    if (!sourceIndex.isValid() || !sourceModel())
        return {};
    const QModelIndex proxyParent = mapFromSource(sourceIndex.parent());
    int proxyRow = sourceIndex.row();
    if (m_parentEntryEnabled && isNavigationRoot(proxyParent))
        ++proxyRow;
    return makeIndex(proxyRow, sourceIndex.column(), sourceIndex);
}

QModelIndex WorkspaceNavigationModel::index(int row, int column, const QModelIndex& parentIndex) const
{
    if (!sourceModel() || row < 0 || column < 0 || column >= columnCount(parentIndex))
        return {};
    if (m_parentEntryEnabled && isNavigationRoot(parentIndex) && row == 0)
        return makeIndex(row, column, {}, true);

    const QModelIndex sourceParent = mapToSource(parentIndex);
    int sourceRow = row;
    if (m_parentEntryEnabled && isNavigationRoot(parentIndex))
        --sourceRow;
    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, column, sourceParent);
    return sourceIndex.isValid() ? makeIndex(row, column, sourceIndex) : QModelIndex();
}

QModelIndex WorkspaceNavigationModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return {};
    if (isParentEntry(child))
        return m_workspaceAtModelRoot ? QModelIndex() : mapFromSource(m_workspaceRoot);
    return mapFromSource(mapToSource(child).parent());
}

int WorkspaceNavigationModel::rowCount(const QModelIndex& parentIndex) const
{
    if (!sourceModel() || isParentEntry(parentIndex))
        return 0;
    const int count = sourceModel()->rowCount(mapToSource(parentIndex));
    return count + (m_parentEntryEnabled && isNavigationRoot(parentIndex) ? 1 : 0);
}

int WorkspaceNavigationModel::columnCount(const QModelIndex& parentIndex) const
{
    return sourceModel() ? sourceModel()->columnCount(mapToSource(parentIndex)) : 0;
}

QVariant WorkspaceNavigationModel::data(const QModelIndex& index, int role) const
{
    if (isParentEntry(index)) {
        if (role == Qt::DisplayRole)
            return QStringLiteral("..");
        if (role == Qt::DecorationRole && index.column() == 0)
            return m_parentEntryIcon;
        if (role == Qt::ToolTipRole)
            return tr("Go to parent folder");
        return {};
    }
    return sourceModel() ? sourceModel()->data(mapToSource(index), role) : QVariant();
}

Qt::ItemFlags WorkspaceNavigationModel::flags(const QModelIndex& index) const
{
    if (isParentEntry(index))
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return sourceModel() ? (sourceModel()->flags(mapToSource(index)) | Qt::ItemIsEditable) : Qt::NoItemFlags;
}

bool WorkspaceNavigationModel::hasChildren(const QModelIndex& parentIndex) const
{
    if (!sourceModel() || isParentEntry(parentIndex))
        return false;
    const QModelIndex sourceParent = mapToSource(parentIndex);
    return rowCount(parentIndex) > 0 || sourceModel()->hasChildren(sourceParent) ||
           sourceModel()->canFetchMore(sourceParent);
}

bool WorkspaceNavigationModel::canFetchMore(const QModelIndex& parentIndex) const
{
    return sourceModel() && !isParentEntry(parentIndex) &&
           sourceModel()->canFetchMore(mapToSource(parentIndex));
}

void WorkspaceNavigationModel::fetchMore(const QModelIndex& parentIndex)
{
    if (!sourceModel() || isParentEntry(parentIndex))
        return;

    QAbstractItemModel* source = sourceModel();
    const QPersistentModelIndex sourceParent(mapToSource(parentIndex));
    QTimer::singleShot(0, source, [source, sourceParent]() {
        if (source->canFetchMore(sourceParent))
            source->fetchMore(sourceParent);
    });
}

void WorkspaceNavigationModel::resetFromSource()
{
    // A source reset invalidates every mapped index, so the node cache (which
    // would otherwise grow for the lifetime of the session) is dropped here
    // too. Nodes must never be freed outside a reset: live QModelIndex values
    // hold raw pointers to them.
    beginResetModel();
    m_sourceNodes.clear();
    m_nodes.clear();
    endResetModel();
}

QModelIndex WorkspaceNavigationModel::makeIndex(int row, int column, const QModelIndex& sourceIndex,
                                                  bool parentEntry) const
{
    if (parentEntry) {
        if (!m_parentEntryNode) {
            m_parentEntryNode = std::make_unique<Node>();
            m_parentEntryNode->isParentEntry = true;
        }
        return createIndex(row, column, m_parentEntryNode.get());
    }

    const QPersistentModelIndex persistentSource(sourceIndex);
    if (Node* node = m_sourceNodes.value(persistentSource, nullptr))
        return createIndex(row, column, node);

    auto node = std::make_unique<Node>();
    node->sourceIndex = persistentSource;
    Node* raw = node.get();
    m_nodes.push_back(std::move(node));
    m_sourceNodes.insert(persistentSource, raw);
    return createIndex(row, column, raw);
}
