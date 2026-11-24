#pragma once

#include <QStyledItemDelegate>
#include <functional>

// Inline create/rename editor for the Workspace tree/search views. The
// QFileSystemModel underneath is read-only (it never writes through the
// model), so this delegate performs the actual filesystem rename itself via
// WorkspaceFileOps and relies on QFileSystemModel's own file-system watcher
// to pick up the change; it does not call QAbstractItemModel::setData().
//
// "New Markdown File"/"New Folder" reuse this same delegate: the file is
// created on disk first with a placeholder name, then edit() is invoked on
// its index — from the delegate's point of view that's just a rename from
// the placeholder name to whatever the user types (or a no-op revert on
// Escape/empty input, leaving the placeholder name).
class WorkspaceRenameDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    // pathResolver maps a (possibly proxy) model index to the absolute
    // filesystem path it represents.
    explicit WorkspaceRenameDelegate(std::function<QString(const QModelIndex&)> pathResolver,
                                      QObject* parent = nullptr);

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                           const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                       const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const override;

    void setTrailingActionWidth(int width) { m_trailingActionWidth = width; }

signals:
    // Emitted after a successful on-disk rename triggered via this delegate.
    void renamed(const QString& oldPath, const QString& newPath) const;

private:
    std::function<QString(const QModelIndex&)> m_pathResolver;
    int m_trailingActionWidth = 0;
};
