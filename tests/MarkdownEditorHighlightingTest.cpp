// Real MarkdownEditor + MarkdownHighlighter, no MainWindow. Guards how loading a
// document reaches the highlighter: one whole-document contentsChange, with the
// 150% line height applied to every block, and a large document highlighted in
// time slices that converge on exactly what a synchronous full pass produces --
// including after edits made while the slices are still running.
#include "MarkdownEditor.h"
#include "MarkdownHighlighter.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
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

QString sampleMarkdown(int sections)
{
    QString text = QStringLiteral("---\ntitle: Sample\n---\n\n");
    for (int i = 0; i < sections; ++i) {
        text += QStringLiteral("# Heading %1\n\nSome *emphasis*, **bold** and `code`.\n\n").arg(i);
        text += QStringLiteral("- item with [link](https://example.com)\n> quoted <mark>text</mark>\n\n");
        switch (i % 4) {
        case 0:
            text += QStringLiteral("```cpp\nint value = %1; // comment\n/* block\n   comment */\n```\n\n").arg(i);
            break;
        case 1:
            text += QStringLiteral("```\nplain fence %1\n# not a heading\n```\n\n").arg(i);
            break;
        case 2:
            text += QStringLiteral("~~~python\ndef f():\n    return \"%1\"\n~~~\n\n").arg(i);
            break;
        default:
            text += QStringLiteral("    indented code %1\n\n---\n\n").arg(i);
            break;
        }
    }
    return text;
}

struct BlockSnapshot {
    QList<QTextLayout::FormatRange> formats;
    int userState = -1;
    bool operator==(const BlockSnapshot& other) const
    {
        return formats == other.formats && userState == other.userState;
    }
};

QList<BlockSnapshot> snapshot(const QTextDocument* document)
{
    QList<BlockSnapshot> blocks;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next())
        blocks.append({ block.layout()->formats(), block.userState() });
    return blocks;
}

// The synchronous whole-document pass the editor used before highlighting was
// time-sliced.
QList<BlockSnapshot> fullPassReference(const QString& text)
{
    QTextDocument document;
    document.setPlainText(text);
    MarkdownHighlighter highlighter(&document);
    highlighter.rehighlight();
    return snapshot(&document);
}

int firstMismatch(const QList<BlockSnapshot>& actual, const QList<BlockSnapshot>& expected)
{
    if (actual.size() != expected.size())
        return -2;
    for (int i = 0; i < actual.size(); ++i) {
        if (!(actual.at(i) == expected.at(i)))
            return i;
    }
    return -1;
}

MarkdownHighlighter* highlighterOf(MarkdownEditor& editor)
{
    return editor.document()->findChild<MarkdownHighlighter*>();
}

bool waitForHighlighting(MarkdownHighlighter* highlighter)
{
    QElapsedTimer timer;
    timer.start();
    while (!highlighter->isHighlightingComplete() && timer.elapsed() < 120000)
        QApplication::processEvents(QEventLoop::AllEvents, 50);
    return highlighter->isHighlightingComplete();
}

bool everyBlockHasEditorLineHeight(const QTextDocument* document)
{
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat format = block.blockFormat();
        if (format.lineHeightType() != QTextBlockFormat::ProportionalHeight || format.lineHeight() != 150)
            return false;
    }
    return true;
}

void loadReportsOneContentsChange()
{
    MarkdownEditor editor(nullptr, 14);
    int insertions = 0;
    QObject::connect(editor.document(), &QTextDocument::contentsChange, &editor,
                     [&insertions](int, int, int added) { insertions += added > 0 ? 1 : 0; });

    editor.setPlainText(sampleMarkdown(200));
    CHECK(insertions == 1);
    CHECK(everyBlockHasEditorLineHeight(editor.document()));

    const QTextBlock heading = editor.document()->findBlockByNumber(4);
    CHECK(!heading.layout()->formats().isEmpty());

    insertions = 0;
    editor.setPlainText(QStringLiteral("plain\ntext\n"));
    CHECK(insertions == 1);
    CHECK(everyBlockHasEditorLineHeight(editor.document()));
}

void smallDocumentIsHighlightedBeforeLoadReturns()
{
    MarkdownEditor editor(nullptr, 14);
    const QString text = sampleMarkdown(4);
    editor.setPlainText(text);
    MarkdownHighlighter* highlighter = highlighterOf(editor);
    CHECK(highlighter && highlighter->isHighlightingComplete());
    CHECK(firstMismatch(snapshot(editor.document()), fullPassReference(text)) == -1);
}

