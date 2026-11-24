#include "GutterSelection.h"

#include <QTextBlock>
#include <QTextDocument>

namespace GutterSelection {

QTextCursor clickCursor(const QTextCursor& current, int blockNumber)
{
    if (current.blockNumber() == blockNumber) {
        QTextCursor collapsed = current;
        collapsed.clearSelection();
        return collapsed;
    }
    QTextDocument* document = current.document();
    const QTextBlock block = document->findBlockByNumber(blockNumber);
    return QTextCursor(block.isValid() ? block : document->lastBlock());
}

QTextCursor lineRangeCursor(QTextDocument* document, int anchorBlock, int currentBlock)
{
    const int lastIndex = document->blockCount() - 1;
    anchorBlock = qBound(0, anchorBlock, lastIndex);
    currentBlock = qBound(0, currentBlock, lastIndex);

    const QTextBlock first = document->findBlockByNumber(qMin(anchorBlock, currentBlock));
    const QTextBlock last = document->findBlockByNumber(qMax(anchorBlock, currentBlock));
    const int start = first.position();
    const int end = last.position() + last.length() - 1;

    QTextCursor cursor(document);
    if (currentBlock >= anchorBlock) {
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(end);
        cursor.setPosition(start, QTextCursor::KeepAnchor);
    }
    return cursor;
}

}
