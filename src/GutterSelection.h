#pragma once
#include <QTextCursor>

class QTextDocument;

// Cursor results for line-number gutter clicks and drags. Lines are logical
// document blocks, never wrapped visual rows.
namespace GutterSelection {

// A single click: the start of another line, or the caret left where it is
// (selection collapsed) when the clicked line already holds the caret.
QTextCursor clickCursor(const QTextCursor& current, int blockNumber);

// Lines anchorBlock..currentBlock, from the first line's start to the end of
// the last line's text (Home, Shift+End): the newlines between them are
// included, the last line's own is not. The caret sits on the currentBlock
// side, so an upward drag keeps it at the top of the range.
QTextCursor lineRangeCursor(QTextDocument* document, int anchorBlock, int currentBlock);

}
