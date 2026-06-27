#pragma once
#include <QDialog>
#include <QTextDocument>

class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;
class MarkdownEditor;

class FindReplaceDialog : public QDialog {
    Q_OBJECT
public:
    explicit FindReplaceDialog(QWidget* parent = nullptr);

    void setEditor(MarkdownEditor* editor);
    void openFind();
    void openReplace();

private slots:
    void findNext();
    void findPrev();
    void replaceOne();
    void replaceAll();

private:
    QTextDocument::FindFlags buildFlags() const;
    void                     showStatus(const QString& msg);

    QLineEdit*     m_findEdit;
    QLineEdit*     m_replaceEdit;
    QCheckBox*     m_caseSensitive;
    QCheckBox*     m_wholeWord;
    QPushButton*   m_btnFindNext;
    QPushButton*   m_btnFindPrev;
    QPushButton*   m_btnReplace;
    QPushButton*   m_btnReplaceAll;
    QLabel*        m_status;
    QWidget*       m_replaceRow;

    MarkdownEditor* m_editor = nullptr;
};
