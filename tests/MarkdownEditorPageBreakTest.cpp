// Real MarkdownEditor driven by synthetic key events. Guards that a Page Break
// line -- hidden under its painted label -- is removed as one unit by
// Backspace after it and Delete before it, that each removal is one undo step,
// and that ordinary deletion around it is unchanged. Also guards that
// insertion splits the line at the caret rather than appending after it.
#include "MarkdownEditor.h"
#include "MarkdownExtensions.h"

#include <QApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <cstdio>

namespace {
int failures = 0;
int checks = 0;
void check(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL line %d: %s\n", line, expression);
    }
}
#define CHECK(x) check((x), #x, __LINE__)

void checkText(const MarkdownEditor& editor, const QString& expected, int line)
{
    const QString actual = editor.toPlainText();
    ++checks;
    if (actual != expected) {
        ++failures;
        std::printf("FAIL line %d:\n  expected: \"%s\"\n  actual:   \"%s\"\n", line,
                    qPrintable(QString(expected).replace(QLatin1Char('\n'), QStringLiteral("\\n"))),
                    qPrintable(QString(actual).replace(QLatin1Char('\n'), QStringLiteral("\\n"))));
    }
}
#define CHECK_TEXT(editor, expected) checkText((editor), (expected), __LINE__)

const QString PB = QStringLiteral("<div style=\"break-after: page;\"></div>");

struct Editor {
    MarkdownEditor editor{ nullptr, 14 };

    explicit Editor(const QString& text)
    {
        editor.setAttribute(Qt::WA_DontShowOnScreen);
        editor.resize(800, 400);
        editor.setPlainText(text);
        editor.show();
        QApplication::processEvents();
    }

    int position(int line, int column) const
    {
        return editor.document()->findBlockByNumber(line).position() + column;
    }
    int lineEnd(int line) const
    {
        const QTextBlock block = editor.document()->findBlockByNumber(line);
        return block.position() + block.text().length();
    }
    void caret(int position)
    {
        QTextCursor cursor = editor.textCursor();
        cursor.setPosition(position);
        editor.setTextCursor(cursor);
    }
    void select(int anchor, int position)
    {
        QTextCursor cursor = editor.textCursor();
        cursor.setPosition(anchor);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        editor.setTextCursor(cursor);
    }
    int caretPosition() const { return editor.textCursor().position(); }
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::keyClick(&editor, key, modifiers);
    }
};

void recognizer()
{
    CHECK(MarkdownExtensions::isPageBreakLine(PB));
    CHECK(MarkdownExtensions::isPageBreakLine(QStringLiteral("   ") + PB));
    CHECK(MarkdownExtensions::isPageBreakLine(QStringLiteral("<div style=\"break-after:page\"></div>")));
    CHECK(!MarkdownExtensions::isPageBreakLine(QStringLiteral("    ") + PB));
    CHECK(!MarkdownExtensions::isPageBreakLine(QStringLiteral("x ") + PB));
    CHECK(!MarkdownExtensions::isPageBreakLine(PB + QStringLiteral("text")));
    CHECK(!MarkdownExtensions::isPageBreakLine(PB.left(PB.size() - 1)));
    CHECK(!MarkdownExtensions::isPageBreakLine(QStringLiteral("<div style=\"break-after: page;\">x</div>")));
}

void backspaceAfter()
{
    const QString original = QStringLiteral("abc\n") + PB + QStringLiteral("\ndef");
    Editor e(original);
    const int undoSteps = e.editor.document()->availableUndoSteps();
    e.caret(e.position(2, 0));
    e.key(Qt::Key_Backspace);
    CHECK_TEXT(e.editor, QStringLiteral("abc\ndef"));
    CHECK(e.caretPosition() == 4);

    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(e.editor, original);
    e.key(Qt::Key_Y, Qt::ControlModifier);
    CHECK_TEXT(e.editor, QStringLiteral("abc\ndef"));
    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(e.editor, original);
    // availableUndoSteps() counts Qt's raw commands, not user-visible steps;
    // returning to the pre-edit depth proves one Ctrl+Z undid the whole removal.
    CHECK(e.editor.document()->availableUndoSteps() == undoSteps);
}

