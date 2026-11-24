#include "DocumentSummary.h"

#include <QCoreApplication>
#include <QDebug>

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
    bool ok = true;

    // Empty text: one logical line, zero words/characters.
    {
        const DocumentSummary s = DocumentSummaryAnalyzer::analyze(QString(), QString(), EditorMode::PlainText);
        ok &= require(s.characterCount == 0 && s.wordCount == 0 && s.lineCount == 1,
                      QStringLiteral("empty document should report 1 logical line"));
    }

    // Unicode characters count as UTF-16 code units via QString::size().
    {
        const QString text = QStringLiteral("café éé");
        const DocumentSummary s = DocumentSummaryAnalyzer::analyze(text, QString(), EditorMode::PlainText);
        ok &= require(s.characterCount == text.size() && s.wordCount == 2,
                      QStringLiteral("unicode character/word counts mismatched"));
    }

    // Whitespace-only text: zero words, characters still counted.
    {
        const QString text = QStringLiteral("   \t  ");
        const DocumentSummary s = DocumentSummaryAnalyzer::analyze(text, QString(), EditorMode::PlainText);
        ok &= require(s.wordCount == 0 && s.characterCount == text.size(),
                      QStringLiteral("whitespace-only text should report zero words"));
    }

    // Multiple lines and trailing newline both increase the logical line count,
    // matching QTextDocument::blockCount() behavior.
    {
        const DocumentSummary noTrailing =
            DocumentSummaryAnalyzer::analyze(QStringLiteral("a\nb\nc"), QString(), EditorMode::PlainText);
        ok &= require(noTrailing.lineCount == 3, QStringLiteral("three-line text should report 3 lines"));

        const DocumentSummary trailing =
            DocumentSummaryAnalyzer::analyze(QStringLiteral("a\nb\nc\n"), QString(), EditorMode::PlainText);
        ok &= require(trailing.lineCount == 4,
                      QStringLiteral("trailing newline should add an extra logical line"));
    }

    // Selection counts: zero with no selection; non-zero with single/multi-line selection.
    {
        const DocumentSummary noSel =
            DocumentSummaryAnalyzer::analyze(QStringLiteral("hello world"), QString(), EditorMode::PlainText);
        ok &= require(noSel.selectedCharacterCount == 0 && noSel.selectedWordCount == 0,
                      QStringLiteral("no selection should report zero selected chars/words"));

        const DocumentSummary singleLineSel = DocumentSummaryAnalyzer::analyze(
            QStringLiteral("hello world"), QStringLiteral("world"), EditorMode::PlainText);
        ok &= require(singleLineSel.selectedCharacterCount == 5 && singleLineSel.selectedWordCount == 1,
                      QStringLiteral("single-line selection counts mismatched"));

        const QString multiLineSel = QStringLiteral("line one line two");
        const DocumentSummary multi = DocumentSummaryAnalyzer::analyze(
            QStringLiteral("line one\nline two"), multiLineSel, EditorMode::PlainText);
        ok &= require(multi.selectedCharacterCount == multiLineSel.size() && multi.selectedWordCount == 4,
                      QStringLiteral("multi-line selection counts mismatched"));
    }

    // Plain-text analysis must not populate Markdown structural counts even
    // when the text contains Markdown-looking syntax.
    {
        const QString text = QStringLiteral("# Heading\n[link](x) ![img](y)\n$a+b$");
        const DocumentSummary s = DocumentSummaryAnalyzer::analyze(text, QString(), EditorMode::PlainText);
        ok &= require(!s.isMarkdown && s.h1Count == 0 && s.h2Count == 0 && s.h3Count == 0
                          && s.linkCount == 0 && s.imageCount == 0
                          && s.tableCount == 0 && s.codeBlockCount == 0 && s.equationCount == 0,
                      QStringLiteral("plain-text mode should report only general statistics"));
    }

    // Mixed Markdown: ATX/setext headings, inline/reference/autolinks, images,
    // multiple tables, fenced/indented code, and all supported math delimiters.
    {
        const QString text = QStringLiteral(
            "# ATX H1\n\n"
            "Setext H2\n---------\n\n"
            "[inline](https://example.com) and [ref][r] and <https://autolink.example>\n\n"
            "[r]: https://example.com/ref\n\n"
            "![alt text](image.png)\n\n"
            "| a | b |\n|---|---|\n| 1 | 2 |\n\n"
            "| c | d |\n|---|---|\n| 3 | 4 |\n\n"
            "~~~\nfenced code\n~~~\n\n"
            "    indented code\n\n"
            "Inline math $a+b$ and display $$c+d$$ and \\(e+f\\) and \\[g+h\\]\n");
        const DocumentSummary s = DocumentSummaryAnalyzer::analyze(text, QString(), EditorMode::Markdown);
        ok &= require(s.isMarkdown, QStringLiteral("markdown mode should set isMarkdown"));
        ok &= require(s.h1Count == 1, QStringLiteral("expected 1 H1 (ATX), got %1").arg(s.h1Count));
        ok &= require(s.h2Count == 1, QStringLiteral("expected 1 H2 (setext), got %1").arg(s.h2Count));
        ok &= require(s.h3Count == 0, QStringLiteral("expected 0 H3, got %1").arg(s.h3Count));
        ok &= require(s.linkCount == 3,
                      QStringLiteral("expected 3 links (inline/reference/autolink), got %1").arg(s.linkCount));
        ok &= require(s.imageCount == 1,
                      QStringLiteral("expected 1 image, got %1").arg(s.imageCount));
        ok &= require(s.tableCount == 2,
                      QStringLiteral("expected 2 tables, got %1").arg(s.tableCount));
        ok &= require(s.codeBlockCount == 2,
                      QStringLiteral("expected 2 code blocks (fenced + indented), got %1").arg(s.codeBlockCount));
        ok &= require(s.equationCount == 4,
                      QStringLiteral("expected 4 equations (all delimiters), got %1").arg(s.equationCount));
    }

    // Markdown-like syntax inside code, escaped/incomplete math, raw HTML, and
    // H4-H6 must not inflate the structural counts.
    {
        const QString text = QStringLiteral(
            "#### H4 ignored\n##### H5 ignored\n###### H6 ignored\n\n"
            "~~~\n# not a heading\n[not a link](x)\n$not math$\n\\* escaped\n~~~\n\n"
            "`$inline code math$` and `[inline code link](x)`\n\n"
            "\\$escaped, not math\\$\n\n"
            "$unclosed math\n\n"
            "<div>raw html block</div>\n\n"
            "<span>inline html</span>\n");
        const DocumentSummary s = DocumentSummaryAnalyzer::analyze(text, QString(), EditorMode::Markdown);
        ok &= require(s.h1Count == 0 && s.h2Count == 0 && s.h3Count == 0,
                      QStringLiteral("H4-H6 should not be counted; got h1=%1 h2=%2 h3=%3")
                          .arg(s.h1Count).arg(s.h2Count).arg(s.h3Count));
        ok &= require(s.linkCount == 0,
                      QStringLiteral("code-contained/escaped markup should not count as links; got %1").arg(s.linkCount));
        ok &= require(s.equationCount == 0,
                      QStringLiteral("escaped/unclosed/code-contained math should not count; got %1").arg(s.equationCount));
        ok &= require(s.codeBlockCount == 1,
                      QStringLiteral("expected exactly 1 fenced code block; got %1").arg(s.codeBlockCount));
    }

    return ok ? 0 : 1;
}
