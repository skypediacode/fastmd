#include "MarkdownHighlighter.h"
#include "FenceLanguageAliases.h"
#include "StartupProfiler.h"
#include "SyntaxHighlightingRepository.h"
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>

namespace {

const QRegularExpression& fenceLineRe()
{
    static const QRegularExpression re(QStringLiteral(R"(^(```+|~~~+)(\S*)\s*$)"));
    return re;
}

QRegularExpressionMatch matchFenceLine(const QString& text)
{
    if (text.isEmpty() || (text.at(0) != u'`' && text.at(0) != u'~'))
        return {};
    return fenceLineRe().match(text);
}

// Time-sliced highlighting. The first slice runs inside the load, so a document
// that highlights within it is complete before the editor first paints, exactly
// as a synchronous pass would leave it; larger ones continue in event-loop
// slices short enough to keep scrolling and typing at frame rate.
constexpr qint64 kInitialSliceMs = 30;
constexpr qint64 kSliceMs = 8;
// How far an edit's cascade (blocks whose start state changed) is highlighted
// synchronously. Past it, blocks keep only their exact fence state and are
// handed back to the time-sliced pass, so typing a fence near the top of a very
// large document does not re-highlight everything below it on the keystroke.
constexpr int kCascadeBudget = 512;

QTextCharFormat toCharFormat(const KSyntaxHighlighting::Format& format,
                              const KSyntaxHighlighting::Theme& theme,
                              const QColor* backgroundOverride = nullptr)
{
    QTextCharFormat cf;
    if (format.hasTextColor(theme))
        cf.setForeground(format.textColor(theme));
    if (backgroundOverride)
        cf.setBackground(*backgroundOverride);
    else if (format.hasBackgroundColor(theme))
        cf.setBackground(format.backgroundColor(theme));
    if (format.isBold(theme))
        cf.setFontWeight(QFont::Bold);
    if (format.isItalic(theme))
        cf.setFontItalic(true);
    if (format.isUnderline(theme))
        cf.setFontUnderline(true);
    if (format.isStrikeThrough(theme))
        cf.setFontStrikeOut(true);
    return cf;
}

// Resolving a definition or a theme walks the repository's index and parses the
// theme JSON; every highlighter instance wants the same handles, so resolve each
// of them once per process.
const KSyntaxHighlighting::Definition& markdownDefinition()
{
    static const KSyntaxHighlighting::Definition definition =
        syntaxRepository().definitionForName(QStringLiteral("FastMD Markdown"));
    return definition;
}

const KSyntaxHighlighting::Theme& fastmdTheme(bool dark)
{
    static const KSyntaxHighlighting::Theme light =
        syntaxRepository().theme(QStringLiteral("FastMD Light"));
    static const KSyntaxHighlighting::Theme night =
        syntaxRepository().theme(QStringLiteral("FastMD Dark"));
    return dark ? night : light;
}

const KSyntaxHighlighting::Theme& embeddedTheme(bool dark)
{
    static const KSyntaxHighlighting::Theme light =
        syntaxRepository().defaultTheme(KSyntaxHighlighting::Repository::LightTheme);
    static const KSyntaxHighlighting::Theme night =
        syntaxRepository().defaultTheme(KSyntaxHighlighting::Repository::DarkTheme);
    return dark ? night : light;
}

} // namespace

void MarkdownHighlighter::EmbeddedHighlighter::applyFormat(int offset, int length,
                                                             const KSyntaxHighlighting::Format& format)
{
    if (formatCallback)
        formatCallback(offset, length, format);
}

void MarkdownHighlighter::EmbeddedHighlighter::applyFolding(int, int, KSyntaxHighlighting::FoldingRegion)
{
}

