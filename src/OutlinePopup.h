#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>

class DocumentModel;
class MarkdownEditor;

class OutlinePopup : public QWidget {
    Q_OBJECT
public:
    explicit OutlinePopup(QWidget* parent = nullptr);

    void setEditor(MarkdownEditor* editor, DocumentModel* model);
    void refresh();
    void resetPinned();
    void setDarkMode(bool dark);

    // Shows the popup at targetPos, sliding up and fading in from just below
    // that position -- used when opening from the top-right outline button
    // so the popup visibly "flies in" toward its resting place.
    void showAt(const QPoint& targetPos);

signals:
    void popupHidden();
    void navigationRequested();

protected:
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QTreeWidget* m_tree;
    QLineEdit* m_search;
    QToolButton* m_pinButton;
    MarkdownEditor* m_editor = nullptr;
    DocumentModel* m_model = nullptr;
    bool m_pinned = false;
    bool m_ignoreSelectionChange = false;

    void onItemClicked(QTreeWidgetItem* item);
    void setPinned(bool pinned);
    void positionSearchClearButton();
    void updatePinButton();
    void filterTree(const QString& text);
    void updateHighlight();
    void updateStylesheet(bool dark);
};
