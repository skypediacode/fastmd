#include "GutterSelection.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTextBlock>
#include <QTextDocument>

namespace {
bool require(bool condition, const QString& message)
{
    if (!condition) qCritical().noquote() << message;
    return condition;
}

QString selected(const QTextCursor& cursor)
{
    return cursor.selectedText().replace(QChar(0x2029), QLatin1Char('\n'));
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    const QString heading = QStringLiteral("## CHƯƠNG 5: GIỚI HẠN. HÀM SỐ LIÊN TỤC");
    QTextDocument doc(QStringLiteral("alpha\n") + heading + QStringLiteral("\n\ngamma delta\nlast"));
    // Blocks: 0 alpha, 1 heading, 2 empty, 3 gamma delta, 4 last (no newline)

    QTextCursor caret(&doc);
    caret.setPosition(doc.findBlockByNumber(3).position() + 4);

    QTextCursor click = GutterSelection::clickCursor(caret, 1);
    ok &= require(click.position() == doc.findBlockByNumber(1).position() && !click.hasSelection(),
                  QStringLiteral("click on another line must go to that line's start"));

    click = GutterSelection::clickCursor(caret, 3);
    ok &= require(click.position() == caret.position() && !click.hasSelection(),
                  QStringLiteral("click on the caret line must keep the caret position"));

    QTextCursor selecting = caret;
    selecting.setPosition(doc.findBlockByNumber(3).position() + 1, QTextCursor::KeepAnchor);
    click = GutterSelection::clickCursor(selecting, 3);
    ok &= require(click.position() == selecting.position() && !click.hasSelection(),
                  QStringLiteral("click on the caret line must collapse the selection in place"));

    selecting.setPosition(doc.findBlockByNumber(0).position() + 2);
    selecting.setPosition(doc.findBlockByNumber(3).position() + 2, QTextCursor::KeepAnchor);
    click = GutterSelection::clickCursor(selecting, 0);
    ok &= require(click.position() == 0 && !click.hasSelection(),
                  QStringLiteral("click on the selection anchor's line is another line"));

    const QTextBlock headingBlock = doc.findBlockByNumber(1);
    QTextCursor homeShiftEnd(headingBlock);
    homeShiftEnd.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);

    const QTextCursor single = GutterSelection::lineRangeCursor(&doc, 1, 1);
    ok &= require(selected(single) == heading,
                  QStringLiteral("double-click must select exactly the line's text, without its newline"));
    ok &= require(single.anchor() == homeShiftEnd.anchor() && single.position() == homeShiftEnd.position(),
                  QStringLiteral("double-click must match Home, Shift+End"));
    ok &= require(single.blockNumber() == 1 && single.atBlockEnd(),
                  QStringLiteral("double-click leaves the caret at the end of that line"));

    const QTextCursor empty = GutterSelection::lineRangeCursor(&doc, 2, 2);
    ok &= require(!empty.hasSelection() && empty.blockNumber() == 2,
                  QStringLiteral("double-click on an empty line is an empty selection on that line"));
    ok &= require(selected(GutterSelection::lineRangeCursor(&doc, 4, 4)) == QStringLiteral("last"),
                  QStringLiteral("final line without newline selects through EOF"));

    const QTextCursor down = GutterSelection::lineRangeCursor(&doc, 1, 3);
    ok &= require(selected(down) == heading + QStringLiteral("\n\ngamma delta"),
                  QStringLiteral("downward drag includes inner newlines but not the last line's"));
    ok &= require(down.anchor() == headingBlock.position() && down.anchor() < down.position(),
                  QStringLiteral("downward drag anchors at the first line's start"));
    ok &= require(down.blockNumber() == 3 && down.atBlockEnd(),
                  QStringLiteral("downward drag leaves the caret at the end of the final line"));

    const QTextCursor up = GutterSelection::lineRangeCursor(&doc, 3, 1);
    ok &= require(selected(up) == selected(down), QStringLiteral("upward drag selects the same lines"));
    ok &= require(up.anchor() == down.position(),
                  QStringLiteral("upward drag anchors at the end of the line it started on"));
    ok &= require(up.position() == headingBlock.position() && up.blockNumber() == 1,
                  QStringLiteral("upward drag leaves the caret on the line it ended on"));

    ok &= require(selected(GutterSelection::lineRangeCursor(&doc, 2, 4)) == QStringLiteral("\ngamma delta\nlast"),
                  QStringLiteral("drag to the final line selects through EOF"));
    ok &= require(selected(GutterSelection::lineRangeCursor(&doc, 0, 99)) == doc.toPlainText(),
                  QStringLiteral("an out-of-range line is clamped to the document"));

    QTextDocument trailing(QStringLiteral("one\ntwo\n"));
    ok &= require(selected(GutterSelection::lineRangeCursor(&trailing, 1, 1)) == QStringLiteral("two"),
                  QStringLiteral("final text line before a trailing newline excludes it"));
    ok &= require(selected(GutterSelection::lineRangeCursor(&trailing, 0, 1)) == QStringLiteral("one\ntwo"),
                  QStringLiteral("drag onto the final text line excludes the trailing newline"));
    ok &= require(!GutterSelection::lineRangeCursor(&trailing, 2, 2).hasSelection(),
                  QStringLiteral("trailing empty block selects nothing"));
    ok &= require(selected(GutterSelection::lineRangeCursor(&trailing, 1, 2)) == QStringLiteral("two\n"),
                  QStringLiteral("drag into the trailing empty block includes the newline between them"));

    const QString longLine = QString(QStringLiteral("wrapped words ")).repeated(200);
    QTextDocument wrapped(QStringLiteral("a\n") + longLine + QStringLiteral("\nb"));
    wrapped.setTextWidth(80);
    const QTextCursor wrappedLine = GutterSelection::lineRangeCursor(&wrapped, 1, 1);
    ok &= require(selected(wrappedLine) == longLine, QStringLiteral("a wrapped line is one logical line"));
    ok &= require(wrappedLine.blockNumber() == 1 && wrappedLine.atBlockEnd(),
                  QStringLiteral("a wrapped line leaves the caret at its logical end"));

    QTextCursor deleting = GutterSelection::lineRangeCursor(&doc, 1, 1);
    deleting.removeSelectedText();
    ok &= require(doc.toPlainText() == QStringLiteral("alpha\n\n\ngamma delta\nlast"),
                  QStringLiteral("deleting a selected line leaves it as an empty line"));
    ok &= require(doc.blockCount() == 5 && deleting.blockNumber() == 1,
                  QStringLiteral("deleting a selected line keeps the following lines in place"));

    QTextCursor deletingRange = GutterSelection::lineRangeCursor(&doc, 3, 4);
    deletingRange.removeSelectedText();
    ok &= require(doc.toPlainText() == QStringLiteral("alpha\n\n\n"),
                  QStringLiteral("deleting a multi-line range leaves one empty line in its place"));

    return ok ? 0 : 1;
}