void deleteBefore()
{
    const QString original = QStringLiteral("abc\n") + PB + QStringLiteral("\ndef");
    Editor e(original);
    const int undoSteps = e.editor.document()->availableUndoSteps();
    e.caret(e.lineEnd(0));
    e.key(Qt::Key_Delete);
    CHECK_TEXT(e.editor, QStringLiteral("abc\ndef"));
    CHECK(e.caretPosition() == 3);

    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(e.editor, original);
    CHECK(e.editor.document()->availableUndoSteps() == undoSteps);
    e.key(Qt::Key_Y, Qt::ControlModifier);
    CHECK_TEXT(e.editor, QStringLiteral("abc\ndef"));
}

void caretInsideMarker()
{
    const QString original = QStringLiteral("abc\n") + PB + QStringLiteral("\ndef");
    Editor b(original);
    b.caret(b.lineEnd(1));
    b.key(Qt::Key_Backspace);
    CHECK_TEXT(b.editor, QStringLiteral("abc\ndef"));

    Editor d(original);
    d.caret(d.position(1, 0));
    d.key(Qt::Key_Delete);
    CHECK_TEXT(d.editor, QStringLiteral("abc\ndef"));
}

void consecutiveBreaks()
{
    const QString original = QStringLiteral("a\n") + PB + QLatin1Char('\n') + PB + QStringLiteral("\nb");
    Editor b(original);
    b.caret(b.position(3, 0));
    b.key(Qt::Key_Backspace);
    CHECK_TEXT(b.editor, QStringLiteral("a\n") + PB + QStringLiteral("\nb"));
    b.key(Qt::Key_Backspace);
    CHECK_TEXT(b.editor, QStringLiteral("a\nb"));
    b.key(Qt::Key_Backspace);
    CHECK_TEXT(b.editor, QStringLiteral("ab"));
    b.key(Qt::Key_Z, Qt::ControlModifier);
    b.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(b.editor, QStringLiteral("a\n") + PB + QStringLiteral("\nb"));
    b.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(b.editor, original);

    Editor d(original);
    d.caret(d.lineEnd(0));
    d.key(Qt::Key_Delete);
    CHECK_TEXT(d.editor, QStringLiteral("a\n") + PB + QStringLiteral("\nb"));
    d.key(Qt::Key_Delete);
    CHECK_TEXT(d.editor, QStringLiteral("a\nb"));
    CHECK(d.caretPosition() == 1);
}

void documentBoundaries()
{
    Editor first(PB + QStringLiteral("\ntext"));
    first.caret(first.position(1, 0));
    first.key(Qt::Key_Backspace);
    CHECK_TEXT(first.editor, QStringLiteral("text"));
    CHECK(first.caretPosition() == 0);

    Editor firstDelete(PB + QStringLiteral("\ntext"));
    firstDelete.caret(0);
    firstDelete.key(Qt::Key_Delete);
    CHECK_TEXT(firstDelete.editor, QStringLiteral("text"));

    Editor last(QStringLiteral("text\n") + PB);
    last.caret(last.lineEnd(0));
    last.key(Qt::Key_Delete);
    CHECK_TEXT(last.editor, QStringLiteral("text"));
    CHECK(last.caretPosition() == 4);

    Editor lastBackspace(QStringLiteral("text\n") + PB);
    lastBackspace.caret(lastBackspace.lineEnd(1));
    lastBackspace.key(Qt::Key_Backspace);
    CHECK_TEXT(lastBackspace.editor, QStringLiteral("text"));

    Editor only(PB);
    only.caret(only.lineEnd(0));
    only.key(Qt::Key_Backspace);
    CHECK_TEXT(only.editor, QString());
}

void insertedBreakRoundTrip()
{
    Editor e(QStringLiteral("abc"));
    e.caret(3);
    e.editor.fmtPageBreak();
    CHECK_TEXT(e.editor, QStringLiteral("abc\n") + PB + QLatin1Char('\n'));
    e.key(Qt::Key_Backspace);
    CHECK_TEXT(e.editor, QStringLiteral("abc\n"));
    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(e.editor, QStringLiteral("abc\n") + PB + QLatin1Char('\n'));
}

