// Real MarkdownEditor driven by synthetic Alt+drag and key events. Guards that
// a column selection survives the edits made through it -- typing, Backspace,
// Delete, replacing a selection, paste -- on lines of any length, that each
// such edit is one undo step, and that ordinary single-caret editing is not
// affected.
#include "MarkdownEditor.h"

#include <QApplication>
#include <QClipboard>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QStyleHints>
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

    // A point inside the viewport on `line` at `position` in that line's text,
    // or `virtualColumns` space widths past its end.
    QPoint point(int line, int position, int virtualColumns = 0)
    {
        const QTextBlock block = editor.document()->findBlockByNumber(line);
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + position);
        const QRect rect = editor.cursorRect(cursor);
        const int spaceWidth = editor.fontMetrics().horizontalAdvance(QLatin1Char(' '));
        return QPoint(rect.left() + virtualColumns * spaceWidth + 1, rect.center().y());
    }

    void altDrag(const QPoint& from, const QPoint& to)
    {
        QWidget* viewport = editor.viewport();
        QTest::mousePress(viewport, Qt::LeftButton, Qt::AltModifier, from);
        QTest::mouseMove(viewport, to);
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::AltModifier, to);
    }

    void type(const QString& text) { QTest::keyClicks(&editor, text); }
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::keyClick(&editor, key, modifiers);
    }
};

void repeatedTyping()
{
    Editor e(QStringLiteral("abcdef\nabcdef\nabcdef"));
    e.altDrag(e.point(0, 2), e.point(2, 2));
    e.type(QStringLiteral("X"));
    CHECK_TEXT(e.editor, QStringLiteral("abXcdef\nabXcdef\nabXcdef"));
    e.type(QStringLiteral("YZ"));
    CHECK_TEXT(e.editor, QStringLiteral("abXYZcdef\nabXYZcdef\nabXYZcdef"));
    e.key(Qt::Key_Tab);
    e.type(QStringLiteral("!"));
    CHECK_TEXT(e.editor, QStringLiteral("abXYZ\t!cdef\nabXYZ\t!cdef\nabXYZ\t!cdef"));
    CHECK(e.editor.textCursor().blockNumber() == 2);
    CHECK(e.editor.textCursor().positionInBlock() == 7);
}

void upwardDragEditsEveryLine()
{
    Editor e(QStringLiteral("abc\nabc\nabc"));
    e.altDrag(e.point(2, 1), e.point(0, 1));
    e.type(QStringLiteral("12"));
    CHECK_TEXT(e.editor, QStringLiteral("a12bc\na12bc\na12bc"));
    CHECK(e.editor.textCursor().blockNumber() == 0);
}

void repeatedBackspace()
{
    Editor e(QStringLiteral("abcdef\nabcdef\nabcdef"));
    e.altDrag(e.point(0, 3), e.point(2, 3));
    e.key(Qt::Key_Backspace);
    CHECK_TEXT(e.editor, QStringLiteral("abdef\nabdef\nabdef"));
    e.key(Qt::Key_Backspace);
    CHECK_TEXT(e.editor, QStringLiteral("adef\nadef\nadef"));
    e.key(Qt::Key_Backspace);
    CHECK_TEXT(e.editor, QStringLiteral("def\ndef\ndef"));
    e.key(Qt::Key_Backspace);
    CHECK_TEXT(e.editor, QStringLiteral("def\ndef\ndef"));
    e.type(QStringLiteral(">"));
    CHECK_TEXT(e.editor, QStringLiteral(">def\n>def\n>def"));
}

void repeatedDelete()
{
    Editor e(QStringLiteral("abcdef\nabcdef\nabcdef"));
    e.altDrag(e.point(0, 2), e.point(2, 2));
    e.key(Qt::Key_Delete);
    CHECK_TEXT(e.editor, QStringLiteral("abdef\nabdef\nabdef"));
    e.key(Qt::Key_Delete);
    CHECK_TEXT(e.editor, QStringLiteral("abef\nabef\nabef"));
    e.type(QStringLiteral("-"));
    CHECK_TEXT(e.editor, QStringLiteral("ab-ef\nab-ef\nab-ef"));
}

