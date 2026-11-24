#include "ExportManager.h"
#include "MathRenderer.h"
#include "TextTransforms.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {
bool require(bool condition, const QString& message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

// Only unwrap the renderer's text-only mapping spans. Keep authored HTML,
// semantic tags, entities and all whitespace exactly as rendered.
QString withoutSourceMapping(QString html)
{
    static const QRegularExpression spans(
        QStringLiteral(R"(<span data-src-line="[0-9]+">([^<]*)</span>)"));
    static const QRegularExpression attrs(
        QStringLiteral(R"( data-src-(?:start|end|line)="[0-9]+")"));
    html.replace(spans, QStringLiteral("\\1"));
    html.remove(attrs);
    return html;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    const QString paragraph = QStringLiteral("First line\nSecond line");
    const QString hard = ExportManager::markdownToHtml(
        paragraph, MathRenderMode::KatexOutput, nullptr, true);
    const QString soft = ExportManager::markdownToHtml(
        paragraph, MathRenderMode::KatexOutput, nullptr, false);
    const QString previewHard = ExportManager::markdownToPreviewHtml(paragraph, nullptr, true);
    const QString previewSoft = ExportManager::markdownToPreviewHtml(paragraph, nullptr, false);
    ok &= require(hard == QStringLiteral("<p>First line<br>\nSecond line</p>\n")
                      && previewHard == QStringLiteral("<p data-src-start=\"1\" data-src-end=\"2\"><span data-src-line=\"1\">First line</span><br data-src-line=\"1\">\n<span data-src-line=\"2\">Second line</span></p>\n"),
                  QStringLiteral("enabled single-line-break rendering is not a parser hard break"));
    ok &= require(soft == QStringLiteral("<p>First line\nSecond line</p>\n")
                      && previewSoft == QStringLiteral("<p data-src-start=\"1\" data-src-end=\"2\"><span data-src-line=\"1\">First line</span><span data-src-line=\"1\">\n</span><span data-src-line=\"2\">Second line</span></p>\n"),
                  QStringLiteral("disabled single-line-break rendering is not CommonMark soft-break output"));
    const QString exportAndPrintHtml = ExportManager::buildFullHtml(
        hard, false, FastMdDefaults::PreviewFontSize, true, false, QStringLiteral("Soft break"));
    ok &= require(exportAndPrintHtml.contains(QStringLiteral("First line<br>\nSecond line")),
                  QStringLiteral("HTML/PDF/print wrapper lost the rendered hard break"));
    ok &= require(ExportManager::markdownToHtml(QStringLiteral("First line\r\nSecond line"),
                      MathRenderMode::KatexOutput, nullptr, true) == hard,
                  QStringLiteral("CRLF and LF soft breaks render differently"));

    const QString explicitBreaks = ExportManager::markdownToHtml(
        QStringLiteral("Two spaces  \nNext\nExplicit<br>\nHTML"),
        MathRenderMode::KatexOutput, nullptr, true);
    ok &= require(explicitBreaks.count(QStringLiteral("<br>")) >= 3,
                  QStringLiteral("explicit Markdown/HTML breaks were not preserved alongside soft breaks"));

    const QString coverage = QStringLiteral(
        "Paragraph one\nParagraph two\n\n\nParagraph three\n\n"
        "# Heading\n\n- outer\n  - nested\n\n> quote one\n> quote two\n\n"
        "| A | B |\n| - | - |\n| one | two |\n\n"
        "```cpp\na\nb\n```\n\n    indented\n    code\n\n"
        "Inline $a+b$\n\n$$\na\\\\b\n$$\n\n"
        "```mermaid\ngraph TD\nA --> B\n```\n\n"
        "> [!NOTE]\n> callout one\n> callout two\n\n- [ ] task one\n- [x] task two\n\n"
        "Term\n: definition one\n  definition two\n\nFootnote[^a]\n\n[^a]: note one\n    note two\n\n"
        "<div>\nraw one\nraw two\n</div>");
    const QString coverageHard = ExportManager::markdownToPreviewHtml(coverage, nullptr, true);
    const QString coverageSoft = ExportManager::markdownToPreviewHtml(coverage, nullptr, false);
    const bool extensionCoverage = coverageHard.contains(QStringLiteral("<h1"))
        && coverageHard.contains(QStringLiteral("<ul"))
        && coverageHard.contains(QStringLiteral("<blockquote"))
        && coverageHard.contains(QStringLiteral("<table"))
        && coverageHard.contains(QStringLiteral("<code class=\"language-cpp\""))
        && coverageHard.contains(QStringLiteral("language-mermaid"))
        && coverageHard.contains(QStringLiteral("fastmd-callout-note"))
        && coverageHard.contains(QStringLiteral("data-fastmd-task-start"))
        && coverageHard.contains(QStringLiteral("<dl data-src-start=\"45\">"))
        && coverageHard.contains(QStringLiteral("id=\"fn-a\""));
    ok &= require(extensionCoverage,
                  QStringLiteral("soft-break coverage fixture lost a Markdown extension"));
    ok &= require(coverageHard.contains(QStringLiteral("<span data-src-line=\"12\">quote one</span><br data-src-line=\"12\">\n<span data-src-line=\"13\">quote two</span>"))
                      && coverageSoft.contains(QStringLiteral("<span data-src-line=\"12\">quote one</span><span data-src-line=\"12\">\n</span><span data-src-line=\"13\">quote two</span>"))
                      && !coverageSoft.contains(QStringLiteral("<br")),
                  QStringLiteral("soft-break option did not stay scoped to paragraph text"));
    QStringList mathSnippets;
    const QString mathPreprocessed = FastMdMath::preprocess(
        QStringLiteral("$$\na\\\\b\n$$"), MathRenderMode::KatexOutput, &mathSnippets);
    QStringList inlineMathSnippets;
    const QString inlineMathPreprocessed = FastMdMath::preprocess(
        QStringLiteral("$a\nb$"), MathRenderMode::KatexOutput, &inlineMathSnippets);
    const bool mathPreserved = mathPreprocessed.startsWith(QStringLiteral("@@FASTMD_MATH_0@@"))
        && mathSnippets.value(0).contains(QStringLiteral("a\\\\b"))
        && inlineMathPreprocessed == QStringLiteral("@@FASTMD_MATH_0@@")
        && inlineMathSnippets.value(0).contains(QStringLiteral("a\nb"));
    ok &= require(mathPreserved,
                  QStringLiteral("line breaks inside math were changed by Markdown rendering"));

    const QString formatSource = QStringLiteral(
        "First line\nSecond line  \n\n\nThird line\n\n```\ncode  \nnext\n```\n");
    const QString once = TextTransforms::formatMarkdown(formatSource);
    const QString twice = TextTransforms::formatMarkdown(once);
    ok &= require(once.contains(QStringLiteral("First line\nSecond line  "))
                      && once.contains(QStringLiteral("code  \nnext"))
                      && once == twice,
                  QStringLiteral("Auto Format Document changed meaningful breaks or was not idempotent"));
    const QString selection = TextTransforms::formatMarkdown(
        QStringLiteral("First line\nSecond line  \n\n#      Heading\n"), 0, 1);
    ok &= require(selection.startsWith(QStringLiteral("First line\nSecond line  "))
                      && selection.endsWith(QStringLiteral("#      Heading\n")),
                  QStringLiteral("Format Selection changed a meaningful break or text outside its range"));

    struct BreakCase { const char* name; const char* markdown; const char* hard; const char* soft; };
    const BreakCase cases[] = {
        {"paragraph EOF", "a\nb", "<p>a<br>\nb</p>\n", "<p>a\nb</p>\n"},
        {"CRLF", "a\r\nb\r\n", "<p>a<br>\nb</p>\n", "<p>a\nb</p>\n"},
        {"blank boundaries", "\na\n\nb\n", "<p>a</p>\n<p>b</p>\n", "<p>a</p>\n<p>b</p>\n"},
        {"two spaces", "a  \nb", "<p>a<br>\nb</p>\n", "<p>a<br>\nb</p>\n"},
        {"backslash", "a\\\nb", "<p>a<br>\nb</p>\n", "<p>a<br>\nb</p>\n"},
        {"inline HTML", "a<br>\nb", "<p>a<br><br>\nb</p>\n", "<p>a<br>\nb</p>\n"},
        {"authored span", "<span>a</span>\nb", "<p><span>a</span><br>\nb</p>\n", "<p><span>a</span>\nb</p>\n"},
        {"emphasis across break", "*a\nb*", "<p><em>a<br>\nb</em></p>\n", "<p><em>a\nb</em></p>\n"},
        {"quote", "> a\n> b", "<blockquote>\n<p>a<br>\nb</p>\n</blockquote>\n", "<blockquote>\n<p>a\nb</p>\n</blockquote>\n"},
        {"list continuation", "- a\n  b", "<ul>\n<li>a<br>\nb</li>\n</ul>\n", "<ul>\n<li>a\nb</li>\n</ul>\n"},
        {"heading boundary", "a\n# b", "<p>a</p>\n<h1 id=\"b\">b</h1>\n", "<p>a</p>\n<h1 id=\"b\">b</h1>\n"},
        {"code span", "`a\nb`", "<p><code>a b</code></p>\n", "<p><code>a b</code></p>\n"},
        {"plain fence", "```\na\nb\n```", "<pre><code>a\nb\n</code></pre>\n", "<pre><code>a\nb\n</code></pre>\n"},
        {"indented code", "    a\n    b", "<pre><code>a\nb\n</code></pre>\n", "<pre><code>a\nb\n</code></pre>\n"},
    };
    QJsonArray browserCases;
    for (const auto& c : cases) {
        for (bool enabled : {false, true}) {
            const QString source = QString::fromUtf8(c.markdown);
            const QString expected = QString::fromUtf8(enabled ? c.hard : c.soft);
            const QString label = QString::fromUtf8(c.name) + (enabled ? " enabled" : " disabled");
            const QString body = ExportManager::markdownToPreviewHtml(source, nullptr, enabled);
            ok &= require(ExportManager::markdownToHtml(source, MathRenderMode::KatexOutput, nullptr, enabled) == expected,
                          label + QStringLiteral(": export differs from semantic expectation"));
            ok &= require(withoutSourceMapping(body) == expected,
                          label + QStringLiteral(": preview differs from semantic expectation: ") + body);
            browserCases.append(QJsonObject{{"name", label}, {"html", body}, {"expected", expected}});
        }
    }
    for (bool enabled : {false, true}) {
        const QString raw = QStringLiteral("<div>\nraw one\nraw two\n</div>");
        const QString exported = ExportManager::markdownToHtml(raw, MathRenderMode::KatexOutput, nullptr, enabled);
        const QString previewed = ExportManager::markdownToPreviewHtml(raw, nullptr, enabled);
        ok &= require(exported == raw + QLatin1Char('\n')
                          && previewed.contains(raw) && !previewed.contains(QStringLiteral("<br")),
                      QStringLiteral("soft-break setting changed raw HTML block content"));
    }
    // Optional fixture output for the real browser harness; expectations above
    // are independent of both the preview renderer and the browser DOM.
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--emit")) {
        QFile out(QString::fromLocal8Bit(argv[2]));
        if (!out.open(QIODevice::WriteOnly)) return 1;
        out.write("window.__SOFT_BREAK_CASES = ");
        out.write(QJsonDocument(browserCases).toJson(QJsonDocument::Compact));
        out.write(";\n");
    }
    return ok ? 0 : 1;
}