MarkdownHighlighter::MarkdownHighlighter(QTextDocument* parent, bool dark)
    : QSyntaxHighlighter(static_cast<QObject*>(parent))
    , m_dark(dark)
{
    // Themes are resolved before the definition so that the caller's single
    // rehighlight() is the only full pass over the document: setting the dark
    // flag afterwards would cost a second one, and the old trailing
    // rehighlight() a third.
    //
    // The pass itself must NOT run here. rehighlight() emits the document's
    // contentsChanged, which MarkdownEditor::onTextChanged answers by calling
    // ensureMarkdownHighlighter() — and that guard is still unset while this
    // constructor runs, so it would build another highlighter, recursively,
    // until the stack is gone.
    applyThemes();
    setDefinition(markdownDefinition());

    // setDocument() installs QSyntaxHighlighter's own contentsChange() handler;
    // connecting one slot before it and one after brackets each reformat, so
    // highlightBlock() can tell an edit's own blocks from its cascade.
    if (parent) {
        connect(parent, &QTextDocument::contentsChange, this, &MarkdownHighlighter::onContentsChange);
        setDocument(parent);
        m_reformatEnd = connect(parent, &QTextDocument::contentsChange, this, [this] { m_reformatting = false; });
        m_frontier = QTextCursor(parent);
    }
}

void MarkdownHighlighter::applyThemes()
{
    setTheme(fastmdTheme(m_dark));
    // Matches the CodeBlockBody custom-style background in fastmd-{light,dark}.theme.
    m_codeBlockBackground = m_dark ? QColor(0x22, 0x29, 0x33) : QColor(0xe8, 0xed, 0xf2);

    // Fenced code content uses KSyntaxHighlighting's own bundled default
    // theme (FastMD's theme only defines colors for its own Markdown
    // definition), only overriding the background so the fence body stays
    // visually consistent with the rest of the code block.
    m_embeddedTheme = embeddedTheme(m_dark);
}

void MarkdownHighlighter::setDarkMode(bool dark)
{
    if (m_dark == dark)
        return;
    const bool wasModified = document()->isModified();
    m_dark = dark;
    applyThemes();
    rehighlight();
    m_pending = false;
    document()->setModified(wasModified);
}

void MarkdownHighlighter::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    const bool wasModified = document()->isModified();
    m_enabled = enabled;
    rehighlight();
    m_pending = false;
    document()->setModified(wasModified);
}

KSyntaxHighlighting::Definition MarkdownHighlighter::definitionForLanguageTag(const QString& tag)
{
    const QString fileName = FenceLanguageAliases::fileNameForTag(tag);
    if (fileName.isEmpty())
        return {};
    return syntaxRepository().definitionForFileName(fileName);
}

void MarkdownHighlighter::applyFormat(int offset, int length, const KSyntaxHighlighting::Format& format)
{
    putFormat(offset, length, toCharFormat(format, theme()));
}

// Where a block's formats go: QSyntaxHighlighter's buffer while it drives the
// block, or this highlighter's own when it writes the block directly.
void MarkdownHighlighter::putFormat(int start, int count, const QTextCharFormat& format)
{
    if (m_discardFormats)
        return;
    if (!m_direct) {
        setFormat(start, count, format);
        return;
    }
    if (start < 0 || start >= m_directFormats.size())
        return;
    const int end = qMin(start + count, int(m_directFormats.size()));
    for (int i = start; i < end; ++i)
        m_directFormats[i] = format;
}

// The end state of a line highlighted from `from`, without touching the
// current block's formats.
KSyntaxHighlighting::State MarkdownHighlighter::stateAfter(const QString& line, const KSyntaxHighlighting::State& from)
{
    m_discardFormats = true;
    const KSyntaxHighlighting::State state = highlightLine(line, from);
    m_discardFormats = false;
    return state;
}

void MarkdownHighlighter::applyFolding(int, int, KSyntaxHighlighting::FoldingRegion)
{
}

void MarkdownHighlighter::highlightBlock(const QString& text)
{
    if (!m_enabled) {
        setCurrentBlockState(0);
        setCurrentBlockUserData(nullptr);
        return;
    }

    if (m_reformatting) {
        const QTextBlock block = currentBlock();
        const bool edited = block.position() <= m_editTo && block.position() + block.length() > m_editFrom;
        if (!edited && m_cascadeBudget-- <= 0) {
            skipBlock(text);
            return;
        }
    }
    BlockState* state = highlightBlockText(currentBlock(), text);
    setCurrentBlockUserData(state);
    setCurrentBlockState(state->insideFence ? 1 : 0);
}

