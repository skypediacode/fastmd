#include "FindController.h"
#include "MarkdownEditor.h"
#include "PreserveCase.h"
#include "PreviewWidget.h"

#include <algorithm>

FindController::FindController(QObject* parent)
    : QObject(parent)
{
}

void FindController::setTarget(MarkdownEditor* editor, PreviewWidget* pdfPreview)
{
    if (m_editor == editor && m_pdfPreview == pdfPreview)
        return;

    if (m_editor) {
        disconnect(m_editor, nullptr, this, nullptr);
        m_editor->clearSearchMatches();
    }
    if (m_pdfPreview) {
        disconnect(m_pdfPreview, nullptr, this, nullptr);
        m_pdfPreview->stopDocumentFind();
    }

    m_editor = editor;
    m_pdfPreview = pdfPreview;
    m_selectionScope = QTextCursor();

    if (m_pdfPreview) {
        connect(m_pdfPreview, &PreviewWidget::documentFindChanged, this,
                [this](int matchCount, int activeIndex) {
            m_pdfMatchCount = matchCount;
            m_currentIndex = activeIndex;
            emit matchesChanged();
        });
        connect(m_pdfPreview, &QObject::destroyed, this, [this]() {
            m_pdfPreview = nullptr;
            invalidate();
        });
    }

    if (m_editor) {
        connect(m_editor, &MarkdownEditor::contentReady, this, [this]() {
            if (m_active)
                refresh();
            else
                m_stale = true;
        });
        connect(m_editor, &QObject::destroyed, this, [this]() {
            m_editor = nullptr;
            invalidate();
        });
    }

    refresh();
}

void FindController::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (m_active) {
        refresh();
    } else if (m_editor) {
        m_editor->clearSearchMatches();
    } else if (m_pdfPreview) {
        m_pdfPreview->stopDocumentFind();
    }
}

void FindController::setTerm(const QString& term)
{
    if (m_term == term)
        return;
    m_term = term;
    refresh();
}

void FindController::setOptions(const Options& options)
{
    if (m_options.matchCase == options.matchCase && m_options.wholeWord == options.wholeWord
        && m_options.regex == options.regex && m_options.inSelection == options.inSelection
        && m_options.preserveCase == options.preserveCase)
        return;
    const bool needsRescan = m_options.matchCase != options.matchCase
        || m_options.wholeWord != options.wholeWord
        || m_options.regex != options.regex
        || m_options.inSelection != options.inSelection;
    m_options = options;
    if (needsRescan)
        refresh();
    else
        emit matchesChanged();
}

bool FindController::captureSelectionScope()
{
    if (!m_editor) {
        m_selectionScope = QTextCursor();
        return false;
    }
    const QTextCursor cur = m_editor->textCursor();
    m_selectionScope = cur.hasSelection() ? cur : QTextCursor();
    return hasSelectionScope();
}

void FindController::clearSelectionScope()
{
    m_selectionScope = QTextCursor();
}

bool FindController::hasSelectionScope() const
{
    return !m_selectionScope.isNull() && m_selectionScope.hasSelection();
}

QTextDocument::FindFlags FindController::buildFlags() const
{
    QTextDocument::FindFlags flags;
    if (m_options.matchCase)
        flags |= QTextDocument::FindCaseSensitively;
    if (m_options.wholeWord && !m_options.regex)
        flags |= QTextDocument::FindWholeWords;
    return flags;
}

QString FindController::effectivePattern() const
{
    // QTextDocument::find(QRegularExpression, ...) ignores FindWholeWords --
    // whole-word semantics have to be encoded into the pattern itself.
    if (m_options.wholeWord)
        return QStringLiteral("\\b(?:") + m_term + QStringLiteral(")\\b");
    return m_term;
}

QRegularExpression FindController::buildRegex() const
{
    QRegularExpression::PatternOptions patOpts = QRegularExpression::NoPatternOption;
    if (!m_options.matchCase)
        patOpts |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(effectivePattern(), patOpts);
}

void FindController::invalidate()
{
    m_matches.clear();
    m_capped = false;
    m_currentIndex = -1;
    m_stale = false;
    emit matchesChanged();
}

void FindController::ensureFresh()
{
    if (m_stale)
        refresh();
}

