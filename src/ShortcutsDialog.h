#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QKeySequence>

class QTreeWidget;
class QTreeWidgetItem;

class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void populateShortcuts();
    void addShortcutRow(QTreeWidget* tree, QTreeWidgetItem* categoryItem, const QString& actionName, const QKeySequence& shortcut);

    QTreeWidget* m_tree = nullptr;
};