MarkdownHighlighter::BlockState* MarkdownHighlighter::highlightBlockText(const QTextBlock& block, const QString& text)
{
    const QTextBlock previousBlock = block.previous();
    const auto* previous = previousBlock.isValid()
        ? dynamic_cast<const BlockState*>(previousBlock.userData())
        : nullptr;

    KSyntaxHighlighting::State markdownState = previous ? previous->markdownState : KSyntaxHighlighting::State();
    KSyntaxHighlighting::State embeddedState = previous ? previous->embeddedState : KSyntaxHighlighting::State();
    bool wasInsideFence = previous && previous->insideFence;
    KSyntaxHighlighting::Definition fenceDefinition = previous ? previous->fenceDefinition
                                                                : KSyntaxHighlighting::Definition();

    // A predecessor the time-sliced pass has not reached yet still carries its
    // exact fence state (primeFenceStates()). The Markdown context is a guess
    // until the pass arrives and replaces this result: top-level prose, or the
    // context the opening fence line leaves behind.
    if (!previous && previousBlock.isValid()) {
        markdownState = stateAfter(QStringLiteral("x"), KSyntaxHighlighting::State());
        if (previousBlock.userState() == 1) {
            QTextBlock opening = previousBlock;
            while (opening.previous().isValid() && opening.previous().userState() == 1)
                opening = opening.previous();
            const QString openingText = opening.text();
            wasInsideFence = true;
            fenceDefinition = definitionForLanguageTag(matchFenceLine(openingText).captured(2));
            markdownState = stateAfter(openingText, markdownState);
        }
    }

    auto* state = new BlockState;

    const QRegularExpressionMatch fenceMatch = matchFenceLine(text);
    if (fenceMatch.hasMatch()) {
        // Fence marker line (open or close) - always colored by the base
        // Markdown definition, never by the embedded per-language highlighter.
        state->markdownState = highlightLine(text, markdownState);
        if (!wasInsideFence) {
            state->insideFence = true;
            state->fenceDefinition = definitionForLanguageTag(fenceMatch.captured(2));
            state->embeddedState = KSyntaxHighlighting::State();
        } else {
            state->insideFence = false;
            state->fenceDefinition = KSyntaxHighlighting::Definition();
            state->embeddedState = KSyntaxHighlighting::State();
        }
    } else if (wasInsideFence && fenceDefinition.isValid()) {
        // Fenced code content with a recognized language: delegate entirely
        // to the embedded highlighter, forcing FastMD's own code-block
        // background so the fence body stays visually uniform.
        QTextCharFormat backgroundFmt;
        backgroundFmt.setBackground(m_codeBlockBackground);
        putFormat(0, text.length(), backgroundFmt);
        m_embedded.setDefinition(fenceDefinition);
        m_embedded.setTheme(m_embeddedTheme);
        m_embedded.formatCallback = [this](int offset, int length, const KSyntaxHighlighting::Format& format) {
            putFormat(offset, length, toCharFormat(format, m_embedded.theme(), &m_codeBlockBackground));
        };
        state->embeddedState = m_embedded.highlightLine(text, embeddedState);
        state->markdownState = markdownState;
        state->insideFence = true;
        state->fenceDefinition = fenceDefinition;
    } else if (wasInsideFence) {
        // Fenced code content with no recognized language: flat code-block
        // coloring from the base Markdown definition.
        state->markdownState = highlightLine(text, markdownState);
        state->insideFence = true;
        state->fenceDefinition = KSyntaxHighlighting::Definition();
        state->embeddedState = KSyntaxHighlighting::State();
    } else {
        state->markdownState = highlightLine(text, markdownState);
        state->insideFence = false;
        state->fenceDefinition = KSyntaxHighlighting::Definition();
        state->embeddedState = KSyntaxHighlighting::State();
    }
    return state;
}

