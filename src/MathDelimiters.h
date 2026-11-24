#pragma once

#include <QStringView>

// Flanking rules for inline $...$ (Pandoc's tex_math_dollars plus the two
// opening-side cases noted below), so a '$' in prose stays text.
namespace FastMdMath {

inline bool dollarEscaped(QStringView text, qsizetype pos)
{
    return pos > 0 && text.at(pos - 1) == QLatin1Char('\\');
}

inline bool inlineDollarCanOpen(QStringView text, qsizetype pos)
{
    return pos + 1 < text.size() && !text.at(pos + 1).isSpace() && !dollarEscaped(text, pos);
}

inline bool inlineDollarCanClose(QStringView text, qsizetype pos)
{
    const QChar before = text.at(pos - 1);
    if (before.isSpace())
        return false;
    if (pos + 1 >= text.size())
        return true;
    const QChar after = text.at(pos + 1);
    if (after.isDigit())
        return false;
    // "($x" and "**$x" open a new expression; they do not end an earlier one.
    const bool opensGroup = before == QLatin1Char('(') || before == QLatin1Char('[')
        || before == QLatin1Char('{')
        || (pos > 1 && before == QLatin1Char('*') && text.at(pos - 2) == QLatin1Char('*'));
    return !(opensGroup && (after.isLetter() || after == QLatin1Char('\\')));
}

// The next unescaped '$' outside code must be able to close, or the opener is
// literal. Searching past it would swallow later text (and later math) instead.
inline qsizetype findInlineDollarClose(QStringView text, qsizetype open, qsizetype limit)
{
    if (!inlineDollarCanOpen(text, open))
        return -1;
    bool codeSpan = false;
    for (qsizetype j = open + 1; j < limit; ++j) {
        const QChar ch = text.at(j);
        if (ch == QLatin1Char('`'))
            codeSpan = !codeSpan;
        else if (!codeSpan && ch == QLatin1Char('$') && !dollarEscaped(text, j))
            return j > open + 1 && inlineDollarCanClose(text, j) ? j : -1;
    }
    return -1;
}

} // namespace FastMdMath