void FindController::refresh()
{
    m_matches.clear();
    m_capped = false;
    m_regexValid = true;
    m_regexError.clear();
    m_currentIndex = -1;
    m_stale = false;
    m_pdfMatchCount = 0;

    if (m_pdfPreview) {
        if (m_active)
            m_pdfPreview->startDocumentFind(m_term);
        emit matchesChanged();
        return;
    }

    if (!m_editor || m_term.isEmpty()) {
        pushMatchesToEditor();
        emit matchesChanged();
        return;
    }

    QTextDocument* doc = m_editor->document();

    int scopeStart = -1;
    int scopeEnd = -1;
    if (m_options.inSelection && hasSelectionScope()) {
        scopeStart = m_selectionScope.selectionStart();
        scopeEnd = m_selectionScope.selectionEnd();
    }

    const QTextDocument::FindFlags flags = buildFlags();
    const int lastPosition = doc->characterCount() - 1;

    if (m_options.regex) {
        const QRegularExpression re = buildRegex();
        if (!re.isValid()) {
            m_regexValid = false;
            m_regexError = re.errorString();
            pushMatchesToEditor();
            emit matchesChanged();
            return;
        }

        QTextCursor cursor(doc);
        if (scopeStart >= 0)
            cursor.setPosition(scopeStart);
        for (;;) {
            const QTextCursor found = doc->find(re, cursor, flags);
            if (found.isNull())
                break;

            const int start = found.selectionStart();
            const int end = found.selectionEnd();
            if (scopeEnd >= 0 && start >= scopeEnd)
                break;

            // Zero-width hits (^, \b, a*) are real regex matches but there is
            // nothing to highlight, navigate to, or replace, so they are not
            // offered as results -- they only drive the scan forward.
            if (end > start && (scopeStart < 0 || (start >= scopeStart && end <= scopeEnd))) {
                m_matches.append({ start, end });
                if (m_matches.size() >= kMaxMatches) {
                    m_capped = true;
                    break;
                }
            }

            if (end == start) {
                if (end >= lastPosition)
                    break;
                cursor = QTextCursor(doc);
                cursor.setPosition(end + 1);
            } else {
                cursor = found;
            }
        }
    } else {
        QTextCursor cursor(doc);
        if (scopeStart >= 0)
            cursor.setPosition(scopeStart);
        for (;;) {
            const QTextCursor found = doc->find(m_term, cursor, flags);
            if (found.isNull())
                break;

            const int start = found.selectionStart();
            const int end = found.selectionEnd();
            if (scopeEnd >= 0 && start >= scopeEnd)
                break;
            if (scopeStart < 0 || (start >= scopeStart && end <= scopeEnd)) {
                m_matches.append({ start, end });
                if (m_matches.size() >= kMaxMatches) {
                    m_capped = true;
                    break;
                }
            }
            cursor = found;
        }
    }

    updateCurrentIndexFromCursor();
    pushMatchesToEditor();
    emit matchesChanged();
}

void FindController::pushMatchesToEditor()
{
    if (!m_editor)
        return;
    // Only paint the highlight-all layer while a view is actively showing
    // results; F3/Shift+F3 still navigate after the overlay closes (VS Code
    // parity), but must not leave the document littered with highlights.
    if (!m_active || m_matches.isEmpty())
        m_editor->clearSearchMatches();
    else
        m_editor->setSearchMatches(m_matches, m_currentIndex);
}

int FindController::indexOfMatchAtOrAfter(int position) const
{
    const auto it = std::lower_bound(m_matches.begin(), m_matches.end(), position,
        [](const QPair<int, int>& match, int pos) { return match.first < pos; });
    return it == m_matches.end() ? -1 : int(it - m_matches.begin());
}

int FindController::indexOfMatchBefore(int position) const
{
    const auto it = std::lower_bound(m_matches.begin(), m_matches.end(), position,
        [](const QPair<int, int>& match, int pos) { return match.first < pos; });
    if (it == m_matches.begin())
        return -1;
    return int(it - m_matches.begin()) - 1;
}

void FindController::updateCurrentIndexFromCursor()
{
    if (!m_editor || m_matches.isEmpty()) {
        m_currentIndex = -1;
        return;
    }

    const QTextCursor cur = m_editor->textCursor();
    if (!cur.hasSelection()) {
        m_currentIndex = -1;
        return;
    }
    const int idx = indexOfMatchAtOrAfter(cur.selectionStart());
    m_currentIndex = (idx >= 0 && m_matches[idx].first == cur.selectionStart()
                      && m_matches[idx].second == cur.selectionEnd())
        ? idx : -1;
}

