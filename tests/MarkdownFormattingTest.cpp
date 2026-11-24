#include "TextTransforms.h"

#include <QCoreApplication>
#include <QDebug>

namespace {
bool require(bool condition, const QString& message)
{
    if (!condition) qCritical().noquote() << message;
    return condition;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    const QString source = QStringLiteral(
        "---\n#      YAML\n---\n"
        "#      Heading\n"
        "-              pretty\n*      [x]      Done\n  +         nested\n1.        ordered\n"
        ">       Quote\n>   >       Nested\n"
        "Some text \nHard break  \nToo much   \n\n\nNext\n"
        "```mermaid\n#      not changed\n\n```\n"
        "    #      indented\n"
        "`#      inline`\n"
        "$#      math$\n"
        "<div>\n#      html\n\n\nAfter\n");
    const QString expected = QStringLiteral(
        "---\n#      YAML\n---\n"
        "# Heading\n"
        "- pretty\n* [x] Done\n  + nested\n1. ordered\n"
        "> Quote\n> > Nested\n"
        "Some text\nHard break  \nToo much  \n\nNext\n"
        "```mermaid\n#      not changed\n\n```\n"
        "    #      indented\n"
        "`#      inline`\n"
        "$#      math$\n"
        "<div>\n#      html\n\n\nAfter\n");
    ok &= require(TextTransforms::formatMarkdown(source) == expected,
                   QStringLiteral("conservative document formatting mismatch"));

    const QString selection = QStringLiteral("#      A\n\n\n-      B\n");
    ok &= require(TextTransforms::formatMarkdown(selection, 0, 0) == QStringLiteral("# A\n\n\n-      B\n"),
                   QStringLiteral("format selection changed a line outside the selection"));
    ok &= require(TextTransforms::formatMarkdown(QStringLiteral("# A\n\n\nB\n"))
                      == QStringLiteral("# A\n\nB\n"),
                   QStringLiteral("excessive blank lines were not collapsed"));
    const QString fenced = TextTransforms::formatMarkdown(QStringLiteral("# A\n```\n\n\n```\n"));
    ok &= require(fenced == QStringLiteral("# A\n```\n\n\n```\n"),
                   QStringLiteral("blank lines inside a fence were changed: ") + fenced.toLatin1().toHex());

    const QString comprehensiveSource = QStringLiteral(
        "###      Heading Level 3\n\n####       Heading Level 4\n\n"
        "- Parent\n    - Child\n        - Grandchild\n\n"
        "1. Parent\n    1. Child\n        1. Grandchild\n\n"
        "-   [ ]      Parent\n    -     [x]      Child\n        -   [ ]      Grandchild\n\n"
        ">       Quote\n>\n> -       item 1\n> *       item 2\n>\n> Paragraph\n\n"
        ">       [!NOTE]\n>\n>     Nested note content\n>     - nested item\n\n"
        ">       [!TIP]\n> Tip content\n\n"
        ">       [!WARNING]\n> Warning content\n\n"
        ">       [!IMPORTANT]\n> Important content\n\n"
        ">       [!CAUTION]\n> Caution content\n\n"
        "Footnote text[^one] and another[^two].\n\n"
        "[^one]: First footnote.\n[^two]: Second footnote with [reference][ref].\n\n"
        "Term One\n: Definition one\n\nTerm Two\n: Definition two\n\n"
        "[TOC]\n\n"
        "[reference]: https://example.com \"Example\"\n\n"
        "![Alt text](image.png)\n![Titled](image.png \"Title\")\n\n"
        "Before rule\n\n\n---\n\n\nAfter dash rule\n\n***\n\n___\n\n"
        "\\*Not italic\\*\n\\# Not heading\n\n"
        "- first unordered\n- second unordered\n1. first ordered\n1. second ordered\n- [ ] first task\n- [x] second task\n- third unordered\n");
    const QString comprehensiveExpected = QStringLiteral(
        "### Heading Level 3\n\n#### Heading Level 4\n\n"
        "- Parent\n    - Child\n        - Grandchild\n\n"
        "1. Parent\n    1. Child\n        1. Grandchild\n\n"
        "- [ ] Parent\n    - [x] Child\n        - [ ] Grandchild\n\n"
        "> Quote\n>\n> - item 1\n> * item 2\n>\n> Paragraph\n\n"
        "> [!NOTE]\n>\n> Nested note content\n> - nested item\n\n"
        "> [!TIP]\n> Tip content\n\n"
        "> [!WARNING]\n> Warning content\n\n"
        "> [!IMPORTANT]\n> Important content\n\n"
        "> [!CAUTION]\n> Caution content\n\n"
        "Footnote text[^one] and another[^two].\n\n"
        "[^one]: First footnote.\n[^two]: Second footnote with [reference][ref].\n\n"
        "Term One\n: Definition one\n\nTerm Two\n: Definition two\n\n"
        "[TOC]\n\n"
        "[reference]: https://example.com \"Example\"\n\n"
        "![Alt text](image.png)\n![Titled](image.png \"Title\")\n\n"
        "Before rule\n\n---\n\nAfter dash rule\n\n***\n\n___\n\n"
        "\\*Not italic\\*\n\\# Not heading\n\n"
        "- first unordered\n- second unordered\n1. first ordered\n2. second ordered\n- [ ] first task\n- [x] second task\n- third unordered\n");
    ok &= require(TextTransforms::formatMarkdown(comprehensiveSource) == comprehensiveExpected,
                   QStringLiteral("comprehensive Markdown formatting fixture mismatch"));

    const QString doNotModify = QStringLiteral(
        "---\n"
        "title: Test\n"
        "author: FastMD\n"
        "---\n\n"
        "## The formatter MUST NOT modify the following content\n\n"
        "```cpp\n"
        "#      fenced heading\n"
        "-        fenced item\n"
        "```\n\n"
        "Inline code: `#      code` and `-        item`.\n\n"
        "```mermaid\n"
        "graph TD\n"
        "    A[Start] --> B[End]\n"
        "```\n\n"
        "$$\n"
        "#      display math\n"
        "-        x\n"
        "$$\n\n"
        "<section>\n"
        "#      raw HTML heading\n"
        "-        raw HTML item\n"
        "</section>\n\n"
        "> [!NOTE]\n"
        ">\n"
        "> - nested callout item\n"
        "> - nested callout item 2\n\n"
        "[TOC]\n\n"
        "[ref]: https://example.com \"Reference\"\n"
        "Use [the reference][ref].\n\n"
        "Footnote[^keep].\n\n"
        "[^keep]: Footnote definition with **Markdown**.\n");
    ok &= require(TextTransforms::formatMarkdown(doNotModify) == doNotModify,
                   QStringLiteral("the protected-content regression section was modified"));

    // Expected Result Checklist:
    // [x] ATX heading levels and spacing are preserved.
    // [x] Nested unordered, ordered, and task-list indentation is preserved.
    // [x] Mixed blockquotes, callouts, paragraphs, and lists remain structurally identical.
    // [x] Footnotes, definition lists, [TOC], reference links, and images are unchanged.
    // [x] Horizontal rules retain their marker style and surrounding spacing is conservative.
    // [x] Escaped Markdown remains escaped.
    // [x] Fenced/indented code, inline code, Mermaid, KaTeX, HTML, YAML, callouts,
    //     TOC placeholders, reference links, and footnotes are not modified.
    return ok ? 0 : 1;
}
