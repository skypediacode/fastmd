#include "ExportManager.h"
#include "MarkdownExtensions.h"
#include "PreviewPatch.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryFile>
#include <QRegularExpression>

namespace {

bool require(bool condition, const QString& message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString source = QStringLiteral(
        "# Café\n## Usage\n## Usage\n\n[TOC]\n\n"
        "Term\n: A **formatted** definition\n    continued\n\n"
        "Text[^a] and again [^a], then [^b].\n\n"
        "[^a]: First *footnote*.\n    More detail.\n[^b]: Second footnote.\n\n"
        "`[TOC] [^a]`\n\n```md\n# Not a heading\n[TOC]\n[^b]\n```");
    const QString advanced = QStringLiteral(
        "> [!nOtE]\n> This is **nested**.\n>\n> - [ ] one\n> - [x] two\n>\n> ```cpp\n> [!CUSTOM]\n> ```\n\n"
        "> [!CUSTOM]\n> ordinary\n\n- [ ] repeated\n- [ ] repeated\n  - [x] nested");
    const QString versionPrompt = QStringLiteral(
        "$version = 2.7.0\n"
        "- Bump the application version to `$version`.\n"
        "- Add a concise **What's New** section for version `$version` to **ms_store.md**.\n"
        "- Run `scripts\\build-all-and-publish-to-github.ps1`.\n"
        "- Create a Git commit with the following message:\n"
        "  `Chore $version: Bump version across all scripts and configuration files.`");
    const QString referenceLinks = QStringLiteral(
        "[FastMD][site], [FastMD][], and [FastMD]. Also [inline](https://example.com).\n\n"
        "[site]: https://skypediacode.github.io/fastmd/ \"FastMD website\"\n"
        "[FastMD]: https://fastmd.example/ 'Collapsed and shortcut title'");
    const QString numericReference = QStringLiteral(
        "Citation[1].\n\n[1]: https://markdown.com.vn/?lang=en \"Citation source\"");
    const QString numericInlineLink = QStringLiteral(
        "[1](https://inline.example/).\n\n[1]: https://markdown.com.vn/?lang=en");

    bool ok = true;

    // Page Break is a complete block, even without user-authored blank lines.
    // Compare against explicitly separated parser input for every pair of blocks.
    const QString pageBreak = QStringLiteral("<div style=\"break-after: page;\"></div>");
    {
        QStringList snippets;
        QVector<int> origins;
        const QString parserInput = MarkdownExtensions::preprocess(
            QStringLiteral("abc\n") + pageBreak + QStringLiteral("\n# def"),
            &snippets, nullptr, &origins);
        ok &= require(parserInput == QStringLiteral("abc\n\n") + pageBreak + QStringLiteral("\n\n# def"),
                      QStringLiteral("Page Break parser input lacks blank boundaries"));
        ok &= require(origins == QVector<int>({1, 2, 2, 2, 3}),
                      QStringLiteral("Page Break synthetic blanks changed source provenance"));
    }
    QStringList pageBreakBlocks = {
        QStringLiteral("- one\n  continuation\n- two"),
        QStringLiteral("1. one\n   continuation\n2. two"),
        QStringLiteral("> quote\n> continued"),
        QStringLiteral("```text\ncode\n```"),
        QStringLiteral("| A | B |\n|---|---|\n| a | b |"),
        QStringLiteral("***"),
        QStringLiteral("paragraph\ncontinued"),
        pageBreak,
        QStringLiteral("- [ ] task\n- [x] done")
    };
    for (int level = 1; level <= 6; ++level)
        pageBreakBlocks.append(QString(level, QLatin1Char('#')) + QStringLiteral(" heading"));
    static const QRegularExpression sourceAttrs(
        QStringLiteral(R"( (?:data-src-(?:start|end|line)|data-fastmd-task-(?:start|end))="[0-9]+")"));
    for (int before = 0; before < pageBreakBlocks.size(); ++before) {
        for (int after = 0; after < pageBreakBlocks.size(); ++after) {
            const QString& left = pageBreakBlocks.at(before);
            const QString& right = pageBreakBlocks.at(after);
            const QString separated = left + QStringLiteral("\n\n") + pageBreak
                + QStringLiteral("\n\n") + right;
            for (int blanks = 0; blanks < 4; ++blanks) {
                const QString tight = left + ((blanks & 1) ? QStringLiteral("\n\n") : QStringLiteral("\n"))
                    + pageBreak + ((blanks & 2) ? QStringLiteral("\n\n") : QStringLiteral("\n")) + right;
                const QString label = QStringLiteral("Page Break blocks %1/%2 blanks %3").arg(before).arg(after).arg(blanks);
                for (MathRenderMode mode : {MathRenderMode::KatexOutput, MathRenderMode::UnicodePreview}) {
                    QString actualExport = ExportManager::markdownToHtml(tight, mode);
                    QString expectedExport = ExportManager::markdownToHtml(separated, mode);
                    // Export task-list spacing intentionally reflects authored
                    // blank lines; compare structure independently of that hint.
                    actualExport.remove(QStringLiteral(" data-fastmd-loose-list=\"1\""));
                    expectedExport.remove(QStringLiteral(" data-fastmd-loose-list=\"1\""));
                    ok &= require(actualExport == expectedExport, label + QStringLiteral(" export boundary"));
                }
                QString actual = ExportManager::markdownToPreviewHtml(tight);
                // The existing renderer maps nonempty fences to their body line.
                const int rightLine = left.count(QLatin1Char('\n')) + 3 + ((blanks & 1) ? 1 : 0) + ((blanks & 2) ? 1 : 0);
                ok &= require(actual.contains(QStringLiteral("data-src-start=\"%1\"").arg(rightLine + (after == 3 ? 1 : 0))),
                              label + QStringLiteral(" source mapping"));
                QString expected = ExportManager::markdownToPreviewHtml(separated);
                actual.remove(sourceAttrs);
                expected.remove(sourceAttrs);
                ok &= require(actual == expected, label + QStringLiteral(" preview boundary"));
            }
        }
    }
    // A literal marker in code, inline text, or an ordinary raw HTML block must
    // retain standard Markdown semantics. CRLF and EOF are legal marker edges.
    for (const QString& literal : {QStringLiteral("```html\n") + pageBreak + QStringLiteral("\n# literal\n```"),
                                  QStringLiteral("~~~html\n") + pageBreak + QStringLiteral("\n# literal\n~~~"),
                                  QStringLiteral("    ") + pageBreak + QStringLiteral("\n    # literal"),
                                  QStringLiteral("prefix ") + pageBreak + QStringLiteral("\n# heading"),
                                  QStringLiteral("<div style=\"break-after: pagewide;\"></div>\n# literal"),
                                  QStringLiteral("<div style=\"break-after: page;\">content</div>\n# literal"),
                                  QStringLiteral("<div>\n# literal\n</div>\n# still literal")}) {
        QStringList snippets;
        ok &= require(MarkdownExtensions::preprocess(literal, &snippets) == literal,
                      QStringLiteral("Page Break normalization changed literal code or ordinary HTML"));
    }
    ok &= require(ExportManager::markdownToHtml(QStringLiteral("abc\r\n") + pageBreak
                      + QStringLiteral("\r\n# def")).contains(QStringLiteral("<h1 id=\"def\">def</h1>")),
                  QStringLiteral("CRLF Page Break boundary"));
    ok &= require(ExportManager::markdownToPreviewHtml(QStringLiteral("abc\r\n") + pageBreak
                      + QStringLiteral("\r\n# def")).contains(QStringLiteral("data-src-start=\"3\" data-src-end=\"3\"")),
                  QStringLiteral("CRLF Page Break source mapping"));
    ok &= require(ExportManager::markdownToHtml(pageBreak) == pageBreak + QLatin1Char('\n'),
                  QStringLiteral("Page Break at EOF"));

    // Math preprocessing must leave fenced code opaque, but must return to
    // ordinary Markdown state immediately after a closing fence.  In
    // particular, an odd-length closing backtick fence must not be consumed as
    // inline-code delimiters and suppress all following math.
    const auto requireMathPipeline = [&](const QString& name, const QString& markdown,
                                         int expectedMathSnippets) {
        QStringList mathSnippets;
        const QString preprocessed = FastMdMath::preprocess(
            markdown, MathRenderMode::KatexOutput, &mathSnippets);
        ok &= require(mathSnippets.size() == expectedMathSnippets,
                      name + QStringLiteral(": math spans were not detected"));
        const QString html = ExportManager::markdownToHtml(markdown, MathRenderMode::KatexOutput);
        const QString preview = ExportManager::markdownToPreviewHtml(markdown);
        for (const QString& rendered : {html, preview}) {
            ok &= require(rendered.contains(QStringLiteral("E = mc^2")),
                          name + QStringLiteral(": display math did not reach the HTML renderer"));
            ok &= require(!rendered.contains(QStringLiteral("@@FASTMD_MATH_")),
                          name + QStringLiteral(": math placeholder leaked into HTML"));
        }
        return preprocessed;
    };
    const QString displayMath = QStringLiteral("$$\nE = mc^2\n$$");
    const QString cppFence = QStringLiteral("```cpp\nint main() {\n    return 0;\n}\n```");
    const QString mermaidFence = QStringLiteral("```mermaid\ngraph LR\nA --> B\n```");
    requireMathPipeline(QStringLiteral("display math alone"), displayMath, 1);
    const QString cppThenMath = requireMathPipeline(
        QStringLiteral("C++ fence followed by display math"), cppFence + QStringLiteral("\n\n") + displayMath, 1);
    ok &= require(cppThenMath.contains(QStringLiteral("int main()")),
                  QStringLiteral("fenced C++ was changed while protecting following display math"));
    const QString cppMathMermaid = cppFence + QStringLiteral("\n\n") + displayMath
        + QStringLiteral("\n\n") + mermaidFence;
    requireMathPipeline(QStringLiteral("C++ fence, display math, Mermaid"), cppMathMermaid, 1);
    const QString cppMathMermaidPreview = ExportManager::markdownToPreviewHtml(cppMathMermaid);
    ok &= require(cppMathMermaidPreview.contains(QStringLiteral("language-cpp"))
                      && cppMathMermaidPreview.contains(QStringLiteral("language-mermaid"))
                      && cppMathMermaidPreview.contains(QStringLiteral("graph LR")),
                  QStringLiteral("fenced C++ or Mermaid was changed while restoring math"));
    requireMathPipeline(QStringLiteral("display math followed by C++ fence"),
                        displayMath + QStringLiteral("\n\n") + cppFence, 1);
    requireMathPipeline(QStringLiteral("multiple fenced code and math blocks"),
                        cppFence + QStringLiteral("\n\n") + displayMath + QStringLiteral("\n\n")
                            + cppFence + QStringLiteral("\n\n$$\na^2 + b^2 = c^2\n$$"), 2);
    const QString inlineMathAfterFence = cppFence + QStringLiteral("\n\nInline $x^2$ after code.");
    QStringList inlineMathSnippets;
    const QString inlineAfterFence = FastMdMath::preprocess(
        inlineMathAfterFence, MathRenderMode::KatexOutput, &inlineMathSnippets);
    ok &= require(inlineMathSnippets.size() == 1
                      && inlineAfterFence.contains(QStringLiteral("@@FASTMD_MATH_0@@"))
                      && ExportManager::markdownToPreviewHtml(inlineMathAfterFence)
                             .contains(QStringLiteral("\\(x^2\\)")),
                  QStringLiteral("inline math after a fence was not tokenized"));
    const QString literalMathInCode = QStringLiteral("```cpp\nconst char* math = \"$$E = mc^2$$\";\n```");
    QStringList codeMathSnippets;
    const QString protectedCode = FastMdMath::preprocess(
        literalMathInCode, MathRenderMode::KatexOutput, &codeMathSnippets);
    ok &= require(codeMathSnippets.isEmpty() && protectedCode == literalMathInCode
                      && ExportManager::markdownToPreviewHtml(literalMathInCode)
                             .contains(QStringLiteral("$$E = mc^2$$")),
                  QStringLiteral("math delimiters inside fenced code were interpreted as KaTeX"));
    // Every math spelling must reach the HTML as exactly one span in KaTeX's own
    // delimiters, whatever Markdown structure holds it. The Markdown-escaped
    // \\[...\\] and \\(...\\) forms (MathJax/Jekyll convention) once left their
    // first backslash ahead of the placeholder; MD4C read "\@" as an escape and
    // the preview's per-run spans split "@@FASTMD_MATH_n@@" so it leaked.
    static const QRegularExpression lineAttrs(
        QStringLiteral(R"( (?:data-src-(?:start|end|line)|data-fastmd-task-(?:start|end))="[0-9]+")"));
    // The preview wraps every MD4C text run (an escaped character is its own
    // run) in a source-mapped span; compare what the reader sees.
    const auto visibleText = [](QString rendered) {
        rendered.remove(lineAttrs);
        rendered.remove(QStringLiteral("<span>"));
        rendered.remove(QStringLiteral("</span>"));
        return rendered;
    };
    const auto requireMathSpans = [&](const QString& name, const QString& markdown,
                                      const QStringList& expected) {
        QStringList snippets;
        FastMdMath::preprocess(markdown, MathRenderMode::KatexOutput, &snippets);
        ok &= require(snippets.size() == expected.size(),
                      name + QStringLiteral(": expected %1 math spans, found %2")
                                 .arg(expected.size()).arg(snippets.size()));
        const QString html = ExportManager::markdownToHtml(markdown, MathRenderMode::KatexOutput);
        const QString preview = ExportManager::markdownToPreviewHtml(markdown);
        for (const QString& rendered : {html, preview}) {
            ok &= require(!rendered.contains(QStringLiteral("FASTMD_MATH")),
                          name + QStringLiteral(": math placeholder leaked into HTML"));
            const QString text = visibleText(rendered);
            for (const QString& span : expected)
                ok &= require(text.count(span.toHtmlEscaped()) == 1,
                              name + QStringLiteral(": span not rendered once: ") + span);
        }
    };
    requireMathSpans(QStringLiteral("bracket display"), QStringLiteral("\\[ a^2 \\]"),
                     {QStringLiteral("\\[ a^2 \\]")});
    requireMathSpans(QStringLiteral("multi-line bracket display"),
                     QStringLiteral("Before\n\n\\[\na^2 + b^2\n\\]\n\nAfter"),
                     {QStringLiteral("\\[\na^2 + b^2\n\\]")});
    requireMathSpans(QStringLiteral("dollar display"), QStringLiteral("$$ b_1 * b_2 * b_3 $$"),
                     {QStringLiteral("$$ b_1 * b_2 * b_3 $$")});
    requireMathSpans(QStringLiteral("inline forms"),
                     QStringLiteral("Text \\(c_1 * c_2\\) and $d_2$ end."),
                     {QStringLiteral("\\(c_1 * c_2\\)"), QStringLiteral("\\(d_2\\)")});
    requireMathSpans(QStringLiteral("escaped bracket display"),
                     QStringLiteral("\\\\[ y=\\frac{2x+3}{x-1} \\\\]"),
                     {QStringLiteral("\\[ y=\\frac{2x+3}{x-1} \\]")});
    requireMathSpans(QStringLiteral("escaped inline"),
                     QStringLiteral("At \\\\(x_a * y_b * z\\\\) and \\\\(f'(2)\\\\)."),
                     {QStringLiteral("\\(x_a * y_b * z\\)"), QStringLiteral("\\(f'(2)\\)")});
    requireMathSpans(QStringLiteral("escaped multi-line display"),
                     QStringLiteral("\\\\[\ny = x^2\n\\\\]"), {QStringLiteral("\\[\ny = x^2\n\\]")});
    const QString standardBraces = QString::fromUtf8(R"(\[
\underbrace{y-y_0}_{\text{Độ thay đổi của }y}
=
\underbrace{f'(x_0)}_{\text{Độ nghiêng}}
\underbrace{(x-x_0)}_{\text{Độ thay đổi của }x}
\])");
    const QString copiedBraces = QString::fromUtf8(R"(\\[ \underbrace{y-y_0}\_{\text{Độ thay đổi của }y} = \underbrace{f'(x_0)}\_{\text{Độ nghiêng}} \underbrace{(x-x_0)}\_{\text{Độ thay đổi của }x} \\])");
    const QString normalizedBraces = QString::fromUtf8(R"(\[ \underbrace{y-y_0}_{\text{Độ thay đổi của }y} = \underbrace{f'(x_0)}_{\text{Độ nghiêng}} \underbrace{(x-x_0)}_{\text{Độ thay đổi của }x} \])");
    requireMathSpans(QStringLiteral("standard Vietnamese underbraces"), standardBraces, {standardBraces});
    requireMathSpans(QStringLiteral("copied Vietnamese underbraces"), copiedBraces, {normalizedBraces});
    requireMathSpans(QStringLiteral("copied overbrace and subscripts"),
                     QStringLiteral(R"(\\(\overbrace{x\_{0}+y}^{n} + a\_ {i} \\))"),
                     {QStringLiteral(R"(\(\overbrace{x_{0}+y}^{n} + a_ {i} \))")});
    requireMathSpans(QStringLiteral("copied callout subscripts"),
                     QStringLiteral("> \\\\[\n> x\\_{0}\n> \\\\]"),
                     {QStringLiteral("\\[x_{0}\\]")});
    // Doubled delimiters opt into a narrow copy dialect. Literal TeX escapes
    // in standard math, prose, text arguments and row separators stay intact.
    const QString literalTex = QStringLiteral(R"(x\_{i} + \text{file\_{name}} + \_i + \{x\} + \begin{matrix}a\\b\end{matrix})");
    for (const auto& delimiters : {qMakePair(QStringLiteral("\\["), QStringLiteral("\\]")),
                                   qMakePair(QStringLiteral("\\("), QStringLiteral("\\)")),
                                   qMakePair(QStringLiteral("$$"), QStringLiteral("$$")),
                                   qMakePair(QStringLiteral("$"), QStringLiteral("$"))}) {
        const QString source = delimiters.first + literalTex + delimiters.second;
        requireMathSpans(QStringLiteral("literal escapes in standard math"), source,
                         {delimiters.first == QLatin1String("$")
                              ? QStringLiteral("\\(") + literalTex + QStringLiteral("\\)") : source});
    }
    requireMathSpans(QStringLiteral("literal escapes in copied math"),
                     QStringLiteral(R"(\\[x\_{i}+\text{file\_{name} {nested\_{text}} \{ok\}}+\operatorname*{a\_{b}}+\_i+\{x\}+\begin{matrix}a\\b\end{matrix}\\])"),
                     {QStringLiteral(R"(\[x_{i}+\text{file\_{name} {nested\_{text}} \{ok\}}+\operatorname*{a\_{b}}+\_i+\{x\}+\begin{matrix}a\\b\end{matrix}\])")});
    for (const QString& protectedSource : {QStringLiteral("`") + copiedBraces + QStringLiteral("`"),
                                           QStringLiteral("`` ` ") + copiedBraces + QStringLiteral(" ` ``"),
                                           QStringLiteral("```latex\n") + copiedBraces + QStringLiteral("\n```"),
                                           QStringLiteral("~~~latex\n") + copiedBraces + QStringLiteral("\n~~~"),
                                           QStringLiteral(R"(prose x\_{i}, path C:\\Temp, unclosed \\[x\_{i})")}) {
        QStringList snippets;
        ok &= require(FastMdMath::preprocess(protectedSource, MathRenderMode::KatexOutput, &snippets)
                          == protectedSource && snippets.isEmpty(),
                      QStringLiteral("copy normalization changed code, prose or unmatched delimiters"));
    }
    for (const auto mode : {MathRenderMode::KatexOutput, MathRenderMode::UnicodePreview}) {
        ok &= require(ExportManager::markdownToHtml(copiedBraces, mode)
                          == ExportManager::markdownToHtml(normalizedBraces, mode),
                      QStringLiteral("copied math and normalized math differ in export"));
    }
    requireMathSpans(QStringLiteral("multi-backtick code followed by copied math"),
                     QStringLiteral("`` ` ") + copiedBraces + QStringLiteral(" ` ``\n\n") + copiedBraces,
                     {normalizedBraces});
    for (const QString& codeSource : {QStringLiteral("    ") + copiedBraces,
                                      QStringLiteral("- item\n\n        ") + copiedBraces,
                                      QStringLiteral("> ```latex\n> ") + copiedBraces + QStringLiteral("\n> ```")}) {
        for (const QString& rendered : {ExportManager::markdownToHtml(codeSource, MathRenderMode::KatexOutput),
                                        ExportManager::markdownToHtml(codeSource, MathRenderMode::UnicodePreview),
                                        ExportManager::markdownToPreviewHtml(codeSource)}) {
            ok &= require(rendered.contains(QStringLiteral("<code"))
                              && rendered.contains(copiedBraces.toHtmlEscaped())
                              && !rendered.contains(QStringLiteral("FASTMD_MATH")),
                          QStringLiteral("MD4C code classification did not preserve copied math source"));
        }
    }
    QString quotedCode = copiedBraces;
    quotedCode.replace(QStringLiteral(" "), QStringLiteral("\n> "));
    const QString fencedQuote = QStringLiteral("> ```latex\n> ") + quotedCode + QStringLiteral("\n> ```");
    QStringList quotedCodeSnippets;
    ok &= require(FastMdMath::preprocess(fencedQuote, MathRenderMode::KatexOutput, &quotedCodeSnippets)
                      == fencedQuote && quotedCodeSnippets.isEmpty(),
                  QStringLiteral("multiline copied math inside a quoted fence was normalized"));
    requireMathSpans(QStringLiteral("escaped backslash before a real delimiter"),
                     QStringLiteral("\\\\\\[x^3\\]"), {QStringLiteral("\\[x^3\\]")});
    ok &= require(visibleText(ExportManager::markdownToPreviewHtml(QStringLiteral("\\\\\\[x^3\\]")))
                      .contains(QStringLiteral(">\\\\[x^3\\]<")),
                  QStringLiteral("a literal backslash before \\[ was dropped"));
    for (const QString& literal : {QStringLiteral("Path C:\\\\Temp and \\\\ alone"),
                                   QStringLiteral("Unclosed \\\\[ bracket"),
                                   QStringLiteral("Unclosed \\[ bracket")}) {
        QStringList snippets;
        ok &= require(FastMdMath::preprocess(literal, MathRenderMode::KatexOutput, &snippets) == literal
                          && snippets.isEmpty(),
                      QStringLiteral("backslashes without math were changed: ") + literal);
    }
    ok &= require(visibleText(ExportManager::markdownToPreviewHtml(QStringLiteral("Path C:\\\\Temp")))
                      .contains(QStringLiteral("Path C:\\Temp")),
                  QStringLiteral("an escaped backslash lost its Markdown meaning"));
    requireMathSpans(QStringLiteral("math in lists"),
                     QStringLiteral("- item \\(a\\)\n- \\\\[ b \\\\]\n- [ ] task $c$\n\n"
                                    "1. first\n\n   $$\n   d\n   $$\n2. \\[ e \\]"),
                     {QStringLiteral("\\(a\\)"), QStringLiteral("\\[ b \\]"), QStringLiteral("\\(c\\)"),
                      QStringLiteral("$$\n   d\n   $$"), QStringLiteral("\\[ e \\]")});
    requireMathSpans(QStringLiteral("math in blockquotes and callouts"),
                     QStringLiteral("> \\[ e \\] and \\\\(f\\\\)\n\n> \\\\[\n> g\n> \\\\]\n\n"
                                    "> $$\n> h\n> $$\n\n> [!NOTE]\n> \\[ k \\]"),
                     {QStringLiteral("\\[ e \\]"), QStringLiteral("\\(f\\)"), QStringLiteral("\\[g\\]"),
                      QStringLiteral("$$h$$"), QStringLiteral("\\[ k \\]")});
    requireMathSpans(QStringLiteral("math in headings, tables and emphasis"),
                     QStringLiteral("## Title \\\\(j\\\\)\n\n| A | B |\n|---|---|\n| \\(m\\) | $n$ |\n\n"
                                    "**bold \\(p\\)** and *it \\\\(q\\\\)*"),
                     {QStringLiteral("\\(j\\)"), QStringLiteral("\\(m\\)"), QStringLiteral("\\(n\\)"),
                      QStringLiteral("\\(p\\)"), QStringLiteral("\\(q\\)")});

    // Issue #1: currency must never be paired as math, and inline math must
    // never leave its block. "Literal" documents must reach MD4C unchanged.
    const QString issue1 = QStringLiteral(
        "## What it costs\n\n"
        "- **$300 annual administrative/membership fee** — separate from insurance.\n"
        "- Another normal bullet item.\n\n"
        "## Action items\n\n"
        "| Owner | Item |\n|---|---|\n| Rob | Example action |\n| Practice | Example action |\n\n"
        "---\n\n"
        "## Questions that were NOT answered\n\n"
        "1. **Example question?** Some normal text mentioning a $300 fee later in the document.\n");
    for (const QString& rendered : {ExportManager::markdownToHtml(issue1, MathRenderMode::KatexOutput),
                                    ExportManager::markdownToHtml(issue1, MathRenderMode::UnicodePreview),
                                    ExportManager::markdownToPreviewHtml(issue1)}) {
        ok &= require(rendered.count(QStringLiteral("<h2")) == 3 && rendered.count(QStringLiteral("<li")) == 3
                          && rendered.count(QStringLiteral("<table")) == 1
                          && rendered.count(QStringLiteral("<hr")) == 1
                          && rendered.count(QStringLiteral("$300")) == 2
                          && rendered.contains(QStringLiteral("<strong>"))
                          && !rendered.contains(QStringLiteral("\\(")),
                      QStringLiteral("issue 1: currency corrupted the document structure"));
    }
    ok &= require(ExportManager::markdownToPreviewHtml(issue1)
                      .contains(QStringLiteral("data-src-start=\"17\"")),
                  QStringLiteral("issue 1: source lines drifted"));
    const QStringList literalDollars = {
        issue1,
        QStringLiteral("It was $300, then $400, then $500."),
        QStringLiteral("From $5-$10 to $5M–$10M, or **$20** and **$30**."),
        QStringLiteral("Pay 5 $ or 6 $ today; spaced $ x $ is text."),
        QStringLiteral("Escaped \\$5 and \\$6, or \\$x\\$."),
        QStringLiteral("| A | B |\n|---|---|\n| $300 | $400 |\n|$5|$6|"),
        QStringLiteral("A | B\n--- | ---\n$5 | a\nb$ | c"),
        QStringLiteral("a $x\n\ny$ b"),
        QStringLiteral("# a $x\ny$ b"),
        QStringLiteral("a $x\n# y$ b"),
        QStringLiteral("a $x\n===\ny$ b"),
        QStringLiteral("a $x\n---\ny$ b"),
        QStringLiteral("a $x\n***\ny$ b"),
        QStringLiteral("- a $x\n- y$ b"),
        QStringLiteral("1. a $x\n2. y$ b"),
        QStringLiteral("a $x\n> y$ b"),
        QStringLiteral("> a $x\n>\n> y$ b"),
        QStringLiteral("a $x\n```\ncode\n```\ny$ b"),
        QStringLiteral("a $x\n<div>\ny$ b"),
        QStringLiteral("a $x\r\n\r\ny$ b"),
        QStringLiteral("Unmatched $$300 and $400"),
    };
    for (const QString& literal : literalDollars) {
        QStringList snippets;
        ok &= require(FastMdMath::preprocess(literal, MathRenderMode::KatexOutput, &snippets) == literal
                          && snippets.isEmpty(),
                      QStringLiteral("literal dollars were read as math: ") + literal);
    }
    requireMathSpans(QStringLiteral("mixed currency and math"),
                     QStringLiteral("Pay $300 for $x^2$ and $400 for ($y_1$) or **$z$**, not $5."),
                     {QStringLiteral("\\(x^2\\)"), QStringLiteral("\\(y_1\\)"), QStringLiteral("\\(z\\)")});
    requireMathSpans(QStringLiteral("currency before a parenthesized or bold expression"),
                     QStringLiteral("A $300 fee ($a$)\n\nA $400 fee **$\\beta$**"),
                     {QStringLiteral("\\(a\\)"), QStringLiteral("\\(\\beta\\)")});
    requireMathSpans(QStringLiteral("currency and display math"),
                     QStringLiteral("Cost $300\n\n$$\nE = 1\n\n+ 2\n$$\n\nor $400 and $$c$$"),
                     {QStringLiteral("$$\nE = 1\n\n+ 2\n$$"), QStringLiteral("$$c$$")});
    requireMathSpans(QStringLiteral("inline math forms kept"),
                     QStringLiteral("$2$, $n$th, 3$\\times$ x, $[0,1)$, $|v|$, $\\$5$, $p$-value, $q$$r$"),
                     {QStringLiteral("\\(2\\)"), QStringLiteral("\\(n\\)"), QStringLiteral("\\(\\times\\)"),
                      QStringLiteral("\\([0,1)\\)"), QStringLiteral("\\(|v|\\)"),
                      QStringLiteral("\\(\\$5\\)"), QStringLiteral("\\(p\\)"),
                      QStringLiteral("\\(q\\)"), QStringLiteral("\\(r\\)")});
    requireMathSpans(QStringLiteral("inline math across a soft break"),
                     QStringLiteral("Paragraph $a =\nb$ and $300\n\nnext $c$ and $400"),
                     {QStringLiteral("\\(a =\nb\\)"), QStringLiteral("\\(c\\)")});
    requireMathSpans(QStringLiteral("math in table cells with currency"),
                     QStringLiteral("| A | B |\n|---|---|\n| $300 | $x$ |\n| $400 | $y$ |"),
                     {QStringLiteral("\\(x\\)"), QStringLiteral("\\(y\\)")});
    ok &= require(ExportManager::markdownToPreviewHtml(QStringLiteral("Code `$300` and    `$x$` and $5"))
                      .contains(QStringLiteral("$x$")),
                  QStringLiteral("dollars in code changed"));
    ok &= require(ExportManager::markdownToPreviewHtml(QStringLiteral("$300 ==marked== $400"))
                      .contains(QStringLiteral("<mark>")),
                  QStringLiteral("currency suppressed inline formatting between amounts"));
    // Literal dollars alone must not pull KaTeX into exported documents, and
    // the page must not pair them.
    ok &= require(!ExportManager::buildPreviewHtml(QStringLiteral("<p>$300 and $400</p>"), false, 17)
                       .contains(QStringLiteral("katex-css'>")),
                  QStringLiteral("currency embedded KaTeX in an export"));

    // The escaped multi-line form collapses lines like any display block, so
    // later blocks must keep their source lines for double-click navigation.
    ok &= require(ExportManager::markdownToPreviewHtml(QStringLiteral("\\\\[\nx\n\\\\]\n\nafter"))
                      .contains(QStringLiteral("<p data-src-start=\"5\" data-src-end=\"5\"><span data-src-line=\"5\">after")),
                  QStringLiteral("escaped display math shifted the following source lines"));

    // Tables inside list items: the table rows are list-item content, not
    // paragraph text, so they must reach MD4C intact in every render path.
    {
        const QString sample = QString::fromUtf8(
            "* Điểm cực đại $x_0$:\n"
            "  |   $x$   | $-\\infty$ |            |        $x_0$         |            | $+\\infty$ |\n"
            "  |:-------:|:---------:|:----------:|:--------------------:|:----------:|:---------:|\n"
            "  | $f'(x)$ |           |    $+$     |          0           |    $-$     |           |\n"
            "  | $f(x)$  |           | $\\nearrow$ | $f(x_0)$ *(Cực đại)* | $\\searrow$ |           |\n"
            "\n"
            "* Điểm cực tiểu $x_0$:\n"
            "  |   $x$   | $-\\infty$ |            |         $x_0$         |            | $+\\infty$ |\n"
            "  |:-------:|:---------:|:----------:|:---------------------:|:----------:|:---------:|\n"
            "  | $f'(x)$ |           |    $-$     |           0           |    $+$     |           |\n"
            "  | $f(x)$  |           | $\\searrow$ | $f(x_0)$ *(Cực tiểu)* | $\\nearrow$ |           |\n");
        for (const QString& rendered : {ExportManager::markdownToHtml(sample, MathRenderMode::KatexOutput),
                                        ExportManager::markdownToPreviewHtml(sample)}) {
            ok &= require(rendered.count(QStringLiteral("<table")) == 2
                              && rendered.count(QStringLiteral("<li")) == 2
                              && rendered.count(QStringLiteral("<tr")) == 6
                              && rendered.count(QStringLiteral("<em>")) == 2
                              && !rendered.contains(QStringLiteral("|:--")),
                          QStringLiteral("table in list item not rendered: ") + rendered);
        }
        struct Case { const char* name; const char* md; int tables; int items; };
        const Case cases[] = {
            {"bullet, no blank", "- item\n  | a | b |\n  |---|---|\n  | 1 | 2 |\n", 1, 1},
            {"bullet, blank line", "- item\n\n  | a | b |\n  |---|---|\n  | 1 | 2 |\n", 1, 1},
            {"ordered", "1. item\n   | a | b |\n   |---|---|\n   | 1 | 2 |\n", 1, 1},
            {"nested list", "- a\n  - b\n    | a | b |\n    |---|---|\n    | 1 | 2 |\n", 1, 2},
            {"empty cells", "- item\n  | a | b |\n  |---|---|\n  |   |   |\n", 1, 1},
            {"math cells", "- item\n  | $a$ | $b|c$ |\n  |---|---|\n  | $f'(x)$ | 0 |\n", 1, 1},
            {"standalone", "| a | b |\n|---|---|\n| 1 | 2 |\n", 1, 0},
            {"after top-level paragraph", "text\n| a | b |\n|---|---|\n| 1 | 2 |\n", 1, 0},
            {"mismatched columns stay text", "- item\n  | a | b |\n  |---|\n  | 1 | 2 |\n", 0, 1},
            {"lazy line stays in item", "- item\n| a | b |\n|---|---|\n", 0, 1},
            {"fenced code untouched", "```\ntext\n| a | b |\n|---|---|\n```\n", 0, 0},
            {"paragraph line with a pipe", "x | y\n| a | b |\n|---|---|\n", 1, 0},
            {"list in quote", "> - x\n>   | a | b |\n>   |---|---|\n> - y\n", 1, 2},
            {"quote in list", "- > x\n  > | a | b |\n  > |---|---|\n- y\n", 1, 2},
            {"lazy header row", "> x\n| a | b |\n> |---|---|\n", 1, 0},
            {"lazy delimiter row is text", "> x\n> | a | b |\n|---|---|\n", 0, 0},
            {"tab indentation", "- x\n\t| a | b |\n\t|---|---|\n- y\n", 1, 2},
            {"mixed indentation", "- x\n  | a | b |\n   |---|---|\n     | 1 | 2 |\n", 1, 1},
            {"over-indented delimiter is text", "x\n| a | b |\n    |---|---|\n", 0, 0},
            {"indented code", "x\n\n    y\n    | a | b |\n    |---|---|\n", 0, 0},
            {"indented code in list", "- x\n\n      y\n      | a | b |\n      |---|---|\n- z\n", 0, 2},
            {"fence in list", "- x\n  ```\n  y\n  | a | b |\n  |---|---|\n  ```\n- z\n", 0, 2},
            {"backticks in tilde fence", "~~~\n```\nx\n| a | b |\n|---|---|\n~~~\n", 0, 0},
            {"raw HTML block", "<div>\nx\n| a | b |\n|---|---|\n</div>\n", 0, 0},
            {"raw HTML comment", "<!-- x\n| a | b |\n|---|---|\n-->\n", 0, 0},
            {"inline HTML paragraph", "<b>x</b>\n| a | b |\n|---|---|\n", 1, 0},
            {"escaped pipe", "x\n| a \\| b | c |\n|---|---|\n", 1, 0},
            {"escaped backslash leaves the pipe", "x\n| a \\\\| b | c |\n|---|---|\n", 0, 0},
            {"escaped pipe in code", "x\n| `a\\|b` | c |\n|---|---|\n", 1, 0},
            {"bare pipe in code is a boundary", "x\n| `a|b` | c |\n|---|---|\n", 0, 0},
            {"invalid delimiter", "x\n| a | b |\n|-x-|---|\n", 0, 0},
            {"empty delimiter cell", "x\n| a | b |\n|---||\n", 0, 0},
            {"too many delimiters", "x\n| a | b |\n|---|---|---|\n", 0, 0},
            {"no pipe delimiter is a heading", "- item\n  text\n  ---\n", 0, 1},
        };
        const QString quote = ExportManager::markdownToHtml(
            QStringLiteral("> text\n> | a | b |\n> |---|---|\n> | 1 | 2 |\n"), MathRenderMode::KatexOutput);
        ok &= require(quote.contains(QStringLiteral("<blockquote>")) && quote.count(QStringLiteral("<table")) == 1
                          && quote.indexOf(QStringLiteral("<table")) < quote.indexOf(QStringLiteral("</blockquote>")),
                      QStringLiteral("table after quoted paragraph left the blockquote: ") + quote);
        const QString mapped = ExportManager::markdownToPreviewHtml(sample);
        ok &= require(mapped.contains(QStringLiteral("<li data-src-start=\"7\""))
                          && mapped.contains(QStringLiteral("<table data-src-start=\"8\" data-src-end=\"11\"")),
                      QStringLiteral("table split shifted source lines: ") + mapped);
        for (const Case& c : cases) {
            for (const QString& rendered : {ExportManager::markdownToHtml(QString::fromUtf8(c.md), MathRenderMode::KatexOutput),
                                            ExportManager::markdownToPreviewHtml(QString::fromUtf8(c.md))}) {
                ok &= require(rendered.count(QStringLiteral("<table")) == c.tables
                                  && rendered.count(QStringLiteral("<li")) == c.items,
                              QStringLiteral("table case failed: ") + QLatin1String(c.name) + QLatin1Char('\n') + rendered);
            }
        }

        // A table must not change the list around it: removing it from the
        // rendered output must leave what the list renders without it.
        static const QRegularExpression renderedTable(
            QStringLiteral("(<div class=\"table-wrap\"[^>]*>\\s*)?<table.*?</table>(\\s*</div>)?"),
            QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression lineAttributes(
            QStringLiteral(" data-(src|fastmd-task)-(start|end|line)=\"\\d+\""));
        static const QRegularExpression whitespace(QStringLiteral("\\s+"));
        const auto listShape = [](QString html) {
            return html.remove(renderedTable).remove(lineAttributes).remove(whitespace);
        };
        struct Shape { const char* name; const char* md; bool loose; };
        const Shape shapes[] = { // %1 becomes the table, at the marker's indentation
            {"tight bullets", "- a\n  %1\n- b\n- c\n", false},
            {"tight, table in the last item", "- a\n- b\n  %1\n", false},
            {"tight ordered", "3. a\n   %1\n4. b\n", false},
            {"tight nested", "- a\n  - b\n    %1\n  - c\n- d\n", false},
            {"tight task list", "- [ ] a\n  %1\n- [x] b\n", false},
            {"tight list in quote", "> - a\n>   %1\n> - b\n", false},
            {"tight, two tables", "- a\n  %1\n- b\n  %1\n- c\n", false},
            {"loose bullets", "- a\n  %1\n\n- b\n\n- c\n", true},
            {"loose by later items", "- a\n  %1\n- b\n\n- c\n", true},
            {"loose outer, tight inner", "- a\n\n  - b\n    %1\n  - c\n\n- d\n", true},
        };
        for (const Shape& shape : shapes) {
            QString with, without;
            for (const QString& line : QString::fromUtf8(shape.md).split(QLatin1Char('\n'))) {
                const int at = line.indexOf(QStringLiteral("%1"));
                if (at < 0) {
                    with += line + QLatin1Char('\n');
                    without += line + QLatin1Char('\n');
                    continue;
                }
                for (const char* row : {"| a | b |", "|---|---|", "| 1 | 2 |"})
                    with += line.left(at) + QLatin1String(row) + QLatin1Char('\n');
            }
            for (const bool preview : {false, true}) {
                const auto render = [preview](const QString& md) {
                    return preview ? ExportManager::markdownToPreviewHtml(md)
                                   : ExportManager::markdownToHtml(md, MathRenderMode::KatexOutput);
                };
                const QString rendered = render(with);
                ok &= require(rendered.contains(QStringLiteral("<table"))
                                  && listShape(rendered) == listShape(render(without))
                                  && rendered.contains(QStringLiteral("<p")) == shape.loose,
                              QStringLiteral("table changed list spacing: ") + QLatin1String(shape.name)
                                  + QLatin1Char('\n') + rendered);
            }
        }

        const QString lines = ExportManager::markdownToPreviewHtml(QStringLiteral(
            "- a\n  | h | i |\n  |---|---|\n  | 1 | 2 |\n- b\n\ntext\n| j | k |\n|---|---|\n| 3 | 4 |\n\nend\n"));
        for (const char* expected : {"<li data-src-start=\"1\" data-src-end=\"4\"",
                                     "<table data-src-start=\"2\" data-src-end=\"4\"",
                                     "<span data-src-line=\"4\">1", "<li data-src-start=\"5\" data-src-end=\"5\"",
                                     "<p data-src-start=\"7\" data-src-end=\"7\"",
                                     "<table data-src-start=\"8\" data-src-end=\"10\"",
                                     "<p data-src-start=\"12\" data-src-end=\"12\""}) {
            ok &= require(lines.contains(QLatin1String(expected)),
                          QStringLiteral("table source line missing: ") + QLatin1String(expected)
                              + QLatin1Char('\n') + lines);
        }

        const QString reference = ExportManager::markdownToHtml(
            QStringLiteral("[f]: /u\n| a | b |\n|---|---|\n\n[f]\n"), MathRenderMode::KatexOutput);
        ok &= require(reference.contains(QStringLiteral("<table"))
                          && reference.contains(QStringLiteral("href=\"/u\""))
                          && !reference.contains(QStringLiteral("[f]")),
                      QStringLiteral("reference definition before a table was lost: ") + reference);
    }

    QFile derivativesFile(QStringLiteral(FASTMD_FIXTURES_DIR "/test-math-derivatives.md"));
    ok &= require(derivativesFile.open(QIODevice::ReadOnly),
                  QStringLiteral("tests/fixtures/test-math-derivatives.md is missing"));
    const QString derivatives = QString::fromUtf8(derivativesFile.readAll());
    requireMathSpans(QStringLiteral("derivatives sample"), derivatives,
                     {QStringLiteral("\\[ y=3x^4-5x^3+2x^2-7x+1 \\]"),
                      QStringLiteral("\\[ y=\\frac{2x+3}{x-1} \\]"),
                      QStringLiteral("\\[ y=\\sqrt{3x^2+2x+1} \\]"),
                      QStringLiteral("\\[ y=(x^2+1)(2x^3-3x) \\]"),
                      QStringLiteral("\\[ y=(2x^2-3x+1)^5 \\]"),
                      QStringLiteral("\\[ y=2\\sin 3x-3\\cos 2x \\]"),
                      QStringLiteral("\\[ y=e^{2x+1}+\\ln(x^2+1) \\]"),
                      QStringLiteral("\\[ f(x)=x^3-3x^2+2x+1 \\]"),
                      QStringLiteral("\\(f'(2)\\)"),
                      QStringLiteral("\\[ y=x^3-2x^2+x+3 \\]"),
                      QStringLiteral("\\(x_0=2\\)"),
                      QStringLiteral("\\[ y=x^3-3mx^2+2x+1 \\]"),
                      QStringLiteral("\\(m\\)"),
                      QStringLiteral("\\(y'(1)=0\\)")});
    const QString derivativesPreview = ExportManager::markdownToPreviewHtml(derivatives);
    ok &= require(derivativesPreview.contains(QStringLiteral("<p data-src-start=\"13\" data-src-end=\"13\">"
                                                             "<span data-src-line=\"13\">\\[ y=3x^4"))
                      && derivativesPreview.contains(QStringLiteral("<p data-src-start=\"61\" data-src-end=\"61\">")),
                  QStringLiteral("derivatives sample equations lost their source lines"));

    // Incremental preview: diff each edit against the previous body the way the
    // page does, apply it, and require the whole-body render of the edited text
    // (line numbers aside, which PreviewPatchTest covers).
    const auto requireIncremental = [&](const QString& name, const QString& beforeMd,
                                        const QString& afterMd, qsizetype expectedInserted) {
        const PreviewBlocksPtr before = PreviewPatch::split(ExportManager::markdownToPreviewHtml(beforeMd));
        const PreviewBlocksPtr after = PreviewPatch::split(ExportManager::markdownToPreviewHtml(afterMd));
        ok &= require(before && after, name + QStringLiteral(": preview body could not be split into blocks"));
        if (!before || !after)
            return;
        const PreviewPatch::Patch patch = PreviewPatch::diff(*before, *after);
        const auto stripped = [](QStringView block) {
            QString s = block.toString();
            s.remove(lineAttrs);
            return s;
        };
        QStringList applied;
        qsizetype oldPos = 0;
        for (const PreviewPatch::Op& op : patch.ops) {
            while (oldPos < op.oldIndex)
                applied << stripped(before->block(oldPos++));
            for (qsizetype k = 0; k < op.insertCount; ++k) {
                const QStringView inserted = after->block(op.newIndex + k);
                ok &= require(!inserted.contains(QStringLiteral("FASTMD_MATH")),
                              name + QStringLiteral(": patch carries a math placeholder"));
                applied << stripped(inserted);
            }
            oldPos += op.removeCount;
        }
        while (oldPos < before->count())
            applied << stripped(before->block(oldPos++));
        QStringList full;
        for (qsizetype i = 0; i < after->count(); ++i)
            full << stripped(after->block(i));
        ok &= require(applied == full, name + QStringLiteral(": incremental result differs from a full render"));
        ok &= require(patch.insertedBlocks() == expectedInserted,
                      name + QStringLiteral(": rebuilt %1 blocks, expected %2")
                                 .arg(patch.insertedBlocks()).arg(expectedInserted));
    };
    // The sample is CRLF: build each edit with its line ending, and require
    // that every edit really changed the text.
    const QString nl = derivatives.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
    QString editedEquation = derivatives;
    editedEquation.replace(QStringLiteral("\\frac{2x+3}{x-1}"), QStringLiteral("\\frac{2x+5}{x-1}"));
    requireIncremental(QStringLiteral("edit an escaped equation"), derivatives, editedEquation, 1);
    ok &= require(ExportManager::markdownToPreviewHtml(editedEquation)
                      .contains(QStringLiteral("\\[ y=\\frac{2x+5}{x-1} \\]")),
                  QStringLiteral("edited equation was not rendered"));
    QString insertedLines = derivatives;
    insertedLines.replace(QStringLiteral("## Phần A. Bài tập") + nl,
                          QStringLiteral("## Phần A. Bài tập") + nl + nl
                              + QStringLiteral("New \\\\(z^2\\\\) line.") + nl);
    requireIncremental(QStringLiteral("insert a line above equations"), derivatives, insertedLines, 1);
    requireIncremental(QStringLiteral("delete the inserted line"), insertedLines, derivatives, 0);
    QString deletedEquation = derivatives;
    deletedEquation.remove(QStringLiteral("\\\\[ y=3x^4-5x^3+2x^2-7x+1 \\\\]") + nl + nl);
    requireIncremental(QStringLiteral("delete an equation"), derivatives, deletedEquation, 0);
    QString escapedToPlain = derivatives;
    escapedToPlain.replace(QStringLiteral("\\\\[ y=(2x^2-3x+1)^5 \\\\]"), QStringLiteral("$$ y=(2x^2-3x+1)^5 $$"));
    ok &= require(editedEquation != derivatives && insertedLines != derivatives
                      && deletedEquation != derivatives && escapedToPlain != derivatives,
                  QStringLiteral("an incremental edit did not change the sample"));
    requireIncremental(QStringLiteral("rewrite an equation with other delimiters"), derivatives, escapedToPlain, 1);
    // Switching documents replaces the whole body; a conversion carries no
    // state from the previous document.
    const QString otherDocument = QStringLiteral("# Other\n\n$$ q $$\n\n\\(r\\)");
    const QString otherFresh = ExportManager::markdownToPreviewHtml(otherDocument);
    ExportManager::markdownToPreviewHtml(derivatives);
    ok &= require(ExportManager::markdownToPreviewHtml(otherDocument) == otherFresh
                      && ExportManager::markdownToPreviewHtml(derivatives) == derivativesPreview,
                  QStringLiteral("switching documents changed a math render"));

    requireMathPipeline(QStringLiteral("Mermaid followed by KaTeX"),
                        mermaidFence + QStringLiteral("\n\n") + displayMath, 1);
    requireMathPipeline(QStringLiteral("KaTeX followed by Mermaid"),
                        displayMath + QStringLiteral("\n\n") + mermaidFence, 1);
    const QString versionHtml = ExportManager::markdownToHtml(versionPrompt, MathRenderMode::KatexOutput);
    const QString versionPreview = ExportManager::markdownToPreviewHtml(versionPrompt);
    for (const QString& rendered : {versionHtml, versionPreview}) {
        ok &= require(rendered.contains(QStringLiteral("$version = 2.7.0"))
                          && rendered.contains(QStringLiteral("<ul"))
                          && rendered.count(QStringLiteral("<li")) == 4
                          && rendered.count(QStringLiteral("<code>")) == 4
                          && rendered.contains(QStringLiteral("scripts\\build-all-and-publish-to-github.ps1")),
                      QStringLiteral("dollar signs inside inline code corrupted the following list"));
    }
    for (const QString& rendered : {ExportManager::markdownToHtml(referenceLinks, MathRenderMode::KatexOutput),
                                    ExportManager::markdownToPreviewHtml(referenceLinks)}) {
        ok &= require(rendered.contains(QStringLiteral("href=\"https://skypediacode.github.io/fastmd/\""))
                          && rendered.contains(QStringLiteral("title=\"FastMD website\""))
                          && rendered.contains(QStringLiteral("href=\"https://fastmd.example/\""))
                          && rendered.contains(QStringLiteral("title=\"Collapsed and shortcut title\""))
                          && rendered.contains(QStringLiteral("href=\"https://example.com\"")),
                      QStringLiteral("reference or inline links were not rendered with their destinations and titles"));
        ok &= require(!rendered.contains(QStringLiteral("[site]:"))
                          && !rendered.contains(QStringLiteral("[FastMD]:")),
                      QStringLiteral("link reference definitions appeared as preview text"));
    }
    for (const QString& rendered : {ExportManager::markdownToHtml(numericReference, MathRenderMode::KatexOutput),
                                    ExportManager::markdownToPreviewHtml(numericReference)}) {
        ok &= require(rendered.contains(QStringLiteral("<sup class=\"reference-link\"><a"))
                          && rendered.contains(QStringLiteral("href=\"https://markdown.com.vn/?lang=en\""))
                          && rendered.contains(QStringLiteral("]</a></sup>")),
                      QStringLiteral("numeric reference link was not rendered as superscript [N]"));
    }
    ok &= require(!ExportManager::markdownToPreviewHtml(numericInlineLink)
                       .contains(QStringLiteral("reference-link")),
                  QStringLiteral("ordinary numeric inline link became a citation"));
    const QString html = ExportManager::markdownToHtml(source, MathRenderMode::KatexOutput);
    const QString preview = ExportManager::markdownToPreviewHtml(source);
    for (const QString& rendered : {html, preview}) {
        ok &= require(rendered.contains(QStringLiteral("<dl"))
                          && rendered.contains(QStringLiteral("Term</dt>"))
                          && rendered.contains(QStringLiteral("formatted")),
                      QStringLiteral("definition list missing from shared renderer"));
        ok &= require(rendered.contains(QStringLiteral("id=\"fn-a\""))
                          && rendered.contains(QStringLiteral("id=\"fnref-a-2\""))
                          && rendered.contains(QStringLiteral("href=\"#fn-b\""))
                          && rendered.contains(QStringLiteral("footnote-backref")),
                      QStringLiteral("footnote links missing from shared renderer"));
        ok &= require(rendered.contains(QStringLiteral("Text"))
                          && rendered.contains(QStringLiteral("and again "))
                          && rendered.contains(QStringLiteral("then "))
                          && rendered.contains(QStringLiteral("<sup id=\"fnref-a-1\""))
                          && rendered.contains(QStringLiteral("<sup id=\"fnref-a-2\""))
                          && rendered.contains(QStringLiteral("<sup id=\"fnref-b-1\"")),
                      QStringLiteral("footnote references consumed surrounding paragraph text"));
        const int noteStart = rendered.indexOf(QStringLiteral("id=\"fn-a\""));
        const int backlink = rendered.indexOf(QStringLiteral("footnote-backlinks"), noteStart);
        const int noteEnd = rendered.indexOf(QStringLiteral("</li>"), backlink);
        ok &= require(backlink > noteStart && backlink < noteEnd
                          && rendered.mid(backlink, noteEnd - backlink).contains(QStringLiteral("<sup>1</sup>")),
                      QStringLiteral("footnote backlinks were not inline and numbered"));
        ok &= require(rendered.contains(QStringLiteral("id=\"café\""))
                          && rendered.contains(QStringLiteral("id=\"usage-1\"")),
                      QStringLiteral("Unicode or duplicate heading anchors missing"));
        ok &= require(!rendered.contains(QStringLiteral("Not a heading"))
                          || !rendered.contains(QStringLiteral("id=\"not-a-heading\"")),
                      QStringLiteral("fenced heading became an anchor"));
    }

    const QString literalMark = QStringLiteral("Before <mark>important</mark> after");
    const QString markHtml = ExportManager::markdownToHtml(literalMark, MathRenderMode::KatexOutput);
    ok &= require(markHtml.contains(QStringLiteral("<mark>important</mark>")),
                  QStringLiteral("literal HTML mark was stripped from the export Markdown path"));
    const QString markPreview = ExportManager::markdownToPreviewHtml(literalMark);
    // The preview renderer wraps ordinary text in source-mapping spans, so the
    // mark's contents are not contiguous with its literal tags as they are in
    // the export path. Assert the preserved element rather than that incidental
    // serialization detail.
    ok &= require(markPreview.contains(QStringLiteral("<mark>"))
                      && markPreview.contains(QStringLiteral("important"))
                      && markPreview.contains(QStringLiteral("</mark>")),
                  QStringLiteral("literal HTML mark was stripped from the preview Markdown path"));
    const QString markPrintHtml = ExportManager::buildPreviewHtml(
        ExportManager::markdownToHtml(literalMark, MathRenderMode::KatexOutput), false, 16, true);
    ok &= require(markPrintHtml.contains(QStringLiteral("mark { background: var(--mark-bg)"))
                      && markPrintHtml.contains(QStringLiteral("--mark-bg:   #fff3b8")),
                  QStringLiteral("standalone print HTML lost mark styling"));
    const QString advancedHtml = ExportManager::markdownToHtml(advanced, MathRenderMode::KatexOutput);
    const QString advancedPreview = ExportManager::markdownToPreviewHtml(advanced);
    const QString plainFencePreview = ExportManager::markdownToPreviewHtml(
        QStringLiteral("```\nThis is prose, not a language.\n```"));
    const QString escapedCallout = QStringLiteral("> \\[!TIP\\] Escaped marker still makes a callout.");
    const QString standaloneCallout = ExportManager::markdownToPreviewHtml(
        QStringLiteral("> [!NOTE]\n>\n> This is a simple note."));
    const QString inlineCallout = ExportManager::markdownToPreviewHtml(
        QStringLiteral("> [!TIP]\n> This is a same-paragraph tip."));
    const QString ordinaryQuote = ExportManager::markdownToPreviewHtml(
        QStringLiteral("> This is an ordinary blockquote."));
    const QString headingSource = QStringLiteral(
        "## Before\n\n> [!NOTE]\n> ### Inside\n> Callout body\n\n## After\n\n[TOC]");
    const QString headingHtml = ExportManager::markdownToHtml(headingSource, MathRenderMode::KatexOutput);
    const QString mathSource = QStringLiteral(
        "> [!NOTE]\n> Math:\n>\n> $$\n> a^2+b^2=c^2\n> $$\n\nOutside $a^2+b^2=c^2$\n\n"
        "> [!TIP]\n> ```mermaid\n> graph LR\n> A --> B\n> ```\n\n> [!WARNING]\n> \\[\n> x^2+y^2\n> \\]");
    const QString mathPreview = ExportManager::markdownToPreviewHtml(mathSource);
    QStringList extensionSnippets;
    const QString extensionMath = MarkdownExtensions::preprocess(
        QStringLiteral("> [!IMPORTANT]\n> $$\n> a^2+b^2=c^2\n> $$"), &extensionSnippets);
    ok &= require(advancedHtml.contains(QStringLiteral("fastmd-callout-note"))
                      && advancedHtml.contains(QStringLiteral("fastmd-callout-title")),
                  QStringLiteral("supported callout was not transformed"));
    ok &= require(advancedHtml.contains(QStringLiteral("[!CUSTOM]")),
                  QStringLiteral("unknown callout marker was not preserved"));
    ok &= require(ordinaryQuote.contains(QStringLiteral("<blockquote"))
                      && !ordinaryQuote.contains(QStringLiteral("fastmd-callout")),
                  QStringLiteral("ordinary blockquote was changed into a callout"));
    ok &= require(advancedPreview.contains(QStringLiteral("data-fastmd-task-start"))
                      && advancedPreview.contains(QStringLiteral("type=\"checkbox\"")),
                  QStringLiteral("preview task source metadata or controls missing"));
    ok &= require(advancedHtml.contains(QStringLiteral("<pre><code")),
                  QStringLiteral("fenced callout-like text was transformed"));
    ok &= require(ExportManager::markdownToHtml(escapedCallout).contains(QStringLiteral("fastmd-callout-tip")),
                  QStringLiteral("escaped callout marker was not normalized"));
    ok &= require(standaloneCallout.contains(QStringLiteral("fastmd-callout-note"))
                      && !standaloneCallout.contains(QStringLiteral(">[!NOTE]")),
                  QStringLiteral("standalone callout marker was not transformed"));
    ok &= require(inlineCallout.contains(QStringLiteral("fastmd-callout-tip"))
                      && inlineCallout.contains(QStringLiteral("This is a same-paragraph tip.")),
                  QStringLiteral("inline callout marker was not transformed"));
    const int beforeId = headingHtml.indexOf(QStringLiteral("id=\"before\""));
    const int insideId = headingHtml.indexOf(QStringLiteral("id=\"inside\""));
    const int afterId = headingHtml.indexOf(QStringLiteral("id=\"after\""));
    ok &= require(beforeId >= 0 && insideId > beforeId && afterId > insideId
                      && headingHtml.contains(QStringLiteral("href=\"#inside\"")),
                  QStringLiteral("heading IDs or TOC link shifted around callout"));
    ok &= require(mathPreview.contains(QStringLiteral("fastmd-callout-note"))
                      && mathPreview.contains(QStringLiteral("$$a^2+b^2=c^2$$"))
                      && mathPreview.contains(QStringLiteral("\\[x^2+y^2\\]"))
                      && !mathPreview.contains(QStringLiteral("<sup>2</sup>+b")),
                  QStringLiteral("KaTeX inside callout was altered by extensions"));
    ok &= require(extensionMath.contains(QStringLiteral("a^2+b^2=c^2")),
                  QStringLiteral("math was altered by the Markdown extensions pass"));
    ok &= require(mathPreview.contains(QStringLiteral("language-mermaid"))
                      && mathPreview.contains(QStringLiteral("graph LR")),
                  QStringLiteral("Mermaid inside callout lost its source block"));
    ok &= require(advancedPreview.count(QStringLiteral("data-fastmd-task-start")) >= 3,
                  QStringLiteral("nested or duplicate task source mappings are missing"));
    ok &= require(plainFencePreview.contains(QStringLiteral("<pre"))
                      && plainFencePreview.contains(QStringLiteral("<code>"))
                      && !plainFencePreview.contains(QStringLiteral("language-")),
                  QStringLiteral("unlabelled fenced code emitted an empty language class"));
    const QString looseTaskPreview = ExportManager::markdownToPreviewHtml(
        QStringLiteral("10. Tenth item\n\n- [ ] Unchecked task\n\n- [x] Checked task"));
    const QString tightTaskPreview = ExportManager::markdownToPreviewHtml(
        QStringLiteral("10. Tenth item\n- [ ] Unchecked task\n- [x] Checked task"));
    const QString mixedTaskPreview = ExportManager::markdownToPreviewHtml(
        QStringLiteral("- [ ] Unchecked task\n\n- [x] Checked task\n\n* [ ] Another task\n\n+ [x] Completed"));
    const QString mixedTaskHtml = ExportManager::markdownToHtml(
        QStringLiteral("- [ ] Unchecked task\n\n- [x] Checked task\n\n* [ ] Another task\n\n+ [x] Completed"));
    const QString orderedTaskHtml = ExportManager::markdownToHtml(
        QStringLiteral("1. [ ] First task\n\n2. [x] Second task"));
    ok &= require(looseTaskPreview.contains(QStringLiteral("data-fastmd-loose-list=\"1\""))
                      && looseTaskPreview.contains(QStringLiteral("<p data-src-start=\"3\""))
                      && !tightTaskPreview.contains(QStringLiteral("data-fastmd-loose-list=\"1\""))
                      && !tightTaskPreview.contains(QStringLiteral("<p data-src-start=\"2\""))
                      && mixedTaskPreview.count(QStringLiteral("data-fastmd-loose-list=\"1\"")) == 3
                      && mixedTaskHtml.count(QStringLiteral("data-fastmd-loose-list=\"1\"")) == 3
                      && mixedTaskPreview.count(QStringLiteral("data-fastmd-task-list=\"1\"")) == 3
                      && mixedTaskHtml.count(QStringLiteral("data-fastmd-task-list=\"1\"")) == 3
                      && orderedTaskHtml.contains(QStringLiteral("<ol data-fastmd-task-list=\"1\""))
                      && orderedTaskHtml.contains(QStringLiteral("data-fastmd-loose-list=\"1\"")),
                  QStringLiteral("tight and loose task-list spacing was not preserved"));
    const QString portableHtml = ExportManager::buildFullHtml(
        QStringLiteral("<p>$$a^2+b^2=c^2$$</p>"), false, 17, false, true);
    ok &= require(!portableHtml.contains(QStringLiteral("file:///"))
                      && portableHtml.contains(QStringLiteral("data:font/woff2;base64,")),
                  QStringLiteral("HTML export leaked a machine path or externalized KaTeX assets"));

    const QString yamlMd = QStringLiteral(
        "---\n"
        "title: \"Test Document Title\"\n"
        "author: 'Jane Doe'\n"
        "description: A document description\n"
        "keywords: [markdown, pdf, fastmd]\n"
        "---\n\n"
        "# Content\nHello world.");

    const auto meta = ExportManager::parseYamlFrontMatter(yamlMd);
    ok &= require(meta.title == QStringLiteral("Test Document Title")
                      && meta.author == QStringLiteral("Jane Doe")
                      && meta.description == QStringLiteral("A document description")
                      && meta.keywords == QStringLiteral("markdown, pdf, fastmd"),
                  QStringLiteral("YAML front matter parsing failed"));

    ok &= require(ExportManager::stripYamlFrontMatter(yamlMd)
                      == QStringLiteral("\n# Content\nHello world."),
                  QStringLiteral("YAML front matter was not stripped exactly at the closing fence"));

    // A bare "---" is also a thematic break: a document that opens with one must
    // not have everything up to its next rule swallowed as metadata.
    const QString hrDoc = QStringLiteral("---\n\nJust a paragraph, no metadata.\n\n---\n\nMore.\n");
    ok &= require(ExportManager::stripYamlFrontMatter(hrDoc) == hrDoc
                      && ExportManager::parseYamlFrontMatter(hrDoc).isEmpty(),
                  QStringLiteral("a leading thematic break was mistaken for YAML front matter"));

    // "---" and "..." are the only YAML terminators.
    const QString noCloseDoc = QStringLiteral("---\ntitle: T\nFinished\n\n# Body\n");
    ok &= require(ExportManager::stripYamlFrontMatter(noCloseDoc) == noCloseDoc,
                  QStringLiteral("an unterminated front matter block was stripped anyway"));
    ok &= require(ExportManager::parseYamlFrontMatter(
                      QStringLiteral("---\ntitle: T\n...\n\n# Body\n")).title == QStringLiteral("T"),
                  QStringLiteral("\"...\" was not accepted as a YAML terminator"));

    ok &= require(ExportManager::stripYamlFrontMatter(
                      QStringLiteral("---\r\ntitle: T\r\n---\r\n\r\n# Body\r\n"))
                      == QStringLiteral("\r\n# Body\r\n"),
                  QStringLiteral("CRLF front matter was not stripped"));

    // Front matter removes leading source lines; the preview's data-src-* line
    // numbers must still refer to the original document (see MathLineShift).
    const QString mappedMd = QStringLiteral(
        "---\ntitle: T\nauthor: A\n---\n\n# Heading\n\nParagraph two.\n");
    const QString mappedPreview = ExportManager::markdownToPreviewHtml(mappedMd);
    ok &= require(mappedPreview.contains(QStringLiteral("data-src-start=\"6\""))
                      && mappedPreview.contains(QStringLiteral("data-src-start=\"8\"")),
                  QStringLiteral("preview source-line mapping was not corrected for stripped front matter"));

    // Block ranges only identify the first line of a paragraph. Text runs in
    // the live preview also need their own line metadata so navigation from
    // the second rendered line does not flash the paragraph's first line.
    const QString lineMappedPreview = ExportManager::markdownToPreviewHtml(
        QStringLiteral("First source line\nSecond source line\nThird source line"));
    ok &= require(lineMappedPreview.contains(QStringLiteral("data-src-line=\"1\">First source line"))
                      && lineMappedPreview.contains(QStringLiteral("data-src-line=\"2\">Second source line"))
                      && lineMappedPreview.contains(QStringLiteral("data-src-line=\"3\">Third source line")),
                  QStringLiteral("inline preview source-line mapping was not emitted"));

    // MD4C hands a fenced block's line breaks over as its own static "\n", not
    // a pointer into the source. They once mapped to the document's last line,
    // so every code block's range ran to the end of the file and every edit
    // below it changed the block's markup.
    const QString fencedPreview = ExportManager::markdownToPreviewHtml(
        QStringLiteral("```cpp\nint a;\nint b;\n```\n\nOne\n\nTwo\n\nThree\n"));
    ok &= require(fencedPreview.contains(QStringLiteral("<pre data-src-start=\"2\" data-src-end=\"3\">"))
                      && !fencedPreview.contains(QStringLiteral("data-src-line=\"10\">\n")),
                  QStringLiteral("fenced code line breaks were mapped outside their block"));

    // Source provenance must survive every line-changing preprocessing pass:
    // [TOC] expands one source line into many, while footnote lowering removes
    // definitions and appends generated HTML. Later ordinary prose must still
    // point to its original editor line.
    const QString provenanceMd = QStringLiteral(
        "# One\n\n[TOC]\n\n## Two\n\nAfter TOC.\n\n"
        "Text[^n].\n\n[^n]: Footnote body.\n\n## Three\n\nAfter footnote.\n");
    const QString provenancePreview = ExportManager::markdownToPreviewHtml(provenanceMd);
    ok &= require(provenancePreview.contains(QStringLiteral("data-src-line=\"7\">After TOC."))
                      && provenancePreview.contains(QStringLiteral("data-src-line=\"15\">After footnote.")),
                  QStringLiteral("TOC or footnote preprocessing drifted preview source mappings"));

    QFile allFeaturesFile(QStringLiteral(FASTMD_FIXTURES_DIR "/test-all-features.md"));
    ok &= require(allFeaturesFile.open(QIODevice::ReadOnly | QIODevice::Text),
                  QStringLiteral("could not open test-all-features.md"));
    if (allFeaturesFile.isOpen()) {
        const QString allFeaturesPreview = ExportManager::markdownToPreviewHtml(
            QString::fromUtf8(allFeaturesFile.readAll()));
        ok &= require(allFeaturesPreview.contains(
                          QStringLiteral("data-src-line=\"481\">End of FastMD feature test.")),
                      QStringLiteral("test-all-features source mapping drifted after TOC/extensions/math"));
    }

    const QString mindMapHtml = ExportManager::buildMindMapHtml(QStringLiteral("# Root"));
    ok &= require(mindMapHtml.contains(QStringLiteral("window.__fastmdOwnWheelZoom=true;"))
                      && !mindMapHtml.contains(QStringLiteral("scrollForPan:true"))
                      && !mindMapHtml.contains(QStringLiteral("window.addEventListener('wheel'"))
                      && mindMapHtml.contains(QStringLiteral("markmap-lib@0.18.12/dist/browser/index.iife.js")),
                  QStringLiteral("Mind-map HTML did not preserve Markmap's browser runtime and native wheel zoom"));
    // Both must be pinned: Markmap's `pan` default adds a second wheel listener
    // that pans while d3.zoom zooms the same notch, and `scrollForPan` defaults
    // to isMacintosh, which would make the same file need Ctrl+wheel on a Mac.
    ok &= require(mindMapHtml.contains(QStringLiteral("pan:false,scrollForPan:false,zoom:true")),
                  QStringLiteral("Mind-map HTML must pin pan/scrollForPan so a bare wheel only zooms"));

    const QString transparentMindMap = ExportManager::buildMindMapRenderHtml(
        QStringLiteral("# Root"), true, QString(), true);
    ok &= require(transparentMindMap.contains(QStringLiteral("body,#mindmap{background:transparent"))
                      && transparentMindMap.contains(QStringLiteral("--markmap-circle-open-bg:transparent")),
                  QStringLiteral("DOCX mind-map rendering did not preserve a transparent canvas"));

    QTemporaryFile tempHtmlFile;
    ok &= require(tempHtmlFile.open(), QStringLiteral("could not create a temporary HTML file"));
    if (tempHtmlFile.isOpen()) {
        const QString tempPath = tempHtmlFile.fileName();
        tempHtmlFile.close();
        ok &= require(ExportManager::exportHtml(yamlMd, tempPath, false),
                      QStringLiteral("HTML export of a front-matter document failed"));
        QFile f(tempPath);
        ok &= require(f.open(QIODevice::ReadOnly | QIODevice::Text),
                      QStringLiteral("could not read back the exported HTML"));
        const QString exportedHtml = QString::fromUtf8(f.readAll());
        ok &= require(exportedHtml.contains(QStringLiteral("<title>Test Document Title</title>"))
                          && exportedHtml.contains(QStringLiteral("<meta name=\"author\" content=\"Jane Doe\">"))
                          && exportedHtml.contains(QStringLiteral("<meta name=\"description\" content=\"A document description\">"))
                          && exportedHtml.contains(QStringLiteral("<meta name=\"keywords\" content=\"markdown, pdf, fastmd\">")),
                      QStringLiteral("Exported HTML missing YAML metadata in head"));
        ok &= require(!exportedHtml.contains(QStringLiteral("author: 'Jane Doe'"))
                          && !exportedHtml.contains(QStringLiteral("title: \"Test Document Title\"")),
                      QStringLiteral("YAML front matter was not stripped from exported body"));
    }

    // Text alignment: md4c already renders a blank-line separated <div> wrapper
    // correctly, and the preview's raw-HTML source-mapping wrapper must not break
    // it. Both paths must emit the div and parse the body as Markdown.
    {
        const QString aligned = QStringLiteral(
            "<div style=\"text-align: center;\">\n\n### Centered\n\n- one\n- two\n\n</div>\n\n"
            "After.\n");
        for (const QString& rendered : {ExportManager::markdownToHtml(aligned, MathRenderMode::KatexOutput),
                                        ExportManager::markdownToPreviewHtml(aligned)}) {
            ok &= require(rendered.contains(QStringLiteral("<div style=\"text-align: center;\">"))
                              && rendered.contains(QStringLiteral("<h3"))
                              && rendered.contains(QStringLiteral("<li"))
                              && !rendered.contains(QStringLiteral("@@FASTMD_ALIGN"))
                              && rendered.count(QStringLiteral("</div>")) >= 1,
                          QStringLiteral("alignment wrapper did not survive rendering"));
        }
        // Without blank-line separation the div is not an HTML block boundary, so
        // nothing may be tokenized -- a leaked sentinel would be visible text.
        const QString tight = QStringLiteral(
            "<div style=\"text-align: right;\">\nplain paragraph\n</div>\n");
        ok &= require(!ExportManager::markdownToPreviewHtml(tight).contains(QStringLiteral("@@FASTMD_ALIGN")),
                      QStringLiteral("alignment sentinel leaked into the preview"));
    }

    {
        ok &= require(ExportManager::hasLabelledCodeBlock(
                          QStringLiteral("<pre><code class=\"language-cpp\">int x;</code></pre>")),
                      QStringLiteral("labelled fence not detected"));
        ok &= require(ExportManager::hasLabelledCodeBlock(
                          QStringLiteral("<PRE><CODE CLASS=\"Language-Python\">x</CODE></PRE>")),
                      QStringLiteral("labelled fence not detected case-insensitively"));
        ok &= require(!ExportManager::hasLabelledCodeBlock(
                          QStringLiteral("<pre><code>plain prose</code></pre>")),
                      QStringLiteral("unlabelled fence reported as labelled"));
        ok &= require(!ExportManager::hasLabelledCodeBlock(
                          QStringLiteral("<p><code class=\"language-cpp\">x</code></p>")),
                      QStringLiteral("inline code reported as a labelled block"));
        ok &= require(!ExportManager::hasLabelledCodeBlock(
                          QStringLiteral("<pre>plain</pre><p>language-cpp</p>")),
                      QStringLiteral("language text outside a code block reported as labelled"));
        ok &= require(!ExportManager::hasLabelledCodeBlock(
                          QStringLiteral("<pre><code class=\"language-mermaid\">graph TD</code></pre>")),
                      QStringLiteral("mermaid-only body reported as needing highlight.js"));
        ok &= require(ExportManager::hasLabelledCodeBlock(
                          QStringLiteral("<pre><code class=\"language-mermaid\">graph TD</code></pre>"
                                         "<pre><code class=\"language-mermaidjs\">x</code></pre>")),
                      QStringLiteral("non-mermaid label after a mermaid fence not detected"));

        const QString unlabelled = ExportManager::markdownToHtml(
            QStringLiteral("Prose.\n\n```\nplain words here\n```\n"), MathRenderMode::KatexOutput);
        ok &= require(!ExportManager::hasLabelledCodeBlock(unlabelled),
                      QStringLiteral("unlabelled markdown fence produced a labelled block"));
        const QString labelled = ExportManager::markdownToHtml(
            QStringLiteral("```cpp\nint x = 1;\n```\n"), MathRenderMode::KatexOutput);
        ok &= require(ExportManager::hasLabelledCodeBlock(labelled),
                      QStringLiteral("labelled markdown fence not detected"));
    }

    QFile sampleFile(QStringLiteral(FASTMD_FIXTURES_DIR "/front-matter.md"));
    ok &= require(sampleFile.open(QIODevice::ReadOnly),
                  QStringLiteral("tests/fixtures/front-matter.md is missing"));
    if (sampleFile.isOpen()) {
        const auto sampleMeta =
            ExportManager::parseYamlFrontMatter(QString::fromUtf8(sampleFile.readAll()));
        ok &= require(sampleMeta.title == QString::fromUtf8("Báo cáo Kỹ thuật FastMD")
                          && sampleMeta.author == QStringLiteral("Antigravity Team")
                          && sampleMeta.description == QString::fromUtf8("Tài liệu thiết kế kiến trúc hệ thống FastMD")
                          && sampleMeta.keywords == QStringLiteral("c++, qt6, markdown, pdf"),
                      QStringLiteral("YAML front matter parsing failed for tests/fixtures/front-matter.md"));
    }

    return ok ? 0 : 1;
}
