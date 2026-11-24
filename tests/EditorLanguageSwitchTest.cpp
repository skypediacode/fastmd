// Regression cover for the invariant the status-bar language selector depends
// on: changing a document's syntax definition is a presentation-only operation.
// It must never alter the text, the cursor/selection, the modified flag, or the
// undo/redo stacks -- otherwise switching language would dirty the tab and
// schedule an auto-save of a document the user never edited.
#include "SourceHighlighter.h"
#include "SyntaxDefinitionResolver.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTextCursor>
#include <QTextDocument>

namespace {

bool require(bool condition, const QString& message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

KSyntaxHighlighting::Definition definitionFor(const QString& name)
{
    return SyntaxDefinitionResolver::definitionForStoredName(name);
}

struct DocumentSnapshot {
    QString text;
    int cursorPosition = 0;
    int anchor = 0;
    bool modified = false;
    bool undoAvailable = false;
    bool redoAvailable = false;

    static DocumentSnapshot capture(const QTextDocument& doc, const QTextCursor& cursor)
    {
        return { doc.toPlainText(), cursor.position(), cursor.anchor(), doc.isModified(),
                 doc.isUndoAvailable(), doc.isRedoAvailable() };
    }

    bool operator==(const DocumentSnapshot& other) const
    {
        return text == other.text && cursorPosition == other.cursorPosition
            && anchor == other.anchor && modified == other.modified
            && undoAvailable == other.undoAvailable && redoAvailable == other.redoAvailable;
    }
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    QTextDocument doc;
    doc.setPlainText(QStringLiteral("#include <cstdio>\nint main() { return 0; }\n"));

    // A real edit, then an undo, so both undo and redo have something in them
    // and the modified flag is genuinely set by user input.
    QTextCursor cursor(&doc);
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("// first\n"));
    cursor.insertText(QStringLiteral("// second\n"));
    doc.undo();
    doc.setModified(true);
    cursor.setPosition(5);
    cursor.setPosition(11, QTextCursor::KeepAnchor);

    ok &= require(doc.isUndoAvailable() && doc.isRedoAvailable(),
                  QStringLiteral("test setup did not leave both undo and redo available"));

    auto* highlighter = new SourceHighlighter(&doc);
    const DocumentSnapshot before = DocumentSnapshot::capture(doc, cursor);

    // C++ -> Python -> Markdown-free Plain Text (invalid definition), the exact
    // sequence a user produces by clicking through the status-bar selector.
    const QStringList sequence = { QStringLiteral("C++"), QStringLiteral("Python"),
                                   QStringLiteral("JavaScript") };
    for (const QString& name : sequence) {
        const KSyntaxHighlighting::Definition definition = definitionFor(name);
        ok &= require(definition.isValid(), QStringLiteral("test prerequisite missing: ") + name);
        highlighter->setDefinition(definition);
        ok &= require(highlighter->definition().name() == name,
                      QStringLiteral("highlighter did not adopt definition ") + name);
        ok &= require(DocumentSnapshot::capture(doc, cursor) == before,
                      QStringLiteral("switching to %1 changed document/cursor/undo state").arg(name));
    }

    // An invalid definition is how Plain Text is represented; it must degrade to
    // unhighlighted text rather than keeping the previous language or crashing.
    highlighter->setDefinition(KSyntaxHighlighting::Definition());
    ok &= require(!highlighter->definition().isValid(),
                  QStringLiteral("Plain Text (invalid definition) did not clear the previous language"));
    ok &= require(DocumentSnapshot::capture(doc, cursor) == before,
                  QStringLiteral("switching to Plain Text changed document/cursor/undo state"));

    // Theme changes reapply formatting and must be equally side-effect free.
    highlighter->setDefinition(definitionFor(QStringLiteral("C++")));
    highlighter->setDarkMode(true);
    ok &= require(DocumentSnapshot::capture(doc, cursor) == before,
                  QStringLiteral("dark-theme reapplication changed document/cursor/undo state"));
    highlighter->setDarkMode(false);
    ok &= require(DocumentSnapshot::capture(doc, cursor) == before,
                  QStringLiteral("light-theme reapplication changed document/cursor/undo state"));

    // No queued rehighlight may land after the switch and dirty the document
    // behind MainWindow's back -- the tab-dirty logic assumes this.
    QCoreApplication::processEvents();
    ok &= require(DocumentSnapshot::capture(doc, cursor) == before,
                  QStringLiteral("a deferred highlighting pass changed document state after the switch"));

    delete highlighter;
    return ok ? 0 : 1;
}