void selectionReplacement()
{
    Editor e(QStringLiteral("abcdef\nabcdef\nabcdef"));
    e.altDrag(e.point(0, 1), e.point(2, 4));
    e.type(QStringLiteral("Z"));
    CHECK_TEXT(e.editor, QStringLiteral("aZef\naZef\naZef"));
    e.type(QStringLiteral("Q"));
    CHECK_TEXT(e.editor, QStringLiteral("aZQef\naZQef\naZQef"));

    Editor d(QStringLiteral("abcdef\nabcdef"));
    d.altDrag(d.point(0, 1), d.point(1, 4));
    d.key(Qt::Key_Backspace);
    CHECK_TEXT(d.editor, QStringLiteral("aef\naef"));
    d.key(Qt::Key_Backspace);
    CHECK_TEXT(d.editor, QStringLiteral("ef\nef"));

    Editor f(QStringLiteral("abcdef\nabcdef"));
    f.altDrag(f.point(0, 1), f.point(1, 4));
    f.key(Qt::Key_Delete);
    CHECK_TEXT(f.editor, QStringLiteral("aef\naef"));
    f.key(Qt::Key_Delete);
    CHECK_TEXT(f.editor, QStringLiteral("af\naf"));
}

void differentLineLengths()
{
    Editor e(QStringLiteral("abcdef\nab\n\nabcdef"));
    e.altDrag(e.point(0, 4), e.point(3, 4));
    e.type(QStringLiteral("X"));
    CHECK_TEXT(e.editor, QStringLiteral("abcdXef\nab  X\n    X\nabcdXef"));
    e.type(QStringLiteral("Y"));
    CHECK_TEXT(e.editor, QStringLiteral("abcdXYef\nab  XY\n    XY\nabcdXYef"));
    e.key(Qt::Key_Backspace);
    e.key(Qt::Key_Backspace);
    CHECK_TEXT(e.editor, QStringLiteral("abcdef\nab  \n    \nabcdef"));

    // A caret in a short line's virtual space never deletes its real text.
    Editor b(QStringLiteral("abcdef\nab\nabcdef"));
    b.altDrag(b.point(0, 4), b.point(2, 4));
    b.key(Qt::Key_Backspace);
    CHECK_TEXT(b.editor, QStringLiteral("abcef\nab\nabcef"));
    b.key(Qt::Key_Backspace);
    CHECK_TEXT(b.editor, QStringLiteral("abef\nab\nabef"));
    b.key(Qt::Key_Backspace);
    CHECK_TEXT(b.editor, QStringLiteral("aef\na\naef"));

    Editor d(QStringLiteral("abcdef\nab\nabcdef"));
    d.altDrag(d.point(0, 4), d.point(2, 4));
    d.key(Qt::Key_Delete);
    CHECK_TEXT(d.editor, QStringLiteral("abcdf\nab\nabcdf"));

    // Deleting a selection never pads a short line with trailing spaces.
    Editor s(QStringLiteral("abcdef\nab\nabcdef"));
    s.altDrag(s.point(0, 3), s.point(2, 5));
    s.key(Qt::Key_Delete);
    CHECK_TEXT(s.editor, QStringLiteral("abcf\nab\nabcf"));

    // Dragged entirely past the ends of the lines.
    Editor v(QStringLiteral("a\nab\na"));
    v.altDrag(v.point(0, 1, 3), v.point(2, 1, 3));
    v.type(QStringLiteral("|"));
    CHECK_TEXT(v.editor, QStringLiteral("a   |\nab  |\na   |"));
    v.key(Qt::Key_Backspace);
    v.key(Qt::Key_Backspace);
    CHECK_TEXT(v.editor, QStringLiteral("a  \nab \na  "));
}

void tabs()
{
    Editor e(QStringLiteral("\t\tx\n\t\ty"));
    e.altDrag(e.point(0, 2), e.point(1, 2));
    e.key(Qt::Key_Backspace);
    CHECK_TEXT(e.editor, QStringLiteral("\tx\n\ty"));
    e.type(QStringLiteral("-"));
    CHECK_TEXT(e.editor, QStringLiteral("\t-x\n\t-y"));
    e.key(Qt::Key_Backspace);
    e.key(Qt::Key_Backspace);
    CHECK_TEXT(e.editor, QStringLiteral("x\ny"));

    // Mixed indentation stays aligned: the whole tab span goes on each line.
    Editor m(QStringLiteral("ab\tx\nabcdx"));
    m.altDrag(m.point(0, 3), m.point(1, 4));
    m.key(Qt::Key_Backspace);
    CHECK_TEXT(m.editor, QStringLiteral("abx\nabx"));
    m.type(QStringLiteral("_"));
    CHECK_TEXT(m.editor, QStringLiteral("ab_x\nab_x"));

    Editor f(QStringLiteral("a\tb\na\tc"));
    f.altDrag(f.point(0, 1), f.point(1, 1));
    f.key(Qt::Key_Delete);
    CHECK_TEXT(f.editor, QStringLiteral("ab\nac"));
}

