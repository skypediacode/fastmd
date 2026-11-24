#include "TextTransforms.h"

#include <QCoreApplication>
#include <QDebug>

namespace {
bool checkCase(const QString& name, const QString& source, const QString& expected)
{
    const QString actual = TextTransforms::formatMarkdown(source);
    if (actual == expected)
        return true;
    qCritical().noquote() << name << "\n--- expected ---\n" << expected
                          << "\n--- actual ---\n" << actual;
    return false;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    ok &= checkCase(QStringLiteral("padded table is normalized"),
        QStringLiteral(
            "| Col 1     | Col 2     | Col 3     |\n"
            "| --------- | --------- | --------- |\n"
            "| đâf      | dafsdaf      | dafasf      |\n"
            "| adfa      | adfasf      | Cell      |\n"
            "| afasf      | afasf      | adfasdf      |\n"),
        QStringLiteral(
            "| Col 1 | Col 2   | Col 3   |\n"
            "|-------|---------|---------|\n"
            "| đâf   | dafsdaf | dafasf  |\n"
            "| adfa  | adfasf  | Cell    |\n"
            "| afasf | afasf   | adfasdf |\n"));

    ok &= checkCase(QStringLiteral("alignment markers are preserved and widened"),
        QStringLiteral(
            "| Left | Center | Right |\n"
            "| :--- | :---: | ---: |\n"
            "| a | bb | ccc |\n"),
        QStringLiteral(
            "| Left | Center | Right |\n"
            "|:-----|:------:|------:|\n"
            "| a    |   bb   |   ccc |\n"));

    ok &= checkCase(QStringLiteral("default alignment marker stays plain dashes"),
        QStringLiteral("| A | B |\n| - | --- |\n| xx | y |\n"),
        QStringLiteral("| A  | B |\n|----|---|\n| xx | y |\n"));

    ok &= checkCase(QStringLiteral("table without leading/trailing pipes keeps its style"),
        QStringLiteral(
            "Name | Value\n"
            "--- | ---\n"
            "alpha | 1\n"
            "b | 22\n"),
        QStringLiteral(
            "Name  | Value\n"
            "------|------\n"
            "alpha | 1\n"
            "b     | 22\n"));

    // Without a trailing pipe the last column must not gain trailing spaces --
    // two of them would become a Markdown hard break.
    ok &= checkCase(QStringLiteral("no trailing pipe means no trailing whitespace"),
        QStringLiteral("a | long value\n--- | ---\nbbbb | x\n"),
        QStringLiteral("a    | long value\n-----|-----------\nbbbb | x\n"));

    ok &= checkCase(QStringLiteral("escaped pipes stay inside their cell"),
        QStringLiteral("| A | B |\n| --- | --- |\n| x \\| y | z |\n"),
        QStringLiteral("| A      | B |\n|--------|---|\n| x \\| y | z |\n"));

    ok &= checkCase(QStringLiteral("pipes inside inline code do not split cells"),
        QStringLiteral("| Code | Note |\n| --- | --- |\n| `a | b` | ok |\n"),
        QStringLiteral("| Code    | Note |\n|---------|------|\n| `a | b` | ok   |\n"));

    ok &= checkCase(QStringLiteral("empty cells are padded, not filled"),
        QStringLiteral("| A | B | C |\n| --- | --- | --- |\n|  | x |  |\n"),
        QStringLiteral("| A | B | C |\n|---|---|---|\n|   | x |   |\n"));

    ok &= checkCase(QStringLiteral("short rows are padded to the delimiter column count"),
        QStringLiteral("| A | B | C |\n| --- | --- | --- |\n| 1 |\n| 1 | 2 | 3 |\n"),
        QStringLiteral("| A | B | C |\n|---|---|---|\n| 1 |   |   |\n| 1 | 2 | 3 |\n"));

    // Extra body cells must survive verbatim, but the delimiter row keeps its
    // own cell count -- it alone decides how many columns GFM renders.
    ok &= checkCase(QStringLiteral("extra body cells are kept without widening the delimiter"),
        QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 | extra |\n"),
        QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 | extra |\n"));

    ok &= checkCase(QStringLiteral("wide CJK text is measured at two columns"),
        QStringLiteral("| Name | Note |\n| --- | --- |\n| 漢字 | x |\n| abcd | y |\n"),
        QStringLiteral("| Name | Note |\n|------|------|\n| 漢字 | x    |\n| abcd | y    |\n"));

    ok &= checkCase(QStringLiteral("combining marks do not inflate the column width"),
        QStringLiteral("| A | B |\n| --- | --- |\n| éé | y |\n| abc | z |\n"),
        QStringLiteral("| A   | B |\n|-----|---|\n| éé  | y |\n| abc | z |\n"));

    ok &= checkCase(QStringLiteral("precomposed Unicode counts as one column"),
        QStringLiteral("| A | B |\n| --- | --- |\n| đâf | yy |\n"),
        QStringLiteral("| A   | B  |\n|-----|----|\n| đâf | yy |\n"));

    ok &= checkCase(QStringLiteral("indentation is preserved"),
        QStringLiteral("  | A | B |\n  | --- | --- |\n  | 1 | 22 |\n"),
        QStringLiteral("  | A | B  |\n  |---|----|\n  | 1 | 22 |\n"));

    ok &= checkCase(QStringLiteral("two tables are formatted independently"),
        QStringLiteral(
            "| A | B |\n| --- | --- |\n| 1 | 2 |\n"
            "\n"
            "text | between\n"
            "\n"
            "| Long header | X |\n| --- | --- |\n| 1 | 2 |\n"),
        QStringLiteral(
            "| A | B |\n|---|---|\n| 1 | 2 |\n"
            "\n"
            "text | between\n"
            "\n"
            "| Long header | X |\n|-------------|---|\n| 1           | 2 |\n"));

    // --- Malformed / non-table content must be left exactly as written ------
    const QString untouched = QStringLiteral(
        "A sentence with a | pipe in it.\n"
        "\n"
        "| header | only |\n"
        "\n"
        "| header | no delimiter |\n"
        "| body   | row          |\n"
        "\n"
        "| a | b |\n"
        "| --- |\n"
        "| 1 | 2 |\n"
        "\n"
        "| a | b |\n"
        "| --- | -x- |\n"
        "| 1 | 2 |\n"
        "\n"
        "Setext heading\n"
        "---\n"
        "\n"
        "> | a  | b |\n"
        "> | -- | - |\n"
        "> | 1  | 2 |\n"
        "\n"
        "```\n"
        "| a   | b |\n"
        "| --- | - |\n"
        "```\n"
        "\n"
        "    | a   | b |\n"
        "    | --- | - |\n");
    ok &= checkCase(QStringLiteral("malformed and non-table content is untouched"), untouched, untouched);

    // The table body ends at the first line that is not a table row.
    ok &= checkCase(QStringLiteral("table stops at the first non-row line"),
        QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |\nafter the table\n"),
        QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |\nafter the table\n"));

    // Format Selection must not rewrite a table that is only partly selected.
    const QString partial = QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 22 |\n");
    ok &= (TextTransforms::formatMarkdown(partial, 0, 1) == partial);
    if (TextTransforms::formatMarkdown(partial, 0, 1) != partial)
        qCritical().noquote() << "a partially selected table was rewritten";
    ok &= (TextTransforms::formatMarkdown(partial, 0, 2)
           == QStringLiteral("| A | B  |\n|---|----|\n| 1 | 22 |\n"));

    // Formatting is idempotent.
    const QString once = TextTransforms::formatMarkdown(QStringLiteral(
        "| Left | Center | Right |\n| :- | :-: | -: |\n| a | bb | ccc |\n"));
    ok &= checkCase(QStringLiteral("table formatting is idempotent"), once, once);

    ok &= checkCase(QStringLiteral("ordered list numbers are repaired"),
        QStringLiteral("1. first\n2. second\n3. third\n3. fourth\n4. fifth\n"),
        QStringLiteral("1. first\n2. second\n3. third\n4. fourth\n5. fifth\n"));

    ok &= checkCase(QStringLiteral("ordered list style and start are preserved"),
        QStringLiteral("  7) first\n  12) second\n  12) third\n\n5. new list\n9. next\n"),
        QStringLiteral("  7) first\n  8) second\n  9) third\n\n1. new list\n2. next\n"));

    return ok ? 0 : 1;
}
