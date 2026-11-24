#pragma once
#include <QSyntaxHighlighter>
#include <QTextBlockUserData>
#include <QTextCursor>
#include <QString>
#include <functional>
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/FoldingRegion>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

class QTimer;

class MarkdownHighlighter : public QSyntaxHighlighter, public KSyntaxHighlighting::AbstractHighlighter {
    Q_OBJECT
    Q_INTERFACES(KSyntaxHighlighting::AbstractHighlighter)
public:
    explicit MarkdownHighlighter(QTextDocument* parent = nullptr, bool dark = false);

    void setDarkMode(bool dark);
    void setEnabled(bool enabled);

    // Bracket a whole-text replacement. The replacement is not highlighted
    // synchronously: endBulkLoad() highlights from the top for a short budget
    // and finishes the rest in time-sliced chunks on the event loop.
    void beginBulkLoad();
    void endBulkLoad();
    // Highlights whatever in [first, last] has not been highlighted yet, so the
    // caller can paint it. Cheap when the range is already done.
    void ensureHighlighted(QTextBlock first, const QTextBlock& last);
    bool isHighlightingComplete() const { return !m_pending; }

protected:
    void highlightBlock(const QString& text) override;
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format& format) override;
    void applyFolding(int offset, int length, KSyntaxHighlighting::FoldingRegion region) override;

private:
    // Runs a second, independently-stateful AbstractHighlighter over fenced
    // code block content once a fence-opening line names a recognized
    // language, so code inside ```cpp ... ``` etc. gets real language-aware
    // highlighting instead of flat code-block coloring.
    class EmbeddedHighlighter : public KSyntaxHighlighting::AbstractHighlighter {
    public:
        std::function<void(int, int, const KSyntaxHighlighting::Format&)> formatCallback;

        KSyntaxHighlighting::State highlightLine(const QString& text, const KSyntaxHighlighting::State& state)
        {
            return AbstractHighlighter::highlightLine(text, state);
        }

    protected:
        void applyFormat(int offset, int length, const KSyntaxHighlighting::Format& format) override;
        void applyFolding(int offset, int length, KSyntaxHighlighting::FoldingRegion region) override;
    };

    struct BlockState : QTextBlockUserData {
        KSyntaxHighlighting::State markdownState;
        KSyntaxHighlighting::State embeddedState;
        bool insideFence = false;
        KSyntaxHighlighting::Definition fenceDefinition; // null Definition = unknown/plain fence
    };

    bool m_enabled = true;
    bool m_dark = false;

    // Blocks before the frontier block are highlighted from their exact
    // predecessor state; the time-sliced pass advances it. A cursor, so edits
    // above it move it with the text.
    QTextCursor m_frontier;
    QTimer* m_sliceTimer = nullptr;
    bool m_pending = false;
    bool m_reformatDetached = false;
    QMetaObject::Connection m_reformatEnd;
    // Set while QSyntaxHighlighter reacts to a contentsChange(): blocks inside
    // [m_editFrom, m_editTo] are the edit itself, the rest are its cascade.
    bool m_reformatting = false;
    int m_editFrom = 0;
    int m_editTo = 0;
    int m_cascadeBudget = 0;
    bool m_discardFormats = false;
    bool m_direct = false;
    QList<QTextCharFormat> m_directFormats;
    int m_dirtyFrom = 0;
    int m_dirtyTo = 0;
    qint64 m_longestSliceNs = 0;
    int m_sliceCount = 0;
    EmbeddedHighlighter m_embedded;
    KSyntaxHighlighting::Theme m_embeddedTheme;
    QColor m_codeBlockBackground;

    void applyThemes();
    static KSyntaxHighlighting::Definition definitionForLanguageTag(const QString& tag);

    void onContentsChange(int from, int charsRemoved, int charsAdded);
    void primeFenceStates();
    void scheduleSlice();
    bool runSlice(qint64 budgetMs);
    void skipBlock(const QString& text);
    BlockState* highlightBlockText(const QTextBlock& block, const QString& text);
    void highlightDirectly(QTextBlock block);
    void reportDirtyBlocks();
    void putFormat(int start, int count, const QTextCharFormat& format);
    KSyntaxHighlighting::State stateAfter(const QString& line, const KSyntaxHighlighting::State& from);
};