// A tab stays four columns wide after the font changes size (editor zoom) on an
// editor that already exists, so it lines up with four spaces, and column
// selection measures it the same way.
void tabWidthFollowsFont()
{
    Editor e(QStringLiteral("\tx\n    x"));
    const auto tabAlignsWithSpaces = [&e]() {
        QTextCursor afterTab(e.editor.document()->findBlockByNumber(0));
        afterTab.setPosition(1);
        QTextCursor afterSpaces(e.editor.document()->findBlockByNumber(1));
        afterSpaces.setPosition(afterSpaces.block().position() + 4);
        return qAbs(e.editor.cursorRect(afterTab).left() - e.editor.cursorRect(afterSpaces).left()) <= 1;
    };
    CHECK(tabAlignsWithSpaces());
    for (const int pixelSize : { 28, 9 }) {
        QFont font = e.editor.font();
        font.setPixelSize(pixelSize);
        e.editor.setFont(font);
        QApplication::processEvents();
        CHECK(tabAlignsWithSpaces());
    }
    e.altDrag(e.point(0, 1), e.point(1, 4));
    e.type(QStringLiteral("-"));
    CHECK_TEXT(e.editor, QStringLiteral("\t-x\n    -x"));
}

void paste()
{
    Editor e(QStringLiteral("abc\nabc\nabc"));
    QApplication::clipboard()->setText(QStringLiteral("--"));
    e.altDrag(e.point(0, 1), e.point(2, 1));
    e.key(Qt::Key_V, Qt::ControlModifier);
    CHECK_TEXT(e.editor, QStringLiteral("a--bc\na--bc\na--bc"));
    e.key(Qt::Key_V, Qt::ControlModifier);
    CHECK_TEXT(e.editor, QStringLiteral("a----bc\na----bc\na----bc"));

    Editor m(QStringLiteral("abc\nabc\nabc"));
    QApplication::clipboard()->setText(QStringLiteral("12\n34\n56"));
    m.altDrag(m.point(0, 1), m.point(2, 1));
    m.key(Qt::Key_V, Qt::ControlModifier);
    CHECK_TEXT(m.editor, QStringLiteral("a12bc\na34bc\na56bc"));
    m.type(QStringLiteral("|"));
    CHECK_TEXT(m.editor, QStringLiteral("a12|bc\na34|bc\na56|bc"));

    // Ragged lines share no column: only the active line keeps its caret.
    Editor r(QStringLiteral("abc\nabc\nabc"));
    QApplication::clipboard()->setText(QStringLiteral("1\n22\n333"));
    r.altDrag(r.point(0, 1), r.point(2, 1));
    r.key(Qt::Key_V, Qt::ControlModifier);
    CHECK_TEXT(r.editor, QStringLiteral("a1bc\na22bc\na333bc"));
    r.type(QStringLiteral("|"));
    CHECK_TEXT(r.editor, QStringLiteral("a1bc\na22bc\na333|bc"));
}

void undoRedo()
{
    Editor e(QStringLiteral("abcdef\nabcdef\nabcdef"));
    e.altDrag(e.point(0, 2), e.point(2, 2));
    e.type(QStringLiteral("XY"));
    CHECK_TEXT(e.editor, QStringLiteral("abXYcdef\nabXYcdef\nabXYcdef"));

    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(e.editor, QStringLiteral("abXcdef\nabXcdef\nabXcdef"));
    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(e.editor, QStringLiteral("abcdef\nabcdef\nabcdef"));
    e.key(Qt::Key_Y, Qt::ControlModifier);
    CHECK_TEXT(e.editor, QStringLiteral("abXcdef\nabXcdef\nabXcdef"));

    // Undo leaves column mode: typing goes to the single restored cursor.
    e.type(QStringLiteral("!"));
    CHECK(e.editor.toPlainText().count(QLatin1Char('!')) == 1);

    Editor b(QStringLiteral("abcdef\nabcdef"));
    b.altDrag(b.point(0, 1), b.point(1, 4));
    b.key(Qt::Key_Backspace);
    b.key(Qt::Key_Backspace);
    CHECK_TEXT(b.editor, QStringLiteral("ef\nef"));
    b.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(b.editor, QStringLiteral("aef\naef"));
    b.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK_TEXT(b.editor, QStringLiteral("abcdef\nabcdef"));

    // Undo issued from outside the editor (Edit > Undo) also leaves column mode.
    Editor m(QStringLiteral("abc\nabc"));
    m.altDrag(m.point(0, 1), m.point(1, 1));
    m.type(QStringLiteral("X"));
    m.editor.undo();
    CHECK_TEXT(m.editor, QStringLiteral("abc\nabc"));
    m.type(QStringLiteral("Y"));
    CHECK(m.editor.toPlainText().count(QLatin1Char('Y')) == 1);
}

