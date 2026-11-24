#pragma once

class QTextCursor;

namespace MarkdownInlineFormatting {

// Toggle a literal <mark> wrapper around the cursor selection.  The cursor is
// updated to select the marked (or unmarked) content; an empty cursor is left
// between a newly inserted tag pair.  This stays independent of MarkdownEditor
// so its source/caret behavior can be regression-tested with QTextDocument.
void toggleMark(QTextCursor& cursor);

} // namespace MarkdownInlineFormatting
