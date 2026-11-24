#pragma once
#include <QString>
#include <QVector>

// Non-prose masking for spell checking. Pure QString algorithms only - no Qt
// Widgets, no COM, no document access - so this links against Qt6::Core alone
// and is covered by SpellCheckExclusionsTest.
namespace SpellCheckExclusions {

struct Span {
    int start = 0;
    int length = 0;
};

// Line-spanning context a single line cannot determine on its own. On the live
// editor path inFence is seeded from MarkdownHighlighter's per-block user state
// rather than recomputed here; advance() exists for the document-wide Fix All
// walk, which visits every line in order anyway.
struct LineState {
    bool inFence = false;
    bool inFrontMatter = false;
    bool inMathBlock = false;
    // Fence delimiters are asymmetric in practice (``` closes ~~~ in FastMD's
    // highlighter), so only the family and length are tracked.
    QChar fenceChar;
    int fenceLength = 0;
};

// The state the line *after* `line` starts in, given the state `line` started in.
LineState advance(const QString& line, LineState state, int lineNumber);

// The ranges of `line` worth spell checking, left to right, non-overlapping.
// Everything else is code, markup, math, a URL, a link destination or another
// piece of Markdown syntax that must not be treated as prose.
QVector<Span> proseSpans(const QString& line, const LineState& state);

// Plain Text documents have no Markdown syntax to mask; the whole line is prose
// apart from leading/trailing whitespace.
QVector<Span> plainTextSpans(const QString& line);

} // namespace SpellCheckExclusions
