#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QVBoxLayout>

class DocumentModel;
class MarkdownEditor;

class OutlinePopup : public QWidget {
    Q_OBJECT
public:
    explicit OutlinePopup(QWidget* parent = nullptr);

    void setEditor(MarkdownEditor* editor, DocumentModel* model);
    void refresh();

signals:
    void popupHidden();

protected:
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QTreeWidget* m_tree;
    QLineEdit* m_search;
    MarkdownEditor* m_editor = nullptr;
    DocumentModel* m_model = nullptr;
    bool m_ignoreSelectionChange = false;

    void onItemClicked(QTreeWidgetItem* item);
    void filterTree(const QString& text);
    void updateHighlight();
};
