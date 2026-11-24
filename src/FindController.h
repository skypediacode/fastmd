#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QPair>
#include <QTextCursor>
#include <QTextDocument>
#include <QRegularExpression>

class MarkdownEditor;
class PreviewWidget;

// Headless Find/Replace engine. Owns the search term, options, and the
// current match list, and drives MarkdownEditor's cursor/selection and
// search-match highlighting. Kept independent of any view so F3/Shift+F3
// work even while FindOverlay itself is not yet constructed or not visible.
//
// A PDF tab has no searchable text on the Qt side, so its PreviewWidget is the
// target instead: the PDF engine owns the matches, highlighting and scrolling,
// and this class only forwards the term and mirrors the counts it reports.
class FindController : public QObject {
    Q_OBJECT
public:
    struct Options {
        bool matchCase = false;
        bool wholeWord = false;
        bool regex = false;
        bool inSelection = false;
        bool preserveCase = false;
    };

    static constexpr int kMaxMatches = 20000;

    explicit FindController(QObject* parent = nullptr);

    // At most one of the two is non-null. They are set together so a tab switch
    // restarts the search once rather than once per target.
    void setTarget(MarkdownEditor* editor, PreviewWidget* pdfPreview);
    MarkdownEditor* editor() const { return m_editor; }
    PreviewWidget* pdfPreview() const { return m_pdfPreview; }

    // Gates whether contentReady() recomputes eagerly; the overlay calls this
    // on open/close so typing with the overlay hidden costs nothing. While
    // inactive the match list is only marked stale and rebuilt on demand.
    void setActive(bool active);
    bool isActive() const { return m_active; }

    void setTerm(const QString& term);
    void setOptions(const Options& options);

    // Remembers the editor's current selection as the candidate Find in
    // Selection scope. Called when the overlay opens, *before* any find moves
    // the cursor, so toggling the option later still scopes to what the user
    // had selected rather than to the match currently under the caret.
    bool captureSelectionScope();
    void clearSelectionScope();
    bool hasSelectionScope() const;

    QString term() const { return m_term; }
    Options options() const { return m_options; }
    int  matchCount() const { return m_pdfPreview ? m_pdfMatchCount : int(m_matches.size()); }
    int  currentIndex() const { return m_currentIndex; }
    bool capped() const { return m_capped; }
    bool regexValid() const { return m_regexValid; }
    QString regexError() const { return m_regexError; }

    bool findNext();
    bool findPrevious();
    bool replaceCurrent(const QString& replacement);
    int  replaceAll(const QString& replacement);
    void refresh();

signals:
    void matchesChanged();
    void navigationRequested();

private:
    QTextDocument::FindFlags buildFlags() const;
    QString effectivePattern() const;
    QRegularExpression buildRegex() const;
    int  indexOfMatchAtOrAfter(int position) const;
    int  indexOfMatchBefore(int position) const;
    bool navigateToMatch(int index);
    void updateCurrentIndexFromCursor();
    void pushMatchesToEditor();
    void ensureFresh();
    void invalidate();
    QString expandReplacement(const QString& replacement, const QString& matchedText,
                              const QRegularExpression* compiledRegex = nullptr) const;

    MarkdownEditor* m_editor = nullptr;
    PreviewWidget* m_pdfPreview = nullptr;
    int     m_pdfMatchCount = 0;
    bool    m_active = false;
    bool    m_stale = false;
    QString m_term;
    Options m_options;
    QVector<QPair<int, int>> m_matches;
    int     m_currentIndex = -1;
    bool    m_capped = false;
    bool    m_regexValid = true;
    QString m_regexError;
    QTextCursor m_selectionScope;
};