void insertAt(const QString& before, int line, int column, const QString& expected,
              int expectedLine, int expectedColumn, int sourceLine)
{
    Editor e(before);
    const int undoSteps = e.editor.document()->availableUndoSteps();
    e.caret(e.position(line, column));
    e.editor.fmtPageBreak();
    checkText(e.editor, expected, sourceLine);
    check(e.caretPosition() == e.position(expectedLine, expectedColumn), "caret after insert", sourceLine);
    e.key(Qt::Key_Z, Qt::ControlModifier);
    checkText(e.editor, before, sourceLine);
    check(e.editor.document()->availableUndoSteps() == undoSteps, "one undo step", sourceLine);
    e.key(Qt::Key_Y, Qt::ControlModifier);
    checkText(e.editor, expected, sourceLine);
}

void insertionFollowsCaret()
{
    const QString PBn = PB + QLatin1Char('\n');
    insertAt(QStringLiteral("abc\nefg"), 1, 0, QStringLiteral("abc\n") + PBn + QStringLiteral("efg"), 2, 0, __LINE__);
    insertAt(QStringLiteral("abc\nefg"), 0, 3, QStringLiteral("abc\n") + PBn + QStringLiteral("\nefg"), 2, 0, __LINE__);
    insertAt(QStringLiteral("abc\nefg"), 1, 1, QStringLiteral("abc\ne\n") + PBn + QStringLiteral("fg"), 3, 0, __LINE__);
    insertAt(QStringLiteral("abc\n\nefg"), 1, 0, QStringLiteral("abc\n") + PBn + QStringLiteral("\nefg"), 2, 0, __LINE__);
    insertAt(QString(), 0, 0, PBn, 1, 0, __LINE__);
    insertAt(QStringLiteral("abc"), 0, 0, PBn + QStringLiteral("abc"), 1, 0, __LINE__);
    insertAt(QStringLiteral("abc"), 0, 3, QStringLiteral("abc\n") + PBn, 2, 0, __LINE__);
    insertAt(QStringLiteral("  - item"), 0, 4, QStringLiteral("  - \n") + PBn + QStringLiteral("item"), 2, 0, __LINE__);
}

void insertionReplacesSelection()
{
    Editor e(QStringLiteral("abcdef"));
    e.select(2, 4);
    e.editor.fmtPageBreak();
    CHECK_TEXT(e.editor, QStringLiteral("ab\n") + PB + QStringLiteral("\nef"));
    CHECK(e.caretPosition() == e.position(2, 0));
    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(e.editor, QStringLiteral("abcdef"));
}

void consecutiveInsertions()
{
    Editor e(QStringLiteral("abc\nefg"));
    e.caret(e.position(1, 0));
    e.editor.fmtPageBreak();
    e.editor.fmtPageBreak();
    CHECK_TEXT(e.editor, QStringLiteral("abc\n") + PB + QLatin1Char('\n') + PB + QStringLiteral("\nefg"));
    CHECK(e.caretPosition() == e.position(3, 0));
    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(e.editor, QStringLiteral("abc\n") + PB + QStringLiteral("\nefg"));
    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(e.editor, QStringLiteral("abc\nefg"));
}

void blankLinesSurvive()
{
    Editor e(QStringLiteral("abc\n\n") + PB + QStringLiteral("\n\ndef"));
    e.caret(e.position(3, 0));
    e.key(Qt::Key_Backspace);
    CHECK_TEXT(e.editor, QStringLiteral("abc\n\n\ndef"));
    CHECK(e.caretPosition() == e.position(2, 0));
}

