#include "MarkdownExtensions.h"
#include "MarkdownHighlighter.h"
#include "MarkdownToc.h"

#include "md4c-html.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTextBlock>
#include <QTextDocument>

namespace {

bool require(bool condition, const QString& message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

QTextCharFormat formatAt(const QTextBlock& block, int position)
{
    for (const QTextLayout::FormatRange& range : block.layout()->formats()) {
        if (position >= range.start && position < range.start + range.length)
            return range.format;
    }
    return {};
}

QString render(const QString& markdown)
{
    const QByteArray utf8 = markdown.toUtf8();
    QString html;
    auto cb = [](const MD_CHAR* out, MD_SIZE size, void* userData) {
        static_cast<QString*>(userData)->append(QString::fromUtf8(out, static_cast<int>(size)));
    };
    md_html(utf8.constData(), MD_SIZE(utf8.size()), cb, &html,
            MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS, 0);
    return html;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStringList snippets;
    const QString source = QStringLiteral(
        "==highlight== 2^10^ H~2~O :rocket: :warning: :smile: 5:07:13\n"
        "`==literal== ^literal^ ~literal~ :rocket:`\n"
        "```markdown\n==code== 2^10^ :rocket:\n```\n");
    const QString preprocessed = MarkdownExtensions::preprocess(source, &snippets);
    bool ok = true;
    ok &= require(preprocessed.contains(QStringLiteral("@@FASTMD_EXT_")),
                  QStringLiteral("inline extension syntax was not tokenized"));
    ok &= require(preprocessed.contains(QStringLiteral("`==literal== ^literal^ ~literal~ :rocket:`")),
                  QStringLiteral("inline code was changed"));
    ok &= require(preprocessed.contains(QStringLiteral("==code== 2^10^ :rocket:")),
                  QStringLiteral("fenced code was changed"));

    const QString restored = MarkdownExtensions::restore(preprocessed, snippets);
    ok &= require(restored.contains(QStringLiteral("<mark>highlight</mark>")),
                  QStringLiteral("highlight was not restored"));
    ok &= require(restored.contains(QStringLiteral("<sup>10</sup>")),
                  QStringLiteral("superscript was not restored"));
    ok &= require(restored.contains(QStringLiteral("<sub>2</sub>")),
                  QStringLiteral("subscript was not restored"));
    ok &= require(restored.contains(QStringLiteral("🚀")) && restored.contains(QStringLiteral("⚠️"))
                      && restored.contains(QStringLiteral("😄")),
                  QStringLiteral("emoji shortcode was not expanded"));
    ok &= require(restored.contains(QStringLiteral("5:07:13")),
                  QStringLiteral("time was changed by emoji shortcode matching"));

    QTextDocument document;
    document.setPlainText(QStringLiteral(
        "Normal ==highlighted text== end.\n"
        "Multiple ==highlights== in one ==sentence==.\n"
        "==start== middle ==end==\n"
        "Before ==two words== after.\n"
        "Unclosed ==highlight must not color the rest of the document.\n"
        "`==inside inline code==`\n"
        "```text\n"
        "==inside fenced code==\n"
        "```"));
    MarkdownHighlighter highlighter(&document);
    highlighter.rehighlight();
    const QTextBlock first = document.firstBlock();
    const int firstHighlight = first.text().indexOf(QStringLiteral("==highlighted text=="));
    const QColor highlightBackground = formatAt(first, firstHighlight + 2).background().color();
    ok &= require(highlightBackground == QColor(0xff, 0xf3, 0xb8),
                  QStringLiteral("highlight content range was not styled"));
    ok &= require(formatAt(first, 0).background().color() != highlightBackground,
                  QStringLiteral("plain text before highlight was styled"));
    ok &= require(formatAt(first, 0).foreground().color() != QColor(0x6f, 0x42, 0xc1),
                  QStringLiteral("plain text before highlight received HTML attribute styling"));
    ok &= require(formatAt(first, firstHighlight).foreground().color() == QColor(0x6e, 0x77, 0x81),
                  QStringLiteral("highlight opening delimiter was not muted"));
    const QTextBlock multiple = first.next();
    const int secondHighlight = multiple.text().lastIndexOf(QStringLiteral("==sentence=="));
    ok &= require(formatAt(multiple, secondHighlight + 2).background().color() == highlightBackground,
                  QStringLiteral("multiple highlight spans were not independent"));
    const QTextBlock unclosed = multiple.next().next().next();
    ok &= require(formatAt(unclosed, unclosed.text().indexOf(QStringLiteral("highlight"))).background().color()
                      != highlightBackground,
                  QStringLiteral("unclosed highlight colored following text"));
    ok &= require(formatAt(unclosed.next(), 1).background().color() != highlightBackground,
                  QStringLiteral("inline code highlight was applied"));
    ok &= require(formatAt(unclosed.next().next(), 0).background().color() != highlightBackground,
                  QStringLiteral("fenced code marker was treated as highlight"));
    ok &= require(formatAt(unclosed.next().next().next(), 0).background().color()
                      != highlightBackground,
                  QStringLiteral("fenced code content was treated as highlight"));

    QTextDocument htmlMarkDoc;
    htmlMarkDoc.setPlainText(QStringLiteral("Before <mark>HTML highlighted</mark> after"));
    MarkdownHighlighter htmlMarkHighlighter(&htmlMarkDoc);
    htmlMarkHighlighter.rehighlight();
    const QTextBlock htmlMarkBlock = htmlMarkDoc.firstBlock();
    const int htmlMarkContent = htmlMarkBlock.text().indexOf(QStringLiteral("HTML highlighted"));
    ok &= require(formatAt(htmlMarkBlock, htmlMarkContent).background().color() == highlightBackground
                      && formatAt(htmlMarkBlock, htmlMarkContent - 1).foreground().color()
                             == QColor(0x0a, 0x69, 0xda),
                  QStringLiteral("literal HTML mark did not retain HTML tag styling"));

    QTextDocument taskDoc;
    taskDoc.setPlainText(QStringLiteral(
        "- [ ] unchecked task\n"
        "- [x] checked task\n"
        "- plain item\n"));
    MarkdownHighlighter taskHighlighter(&taskDoc);
    taskHighlighter.rehighlight();
    const QTextBlock uncheckedBlock = taskDoc.firstBlock();
    const int checkboxPos = uncheckedBlock.text().indexOf(QStringLiteral("[ ]"));
    const QColor taskMarkerColor = formatAt(uncheckedBlock, checkboxPos).foreground().color();
    ok &= require(taskMarkerColor == QColor(0x9a, 0x67, 0x00),
                  QStringLiteral("task checkbox marker was not styled distinctly"));
    const QTextBlock plainItemBlock = uncheckedBlock.next().next();
    ok &= require(formatAt(plainItemBlock, 0).foreground().color() == QColor(0x9a, 0x67, 0x00),
                  QStringLiteral("plain list marker lost its styling"));

    QTextDocument fenceDoc;
    fenceDoc.setPlainText(QStringLiteral(
        "```cpp\n"
        "int value = 1;\n"
        "```\n"
        "```notalanguage\n"
        "plain text content\n"
        "```\n"));
    MarkdownHighlighter fenceHighlighter(&fenceDoc);
    fenceHighlighter.rehighlight();
    const QTextBlock cppLine = fenceDoc.firstBlock().next();
    const int intPos = cppLine.text().indexOf(QStringLiteral("int"));
    const QColor cppKeywordColor = formatAt(cppLine, intPos).foreground().color();
    const QTextBlock plainFenceLine = cppLine.next().next().next();
    const int plainPos = plainFenceLine.text().indexOf(QStringLiteral("plain"));
    const QColor plainFenceColor = formatAt(plainFenceLine, plainPos).foreground().color();
    ok &= require(cppKeywordColor != plainFenceColor,
                  QStringLiteral("recognized fence language did not get distinct highlighting"));
    ok &= require(formatAt(plainFenceLine, plainPos).background().color()
                      == formatAt(cppLine, intPos).background().color(),
                  QStringLiteral("fence body background was not uniform across languages"));

    const QString compatibility = QStringLiteral(
        "# Guide\n## API\n## API\n\n[TOC]\n\n"
        "Apple\nBanana\n: A **fruit**\n    with a second line\n\n"
        "FastMD supports footnotes.[^one] Also [^one] and [^two].\n\n"
        "[^one]: First *note*.\n    Continued.\n[^two]: Second note.\n\n"
        "`[^one]` and `\\[TOC\\]`\n\n```md\n[^two]\n[TOC]\n```\n");
    QStringList extensionSnippets;
    const QString lowered = MarkdownExtensions::preprocess(
        MarkdownToc::analyze(compatibility).expandedMarkdown, &extensionSnippets);
    const QString compatibilityHtml = render(lowered);
    ok &= require(compatibilityHtml.contains(QStringLiteral("<dl>"))
                      && compatibilityHtml.contains(QStringLiteral("<dt>Apple</dt>"))
                      && compatibilityHtml.contains(QStringLiteral("<dt>Banana</dt>"))
                      && compatibilityHtml.contains(QStringLiteral("<dd><p>A <strong>fruit</strong>")),
                  QStringLiteral("definition list was not rendered semantically"));
    ok &= require(compatibilityHtml.contains(QStringLiteral("id=\"fn-one\""))
                      && compatibilityHtml.contains(QStringLiteral("id=\"fnref-one-2\""))
                      && compatibilityHtml.contains(QStringLiteral("href=\"#fn-two\"")),
                  QStringLiteral("footnote references/footnotes were not linked"));
    ok &= require(compatibilityHtml.contains(QStringLiteral("href=\"#fnref-one-1\""))
                      && compatibilityHtml.contains(QStringLiteral("Continued.")),
                  QStringLiteral("footnote backlink or multiline content was lost"));
    ok &= require(!compatibilityHtml.contains(QStringLiteral("id=\"fn-two\""))
                      || compatibilityHtml.indexOf(QStringLiteral("id=\"fn-two\""))
                             > compatibilityHtml.indexOf(QStringLiteral("id=\"fn-one\"")),
                  QStringLiteral("footnote order was not based on reference order"));

    const MarkdownToc::Result toc = MarkdownToc::analyze(compatibility);
    ok &= require(toc.placeholderLineNumbers.size() == 1 && toc.headings.size() == 3
                      && toc.headings.at(1).slug != toc.headings.at(2).slug,
                  QStringLiteral("TOC placeholder or duplicate heading analysis failed"));
    ok &= require(!MarkdownToc::analyze(QStringLiteral("`[TOC]`\n\n```md\n[TOC]\n```\n")).placeholderLineNumbers.size(),
                  QStringLiteral("TOC was recognized inside code"));
    return ok ? 0 : 1;
}