void leavingColumnMode()
{
    Editor e(QStringLiteral("abc\nabc"));
    e.altDrag(e.point(0, 1), e.point(1, 1));
    e.key(Qt::Key_Escape);
    e.type(QStringLiteral("X"));
    CHECK(e.editor.toPlainText().count(QLatin1Char('X')) == 1);

    Editor c(QStringLiteral("abc\nabc"));
    c.altDrag(c.point(0, 1), c.point(1, 1));
    QTest::mouseClick(c.editor.viewport(), Qt::LeftButton, Qt::NoModifier, c.point(0, 3));
    c.type(QStringLiteral("X"));
    CHECK_TEXT(c.editor, QStringLiteral("abcX\nabc"));

    Editor a(QStringLiteral("abc\nabc"));
    a.altDrag(a.point(0, 1), a.point(1, 1));
    a.key(Qt::Key_Right);
    a.type(QStringLiteral("X"));
    CHECK(a.editor.toPlainText().count(QLatin1Char('X')) == 1);
}

// Whether a caret is drawn at `caret` in the rendered viewport. The caret sits
// between two spaces, so its column is otherwise plain background; that
// background is read from the same row half a space to the right, which also
// accounts for the current-line highlight on the active line.
bool caretInked(const QImage& image, const QRect& caret, int spaceWidth)
{
    const qreal dpr = image.devicePixelRatio();
    const int x = qRound((caret.left() + 0.5) * dpr);
    const int backgroundX = qRound((caret.left() + spaceWidth / 2.0) * dpr);
    for (int y = qRound((caret.top() + 2) * dpr); y < qRound((caret.bottom() - 2) * dpr); ++y) {
        const QColor ink = image.pixelColor(x, y);
        const QColor background = image.pixelColor(backgroundX, y);
        const int difference = qAbs(ink.red() - background.red()) + qAbs(ink.green() - background.green())
            + qAbs(ink.blue() - background.blue());
        if (difference > 120)
            return true;
    }
    return false;
}

struct CaretSample {
    bool primary = false;
    bool first = false;
    bool middle = false;
};

CaretSample sampleCarets(Editor& e, int position)
{
    const QImage image = e.editor.viewport()->grab().toImage();
    const int spaceWidth = e.editor.fontMetrics().horizontalAdvance(QLatin1Char(' '));
    auto rectAt = [&](int line) {
        const QTextBlock block = e.editor.document()->findBlockByNumber(line);
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + position);
        return e.editor.cursorRect(cursor);
    };
    CaretSample sample;
    sample.primary = caretInked(image, e.editor.cursorRect(), spaceWidth);
    sample.first = caretInked(image, rectAt(0), spaceWidth);
    sample.middle = caretInked(image, rectAt(1), spaceWidth);
    return sample;
}

void focusEditor(Editor& e, QEvent::Type type)
{
    QFocusEvent event(type, Qt::OtherFocusReason);
    QApplication::sendEvent(&e.editor, &event);
}

