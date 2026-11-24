#include "TextTransforms.h"

#include <QCoreApplication>
#include <QDebug>

namespace {
bool check(const QString& name, const QString& actual, const QString& expected)
{
    if (actual == expected)
        return true;
    qCritical().noquote() << name << "\nExpected:\n" << expected << "\nActual:\n" << actual;
    return false;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    // Sort Lines
    ok &= check(QStringLiteral("sort: basic ascending"),
                TextTransforms::sortLines(QStringLiteral("banana\napple\ncherry\n")),
                QStringLiteral("apple\nbanana\ncherry\n"));
    ok &= check(QStringLiteral("sort: no trailing newline preserved"),
                TextTransforms::sortLines(QStringLiteral("b\na")),
                QStringLiteral("a\nb"));
    ok &= check(QStringLiteral("sort: duplicate empty lines stay in place, stable"),
                TextTransforms::sortLines(QStringLiteral("b\n\na\n\n")),
                QStringLiteral("\n\na\nb\n"));
    ok &= check(QStringLiteral("sort: unicode code-unit order"),
                TextTransforms::sortLines(QStringLiteral("éclair\napple\n")),
                QStringLiteral("apple\néclair\n"));
    ok &= check(QStringLiteral("sort: empty document"),
                TextTransforms::sortLines(QStringLiteral("")),
                QStringLiteral(""));

    // Reverse Lines
    ok &= check(QStringLiteral("reverse: basic"),
                TextTransforms::reverseLines(QStringLiteral("one\ntwo\nthree\n")),
                QStringLiteral("three\ntwo\none\n"));
    ok &= check(QStringLiteral("reverse: no trailing newline preserved"),
                TextTransforms::reverseLines(QStringLiteral("one\ntwo")),
                QStringLiteral("two\none"));
    ok &= check(QStringLiteral("reverse: single line unchanged"),
                TextTransforms::reverseLines(QStringLiteral("only")),
                QStringLiteral("only"));

    // Remove Duplicate Lines
    ok &= check(QStringLiteral("dedupe: preserves first occurrence and order"),
                TextTransforms::removeDuplicateLines(QStringLiteral("b\na\nb\na\nc\n")),
                QStringLiteral("b\na\nc\n"));
    ok &= check(QStringLiteral("dedupe: duplicate empty lines collapse to one"),
                TextTransforms::removeDuplicateLines(QStringLiteral("a\n\n\nb\n")),
                QStringLiteral("a\n\nb\n"));
    ok &= check(QStringLiteral("dedupe: no trailing newline preserved"),
                TextTransforms::removeDuplicateLines(QStringLiteral("a\na")),
                QStringLiteral("a"));

    // Unique + Sort
    ok &= check(QStringLiteral("unique+sort: dedupes then sorts"),
                TextTransforms::uniqueSortLines(QStringLiteral("b\na\nb\nc\na\n")),
                QStringLiteral("a\nb\nc\n"));

    // Join Lines
    ok &= check(QStringLiteral("join: basic, single space separator"),
                TextTransforms::joinLines(QStringLiteral("one\ntwo\nthree\n")),
                QStringLiteral("one two three\n"));
    ok &= check(QStringLiteral("join: trims each line and skips blank lines"),
                TextTransforms::joinLines(QStringLiteral("  one  \n\n  two  \n")),
                QStringLiteral("one two\n"));
    ok &= check(QStringLiteral("join: no trailing newline preserved"),
                TextTransforms::joinLines(QStringLiteral("a\nb")),
                QStringLiteral("a b"));
    ok &= check(QStringLiteral("join: all-blank selection collapses to an empty line"),
                TextTransforms::joinLines(QStringLiteral("\n\n")),
                QStringLiteral("\n"));

    // Split Lines
    ok &= check(QStringLiteral("split: comma delimiter, trims segments"),
                TextTransforms::splitLines(QStringLiteral("a, b ,c\n"), QStringLiteral(",")),
                QStringLiteral("a\nb\nc\n"));
    ok &= check(QStringLiteral("split: line without delimiter is left unchanged"),
                TextTransforms::splitLines(QStringLiteral("no-delimiter-here\n"), QStringLiteral(",")),
                QStringLiteral("no-delimiter-here\n"));
    ok &= check(QStringLiteral("split: tab delimiter"),
                TextTransforms::splitLines(QStringLiteral("a\tb\tc"), QStringLiteral("\t")),
                QStringLiteral("a\nb\nc"));
    ok &= check(QStringLiteral("split: custom multi-character delimiter"),
                TextTransforms::splitLines(QStringLiteral("a :: b :: c"), QStringLiteral("::")),
                QStringLiteral("a\nb\nc"));
    ok &= check(QStringLiteral("split: mixed lines, only matching ones split"),
                TextTransforms::splitLines(QStringLiteral("a,b\nplain\nc,d\n"), QStringLiteral(",")),
                QStringLiteral("a\nb\nplain\nc\nd\n"));

    // Normalize Whitespace
    ok &= check(QStringLiteral("normalize: collapses interior runs, trims ends"),
                TextTransforms::normalizeWhitespace(QStringLiteral("  a\t\t b   c  \n")),
                QStringLiteral("a b c\n"));
    ok &= check(QStringLiteral("normalize: NBSP counts as whitespace"),
                TextTransforms::normalizeWhitespace(QString(QStringLiteral("a")) + QChar(0x00A0) + QChar(0x00A0) + QStringLiteral("b\n")),
                QStringLiteral("a b\n"));
    ok &= check(QStringLiteral("normalize: all-whitespace line becomes empty"),
                TextTransforms::normalizeWhitespace(QStringLiteral("   \n")),
                QStringLiteral("\n"));

    // Tabs <-> Spaces (width 4)
    ok &= check(QStringLiteral("tabsToSpaces: leading tab to column stop"),
                TextTransforms::tabsToSpaces(QStringLiteral("\tcode\n"), 4),
                QStringLiteral("    code\n"));
    ok &= check(QStringLiteral("tabsToSpaces: mid-line tab aligns to next stop"),
                TextTransforms::tabsToSpaces(QStringLiteral("ab\tc\n"), 4),
                QStringLiteral("ab  c\n"));
    ok &= check(QStringLiteral("spacesToTabs: leading 4 spaces become one tab"),
                TextTransforms::spacesToTabs(QStringLiteral("    code\n"), 4),
                QStringLiteral("\tcode\n"));
    ok &= check(QStringLiteral("spacesToTabs: leading 6 spaces become tab + 2 spaces"),
                TextTransforms::spacesToTabs(QStringLiteral("      code\n"), 4),
                QStringLiteral("\t  code\n"));
    ok &= check(QStringLiteral("spacesToTabs: mid-line spaces untouched"),
                TextTransforms::spacesToTabs(QStringLiteral("a    b\n"), 4),
                QStringLiteral("a    b\n"));
    ok &= check(QStringLiteral("spacesToTabs: fewer than one stop's worth left alone"),
                TextTransforms::spacesToTabs(QStringLiteral("  code\n"), 4),
                QStringLiteral("  code\n"));
    ok &= check(QStringLiteral("tabs/spaces round trip idempotency"),
                TextTransforms::tabsToSpaces(TextTransforms::tabsToSpaces(QStringLiteral("\t\ta\n"), 4), 4),
                TextTransforms::tabsToSpaces(QStringLiteral("\t\ta\n"), 4));

    // Smart Quotes to Straight
    ok &= check(QStringLiteral("smart quotes: curly double and single quotes"),
                TextTransforms::smartQuotesToStraight(
                    QString(QChar(0x201C)) + QStringLiteral("hi") + QChar(0x201D) + QStringLiteral(" ") +
                    QChar(0x2018) + QStringLiteral("there") + QChar(0x2019)),
                QStringLiteral("\"hi\" 'there'"));
    ok &= check(QStringLiteral("smart quotes: plain text unaffected"),
                TextTransforms::smartQuotesToStraight(QStringLiteral("already \"straight\"")),
                QStringLiteral("already \"straight\""));

    // Remove Invisible Characters
    ok &= check(QStringLiteral("invisible: strips zero-width space"),
                TextTransforms::removeInvisibleCharacters(QStringLiteral("a") + QChar(0x200B) + QStringLiteral("b")),
                QStringLiteral("ab"));
    ok &= check(QStringLiteral("invisible: strips BOM mid-text"),
                TextTransforms::removeInvisibleCharacters(QStringLiteral("a") + QChar(0xFEFF) + QStringLiteral("b")),
                QStringLiteral("ab"));
    ok &= check(QStringLiteral("invisible: keeps newline and tab"),
                TextTransforms::removeInvisibleCharacters(QStringLiteral("a\tb\nc")),
                QStringLiteral("a\tb\nc"));
    ok &= check(QStringLiteral("invisible: strips stray control character"),
                TextTransforms::removeInvisibleCharacters(QStringLiteral("a") + QChar(0x0001) + QStringLiteral("b")),
                QStringLiteral("ab"));

    // Search-in-Files seed from an editor selection
    ok &= check(QStringLiteral("seed: single line kept"),
                TextTransforms::singleLineSearchSeed(QStringLiteral("needle")),
                QStringLiteral("needle"));
    ok &= check(QStringLiteral("seed: no selection is empty"),
                TextTransforms::singleLineSearchSeed(QString()),
                QString());
    ok &= check(QStringLiteral("seed: newline selection is a scope, not a term"),
                TextTransforms::singleLineSearchSeed(QStringLiteral("a\nb")),
                QString());
    ok &= check(QStringLiteral("seed: QTextCursor block break is multi-line"),
                TextTransforms::singleLineSearchSeed(QStringLiteral("a") + QChar(QChar::ParagraphSeparator) + QStringLiteral("b")),
                QString());
    ok &= check(QStringLiteral("seed: spaces inside a line are kept"),
                TextTransforms::singleLineSearchSeed(QStringLiteral("  two words ")),
                QStringLiteral("  two words "));

    return ok ? 0 : 1;
}