// What QSyntaxHighlighter's reformat does for one block -- same formats, same
// run grouping, same user data and state -- minus its layout notification. That
// one re-lays the block out on the spot, which for a block nobody is looking at
// cost more than highlighting it. setFormats() already drops the block's line
// layout, so a block that was never laid out needs no notification at all; one
// that was (it may be on screen) is collected and reported by
// reportDirtyBlocks(), once per batch.
void MarkdownHighlighter::highlightDirectly(QTextBlock block)
{
    QTextLayout* layout = block.layout();
    if (layout->lineCount() > 0) {
        const int end = block.position() + block.length();
        m_dirtyFrom = m_dirtyTo > m_dirtyFrom ? qMin(m_dirtyFrom, block.position()) : block.position();
        m_dirtyTo = qMax(m_dirtyTo, end);
    }
    if (!layout->preeditAreaText().isEmpty()) {
        // Input-method composition in progress: QSyntaxHighlighter knows how to
        // keep the preedit text's own formats. Its edit block would read as a
        // text change to the editor and MainWindow; nothing textual changes.
        const QSignalBlocker blocker(document());
        rehighlightBlock(block);
        return;
    }
    const QString text = block.text();
    m_directFormats.fill(QTextCharFormat(), text.length());
    m_direct = true;
    BlockState* state = highlightBlockText(block, text);
    m_direct = false;

    QList<QTextLayout::FormatRange> ranges;
    int i = 0;
    const QTextCharFormat plain;
    while (i < m_directFormats.size()) {
        while (i < m_directFormats.size() && m_directFormats.at(i) == plain)
            ++i;
        if (i == m_directFormats.size())
            break;
        QTextLayout::FormatRange range;
        range.start = i;
        range.format = m_directFormats.at(i);
        while (i < m_directFormats.size() && m_directFormats.at(i) == range.format)
            ++i;
        range.length = i - range.start;
        ranges.append(range);
    }
    if (!ranges.isEmpty() || !layout->formats().isEmpty())
        layout->setFormats(ranges);
    block.setUserData(state);
    block.setUserState(state->insideFence ? 1 : 0);
}

void MarkdownHighlighter::reportDirtyBlocks()
{
    if (m_dirtyTo > m_dirtyFrom)
        document()->markContentsDirty(m_dirtyFrom, m_dirtyTo - m_dirtyFrom);
    m_dirtyFrom = 0;
    m_dirtyTo = 0;
}

// Keeps the block's fence state exact without highlighting it, and hands the
// block to the time-sliced pass. Only ever reached for blocks an edit's cascade
// pushed past kCascadeBudget.
void MarkdownHighlighter::skipBlock(const QString& text)
{
    const QTextBlock block = currentBlock();
    bool insideFence = block.previous().isValid() && block.previous().userState() == 1;
    if (matchFenceLine(text).hasMatch())
        insideFence = !insideFence;
    setCurrentBlockUserData(nullptr);
    setCurrentBlockState(insideFence ? 1 : 0);
    if (block.position() < m_frontier.position())
        m_frontier.setPosition(block.position());
    scheduleSlice();
}

void MarkdownHighlighter::onContentsChange(int from, int charsRemoved, int charsAdded)
{
    // The same span QSyntaxHighlighter re-highlights before it starts cascading.
    m_reformatting = true;
    m_editFrom = from;
    m_editTo = from + charsAdded + (charsRemoved > 0 ? 1 : 0);
    m_cascadeBudget = kCascadeBudget;
}

void MarkdownHighlighter::beginBulkLoad()
{
    if (m_sliceTimer)
        m_sliceTimer->stop();
    // QSyntaxHighlighter would otherwise answer the replacement's single
    // whole-document contentsChange() by highlighting every block before the
    // editor can paint. If this private slot ever stops matching, disconnect()
    // fails and the load simply takes the old synchronous path.
    if (!m_reformatDetached) {
        m_reformatDetached = QObject::disconnect(document(), SIGNAL(contentsChange(int,int,int)),
                                                 this, SLOT(_q_reformatBlocks(int,int,int)));
    }
}

