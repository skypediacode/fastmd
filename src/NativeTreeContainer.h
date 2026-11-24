#pragma once

#include <QFrame>

class QTreeView;
class QHeaderView;

class NativeTreeContainer : public QFrame {
    Q_OBJECT
public:
    explicit NativeTreeContainer(QTreeView* treeView, QWidget* parent = nullptr);

    QTreeView* treeView() const { return m_treeView; }
    QHeaderView* standaloneHeader() const { return m_standaloneHeader; }

    // Synchronizes the standalone header with the tree's internal header.
    // Call this after changing the tree's model, hiding columns, or moving sections.
    void syncHeaderState();

private:
    QTreeView* m_treeView;
    QHeaderView* m_standaloneHeader;
};
