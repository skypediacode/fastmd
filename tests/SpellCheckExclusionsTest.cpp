#include "SpellCheckExclusions.h"

#include <QCoreApplication>
#include <QStringList>

#include <cstdio>

using namespace SpellCheckExclusions;

static bool require(bool value, const char* message)
{
    if (!value)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return value;
}

// The prose fragments a line yields, joined with '|' so an expectation reads
// as the text that would actually reach the spell checker.
static QString proseOf(const QString& line, LineState state = {})
{
    QStringList parts;
    for (const Span& span : proseSpans(line, state))
        parts.append(line.mid(span.start, span.length));
    return parts.join(QLatin1Char('|'));
}

static bool check(const char* what, const QString& line, const QString& expected,
                  LineState state = {})
{
    const QString actual = proseOf(line, state);
    if (actual == expected)
        return true;
    std::fprintf(stderr, "FAIL: %s\n  line:     %s\n  expected: %s\n  actual:   %s\n",
                 what, qPrintable(line), qPrintable(expected), qPrintable(actual));
    return false;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    // --- plain prose is passed through untouched --------------------------
    ok &= check("plain prose", QStringLiteral("The quick brown fox."),
                QStringLiteral("The quick brown fox."));
    ok &= check("heading text is prose", QStringLiteral("## A heading here"),
                QStringLiteral("## A heading here"));
    ok &= check("blockquote text is prose", QStringLiteral("> quoted prose"),
                QStringLiteral("> quoted prose"));

    // --- fenced code blocks -----------------------------------------------
    {
        LineState state;
        state = advance(QStringLiteral("```cpp"), state, 0);
        ok &= require(state.inFence, "fence opens");
        ok &= check("fence body excluded", QStringLiteral("int mispeled = 0;"),
                    QString(), state);
        state = advance(QStringLiteral("int mispeled = 0;"), state, 1);
        ok &= require(state.inFence, "fence stays open over body");
        state = advance(QStringLiteral("```"), state, 2);
        ok &= require(!state.inFence, "fence closes");
    }
    ok &= check("fence opening line excluded", QStringLiteral("```cpp"), QString());
    ok &= check("tilde fence line excluded", QStringLiteral("~~~"), QString());
    {
        LineState state;
        state = advance(QStringLiteral("```mermaid"), state, 0);
        ok &= check("mermaid body excluded", QStringLiteral("graph TD; A-->B;"),
                    QString(), state);
    }

    // --- indented code ------------------------------------------------------
    ok &= check("indented code excluded", QStringLiteral("    int x = teh;"), QString());
    ok &= check("three spaces is still prose", QStringLiteral("   still prose"),
                QStringLiteral("still prose"));

    // --- inline code --------------------------------------------------------
    ok &= check("inline code excluded", QStringLiteral("Call `mispeled()` now."),
                QStringLiteral("Call|now."));
    ok &= check("double backtick span", QStringLiteral("a ``x ` y`` b"),
                QStringLiteral("a|b"));
    ok &= check("unterminated backtick eats the rest",
                QStringLiteral("start `unclosed code"), QStringLiteral("start"));

    // --- URLs and autolinks --------------------------------------------------
    ok &= check("bare url excluded", QStringLiteral("See https://exmple.com/teh for more."),
                QStringLiteral("See|for more."));
    ok &= check("www url excluded", QStringLiteral("go www.exmple.com now"),
                QStringLiteral("go|now"));
    ok &= check("autolink excluded", QStringLiteral("mail <https://a.exmple.com> ok"),
                QStringLiteral("mail|ok"));

    // --- links: text stays prose, destination does not ----------------------
    ok &= check("link text kept, destination dropped",
                QStringLiteral("A [visible label](http://exmple.com/teh) end"),
                QStringLiteral("A [visible label|end"));
    ok &= check("image alt kept, source dropped",
                QStringLiteral("![alt words](img/teh.png)"),
                QStringLiteral("![alt words"));
    ok &= check("reference definition excluded",
                QStringLiteral("[label]: http://exmple.com/teh \"Title\""), QString());
    ok &= check("footnote label excluded", QStringLiteral("text[^mispeledref] after"),
                QStringLiteral("text|after"));

    // --- math ----------------------------------------------------------------
    ok &= check("inline math excluded", QStringLiteral("Given $x = ymc^2$ we see."),
                QStringLiteral("Given|we see."));
    ok &= check("paren math excluded", QStringLiteral(R"(Given \(a+b\) done)"),
                QStringLiteral("Given|done"));
    ok &= check("bracket math excluded", QStringLiteral(R"(pre \[a+b\] post)"),
                QStringLiteral("pre|post"));
    ok &= check("latex command excluded", QStringLiteral(R"(use \alpha here)"),
                QStringLiteral("use|here"));
    ok &= check("lone dollar stays prose", QStringLiteral("costs 5 dollars and change"),
                QStringLiteral("costs 5 dollars and change"));
    ok &= check("unmatched dollar does not eat the line",
                QStringLiteral("costs $5 and more prose"),
                QStringLiteral("costs|5 and more prose"));
    {
        LineState state;
        state = advance(QStringLiteral("$$"), state, 0);
        ok &= require(state.inMathBlock, "display math opens");
        ok &= check("display math body excluded", QStringLiteral("E = mc^2"), QString(), state);
        state = advance(QStringLiteral("$$"), state, 2);
        ok &= require(!state.inMathBlock, "display math closes");
    }

    // --- HTML ----------------------------------------------------------------
    ok &= check("html tags excluded, content kept",
                QStringLiteral("<div class=\"someclas\">real words</div>"),
                QStringLiteral("real words"));
    ok &= check("self closing tag excluded", QStringLiteral("before <br/> after"),
                QStringLiteral("before|after"));

    // --- emoji shortcodes ------------------------------------------------------
    ok &= check("emoji shortcode excluded", QStringLiteral("nice :thumbsup: work"),
                QStringLiteral("nice|work"));
    ok &= check("clock time is not a shortcode", QStringLiteral("at 10:30:45 today"),
                QStringLiteral("at 10:30:45 today"));

    // --- front matter ----------------------------------------------------------
    {
        LineState state;
        state = advance(QStringLiteral("---"), state, 0);
        ok &= require(state.inFrontMatter, "front matter opens on line 1");
        ok &= check("front matter body excluded", QStringLiteral("title: Teh Documnet"),
                    QString(), state);
        state = advance(QStringLiteral("title: Teh Documnet"), state, 1);
        state = advance(QStringLiteral("---"), state, 2);
        ok &= require(!state.inFrontMatter, "front matter closes");
    }
    {
        // A --- that is not on line 1 is a thematic break, not front matter.
        LineState state;
        state = advance(QStringLiteral("prose"), state, 0);
        state = advance(QStringLiteral("---"), state, 1);
        ok &= require(!state.inFrontMatter, "mid-document --- is not front matter");
    }

    // --- escapes -----------------------------------------------------------------
    ok &= check("escaped punctuation consumed", QStringLiteral(R"(a \* b)"),
                QStringLiteral("a|b"));

    // --- plain text documents get no masking -------------------------------------
    {
        const QString line = QStringLiteral("  a `b` https://c.d  ");
        QStringList parts;
        for (const Span& span : plainTextSpans(line))
            parts.append(line.mid(span.start, span.length));
        ok &= require(parts.join(QLatin1Char('|')) == QStringLiteral("a `b` https://c.d"),
                      "plain text spans are the trimmed line");
    }

    if (ok)
        std::printf("SpellCheckExclusionsTest: all checks passed\n");
    return ok ? 0 : 1;
}