void wordDeletion()
{
    Editor b(QStringLiteral("abc\n") + PB + QStringLiteral("\ndef"));
    b.caret(b.position(2, 0));
    b.key(Qt::Key_Backspace, Qt::ControlModifier);
    CHECK_TEXT(b.editor, QStringLiteral("abc\ndef"));

    Editor d(QStringLiteral("abc\n") + PB + QStringLiteral("\ndef"));
    d.caret(d.lineEnd(0));
    d.key(Qt::Key_Delete, Qt::ControlModifier);
    CHECK_TEXT(d.editor, QStringLiteral("abc\ndef"));
}

void selections()
{
    const QString original = QStringLiteral("abc\n") + PB + QStringLiteral("\ndef");

    Editor endInside(original);
    endInside.select(2, endInside.position(1, 5));
    endInside.key(Qt::Key_Delete);
    CHECK_TEXT(endInside.editor, QStringLiteral("ab\ndef"));
    endInside.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(endInside.editor, original);

    Editor startInside(original);
    startInside.select(startInside.position(2, 2), startInside.position(1, 5));
    startInside.key(Qt::Key_Backspace);
    CHECK_TEXT(startInside.editor, QStringLiteral("abc\nf"));

    Editor whole(original);
    whole.select(whole.position(1, 0), whole.position(2, 0));
    whole.key(Qt::Key_Backspace);
    CHECK_TEXT(whole.editor, QStringLiteral("abc\ndef"));

    Editor ordinary(original);
    ordinary.select(0, 2);
    ordinary.key(Qt::Key_Delete);
    CHECK_TEXT(ordinary.editor, QStringLiteral("c\n") + PB + QStringLiteral("\ndef"));
}

void ordinaryDeletionUnchanged()
{
    Editor joined(QStringLiteral("abc\ndef"));
    joined.caret(joined.position(1, 0));
    joined.key(Qt::Key_Backspace);
    CHECK_TEXT(joined.editor, QStringLiteral("abcdef"));
    joined.key(Qt::Key_Delete);
    CHECK_TEXT(joined.editor, QStringLiteral("abcef"));

    Editor inline_(QStringLiteral("x ") + PB + QStringLiteral("\ndef"));
    inline_.caret(inline_.position(1, 0));
    inline_.key(Qt::Key_Backspace);
    CHECK_TEXT(inline_.editor, QStringLiteral("x ") + PB + QStringLiteral("def"));

    Editor beside(QStringLiteral("abc\n") + PB + QStringLiteral("\ndef"));
    beside.caret(beside.position(2, 1));
    beside.key(Qt::Key_Backspace);
    CHECK_TEXT(beside.editor, QStringLiteral("abc\n") + PB + QStringLiteral("\nef"));
    beside.caret(beside.position(0, 1));
    beside.key(Qt::Key_Delete);
    CHECK_TEXT(beside.editor, QStringLiteral("ac\n") + PB + QStringLiteral("\nef"));

    Editor readOnly(QStringLiteral("abc\n") + PB + QStringLiteral("\ndef"));
    readOnly.editor.setReadOnly(true);
    readOnly.caret(readOnly.position(2, 0));
    readOnly.key(Qt::Key_Backspace);
    CHECK_TEXT(readOnly.editor, QStringLiteral("abc\n") + PB + QStringLiteral("\ndef"));
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const struct { const char* name; void (*run)(); } cases[] = {
        { "recognizer", recognizer },
        { "backspaceAfter", backspaceAfter },
        { "deleteBefore", deleteBefore },
        { "caretInsideMarker", caretInsideMarker },
        { "consecutiveBreaks", consecutiveBreaks },
        { "documentBoundaries", documentBoundaries },
        { "insertedBreakRoundTrip", insertedBreakRoundTrip },
        { "insertionFollowsCaret", insertionFollowsCaret },
        { "insertionReplacesSelection", insertionReplacesSelection },
        { "consecutiveInsertions", consecutiveInsertions },
        { "blankLinesSurvive", blankLinesSurvive },
        { "wordDeletion", wordDeletion },
        { "selections", selections },
        { "ordinaryDeletionUnchanged", ordinaryDeletionUnchanged },
    };
    for (const auto& testCase : cases) {
        std::printf("%s\n", testCase.name);
        std::fflush(stdout);
        testCase.run();
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
