#include "MarkdownInlineFormatting.h"

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

QTextCursor selection(QTextDocument& document, int start, int end)
{
    QTextCursor cursor(&document);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    return cursor;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    QTextDocument inserted;
    inserted.setPlainText(QStringLiteral("abc"));
    QTextCursor caret(&inserted);
    caret.setPosition(1);
    MarkdownInlineFormatting::toggleMark(caret);
    ok &= require(inserted.toPlainText() == QStringLiteral("a<mark></mark>bc")
                      && caret.position() == 7 && !caret.hasSelection(),
                  QStringLiteral("empty mark insertion did not leave the caret inside the tags"));

    QTextDocument wrapped;
    wrapped.setPlainText(QStringLiteral("before **bold** [link](https://example.test) `code` after"));
    const int start = wrapped.toPlainText().indexOf(QStringLiteral("**bold**"));
    const int end = wrapped.toPlainText().indexOf(QStringLiteral(" after"));
    QTextCursor wrapCursor = selection(wrapped, start, end);
    MarkdownInlineFormatting::toggleMark(wrapCursor);
    ok &= require(wrapped.toPlainText() == QStringLiteral("before <mark>**bold** [link](https://example.test) `code`</mark> after")
                      && wrapCursor.selectedText() == QStringLiteral("**bold** [link](https://example.test) `code`"),
                  QStringLiteral("mark wrapping changed inline Markdown or did not preserve the content selection"));

    MarkdownInlineFormatting::toggleMark(wrapCursor);
    ok &= require(wrapped.toPlainText() == QStringLiteral("before **bold** [link](https://example.test) `code` after")
                      && wrapCursor.selectedText() == QStringLiteral("**bold** [link](https://example.test) `code`"),
                  QStringLiteral("content-only mark toggle did not remove exactly one wrapper"));

    QTextDocument inclusive;
    inclusive.setPlainText(QStringLiteral("<mark>chosen</mark>"));
    QTextCursor inclusiveCursor = selection(inclusive, 0, inclusive.toPlainText().size());
    MarkdownInlineFormatting::toggleMark(inclusiveCursor);
    ok &= require(inclusive.toPlainText() == QStringLiteral("chosen")
                      && inclusiveCursor.selectedText() == QStringLiteral("chosen"),
                  QStringLiteral("wrapper-inclusive mark toggle did not retain only its content"));

    QTextDocument multiline;
    multiline.setPlainText(QStringLiteral("one\ntwo"));
    QTextCursor multilineCursor = selection(multiline, 0, multiline.toPlainText().size());
    MarkdownInlineFormatting::toggleMark(multilineCursor);
    ok &= require(multiline.toPlainText() == QStringLiteral("<mark>one\ntwo</mark>"),
                  QStringLiteral("multiline selection was not wrapped intact"));

    QTextDocument list;
    list.setPlainText(QStringLiteral("- first item\n- second item"));
    QTextCursor listCursor = selection(list, 0, list.toPlainText().size());
    MarkdownInlineFormatting::toggleMark(listCursor);
    ok &= require(list.toPlainText() == QStringLiteral("- <mark>first item</mark>\n- <mark>second item</mark>"),
                  QStringLiteral("mark wrapping consumed Markdown list markers: ") + list.toPlainText());

    QTextDocument code;
    code.setPlainText(QStringLiteral("Before `literal` after"));
    QTextCursor codeCursor = selection(code, 8, 15);
    MarkdownInlineFormatting::toggleMark(codeCursor);
    ok &= require(code.toPlainText() == QStringLiteral("Before <mark>`literal`</mark> after"),
                  QStringLiteral("mark wrapping was inserted inside an inline code span: ") + code.toPlainText());

    return ok ? 0 : 1;
}
