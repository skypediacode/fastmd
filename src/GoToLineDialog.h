#pragma once
#include <QDialog>

class QLineEdit;
class QLabel;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class MarkdownEditor;

class GoToLineDialog : public QDialog {
    Q_OBJECT
public:
    explicit GoToLineDialog(QWidget* parent = nullptr);

    void setEditor(MarkdownEditor* editor);
    void updateLineRange();

    // Ctrl+G always lands on the Line tab, regardless of which tab was last used.
    void selectLineTab();

signals:
    void navigationRequested();

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onGoClicked();

private:
    enum Tab { LineTab = 0, OutlineTab = 1 };

    QTabWidget* m_tabs = nullptr;
    QLineEdit*  m_lineEdit = nullptr;
    QLabel*     m_rangeLabel = nullptr;
    QLineEdit*  m_outlineSearch = nullptr;
    QTreeWidget* m_outlineTree = nullptr;
    MarkdownEditor* m_editor = nullptr;

    QWidget* buildLineTab();
    QWidget* buildOutlineTab();
    void onTabChanged(int index);
    void applyTabSize(int index);
    void refreshOutline();
    void selectHeadingAtCursor();
    void activateOutlineItem(QTreeWidgetItem* item);
    void moveOutlineSelection(int delta);
    QTreeWidgetItem* currentOutlineItem() const;

    // This dialog is a cached singleton (MainWindow keeps it alive across
    // invocations), so its title bar must be re-themed on every show — not
    // just once at construction — to track later light/dark toggles.
    void applyTitleBarTheme();
};
