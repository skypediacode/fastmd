#pragma once

#include <QString>

// Pure QString -> QString text-transform helpers used by MarkdownEditor's
// case-conversion, whitespace, and "Join Wrapped Lines" commands. These have
// no QTextCursor/QPlainTextEdit dependency, so they live outside the editor
// class and are trivially unit-testable on their own.
namespace TextTransforms {

// Normalizes Qt's paragraph-separator character (used internally by
// QTextCursor::selectedText() for line breaks) back to a plain '\n'.
QString blockSeparatorsToNewline(QString text);
// Query text for Search in Files from an editor selection. Empty when nothing
// is selected or the selection spans lines: a multi-line selection is a scope
// for Find, not a term to search for.
QString singleLineSearchSeed(const QString& selected);

QString transformProperCase(const QString& text);
QString transformSentenceCase(const QString& text);

QString stripLeadingSpacesTabs(QString line);
QString stripTrailingSpacesTabs(QString line);

// Line-oriented utilities operating on '\n'-separated text. Each treats a
// trailing empty element from a final '\n' as "no trailing line" so a
// trailing newline is preserved/dropped exactly as it was in the input.
QString sortLines(const QString& text);
QString reverseLines(const QString& text);
QString removeDuplicateLines(const QString& text);
QString uniqueSortLines(const QString& text);

// Joins the affected lines into one, separated by a single space. Unlike
// joinWrappedLinesText, this has no Markdown-structure awareness -- it is a
// generic "collapse these lines into one" operation.
QString joinLines(const QString& text);

// Splits each affected line on every occurrence of `delimiter`, trimming
// each resulting segment; a line with no occurrence of `delimiter` is left
// unchanged (as a single line).
QString splitLines(const QString& text, const QString& delimiter);

// Collapses runs of Unicode horizontal whitespace (space/tab/NBSP/etc, never
// newlines) within each line to a single space, then trims the line.
QString normalizeWhitespace(const QString& text);

// Convert between leading/embedded tabs and a fixed-width column model of
// `width` columns per tab stop (matching MarkdownEditor's own tab stops).
QString tabsToSpaces(const QString& text, int width);
QString spacesToTabs(const QString& text, int width);

// Replaces curly/smart quotation marks and apostrophes with their straight
// ASCII equivalents.
QString smartQuotesToStraight(const QString& text);

// Strips Unicode "Format" (Cf) characters (zero-width spaces/joiners, BOM,
// directional marks, etc.) and stray C0 control characters other than '\n'
// and '\t'.
QString removeInvisibleCharacters(const QString& text);

// Converts hard-wrapped paragraphs into normally-flowing paragraphs by
// joining soft-wrapped lines with a space, while leaving fenced/indented code,
// headings, horizontal rules, tables, HTML blocks, and blank lines untouched.
QString joinWrappedLinesText(const QString& text);

// Conservative, line-oriented Markdown cleanup. When line limits are set,
// only those complete zero-based lines are changed; protected-region state is
// still discovered from the start of the document.
QString formatMarkdown(const QString& text, int firstLine = -1, int lastLine = -1);

// Recalculates ATX heading numbers while leaving H1 as an unnumbered chapter
// title. When line limits are set, only those complete zero-based lines are
// rewritten; heading and fenced-code context is still read from document start.
QString updateSectionNumbers(const QString& text, int firstLine = -1, int lastLine = -1);

// Recalculates ordered-list markers independently for each indentation level.
// A limited range is read in document context but only requested lines change.
QString updateListNumbers(const QString& text, int firstLine = -1, int lastLine = -1);

} // namespace TextTransforms
