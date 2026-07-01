#pragma once
#include <QDialog>
#include <QTextDocument>

class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;
class QGridLayout;
class QTabBar;
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
    void                     focusFindField();
    void                     applyModeUi();
    void                     markSearchDirty();
    bool                     shouldRestartSearch(const QString& term, QTextDocument::FindFlags flags) const;
    QTextCursor              makeSearchCursor(bool backward) const;
    bool                     runFind(const QString& term, QTextDocument::FindFlags flags, bool backward);

    QTabBar*        m_modeTabs;
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
    QGridLayout*   m_buttonGrid;

    MarkdownEditor* m_editor = nullptr;
    bool            m_replaceMode = false;
    QString         m_lastSearchTerm;
    QTextDocument::FindFlags m_lastSearchFlags;
    bool            m_searchDirty = true;
};
