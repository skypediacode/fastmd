#include "FindReplaceDialog.h"
#include "MarkdownEditor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTabBar>
#include <QTextDocument>
#include <QTextCursor>
#include <QShortcut>
#include <QKeySequence>
#include <QTimer>

FindReplaceDialog::FindReplaceDialog(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
{
    setWindowTitle(tr("Find and Replace"));
    setMinimumWidth(460);
    setSizeGripEnabled(false);

    m_modeTabs = new QTabBar;
    m_modeTabs->addTab(tr("Find"));
    m_modeTabs->addTab(tr("Replace"));
    m_modeTabs->setExpanding(false);
    m_modeTabs->setDocumentMode(true);
    m_modeTabs->setDrawBase(false);

    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    grid->addWidget(new QLabel(tr("Find what:")), 0, 0);
    m_findEdit = new QLineEdit;
    grid->addWidget(m_findEdit, 0, 1);

    m_replaceRow = new QWidget;
    auto* rl = new QHBoxLayout(m_replaceRow);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(10);
    rl->addWidget(new QLabel(tr("Replace with:")));
    m_replaceEdit = new QLineEdit;
    rl->addWidget(m_replaceEdit);
    grid->addWidget(m_replaceRow, 1, 0, 1, 2);

    m_caseSensitive = new QCheckBox(tr("Case sensitive"));
    m_wholeWord     = new QCheckBox(tr("Whole word"));

    auto* optRow = new QHBoxLayout;
    optRow->setContentsMargins(0, 0, 0, 0);
    optRow->setSpacing(14);
    optRow->addWidget(m_caseSensitive);
    optRow->addWidget(m_wholeWord);
    optRow->addStretch();

    m_btnFindNext  = new QPushButton(tr("Find Next"));
    m_btnFindPrev  = new QPushButton(tr("Find Previous"));
    m_btnReplace   = new QPushButton(tr("Replace"));
    m_btnReplaceAll= new QPushButton(tr("Replace All"));
    for (QPushButton* button : { m_btnFindNext, m_btnFindPrev, m_btnReplace, m_btnReplaceAll }) {
        button->setMinimumHeight(36);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    auto* buttonPanel = new QWidget;
    m_buttonGrid = new QGridLayout(buttonPanel);
    m_buttonGrid->setContentsMargins(0, 0, 0, 0);
    m_buttonGrid->setHorizontalSpacing(10);
    m_buttonGrid->setVerticalSpacing(10);
    m_buttonGrid->addWidget(m_btnFindNext,   0, 0);
    m_buttonGrid->addWidget(m_btnFindPrev,   0, 1);
    m_buttonGrid->addWidget(m_btnReplace,    1, 0);
    m_buttonGrid->addWidget(m_btnReplaceAll, 1, 1);

    m_status = new QLabel;
    m_status->setStyleSheet(QStringLiteral("color: grey; font-size: 11px;"));
    m_status->setVisible(false);
    m_status->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(12);
    root->addWidget(m_modeTabs);
    root->addLayout(grid);
    root->addLayout(optRow);
    root->addWidget(buttonPanel);
    root->addWidget(m_status);

    connect(m_modeTabs,      &QTabBar::currentChanged, this, [this](int index) {
        m_replaceMode = (index == 1);
        applyModeUi();
    });
    connect(m_btnFindNext,   &QPushButton::clicked, this, &FindReplaceDialog::findNext);
    connect(m_btnFindPrev,   &QPushButton::clicked, this, &FindReplaceDialog::findPrev);
    connect(m_btnReplace,    &QPushButton::clicked, this, &FindReplaceDialog::replaceOne);
    connect(m_btnReplaceAll, &QPushButton::clicked, this, &FindReplaceDialog::replaceAll);
    connect(m_findEdit,      &QLineEdit::returnPressed, this, &FindReplaceDialog::findNext);
    connect(m_replaceEdit,   &QLineEdit::returnPressed, this, &FindReplaceDialog::replaceOne);
    connect(m_findEdit,      &QLineEdit::textChanged, this, &FindReplaceDialog::markSearchDirty);
    connect(m_caseSensitive, &QCheckBox::toggled,     this, &FindReplaceDialog::markSearchDirty);
    connect(m_wholeWord,     &QCheckBox::toggled,     this, &FindReplaceDialog::markSearchDirty);

    // Escape to close
    auto* esc = new QShortcut(QKeySequence::Cancel, this);
    connect(esc, &QShortcut::activated, this, &QDialog::hide);

    m_modeTabs->setCurrentIndex(0);
    m_replaceMode = false;
    applyModeUi();
}

void FindReplaceDialog::setEditor(MarkdownEditor* editor)
{
    m_editor = editor;
}

void FindReplaceDialog::openFind()
{
    m_modeTabs->setCurrentIndex(0);
    markSearchDirty();
    show();
    raise();
    activateWindow();
    focusFindField();
}

void FindReplaceDialog::openReplace()
{
    m_modeTabs->setCurrentIndex(1);
    markSearchDirty();
    show();
    raise();
    activateWindow();
    focusFindField();
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
    m_status->setVisible(!msg.isEmpty());
    adjustSize();
}

void FindReplaceDialog::focusFindField()
{
    QTimer::singleShot(0, this, [this]() {
        m_findEdit->setFocus(Qt::ShortcutFocusReason);
        m_findEdit->selectAll();
    });
}

void FindReplaceDialog::applyModeUi()
{
    m_replaceRow->setVisible(m_replaceMode);
    m_btnReplace->setVisible(m_replaceMode);
    m_btnReplaceAll->setVisible(m_replaceMode);
    m_buttonGrid->removeWidget(m_btnFindNext);
    m_buttonGrid->removeWidget(m_btnFindPrev);
    m_buttonGrid->removeWidget(m_btnReplace);
    m_buttonGrid->removeWidget(m_btnReplaceAll);
    m_buttonGrid->addWidget(m_btnFindNext, 0, 0);
    m_buttonGrid->addWidget(m_btnFindPrev, 0, 1);
    if (m_replaceMode) {
        m_buttonGrid->addWidget(m_btnReplace, 1, 0);
        m_buttonGrid->addWidget(m_btnReplaceAll, 1, 1);
        m_buttonGrid->setVerticalSpacing(10);
    } else {
        m_buttonGrid->setVerticalSpacing(0);
    }
    m_btnFindNext->setDefault(!m_replaceMode);
    m_btnFindNext->setAutoDefault(!m_replaceMode);
    m_btnFindPrev->setDefault(false);
    m_btnFindPrev->setAutoDefault(false);
    m_btnReplace->setDefault(m_replaceMode);
    m_btnReplace->setAutoDefault(m_replaceMode);
    m_btnReplaceAll->setDefault(false);
    m_btnReplaceAll->setAutoDefault(false);
    adjustSize();
}

void FindReplaceDialog::markSearchDirty()
{
    m_searchDirty = true;
}

bool FindReplaceDialog::shouldRestartSearch(const QString& term, QTextDocument::FindFlags flags) const
{
    return m_searchDirty || term != m_lastSearchTerm || flags != m_lastSearchFlags;
}

QTextCursor FindReplaceDialog::makeSearchCursor(bool backward) const
{
    if (!m_editor)
        return {};

    QTextCursor c = m_editor->textCursor();
    if (shouldRestartSearch(m_lastSearchTerm, m_lastSearchFlags)) {
        c = QTextCursor(m_editor->document());
        c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        return c;
    }

    if (c.hasSelection())
        c.setPosition(backward ? c.selectionStart() : c.selectionEnd());

    return c;
}

bool FindReplaceDialog::runFind(const QString& term, QTextDocument::FindFlags flags, bool backward)
{
    if (!m_editor)
        return false;

    QTextCursor searchCursor = makeSearchCursor(backward);
    QTextCursor found = m_editor->document()->find(term, searchCursor, flags);
    if (found.isNull()) {
        searchCursor = QTextCursor(m_editor->document());
        searchCursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        found = m_editor->document()->find(term, searchCursor, flags);
    }

    if (!found.isNull()) {
        m_editor->setTextCursor(found);
        m_editor->ensureCursorVisible();
        m_lastSearchTerm = term;
        m_lastSearchFlags = flags;
        m_searchDirty = false;
        showStatus(QString());
        return true;
    }

    m_lastSearchTerm = term;
    m_lastSearchFlags = flags;
    m_searchDirty = false;
    showStatus(tr("Not found"));
    return false;
}

void FindReplaceDialog::findNext()
{
    if (!m_editor) return;
    QString term = m_findEdit->text();
    if (term.isEmpty()) return;
    const auto flags = buildFlags();
    if (shouldRestartSearch(term, flags)) {
        m_lastSearchTerm = term;
        m_lastSearchFlags = flags;
    }
    runFind(term, flags, false);
}

void FindReplaceDialog::findPrev()
{
    if (!m_editor) return;
    QString term = m_findEdit->text();
    if (term.isEmpty()) return;

    auto flags = buildFlags() | QTextDocument::FindBackward;
    if (shouldRestartSearch(term, flags)) {
        m_lastSearchTerm = term;
        m_lastSearchFlags = flags;
    }
    runFind(term, flags, true);
}

void FindReplaceDialog::replaceOne()
{
    if (!m_editor || !m_replaceMode) return;
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
    if (!m_editor || !m_replaceMode) return;
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