void MarkdownHighlighter::endBulkLoad()
{
    QTextDocument* doc = document();
    if (m_reformatDetached) {
        // Reconnected ahead of the end-of-reformat slot, which must stay last.
        disconnect(m_reformatEnd);
        QObject::connect(doc, SIGNAL(contentsChange(int,int,int)), this, SLOT(_q_reformatBlocks(int,int,int)));
        m_reformatEnd = connect(doc, &QTextDocument::contentsChange, this, [this] { m_reformatting = false; });
        m_reformatDetached = false;
    }
    m_frontier.setPosition(0);
    m_pending = false;
    if (!m_enabled)
        return;

    primeFenceStates();
    m_pending = true;
    m_longestSliceNs = 0;
    m_sliceCount = 0;
    if (!runSlice(kInitialSliceMs))
        scheduleSlice();
}

// Whether a block is inside a fenced code block depends only on the fence lines
// above it, which are cheap to find, so every block gets its exact fence state
// up front. The editor's code-block painting and spell-check exclusions read it
// before the block is highlighted, and highlightBlockText() starts from it.
void MarkdownHighlighter::primeFenceStates()
{
    // toRawText() is indexed exactly like document positions, and a block's
    // length() includes its separator, so walking both together needs no
    // per-block position lookup.
    const QString raw = document()->toRawText();
    bool insideFence = false;
    qsizetype position = 0;
    for (QTextBlock block = document()->firstBlock(); block.isValid(); block = block.next()) {
        const int length = block.length();
        if (position < raw.size()) {
            const QChar first = raw.at(position);
            if ((first == u'`' || first == u'~')
                && matchFenceLine(raw.mid(position, length - 1)).hasMatch()) {
                insideFence = !insideFence;
            }
        }
        block.setUserState(insideFence ? 1 : 0);
        position += length;
    }
}

void MarkdownHighlighter::scheduleSlice()
{
    m_pending = true;
    if (!m_sliceTimer) {
        m_sliceTimer = new QTimer(this);
        m_sliceTimer->setSingleShot(true);
        m_sliceTimer->setInterval(0);
        connect(m_sliceTimer, &QTimer::timeout, this, [this] {
            if (m_enabled && m_pending && !runSlice(kSliceMs))
                m_sliceTimer->start();
        });
    }
    if (!m_sliceTimer->isActive())
        m_sliceTimer->start();
}

// Highlights forward from the frontier in document order, so every block is
// computed from its predecessor's exact end state. Returns true once the end of
// the document is reached.
bool MarkdownHighlighter::runSlice(qint64 budgetMs)
{
    QElapsedTimer timer;
    timer.start();
    QTextDocument* doc = document();
    QTextBlock block = doc->findBlock(m_frontier.position());
    while (block.previous().isValid() && !dynamic_cast<const BlockState*>(block.previous().userData()))
        block = block.previous();

    if (block.isValid()) {
        int count = 0;
        while (block.isValid()) {
            highlightDirectly(block);
            block = block.next();
            if ((++count & 15) == 0 && timer.elapsed() >= budgetMs)
                break;
        }
        reportDirtyBlocks();
    }
    m_frontier.setPosition(block.isValid() ? block.position() : doc->characterCount() - 1);

    const qint64 elapsed = timer.nsecsElapsed();
    if (++m_sliceCount > 1 && elapsed > m_longestSliceNs)
        m_longestSliceNs = elapsed;
    if (block.isValid())
        return false;

    m_pending = false;
    if (StartupProfiler::enabled()) {
        const QByteArray label = QStringLiteral("markdown highlighting complete (%1 slices, longest deferred %2 ms)")
            .arg(m_sliceCount).arg(m_longestSliceNs / 1.0e6, 0, 'f', 1).toUtf8();
        FASTMD_MARK(label.constData());
    }
    return true;
}

void MarkdownHighlighter::ensureHighlighted(QTextBlock first, const QTextBlock& last)
{
    if (!m_pending || !m_enabled || !first.isValid())
        return;
    const QTextBlock end = last.isValid() ? last.next() : QTextBlock();
    while (first.isValid() && first != end && dynamic_cast<const BlockState*>(first.userData()))
        first = first.next();
    if (!first.isValid() || first == end)
        return;

    for (QTextBlock block = first; block.isValid() && block != end; block = block.next()) {
        if (!dynamic_cast<const BlockState*>(block.userData()))
            highlightDirectly(block);
    }
    reportDirtyBlocks();
}