// Every column caret follows the primary caret's blink phase: sampled across
// several blink periods they are always all lit or all dark, and both phases
// occur. Edits keep that true at the carets' new positions, and an unfocused
// editor draws none of them.
void caretsBlinkTogether()
{
    Editor e(QStringLiteral("ab  cd\nab  cd\nab  cd"));
    e.altDrag(e.point(0, 3), e.point(2, 3));
    focusEditor(e, QEvent::FocusIn);

    const int flashTime = QGuiApplication::styleHints()->cursorFlashTime();
    const int interval = flashTime >= 2 ? qMax(60, flashTime / 7) : 100;
    int lit = 0;
    int dark = 0;
    int position = 3;
    for (int i = 0; i < 24; ++i) {
        if (i == 12) {
            e.type(QStringLiteral("  "));
            position += 2;
        }
        const CaretSample sample = sampleCarets(e, position);
        CHECK(sample.first == sample.primary);
        CHECK(sample.middle == sample.primary);
        (sample.primary ? lit : dark) += 1;
        QTest::qWait(interval);
    }
    CHECK_TEXT(e.editor, QStringLiteral("ab    cd\nab    cd\nab    cd"));
    CHECK(lit > 0);
    if (flashTime >= 2)
        CHECK(dark > 0);

    focusEditor(e, QEvent::FocusOut);
    for (int i = 0; i < 6; ++i) {
        const CaretSample sample = sampleCarets(e, position);
        CHECK(!sample.primary && !sample.first && !sample.middle);
        QTest::qWait(interval);
    }

    focusEditor(e, QEvent::FocusIn);
    const CaretSample regained = sampleCarets(e, position);
    CHECK(regained.primary && regained.first && regained.middle);
}

// Plain single-caret editing still blinks, and paints no column carets.
void singleCaretBlinks()
{
    Editor e(QStringLiteral("ab  cd\nab  cd"));
    QTest::mouseClick(e.editor.viewport(), Qt::LeftButton, Qt::NoModifier, e.point(1, 3));
    focusEditor(e, QEvent::FocusIn);

    const int flashTime = QGuiApplication::styleHints()->cursorFlashTime();
    const int interval = flashTime >= 2 ? qMax(60, flashTime / 7) : 100;
    int lit = 0;
    int dark = 0;
    for (int i = 0; i < 14; ++i) {
        const CaretSample sample = sampleCarets(e, 3);
        CHECK(!sample.first);
        (sample.middle ? lit : dark) += 1;
        QTest::qWait(interval);
    }
    CHECK(lit > 0);
    if (flashTime >= 2)
        CHECK(dark > 0);
}

void singleCaretEditing()
{
    Editor e(QStringLiteral("abc"));
    QTest::mouseClick(e.editor.viewport(), Qt::LeftButton, Qt::NoModifier, e.point(0, 3));
    e.type(QStringLiteral("de"));
    CHECK_TEXT(e.editor, QStringLiteral("abcde"));
    e.key(Qt::Key_Backspace);
    CHECK_TEXT(e.editor, QStringLiteral("abcd"));
    e.key(Qt::Key_Home);
    e.key(Qt::Key_Delete);
    CHECK_TEXT(e.editor, QStringLiteral("bcd"));
    e.key(Qt::Key_End);
    e.key(Qt::Key_Return);
    e.type(QStringLiteral("x"));
    CHECK_TEXT(e.editor, QStringLiteral("bcd\nx"));
    e.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK(e.editor.toPlainText().startsWith(QStringLiteral("bcd")));

    Editor l(QStringLiteral("- item"));
    QTest::mouseClick(l.editor.viewport(), Qt::LeftButton, Qt::NoModifier, l.point(0, 6));
    l.key(Qt::Key_Return);
    l.type(QStringLiteral("next"));
    CHECK_TEXT(l.editor, QStringLiteral("- item\n- next"));

    Editor i(QStringLiteral("    code"));
    QTest::mouseClick(i.editor.viewport(), Qt::LeftButton, Qt::NoModifier, i.point(0, 8));
    i.key(Qt::Key_Return);
    i.type(QStringLiteral("more"));
    CHECK_TEXT(i.editor, QStringLiteral("    code\n    more"));
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const struct { const char* name; void (*run)(); } cases[] = {
        { "repeatedTyping", repeatedTyping },
        { "upwardDragEditsEveryLine", upwardDragEditsEveryLine },
        { "repeatedBackspace", repeatedBackspace },
        { "repeatedDelete", repeatedDelete },
        { "selectionReplacement", selectionReplacement },
        { "differentLineLengths", differentLineLengths },
        { "tabs", tabs },
        { "tabWidthFollowsFont", tabWidthFollowsFont },
        { "paste", paste },
        { "undoRedo", undoRedo },
        { "leavingColumnMode", leavingColumnMode },
        { "singleCaretEditing", singleCaretEditing },
        { "caretsBlinkTogether", caretsBlinkTogether },
        { "singleCaretBlinks", singleCaretBlinks },
    };
    for (const auto& testCase : cases) {
        std::printf("%s\n", testCase.name);
        std::fflush(stdout);
        testCase.run();
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