void largeDocumentConvergesOnTheFullPass()
{
    MarkdownEditor editor(nullptr, 14);
    const QString text = sampleMarkdown(6000);
    int textChanges = 0;
    editor.setPlainText(text);
    editor.document()->setModified(false);
    QObject::connect(&editor, &QPlainTextEdit::textChanged, &editor, [&textChanges] { ++textChanges; });

    MarkdownHighlighter* highlighter = highlighterOf(editor);
    CHECK(highlighter && !highlighter->isHighlightingComplete());
    if (!highlighter)
        return;
    const QList<BlockSnapshot> reference = fullPassReference(text);

    // Fence state is exact for every block before any of it is highlighted.
    bool statesExact = true;
    int number = 0;
    for (QTextBlock block = editor.document()->begin(); block.isValid(); block = block.next(), ++number)
        statesExact = statesExact && block.userState() == reference.at(number).userState;
    CHECK(statesExact);

    // A region far beyond the pass is highlighted on request, inside a fence too.
    const int far = editor.document()->blockCount() - 200;
    const QTextBlock first = editor.document()->findBlockByNumber(far);
    const QTextBlock last = editor.document()->findBlockByNumber(far + 40);
    CHECK(first.layout()->formats().isEmpty());
    highlighter->ensureHighlighted(first, last);
    CHECK(!highlighter->isHighlightingComplete());
    const QList<BlockSnapshot> early = snapshot(editor.document());
    int matching = 0;
    for (int i = far; i <= far + 40; ++i) {
        if (early.at(i) == reference.at(i))
            ++matching;
    }
    CHECK(matching == 41);

    CHECK(waitForHighlighting(highlighter));
    CHECK(firstMismatch(snapshot(editor.document()), reference) == -1);
    CHECK(textChanges == 0);
    CHECK(!editor.document()->isModified());
}

void editsWhileHighlightingConverge()
{
    MarkdownEditor editor(nullptr, 14);
    editor.setPlainText(sampleMarkdown(6000));
    MarkdownHighlighter* highlighter = highlighterOf(editor);
    CHECK(highlighter && !highlighter->isHighlightingComplete());
    if (!highlighter)
        return;
    QTextDocument* document = editor.document();
    auto blockStart = [document](int number) { return document->findBlockByNumber(number).position(); };

    // Opening a fence near the top flips the fence state of everything below:
    // the cascade is cut short and handed back to the pass.
    QTextCursor cursor(document);
    cursor.setPosition(blockStart(6));
    cursor.insertText(QStringLiteral("```\n"));
    QApplication::processEvents();

    // Typing in a region the pass has not reached, then in one it has.
    cursor.setPosition(blockStart(document->blockCount() - 500) + 2);
    cursor.insertText(QStringLiteral("typed **far** away"));
    for (int i = 0; i < 5; ++i)
        QApplication::processEvents();
    cursor.setPosition(blockStart(20));
    cursor.insertText(QStringLiteral("\n"));

    // Removing a closing fence in the middle, then closing the one opened above.
    QTextBlock closing = document->findBlockByNumber(document->blockCount() / 2);
    while (closing.isValid() && closing.text() != QStringLiteral("```"))
        closing = closing.next();
    if (closing.isValid()) {
        cursor.setPosition(closing.position());
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    cursor.setPosition(blockStart(8));
    cursor.insertText(QStringLiteral("```\n"));

    CHECK(waitForHighlighting(highlighter));
    CHECK(firstMismatch(snapshot(document), fullPassReference(editor.toPlainText())) == -1);

    // After completion, a fence edit still re-highlights its neighbourhood on
    // the keystroke; the far side of a long cascade follows in slices.
    cursor.setPosition(blockStart(4));
    cursor.insertText(QStringLiteral("```\n"));
    const int mismatch = firstMismatch(snapshot(document), fullPassReference(editor.toPlainText()));
    CHECK(mismatch == -1 || mismatch > 4 + 512);
    std::printf("first deferred block after a top fence edit: %d\n", mismatch);
    CHECK(waitForHighlighting(highlighter));
    CHECK(firstMismatch(snapshot(document), fullPassReference(editor.toPlainText())) == -1);
}

void reloadRestartsTheHighlighting()
{
    MarkdownEditor editor(nullptr, 14);
    editor.setPlainText(sampleMarkdown(6000));
    MarkdownHighlighter* highlighter = highlighterOf(editor);
    for (int i = 0; i < 3; ++i)
        QApplication::processEvents();
    const QString replacement = sampleMarkdown(3000).replace(QStringLiteral("```cpp"), QStringLiteral("```"));
    editor.setPlainText(replacement);
    CHECK(highlighter == highlighterOf(editor));
    CHECK(waitForHighlighting(highlighter));
    CHECK(firstMismatch(snapshot(editor.document()), fullPassReference(replacement)) == -1);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    loadReportsOneContentsChange();
    smallDocumentIsHighlightedBeforeLoadReturns();
    largeDocumentConvergesOnTheFullPass();
    editsWhileHighlightingConverge();
    reloadRestartsTheHighlighting();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
