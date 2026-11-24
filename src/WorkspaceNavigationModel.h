#pragma once

#include <QAbstractProxyModel>
#include <QHash>
#include <QPersistentModelIndex>
#include <QIcon>

#include <memory>
#include <vector>

// Adds one virtual, non-filesystem ".." row as the first child of the
// workspace root. Filtering remains the responsibility of the wrapped model.
class WorkspaceNavigationModel final : public QAbstractProxyModel {
    Q_OBJECT
public:
    explicit WorkspaceNavigationModel(QObject* parent = nullptr);

    void setSourceModel(QAbstractItemModel* sourceModel) override;
    void setWorkspaceRootIndex(const QModelIndex& sourceRoot);
    void setParentEntryEnabled(bool enabled);
    void setParentEntryIcon(const QIcon& icon);
    bool isParentEntry(const QModelIndex& index) const;
    QModelIndex sourceIndexForEntry(const QModelIndex& index) const;

    QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool hasChildren(const QModelIndex& parent = {}) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

private:
    struct Node {
        QPersistentModelIndex sourceIndex;
        bool isParentEntry = false;
    };

    bool isWorkspaceRoot(const QModelIndex& index) const;
    bool isNavigationRoot(const QModelIndex& index) const;
    QModelIndex makeIndex(int row, int column, const QModelIndex& sourceIndex, bool parentEntry = false) const;
    void resetFromSource();
    QList<QPersistentModelIndex> mapParentsFromSource(const QList<QPersistentModelIndex>& sourceParents) const;

    QPersistentModelIndex m_workspaceRoot;
    QIcon m_parentEntryIcon;
    bool m_parentEntryEnabled = false;
    bool m_workspaceAtModelRoot = false;
    QModelIndexList m_layoutChangeProxyIndexes;
    mutable QHash<QPersistentModelIndex, Node*> m_sourceNodes;
    mutable std::unique_ptr<Node> m_parentEntryNode;
    mutable std::vector<std::unique_ptr<Node>> m_nodes;
};
