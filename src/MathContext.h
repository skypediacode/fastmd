#pragma once

class QTextCursor;
class QTextDocument;

// Detects complete Markdown math spans while ignoring escaped delimiters and
// Markdown code. Math and Chemistry toolbar insertions use this to avoid
// nesting delimiters.
namespace MathContext {

enum class Kind { None, Inline, Block };

struct Result {
    Kind kind = Kind::None;
    int spanStart = -1;   // document offset of the opening delimiter, if kind != None
    int spanEnd = -1;     // document offset just past the closing delimiter
};

Result at(const QTextCursor& cursor);
Result at(const QTextDocument* document, int start, int end);

} // namespace MathContext
