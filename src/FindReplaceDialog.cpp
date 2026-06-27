#include "FindReplaceDialog.h"
#include "MarkdownEditor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTextDocument>
#include <QTextCursor>
#include <QShortcut>
#include <QKeySequence>

FindReplaceDialog::FindReplaceDialog(QWidget* parent)
    : QDialog(parent, Qt::Tool | Qt::WindowStaysOnTopHint)
{
    setWindowTitle(tr("Find / Replace"));
    setMinimumWidth(400);

    auto* grid = new QGridLayout;

    grid->addWidget(new QLabel(tr("Find:")),    0, 0);
    m_findEdit = new QLineEdit;
    grid->addWidget(m_findEdit, 0, 1);

    m_replaceRow = new QWidget;
    auto* rl = new QHBoxLayout(m_replaceRow);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(new QLabel(tr("Replace:")));
    m_replaceEdit = new QLineEdit;
    rl->addWidget(m_replaceEdit);
    grid->addWidget(m_replaceRow, 1, 0, 1, 2);

    m_caseSensitive = new QCheckBox(tr("Case sensitive"));
    m_wholeWord     = new QCheckBox(tr("Whole word"));

    auto* optRow = new QHBoxLayout;
    optRow->addWidget(m_caseSensitive);
    optRow->addWidget(m_wholeWord);
    optRow->addStretch();

    m_btnFindNext  = new QPushButton(tr("Find Next"));
    m_btnFindPrev  = new QPushButton(tr("Find Previous"));
    m_btnReplace   = new QPushButton(tr("Replace"));
    m_btnReplaceAll= new QPushButton(tr("Replace All"));

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(m_btnFindNext);
    btnRow->addWidget(m_btnFindPrev);
    btnRow->addWidget(m_btnReplace);
    btnRow->addWidget(m_btnReplaceAll);

    m_status = new QLabel;
    m_status->setStyleSheet(QStringLiteral("color: grey; font-size: 11px;"));

    auto* root = new QVBoxLayout(this);
    root->addLayout(grid);
    root->addLayout(optRow);
    root->addLayout(btnRow);
    root->addWidget(m_status);

    connect(m_btnFindNext,   &QPushButton::clicked, this, &FindReplaceDialog::findNext);
    connect(m_btnFindPrev,   &QPushButton::clicked, this, &FindReplaceDialog::findPrev);
    connect(m_btnReplace,    &QPushButton::clicked, this, &FindReplaceDialog::replaceOne);
    connect(m_btnReplaceAll, &QPushButton::clicked, this, &FindReplaceDialog::replaceAll);
    connect(m_findEdit,      &QLineEdit::returnPressed, this, &FindReplaceDialog::findNext);

    // Escape to close
    auto* esc = new QShortcut(QKeySequence::Cancel, this);
    connect(esc, &QShortcut::activated, this, &QDialog::hide);
}

void FindReplaceDialog::setEditor(MarkdownEditor* editor)
{
    m_editor = editor;
}

void FindReplaceDialog::openFind()
{
    m_replaceRow->hide();
    m_btnReplace->hide();
    m_btnReplaceAll->hide();
    setWindowTitle(tr("Find"));
    show();
    raise();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void FindReplaceDialog::openReplace()
{
    m_replaceRow->show();
    m_btnReplace->show();
    m_btnReplaceAll->show();
    setWindowTitle(tr("Find / Replace"));
    show();
    raise();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

QTextDocument::FindFlags FindReplaceDialog::buildFlags() const
{
    QTextDocument::FindFlags f;
    if (m_caseSensitive->isChecked()) f |= QTextDocument::FindCaseSensitively;
    if (m_wholeWord->isChecked())     f |= QTextDocument::FindWholeWords;
    return f;
}

void FindReplaceDialog::showStatus(const QString& msg)
{
    m_status->setText(msg);
}

void FindReplaceDialog::findNext()
{
    if (!m_editor) return;
    QString term = m_findEdit->text();
    if (term.isEmpty()) return;

    bool found = m_editor->find(term, buildFlags());
    if (!found) {
        // wrap around
        QTextCursor c = m_editor->textCursor();
        c.movePosition(QTextCursor::Start);
        m_editor->setTextCursor(c);
        found = m_editor->find(term, buildFlags());
    }
    showStatus(found ? QString() : tr("Not found"));
}

void FindReplaceDialog::findPrev()
{
    if (!m_editor) return;
    QString term = m_findEdit->text();
    if (term.isEmpty()) return;

    auto flags = buildFlags() | QTextDocument::FindBackward;
    bool found = m_editor->find(term, flags);
    if (!found) {
        QTextCursor c = m_editor->textCursor();
        c.movePosition(QTextCursor::End);
        m_editor->setTextCursor(c);
        found = m_editor->find(term, flags);
    }
    showStatus(found ? QString() : tr("Not found"));
}

void FindReplaceDialog::replaceOne()
{
    if (!m_editor) return;
    QTextCursor c = m_editor->textCursor();
    if (c.hasSelection() && c.selectedText().compare(
            m_findEdit->text(),
            m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0)
    {
        c.insertText(m_replaceEdit->text());
    }
    findNext();
}

void FindReplaceDialog::replaceAll()
{
    if (!m_editor) return;
    QString term    = m_findEdit->text();
    QString replace = m_replaceEdit->text();
    if (term.isEmpty()) return;

    QTextCursor c = m_editor->textCursor();
    c.movePosition(QTextCursor::Start);
    m_editor->setTextCursor(c);

    int count = 0;
    m_editor->document()->blockSignals(true);
    QTextCursor cur(m_editor->document());
    cur.beginEditBlock();
    auto flags = buildFlags();
    while (!(c = m_editor->document()->find(term, c, flags)).isNull()) {
        c.insertText(replace);
        ++count;
    }
    cur.endEditBlock();
    m_editor->document()->blockSignals(false);

    showStatus(count > 0
        ? tr("Replaced %1 occurrence(s)").arg(count)
        : tr("Not found"));
}