bool FindController::navigateToMatch(int index)
{
    if (!m_editor || index < 0 || index >= m_matches.size())
        return false;

    const auto& m = m_matches[index];
    QTextCursor cursor(m_editor->document());
    cursor.setPosition(m.first);
    cursor.setPosition(m.second, QTextCursor::KeepAnchor);

    emit navigationRequested();
    m_editor->setTextCursor(cursor);
    m_editor->ensureCursorVisible();
    m_editor->flashCurrentLine();

    m_currentIndex = index;
    pushMatchesToEditor();
    emit matchesChanged();
    return true;
}

bool FindController::findNext()
{
    if (m_pdfPreview)
        return m_pdfPreview->findNextMatch();
    ensureFresh();
    if (!m_editor || m_matches.isEmpty())
        return false;

    const QTextCursor cur = m_editor->textCursor();
    const int pos = cur.hasSelection() ? cur.selectionStart() : cur.position();
    int idx = indexOfMatchAtOrAfter(pos);
    if (idx < 0) {
        idx = 0;
    } else if (m_matches[idx].first == cur.selectionStart() && cur.hasSelection()
               && m_matches[idx].second == cur.selectionEnd()) {
        idx = (idx + 1) % m_matches.size();
    }
    return navigateToMatch(idx);
}

bool FindController::findPrevious()
{
    if (m_pdfPreview)
        return m_pdfPreview->findPreviousMatch();
    ensureFresh();
    if (!m_editor || m_matches.isEmpty())
        return false;

    const QTextCursor cur = m_editor->textCursor();
    const int pos = cur.hasSelection() ? cur.selectionStart() : cur.position();
    int idx = indexOfMatchBefore(pos);
    if (idx < 0)
        idx = m_matches.size() - 1;
    return navigateToMatch(idx);
}

QString FindController::expandReplacement(const QString& replacement, const QString& matchedText,
                                          const QRegularExpression* compiledRegex) const
{
    QString expanded = replacement;

    if (m_options.regex) {
        const QRegularExpression re = compiledRegex ? *compiledRegex : buildRegex();
        if (re.isValid()) {
            const QRegularExpressionMatch match = re.match(matchedText);
            if (match.hasMatch() && match.capturedStart() == 0
                && match.capturedLength() == matchedText.length()) {
                for (int g = 9; g >= 1; --g)
                    expanded.replace(QStringLiteral("$%1").arg(g), match.captured(g));
            }
        }
    }

    if (m_options.preserveCase)
        expanded = applyPreservedCase(matchedText, expanded);
    return expanded;
}

bool FindController::replaceCurrent(const QString& replacement)
{
    ensureFresh();
    if (!m_editor || m_matches.isEmpty())
        return false;

    // Replace only if the caret is actually sitting on a match; otherwise this
    // acts as a plain Find Next, which is what VS Code does too.
    QTextCursor cur = m_editor->textCursor();
    bool replaced = false;
    if (cur.hasSelection()) {
        const int idx = indexOfMatchAtOrAfter(cur.selectionStart());
        if (idx >= 0 && m_matches[idx].first == cur.selectionStart()
            && m_matches[idx].second == cur.selectionEnd()) {
            QString matchedText = cur.selectedText();
            matchedText.replace(QChar(0x2029), QLatin1Char('\n'));
            cur.insertText(expandReplacement(replacement, matchedText));
            replaced = true;
        }
    }

    refresh();
    findNext();
    return replaced;
}

int FindController::replaceAll(const QString& replacement)
{
    ensureFresh();
    if (!m_editor || m_term.isEmpty())
        return 0;

    refresh();
    if (m_matches.isEmpty())
        return 0;

    QTextDocument* doc = m_editor->document();
    // One edit block on the *same* cursor that inserts, so Replace All is a
    // single undo step and the document/preview see it as one change.
    QTextCursor editCursor(doc);
    editCursor.beginEditBlock();

    int count = 0;
    const QRegularExpression compiledRegex = m_options.regex ? buildRegex() : QRegularExpression();
    // Back-to-front so the earlier offsets in m_matches stay valid as
    // later-in-document matches are rewritten.
    for (int i = m_matches.size() - 1; i >= 0; --i) {
        const auto& m = m_matches[i];
        editCursor.setPosition(m.first);
        editCursor.setPosition(m.second, QTextCursor::KeepAnchor);

        QString matchedText = editCursor.selectedText();
        matchedText.replace(QChar(0x2029), QLatin1Char('\n'));
        editCursor.insertText(expandReplacement(
            replacement, matchedText, m_options.regex ? &compiledRegex : nullptr));
        ++count;
    }

    editCursor.endEditBlock();
    refresh();
    return count;
}
