#include "TextTransforms.h"

#include <QStringList>
#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace TextTransforms {

QString blockSeparatorsToNewline(QString text)
{
    text.replace(QChar(QChar::ParagraphSeparator), QLatin1Char('\n'));
    return text;
}

QString singleLineSearchSeed(const QString& selected)
{
    const QString text = blockSeparatorsToNewline(selected);
    if (text.isEmpty() || text.contains(QLatin1Char('\n')))
        return {};
    return text;
}

QString transformProperCase(const QString& text)
{
    QString result = text.toLower();
    bool atWordStart = true;
    for (int i = 0; i < result.length(); ++i) {
        const QChar ch = result.at(i);
        if (ch.isLetter()) {
            if (atWordStart) {
                result[i] = ch.toUpper();
                atWordStart = false;
            }
        } else {
            atWordStart = true;
        }
    }
    return result;
}

QString transformSentenceCase(const QString& text)
{
    QString result = text.toLower();
    bool capitalizeNext = true;
    for (int i = 0; i < result.length(); ++i) {
        const QChar ch = result.at(i);
        if (ch.isLetter()) {
            if (capitalizeNext) {
                result[i] = ch.toUpper();
                capitalizeNext = false;
            }
        } else if (ch == QLatin1Char('.') || ch == QLatin1Char('!') || ch == QLatin1Char('?')) {
            capitalizeNext = true;
        }
        // Any other character (spaces, quotes, digits, paragraph separators)
        // leaves capitalizeNext untouched, so it carries across intervening
        // punctuation/whitespace until the next letter is reached.
    }
    return result;
}

QString stripLeadingSpacesTabs(QString line)
{
    int i = 0;
    while (i < line.size() && (line.at(i) == QLatin1Char(' ') || line.at(i) == QLatin1Char('\t')))
        ++i;
    if (i > 0)
        line.remove(0, i);
    return line;
}

QString stripTrailingSpacesTabs(QString line)
{
    int end = line.size();
    while (end > 0 && (line.at(end - 1) == QLatin1Char(' ') || line.at(end - 1) == QLatin1Char('\t')))
        --end;
    line.truncate(end);
    return line;
}

} // namespace TextTransforms

namespace {

// Splits '\n'-separated text into lines, reporting separately whether the
// text ended with a trailing newline, so callers can reassemble it exactly
// (a trailing newline must never become an extra empty line to sort/dedupe).
QStringList splitLinesPreservingTrailingNewline(const QString& text, bool* hadTrailingNewline)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    *hadTrailingNewline = text.endsWith(QLatin1Char('\n'));
    if (*hadTrailingNewline && !lines.isEmpty())
        lines.removeLast();
    return lines;
}

QString joinLinesRestoringTrailingNewline(const QStringList& lines, bool hadTrailingNewline)
{
    QString result = lines.join(QLatin1Char('\n'));
    if (hadTrailingNewline)
        result += QLatin1Char('\n');
    return result;
}

// Collapses interior runs of Unicode whitespace to a single space and drops
// leading/trailing whitespace, in one linear scan.
QString normalizeWhitespaceLine(const QString& line)
{
    QString result;
    result.reserve(line.size());
    bool pendingSpace = false;
    for (const QChar ch : line) {
        if (ch.isSpace()) {
            pendingSpace = true;
            continue;
        }
        if (pendingSpace && !result.isEmpty())
            result.append(QLatin1Char(' '));
        pendingSpace = false;
        result.append(ch);
    }
    return result;
}

QString tabsToSpacesLine(const QString& line, int width)
{
    QString result;
    result.reserve(line.size());
    int column = 0;
    for (const QChar ch : line) {
        if (ch == QLatin1Char('\t')) {
            const int spaces = width - (column % width);
            result.append(QString(spaces, QLatin1Char(' ')));
            column += spaces;
        } else {
            result.append(ch);
            ++column;
        }
    }
    return result;
}

// Only leading indentation is retabbed -- converting spaces to tabs anywhere
// they occur would corrupt ordinary mid-line prose spacing.
QString spacesToTabsLine(const QString& line, int width)
{
    int leading = 0;
    while (leading < line.size() && line.at(leading) == QLatin1Char(' '))
        ++leading;
    if (leading < width)
        return line;
    const int tabCount = leading / width;
    const int remainder = leading % width;
    QString result;
    result.reserve(line.size());
    result.append(QString(tabCount, QLatin1Char('\t')));
    result.append(QString(remainder, QLatin1Char(' ')));
    result.append(line.mid(leading));
    return result;
}

} // namespace

namespace TextTransforms {

QString sortLines(const QString& text)
{
    bool hadTrailingNewline = false;
    QStringList lines = splitLinesPreservingTrailingNewline(text, &hadTrailingNewline);
    std::stable_sort(lines.begin(), lines.end());
    return joinLinesRestoringTrailingNewline(lines, hadTrailingNewline);
}

QString reverseLines(const QString& text)
{
    bool hadTrailingNewline = false;
    QStringList lines = splitLinesPreservingTrailingNewline(text, &hadTrailingNewline);
    std::reverse(lines.begin(), lines.end());
    return joinLinesRestoringTrailingNewline(lines, hadTrailingNewline);
}

QString removeDuplicateLines(const QString& text)
{
    bool hadTrailingNewline = false;
    const QStringList lines = splitLinesPreservingTrailingNewline(text, &hadTrailingNewline);
    QStringList kept;
    kept.reserve(lines.size());
    QSet<QString> seen;
    seen.reserve(lines.size());
    for (const QString& line : lines) {
        if (!seen.contains(line)) {
            seen.insert(line);
            kept.append(line);
        }
    }
    return joinLinesRestoringTrailingNewline(kept, hadTrailingNewline);
}

QString uniqueSortLines(const QString& text)
{
    return sortLines(removeDuplicateLines(text));
}

QString joinLines(const QString& text)
{
    bool hadTrailingNewline = false;
    const QStringList lines = splitLinesPreservingTrailingNewline(text, &hadTrailingNewline);
    QStringList parts;
    parts.reserve(lines.size());
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            parts.append(trimmed);
    }
    QString joined = parts.join(QLatin1Char(' '));
    if (hadTrailingNewline)
        joined += QLatin1Char('\n');
    return joined;
}

QString splitLines(const QString& text, const QString& delimiter)
{
    if (delimiter.isEmpty())
        return text;
    bool hadTrailingNewline = false;
    const QStringList lines = splitLinesPreservingTrailingNewline(text, &hadTrailingNewline);
    QStringList out;
    out.reserve(lines.size());
    for (const QString& line : lines) {
        if (line.contains(delimiter)) {
            const QStringList segments = line.split(delimiter);
            for (const QString& segment : segments)
                out.append(segment.trimmed());
        } else {
            out.append(line);
        }
    }
    return joinLinesRestoringTrailingNewline(out, hadTrailingNewline);
}

QString normalizeWhitespace(const QString& text)
{
    bool hadTrailingNewline = false;
    QStringList lines = splitLinesPreservingTrailingNewline(text, &hadTrailingNewline);
    for (QString& line : lines)
        line = normalizeWhitespaceLine(line);
    return joinLinesRestoringTrailingNewline(lines, hadTrailingNewline);
}

QString tabsToSpaces(const QString& text, int width)
{
    width = qMax(1, width);
    bool hadTrailingNewline = false;
    QStringList lines = splitLinesPreservingTrailingNewline(text, &hadTrailingNewline);
    for (QString& line : lines)
        line = tabsToSpacesLine(line, width);
    return joinLinesRestoringTrailingNewline(lines, hadTrailingNewline);
}

QString spacesToTabs(const QString& text, int width)
{
    width = qMax(1, width);
    bool hadTrailingNewline = false;
    QStringList lines = splitLinesPreservingTrailingNewline(text, &hadTrailingNewline);
    for (QString& line : lines)
        line = spacesToTabsLine(line, width);
    return joinLinesRestoringTrailingNewline(lines, hadTrailingNewline);
}

QString smartQuotesToStraight(const QString& text)
{
    QString result = text;
    for (int i = 0; i < result.size(); ++i) {
        switch (result.at(i).unicode()) {
        case 0x2018: case 0x2019: case 0x201A: case 0x201B:
            result[i] = QLatin1Char('\'');
            break;
        case 0x201C: case 0x201D: case 0x201E: case 0x201F:
            result[i] = QLatin1Char('"');
            break;
        default:
            break;
        }
    }
    return result;
}

QString removeInvisibleCharacters(const QString& text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text) {
        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\t')) {
            result.append(ch);
            continue;
        }
        const QChar::Category category = ch.category();
        if (category == QChar::Other_Format || category == QChar::Other_Control)
            continue;
        result.append(ch);
    }
    return result;
}

} // namespace TextTransforms

namespace {

// ---- Join Wrapped Lines helpers -------------------------------------------
//
// A lightweight, line-oriented Markdown block classifier. It is not a full
// CommonMark parser: it recognizes just enough structure (fenced/indented
// code, ATX/setext headings, horizontal rules, block quotes, list items,
// tables, HTML blocks) to decide which runs of consecutive lines are
// "flowing paragraph text" that soft-wrapped lines should be joined into,
// while leaving every other block byte-for-byte untouched.

bool jwlIsBlank(const QString& line)
{
    return line.trimmed().isEmpty();
}

bool jwlIsAtxHeading(const QString& line)
{
    int lead = 0;
    while (lead < line.size() && lead < 3 && line.at(lead) == QLatin1Char(' '))
        ++lead;
    int hashes = lead;
    while (hashes < line.size() && line.at(hashes) == QLatin1Char('#'))
        ++hashes;
    const int count = hashes - lead;
    if (count == 0 || count > 6)
        return false;
    if (hashes == line.size())
        return true;
    const QChar next = line.at(hashes);
    return next == QLatin1Char(' ') || next == QLatin1Char('\t');
}

bool jwlIsHorizontalRule(const QString& line)
{
    int lead = 0;
    while (lead < line.size() && lead < 3 && line.at(lead) == QLatin1Char(' '))
        ++lead;
    QChar ch;
    int count = 0;
    for (int i = lead; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char(' ') || c == QLatin1Char('\t'))
            continue;
        if (c != QLatin1Char('-') && c != QLatin1Char('*') && c != QLatin1Char('_'))
            return false;
        if (ch.isNull())
            ch = c;
        else if (c != ch)
            return false;
        ++count;
    }
    return count >= 3;
}

bool jwlIsSetextUnderline(const QString& line)
{
    const QString s = line.trimmed();
    if (s.isEmpty())
        return false;
    const QChar ch = s.at(0);
    if (ch != QLatin1Char('=') && ch != QLatin1Char('-'))
        return false;
    for (const QChar& c : s) {
        if (c != ch)
            return false;
    }
    return true;
}

struct JwlFenceInfo {
    QChar ch;
    int len = 0;
    int indent = 0;
};

JwlFenceInfo jwlDetectFence(const QString& line)
{
    int lead = 0;
    while (lead < line.size() && lead < 3 && line.at(lead) == QLatin1Char(' '))
        ++lead;
    if (lead >= line.size())
        return {};
    const QChar c = line.at(lead);
    if (c != QLatin1Char('`') && c != QLatin1Char('~'))
        return {};
    int i = lead;
    int len = 0;
    while (i < line.size() && line.at(i) == c) {
        ++len;
        ++i;
    }
    if (len < 3)
        return {};
    return JwlFenceInfo{c, len, lead};
}

bool jwlIsHtmlLine(const QString& line)
{
    int lead = 0;
    while (lead < line.size() && lead < 3 && line.at(lead) == QLatin1Char(' '))
        ++lead;
    return lead < line.size() && line.at(lead) == QLatin1Char('<');
}

bool jwlMatchBlockquotePrefix(const QString& line, QString& prefixOut, QString& remainderOut)
{
    int i = 0;
    int lead = 0;
    while (i < line.size() && lead < 3 && line.at(i) == QLatin1Char(' ')) {
        ++i;
        ++lead;
    }
    if (i >= line.size() || line.at(i) != QLatin1Char('>'))
        return false;
    const int start = i;
    ++i;
    if (i < line.size() && line.at(i) == QLatin1Char(' '))
        ++i;
    prefixOut = line.mid(start, i - start);
    remainderOut = line.mid(i);
    return true;
}

struct JwlListMarker {
    int contentCol = 0;
    QString content;
};

bool jwlMatchListMarker(const QString& line, JwlListMarker& out)
{
    int i = 0;
    while (i < line.size() && i < 3 && line.at(i) == QLatin1Char(' '))
        ++i;
    if (i >= line.size())
        return false;
    const QChar c = line.at(i);
    int markerLen = 0;
    if (c == QLatin1Char('-') || c == QLatin1Char('*') || c == QLatin1Char('+')) {
        markerLen = 1;
    } else if (c.isDigit()) {
        int j = i;
        while (j < line.size() && line.at(j).isDigit() && (j - i) < 9)
            ++j;
        if (j < line.size() && (line.at(j) == QLatin1Char('.') || line.at(j) == QLatin1Char(')')))
            markerLen = j - i + 1;
        else
            return false;
    } else {
        return false;
    }
    const int afterMarker = i + markerLen;
    if (afterMarker >= line.size())
        return false;
    if (line.at(afterMarker) != QLatin1Char(' ') && line.at(afterMarker) != QLatin1Char('\t'))
        return false;
    int j = afterMarker;
    while (j < line.size() && line.at(j) == QLatin1Char(' '))
        ++j;
    if (j == afterMarker)
        return false;
    out.contentCol = j;
    out.content = line.mid(j);
    return true;
}

bool jwlIsTableDelimiterRow(const QString& line)
{
    const QString s = line.trimmed();
    if (s.isEmpty() || !s.contains(QLatin1Char('-')))
        return false;
    static const QRegularExpression re(QStringLiteral(R"(^\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?$)"));
    return re.match(s).hasMatch();
}

QString jwlJoinTwoLines(const QString& left, const QString& right)
{
    QString l = left;
    while (l.endsWith(QLatin1Char(' ')) || l.endsWith(QLatin1Char('\t')))
        l.chop(1);
    int k = 0;
    while (k < right.size() && (right.at(k) == QLatin1Char(' ') || right.at(k) == QLatin1Char('\t')))
        ++k;
    const QString r = right.mid(k);
    if (l.isEmpty())
        return r;
    if (r.isEmpty())
        return l;
    return l + QLatin1Char(' ') + r;
}

// Is `line` the start of any block that should never be joined into a
// preceding paragraph/list-item/blockquote run?
bool jwlBreaksFlow(const QString& line)
{
    return jwlIsBlank(line) || jwlIsAtxHeading(line) || jwlIsHorizontalRule(line)
        || jwlIsSetextUnderline(line) || jwlDetectFence(line).len > 0;
}

} // namespace

QString TextTransforms::joinWrappedLinesText(const QString& text)
{
    const QStringList rawLines = text.split(QLatin1Char('\n'));
    const int n = rawLines.size();
    QStringList out;
    out.reserve(n);

    int i = 0;
    bool inFence = false;
    QChar fenceChar;
    int fenceLen = 0;

    while (i < n) {
        const QString& line = rawLines[i];

        if (inFence) {
            out.append(line);
            const JwlFenceInfo fi = jwlDetectFence(line);
            if (fi.len > 0 && fi.ch == fenceChar && fi.len >= fenceLen) {
                const QString rest = line.mid(fi.indent + fi.len).trimmed();
                if (rest.isEmpty())
                    inFence = false;
            }
            ++i;
            continue;
        }

        if (jwlIsBlank(line)) {
            out.append(line);
            ++i;
            continue;
        }

        const JwlFenceInfo fi = jwlDetectFence(line);
        if (fi.len > 0) {
            out.append(line);
            inFence = true;
            fenceChar = fi.ch;
            fenceLen = fi.len;
            ++i;
            continue;
        }

        if (jwlIsAtxHeading(line) || jwlIsHorizontalRule(line)) {
            out.append(line);
            ++i;
            continue;
        }

        // Indented code block: 4+ leading spaces (or a tab), only recognized
        // right after a blank line / start of the range (top-level only).
        {
            const bool tabIndent = line.startsWith(QLatin1Char('\t'));
            int lead = 0;
            while (lead < line.size() && line.at(lead) == QLatin1Char(' '))
                ++lead;
            const bool prevBlank = out.isEmpty() || jwlIsBlank(out.last());
            if ((lead >= 4 || tabIndent) && prevBlank) {
                while (i < n) {
                    const QString& cl = rawLines[i];
                    if (jwlIsBlank(cl)) {
                        out.append(cl);
                        ++i;
                        continue;
                    }
                    int clLead = 0;
                    while (clLead < cl.size() && cl.at(clLead) == QLatin1Char(' '))
                        ++clLead;
                    if (clLead >= 4 || cl.startsWith(QLatin1Char('\t'))) {
                        out.append(cl);
                        ++i;
                        continue;
                    }
                    break;
                }
                continue;
            }
        }

        if (jwlIsHtmlLine(line)) {
            while (i < n && !jwlIsBlank(rawLines[i])) {
                out.append(rawLines[i]);
                ++i;
            }
            continue;
        }

        // Table: a row containing '|' immediately followed by a delimiter row.
        if (i + 1 < n && line.contains(QLatin1Char('|')) && jwlIsTableDelimiterRow(rawLines[i + 1])) {
            out.append(line);
            out.append(rawLines[i + 1]);
            i += 2;
            while (i < n && !jwlIsBlank(rawLines[i]) && rawLines[i].contains(QLatin1Char('|'))) {
                out.append(rawLines[i]);
                ++i;
            }
            continue;
        }

        // Setext heading: plain text line immediately followed by an
        // underline, and the text line isn't itself a list/quote marker.
        if (i + 1 < n && jwlIsSetextUnderline(rawLines[i + 1])) {
            JwlListMarker lmTmp;
            QString bqPrefix, bqRemainder;
            const bool isListStart = jwlMatchListMarker(line, lmTmp);
            const bool isBqStart = jwlMatchBlockquotePrefix(line, bqPrefix, bqRemainder);
            if (!isListStart && !isBqStart) {
                out.append(line);
                out.append(rawLines[i + 1]);
                i += 2;
                continue;
            }
        }

        // Blockquote paragraph: join consecutive lines sharing the same
        // quote prefix (e.g. "> ") into one line.
        {
            QString prefix, remainder;
            if (jwlMatchBlockquotePrefix(line, prefix, remainder)) {
                QString joined = remainder;
                ++i;
                while (i < n) {
                    QString p2, r2;
                    if (!jwlMatchBlockquotePrefix(rawLines[i], p2, r2) || p2 != prefix)
                        break;
                    if (jwlBreaksFlow(r2))
                        break;
                    JwlListMarker lmTmp;
                    if (jwlMatchListMarker(r2, lmTmp))
                        break;
                    joined = jwlJoinTwoLines(joined, r2);
                    ++i;
                }
                out.append(prefix + joined);
                continue;
            }
        }

        // List item: join continuation lines indented under the marker's
        // content column into the item's paragraph text.
        {
            JwlListMarker lm;
            if (jwlMatchListMarker(line, lm)) {
                const QString prefix = line.left(lm.contentCol);
                QString joined = lm.content;
                ++i;
                while (i < n) {
                    const QString& cl = rawLines[i];
                    if (jwlBreaksFlow(cl))
                        break;
                    JwlListMarker lm2;
                    if (jwlMatchListMarker(cl, lm2))
                        break;
                    QString p2, r2;
                    if (jwlMatchBlockquotePrefix(cl, p2, r2))
                        break;
                    int lead = 0;
                    while (lead < cl.size() && cl.at(lead) == QLatin1Char(' '))
                        ++lead;
                    const int strip = qMin(lead, lm.contentCol);
                    joined = jwlJoinTwoLines(joined, cl.mid(strip));
                    ++i;
                }
                out.append(prefix + joined);
                continue;
            }
        }

        // Plain paragraph: join consecutive flowing text lines.
        {
            QString joined = line;
            ++i;
            while (i < n) {
                const QString& cl = rawLines[i];
                if (jwlBreaksFlow(cl))
                    break;
                JwlListMarker lm2;
                if (jwlMatchListMarker(cl, lm2))
                    break;
                QString p2, r2;
                if (jwlMatchBlockquotePrefix(cl, p2, r2))
                    break;
                int lead = 0;
                while (lead < cl.size() && cl.at(lead) == QLatin1Char(' '))
                    ++lead;
                if (lead >= 4)
                    break;
                joined = jwlJoinTwoLines(joined, cl);
                ++i;
            }
            out.append(joined);
            continue;
        }
    }

    return out.join(QLatin1Char('\n'));
}

namespace TextTransforms {

namespace {
bool fmtBlank(const QString& s) { return s.trimmed().isEmpty(); }

bool fmtIndentedCode(const QString& s)
{
    if (s.startsWith(QLatin1Char('\t'))) return true;
    int n = 0;
    while (n < s.size() && s.at(n) == QLatin1Char(' ')) ++n;
    if (n < 4) return false;
    if (n < s.size() && (s.at(n) == QLatin1Char('-') || s.at(n) == QLatin1Char('*')
                         || s.at(n) == QLatin1Char('+') || s.at(n) == QLatin1Char('>')))
        return false;
    int digit = n;
    while (digit < s.size() && s.at(digit).isDigit()) ++digit;
    if (digit > n && digit < s.size() && (s.at(digit) == QLatin1Char('.') || s.at(digit) == QLatin1Char(')')))
        return false;
    return true;
}

struct FmtFenceInfo {
    QChar character;
    int length = 0;
};

// Shared line-oriented fence classifier for the Markdown transforms: up to
// three leading spaces and a run of at least three matching markers.
FmtFenceInfo fmtFenceInfo(const QString& line)
{
    int lead = 0;
    while (lead < line.size() && lead < 3 && line.at(lead) == QLatin1Char(' '))
        ++lead;
    if (lead >= line.size()
        || (line.at(lead) != QLatin1Char('`') && line.at(lead) != QLatin1Char('~')))
        return {};
    const QChar character = line.at(lead);
    int end = lead;
    while (end < line.size() && line.at(end) == character)
        ++end;
    if (end - lead < 3)
        return {};
    return { character, end - lead };
}

bool fmtClosesFence(const QString& line, const FmtFenceInfo& opener)
{
    const FmtFenceInfo candidate = fmtFenceInfo(line);
    if (candidate.length < opener.length || candidate.character != opener.character)
        return false;
    int lead = 0;
    while (lead < line.size() && lead < 3 && line.at(lead) == QLatin1Char(' '))
        ++lead;
    return line.mid(lead + candidate.length).trimmed().isEmpty();
}

bool fmtInlineProtected(const QString& s, int pos)
{
    for (int i = 0; i < s.size();) {
        int end = -1;
        if (s.at(i) == QLatin1Char('`')) {
            int run = 1;
            while (i + run < s.size() && s.at(i + run) == QLatin1Char('`')) ++run;
            const int close = s.indexOf(QString(run, QLatin1Char('`')), i + run);
            if (close >= 0) end = close + run;
            else { ++i; continue; }
        } else if (s.at(i) == QLatin1Char('$')) {
            const int run = i + 1 < s.size() && s.at(i + 1) == QLatin1Char('$') ? 2 : 1;
            const int close = s.indexOf(QString(run, QLatin1Char('$')), i + run);
            if (close >= 0) end = close + run;
            else { ++i; continue; }
        } else if (s.mid(i, 2) == QStringLiteral("\\(") || s.mid(i, 2) == QStringLiteral("\\[")) {
            const QString closeToken = s.mid(i + 1, 1) == QStringLiteral("(")
                ? QStringLiteral("\\)") : QStringLiteral("\\]");
            const int close = s.indexOf(closeToken, i + 2);
            if (close >= 0) end = close + 2;
            else { ++i; continue; }
        }
        if (end >= 0) {
            if (pos >= i && pos < end) return true;
            i = end;
        } else {
            ++i;
        }
    }
    return false;
}

QString fmtLine(const QString& source)
{
    QString line = source;
    int trailing = line.size();
    while (trailing > 0 && (line.at(trailing - 1) == QLatin1Char(' ') || line.at(trailing - 1) == QLatin1Char('\t')))
        --trailing;
    const int count = line.size() - trailing;
    if (count > 0 && !fmtInlineProtected(line, trailing)) {
        if (count == 1) line.truncate(trailing);
        else line = line.left(trailing) + (count >= 2 ? QStringLiteral("  ") : QString());
    }

    int p = 0;
    while (p < line.size() && line.at(p) == QLatin1Char(' ')) ++p;
    const int prefix = p;
    if (p < line.size() && line.at(p) == QLatin1Char('#')) {
        int q = p;
        while (q < line.size() && line.at(q) == QLatin1Char('#')) ++q;
        const int hashEnd = q;
        if (q - p <= 6 && q < line.size() && (line.at(q) == QLatin1Char(' ') || line.at(q) == QLatin1Char('\t'))) {
            while (q < line.size() && (line.at(q) == QLatin1Char(' ') || line.at(q) == QLatin1Char('\t'))) ++q;
            if (!fmtInlineProtected(line, p)) line = line.left(p) + line.mid(p, hashEnd - p) + QLatin1Char(' ') + line.mid(q);
        }
    } else if (p < line.size() && line.at(p) == QLatin1Char('>')) {
        int q = p;
        while (q < line.size() && line.at(q) == QLatin1Char('>')) {
            ++q;
            while (q < line.size() && (line.at(q) == QLatin1Char(' ') || line.at(q) == QLatin1Char('\t'))) ++q;
        }
        if (!fmtInlineProtected(line, p) && q > p + 1) {
            QString rebuilt = line.left(prefix);
            for (int i = p; i < q;) {
                rebuilt += QLatin1Char('>'); ++i;
                while (i < q && (line.at(i) == QLatin1Char(' ') || line.at(i) == QLatin1Char('\t'))) ++i;
                rebuilt += QLatin1Char(' ');
            }
            rebuilt += fmtLine(line.mid(q));
            line = rebuilt;
        }
    } else {
        int q = p;
        while (q < line.size() && line.at(q).isDigit()) ++q;
        if (q > p && q < line.size() && (line.at(q) == QLatin1Char('.') || line.at(q) == QLatin1Char(')'))) ++q;
        if (q > p && q < line.size() && (line.at(q) == QLatin1Char(' ') || line.at(q) == QLatin1Char('\t'))) {
            while (q < line.size() && (line.at(q) == QLatin1Char(' ') || line.at(q) == QLatin1Char('\t'))) ++q;
            if (!fmtInlineProtected(line, p)) {
                QString rest = line.mid(q);
                if (rest.startsWith(QStringLiteral("[ ]")) || rest.startsWith(QStringLiteral("[x]")) || rest.startsWith(QStringLiteral("[X]"))) {
                    int r = 3;
                    while (r < rest.size() && (rest.at(r) == QLatin1Char(' ') || rest.at(r) == QLatin1Char('\t'))) ++r;
                    rest = rest.left(3) + QLatin1Char(' ') + rest.mid(r);
                }
                line = line.left(p) + line.mid(p, q - p).trimmed() + QLatin1Char(' ') + rest;
            }
        } else if (p < line.size() && (line.at(p) == QLatin1Char('-') || line.at(p) == QLatin1Char('*') || line.at(p) == QLatin1Char('+'))
                   && p + 1 < line.size() && (line.at(p + 1) == QLatin1Char(' ') || line.at(p + 1) == QLatin1Char('\t'))) {
            q = p + 1;
            while (q < line.size() && (line.at(q) == QLatin1Char(' ') || line.at(q) == QLatin1Char('\t'))) ++q;
            if (!fmtInlineProtected(line, p)) {
                QString rest = line.mid(q);
                if (rest.startsWith(QStringLiteral("[ ]")) || rest.startsWith(QStringLiteral("[x]")) || rest.startsWith(QStringLiteral("[X]"))) {
                    int r = 3;
                    while (r < rest.size() && (rest.at(r) == QLatin1Char(' ') || rest.at(r) == QLatin1Char('\t'))) ++r;
                    rest = rest.left(3) + QLatin1Char(' ') + rest.mid(r);
                }
                line = line.left(p + 1) + QLatin1Char(' ') + rest;
            }
        }
    }
    return line;
}

// Keep the first number chosen by the author for the document's first list,
// but restart later list runs at one. Markdown renderers use the first marker
// as the list start and ignore later marker values, so stale numbers after a
// blank line are especially easy to miss while editing.
bool fmtOrderedListInfo(const QString& line, int& indent, int& markerPosition,
                        QChar& delimiter, int& number)
{
    indent = 0;
    while (indent < line.size() && line.at(indent) == QLatin1Char(' ')) ++indent;
    int q = indent;
    while (q < line.size() && line.at(q).isDigit()) ++q;
    if (q == indent || q >= line.size()
        || (line.at(q) != QLatin1Char('.') && line.at(q) != QLatin1Char(')')))
        return false;

    markerPosition = q;
    const int markerEnd = q + 1;
    if (markerEnd >= line.size()
        || (line.at(markerEnd) != QLatin1Char(' ') && line.at(markerEnd) != QLatin1Char('\t')))
        return false;
    if (fmtInlineProtected(line, indent))
        return false;

    bool ok = false;
    number = line.mid(indent, q - indent).toInt(&ok);
    if (!ok)
        return false;
    delimiter = line.at(q);
    return true;
}

bool fmtNormalizeOrderedListNumber(QString& line, int expectedNumber)
{
    int indent = 0;
    int markerPosition = 0;
    QChar delimiter;
    int ignoredNumber = 0;
    if (!fmtOrderedListInfo(line, indent, markerPosition, delimiter, ignoredNumber))
        return false;

    const int contentStart = markerPosition + 1;
    int content = contentStart;
    while (content < line.size()
           && (line.at(content) == QLatin1Char(' ') || line.at(content) == QLatin1Char('\t'))) ++content;
    const QString number = QString::number(expectedNumber);
    line = line.left(indent) + number + delimiter + QLatin1Char(' ') + line.mid(content);
    return true;
}

bool fmtHeadingInfo(const QString& line, int& contentStart, int& level)
{
    int lead = 0;
    while (lead < line.size() && lead < 3 && line.at(lead) == QLatin1Char(' '))
        ++lead;
    int hashes = lead;
    while (hashes < line.size() && line.at(hashes) == QLatin1Char('#'))
        ++hashes;
    level = hashes - lead;
    if (level < 1 || level > 6 || hashes >= line.size() || !line.at(hashes).isSpace())
        return false;
    contentStart = hashes;
    while (contentStart < line.size() && line.at(contentStart).isSpace())
        ++contentStart;
    return true;
}

QString fmtStripHeadingNumber(QString title)
{
    int pos = 0;
    while (pos < title.size()) {
        const int begin = pos;
        while (pos < title.size() && title.at(pos).isDigit())
            ++pos;
        if (pos == begin)
            return title;
        if (pos < title.size() && title.at(pos) == QLatin1Char('.')) {
            ++pos;
            if (pos < title.size() && title.at(pos).isDigit())
                continue;
        }
        break;
    }
    if (pos >= title.size() || !title.at(pos).isSpace())
        return title;
    while (pos < title.size() && title.at(pos).isSpace())
        ++pos;
    return title.mid(pos);
}

// ---------------------------------------------------------------------------
// Markdown table prettifier
// ---------------------------------------------------------------------------

enum class FmtTableAlign { Default, Left, Center, Right };

struct FmtTableRow {
    QStringList cells;
    QString     lineEnd;
    bool        leadingPipe = false;
    bool        trailingPipe = false;
    bool        valid = false;
};

// Terminal columns occupied by one code point. Combining marks and format
// characters take none; the East Asian Wide/Fullwidth blocks take two.
int fmtCodePointWidth(char32_t cp)
{
    const QChar::Category cat = QChar::category(cp);
    if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_Enclosing
        || cat == QChar::Other_Format || cat == QChar::Other_Control)
        return 0;
    if ((cp >= 0x1100 && cp <= 0x115F)
        || (cp >= 0x2E80 && cp <= 0x303E)
        || (cp >= 0x3041 && cp <= 0x33FF)
        || (cp >= 0x3400 && cp <= 0x4DBF)
        || (cp >= 0x4E00 && cp <= 0x9FFF)
        || (cp >= 0xA000 && cp <= 0xA4CF)
        || (cp >= 0xAC00 && cp <= 0xD7A3)
        || (cp >= 0xF900 && cp <= 0xFAFF)
        || (cp >= 0xFE10 && cp <= 0xFE19)
        || (cp >= 0xFE30 && cp <= 0xFE6F)
        || (cp >= 0xFF00 && cp <= 0xFF60)
        || (cp >= 0xFFE0 && cp <= 0xFFE6)
        || (cp >= 0x1F300 && cp <= 0x1F64F)
        || (cp >= 0x1F680 && cp <= 0x1F6FF)
        || (cp >= 0x1F900 && cp <= 0x1F9FF)
        || (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}

int fmtCellWidth(const QString& s)
{
    int width = 0;
    for (int i = 0; i < s.size(); ++i) {
        char32_t cp = s.at(i).unicode();
        if (s.at(i).isHighSurrogate() && i + 1 < s.size() && s.at(i + 1).isLowSurrogate()) {
            cp = QChar::surrogateToUcs4(s.at(i), s.at(i + 1));
            ++i;
        }
        width += fmtCodePointWidth(cp);
    }
    return width;
}

// Splits one physical line into table cells. A pipe only separates cells when
// it is neither backslash-escaped nor inside an inline code span; one leading
// and one trailing delimiter pipe are dropped the way GFM drops them.
FmtTableRow fmtSplitTableRow(const QString& line)
{
    FmtTableRow row;
    QString body = line;
    while (body.endsWith(QLatin1Char('\r'))) {
        row.lineEnd.prepend(QLatin1Char('\r'));
        body.chop(1);
    }
    const QString t = body.trimmed();
    if (t.isEmpty())
        return row;

    QStringList cells;
    QString cur;
    bool sawDelimiter = false;
    int codeRun = 0;
    for (int i = 0; i < t.size(); ++i) {
        const QChar ch = t.at(i);
        if (codeRun == 0 && ch == QLatin1Char('\\') && i + 1 < t.size()) {
            cur += ch;
            cur += t.at(i + 1);
            ++i;
            continue;
        }
        if (ch == QLatin1Char('`')) {
            int run = 1;
            while (i + run < t.size() && t.at(i + run) == QLatin1Char('`')) ++run;
            if (codeRun == 0) {
                if (t.indexOf(QString(run, QLatin1Char('`')), i + run) >= 0) codeRun = run;
            } else if (run == codeRun) {
                codeRun = 0;
            }
            cur += QString(run, QLatin1Char('`'));
            i += run - 1;
            continue;
        }
        if (codeRun == 0 && ch == QLatin1Char('|')) {
            sawDelimiter = true;
            cells.append(cur);
            cur.clear();
            continue;
        }
        cur += ch;
    }
    if (!sawDelimiter)
        return row;

    const bool tailEmpty = cur.isEmpty();
    cells.append(cur);
    if (t.startsWith(QLatin1Char('|'))) {
        row.leadingPipe = true;
        cells.removeFirst();
    }
    if (!cells.isEmpty() && tailEmpty && t.endsWith(QLatin1Char('|'))) {
        row.trailingPipe = true;
        cells.removeLast();
    }
    if (cells.isEmpty())
        return row;

    for (QString& cell : cells)
        cell = cell.trimmed();
    row.cells = cells;
    row.valid = true;
    return row;
}

bool fmtDelimiterAlign(const QString& cell, FmtTableAlign& align)
{
    if (cell.isEmpty())
        return false;
    int first = 0;
    int last = cell.size() - 1;
    bool left = false, right = false;
    if (cell.at(first) == QLatin1Char(':')) { left = true; ++first; }
    if (last >= first && cell.at(last) == QLatin1Char(':')) { right = true; --last; }
    if (last < first)
        return false;
    for (int i = first; i <= last; ++i) {
        if (cell.at(i) != QLatin1Char('-'))
            return false;
    }
    align = left && right ? FmtTableAlign::Center
          : right         ? FmtTableAlign::Right
          : left          ? FmtTableAlign::Left
                          : FmtTableAlign::Default;
    return true;
}

bool fmtParseDelimiterRow(const FmtTableRow& row, QList<FmtTableAlign>& aligns)
{
    if (!row.valid || row.cells.isEmpty())
        return false;
    aligns.clear();
    for (const QString& cell : row.cells) {
        FmtTableAlign align = FmtTableAlign::Default;
        if (!fmtDelimiterAlign(cell, align))
            return false;
        aligns.append(align);
    }
    return true;
}

QString fmtPadCell(const QString& content, int width, FmtTableAlign align, bool padRight)
{
    const int pad = qMax(0, width - fmtCellWidth(content));
    switch (align) {
    case FmtTableAlign::Right:
        return QString(pad, QLatin1Char(' ')) + content;
    case FmtTableAlign::Center: {
        const int leftPad = pad / 2;
        QString out = QString(leftPad, QLatin1Char(' ')) + content;
        if (padRight) out += QString(pad - leftPad, QLatin1Char(' '));
        return out;
    }
    default:
        return padRight ? content + QString(pad, QLatin1Char(' ')) : content;
    }
}

QString fmtBuildTableRow(const QStringList& cells, int columns, const QList<int>& widths,
                         const QList<FmtTableAlign>& aligns, bool leadingPipe, bool trailingPipe,
                         const QString& indent, const QString& lineEnd)
{
    QString out = indent;
    if (leadingPipe)
        out += QLatin1Char('|');
    for (int c = 0; c < columns; ++c) {
        const bool spaceBefore = c > 0 || leadingPipe;
        const bool spaceAfter = c + 1 < columns || trailingPipe;
        const QString content = c < cells.size() ? cells.at(c) : QString();
        const FmtTableAlign align = c < aligns.size() ? aligns.at(c) : FmtTableAlign::Default;
        if (spaceBefore)
            out += QLatin1Char(' ');
        out += fmtPadCell(content, c < widths.size() ? widths.at(c) : 0, align, spaceAfter);
        if (spaceAfter)
            out += QLatin1Char(' ');
        if (c + 1 < columns)
            out += QLatin1Char('|');
    }
    if (trailingPipe)
        out += QLatin1Char('|');
    return out + lineEnd;
}

QString fmtBuildDelimiterRow(int columns, const QList<int>& widths, const QList<FmtTableAlign>& aligns,
                             bool leadingPipe, bool trailingPipe, const QString& indent,
                             const QString& lineEnd)
{
    QString out = indent;
    if (leadingPipe)
        out += QLatin1Char('|');
    for (int c = 0; c < columns; ++c) {
        int slot = c < widths.size() ? widths.at(c) : 1;
        if (c > 0 || leadingPipe) ++slot;
        if (c + 1 < columns || trailingPipe) ++slot;
        const FmtTableAlign align = c < aligns.size() ? aligns.at(c) : FmtTableAlign::Default;
        switch (align) {
        case FmtTableAlign::Left:
            out += QLatin1Char(':') + QString(slot - 1, QLatin1Char('-'));
            break;
        case FmtTableAlign::Right:
            out += QString(slot - 1, QLatin1Char('-')) + QLatin1Char(':');
            break;
        case FmtTableAlign::Center:
            out += QLatin1Char(':') + QString(slot - 2, QLatin1Char('-')) + QLatin1Char(':');
            break;
        default:
            out += QString(slot, QLatin1Char('-'));
            break;
        }
        if (c + 1 < columns)
            out += QLatin1Char('|');
    }
    if (trailingPipe)
        out += QLatin1Char('|');
    return out + lineEnd;
}

// A table starts where a pipe-bearing line is immediately followed by a
// delimiter row with the same number of cells; it runs until a line that is
// blank, indented as code, or carries no cell-separating pipe. Returns the
// number of source lines consumed, 0 when `start` is not a table.
int fmtFormatTableAt(const QStringList& lines, int start, QStringList& formatted)
{
    if (start + 1 >= lines.size())
        return 0;

    const QString& headerLine = lines.at(start);
    if (fmtIndentedCode(headerLine))
        return 0;
    int indentLen = 0;
    while (indentLen < headerLine.size() && headerLine.at(indentLen) == QLatin1Char(' ')) ++indentLen;
    // Blockquoted and list-nested tables keep a prefix this formatter does not
    // reconstruct; leaving them untouched is safer than rewriting them wrong.
    if (indentLen >= headerLine.size() || headerLine.at(indentLen) == QLatin1Char('>'))
        return 0;
    const QString indent = headerLine.left(indentLen);

    const FmtTableRow header = fmtSplitTableRow(headerLine);
    if (!header.valid)
        return 0;
    const FmtTableRow delimiter = fmtSplitTableRow(lines.at(start + 1));
    QList<FmtTableAlign> aligns;
    if (!fmtParseDelimiterRow(delimiter, aligns))
        return 0;
    if (aligns.size() != header.cells.size())
        return 0;

    QList<FmtTableRow> body;
    int end = start + 2;
    while (end < lines.size()) {
        const QString& line = lines.at(end);
        if (line.trimmed().isEmpty() || fmtIndentedCode(line))
            break;
        const FmtTableRow row = fmtSplitTableRow(line);
        if (!row.valid)
            break;
        body.append(row);
        ++end;
    }

    const int columnCount = aligns.size();
    QList<int> widths;
    const auto measure = [&](const FmtTableRow& row) {
        for (int c = 0; c < row.cells.size(); ++c) {
            const int w = fmtCellWidth(row.cells.at(c));
            if (c >= widths.size()) widths.append(w);
            else widths[c] = qMax(widths.at(c), w);
        }
    };
    measure(header);
    for (const FmtTableRow& row : body)
        measure(row);
    while (widths.size() < columnCount)
        widths.append(0);
    for (int& w : widths)
        w = qMax(1, w);

    formatted.clear();
    formatted.append(fmtBuildTableRow(header.cells, qMax(columnCount, header.cells.size()), widths,
                                      aligns, header.leadingPipe, header.trailingPipe, indent,
                                      header.lineEnd));
    // The delimiter row alone decides how many columns the table renders, so
    // it keeps its original cell count even when a body row has extra cells.
    formatted.append(fmtBuildDelimiterRow(columnCount, widths, aligns, header.leadingPipe,
                                          header.trailingPipe, indent, delimiter.lineEnd));
    for (const FmtTableRow& row : body) {
        formatted.append(fmtBuildTableRow(row.cells, qMax(columnCount, row.cells.size()), widths,
                                          aligns, header.leadingPipe, header.trailingPipe, indent,
                                          row.lineEnd));
    }
    return end - start;
}
} // namespace

QString formatMarkdown(const QString& text, int firstLine, int lastLine)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    QStringList out;
    QList<bool> protectedLines;
    out.reserve(lines.size());
    protectedLines.reserve(lines.size());
    bool fence = false, frontMatter = false, html = false, displayMath = false;
    QChar fenceChar;
    int fenceLen = 0;
    bool orderedList = false;
    int orderedListIndent = -1;
    QChar orderedListDelimiter;
    int nextOrderedNumber = 1;
    bool orderedListStartsAtOne = false;
    const bool limited = firstLine >= 0 && lastLine >= firstLine;
    for (int i = 0; i < lines.size(); ++i) {
        const QString& source = lines.at(i);
        bool openedFence = false;
        bool openedMath = false;
        bool protectedLine = fence || frontMatter || html || displayMath || fmtIndentedCode(source);
        const QString trimmed = source.trimmed();
        const bool mathDelimiter = trimmed == QStringLiteral("$$")
            || trimmed == QStringLiteral("\\[") || trimmed == QStringLiteral("\\]");
        if (!fence && !frontMatter && !html && !displayMath && mathDelimiter)
            displayMath = true, openedMath = true, protectedLine = true;
        if (i == 0 && (trimmed == QStringLiteral("---"))) frontMatter = true;
        if (frontMatter && i > 0 && (trimmed == QStringLiteral("---") || trimmed == QStringLiteral("..."))) protectedLine = true;
        int lead = 0;
        while (lead < source.size() && lead < 3 && source.at(lead) == QLatin1Char(' ')) ++lead;
        if (!fence && !frontMatter && !html && lead < source.size()
            && (source.at(lead) == QLatin1Char('`') || source.at(lead) == QLatin1Char('~'))) {
            int n = lead;
            while (n < source.size() && source.at(n) == source.at(lead)) ++n;
            if (n - lead >= 3) { protectedLine = true; fence = true; openedFence = true; fenceChar = source.at(lead); fenceLen = n - lead; }
        }
        if (!fence && !frontMatter && !html && lead < source.size() && source.at(lead) == QLatin1Char('<'))
            html = true, protectedLine = true;
        if (!protectedLine) {
            QStringList table;
            const int consumed = fmtFormatTableAt(lines, i, table);
            // Only rewrite a table that lies entirely inside the requested
            // range, so "Format Selection" never touches a row outside it.
            if (consumed > 0) {
                const bool inRange = !limited || (i >= firstLine && i + consumed - 1 <= lastLine);
                for (int k = 0; k < consumed; ++k) {
                    out.append(inRange ? table.at(k) : lines.at(i + k));
                    protectedLines.append(true);
                }
                orderedList = false;
                orderedListStartsAtOne = true;
                i += consumed - 1;
                continue;
            }
        }
        QString value = source;
        if (!protectedLine) {
            int listIndent = 0;
            int markerPosition = 0;
            QChar delimiter;
            int sourceNumber = 0;
            const bool isOrdered = fmtOrderedListInfo(source, listIndent, markerPosition,
                                                      delimiter, sourceNumber);
            if (isOrdered) {
                if (!orderedList || listIndent != orderedListIndent
                    || delimiter != orderedListDelimiter) {
                    orderedList = true;
                    orderedListIndent = listIndent;
                    orderedListDelimiter = delimiter;
                    nextOrderedNumber = orderedListStartsAtOne ? 1 : sourceNumber;
                    orderedListStartsAtOne = false;
                }
                if (!limited || (i >= firstLine && i <= lastLine)) {
                    value = source;
                    fmtNormalizeOrderedListNumber(value, nextOrderedNumber);
                    value = fmtLine(value);
                }
                if (nextOrderedNumber < 999999999)
                    ++nextOrderedNumber;
            } else {
                orderedList = false;
                orderedListStartsAtOne = true;
                if (!limited || (i >= firstLine && i <= lastLine)) value = fmtLine(source);
            }
        } else {
            orderedList = false;
            orderedListStartsAtOne = true;
        }
        out.append(value);
        protectedLines.append(protectedLine);
        if (fence && !openedFence && lead < source.size() && source.at(lead) == fenceChar) {
            int n = lead; while (n < source.size() && source.at(n) == fenceChar) ++n;
            if (n - lead >= fenceLen && source.mid(n).trimmed().isEmpty()) fence = false;
        }
        if (frontMatter && i > 0 && (trimmed == QStringLiteral("---") || trimmed == QStringLiteral("..."))) frontMatter = false;
        if (html && fmtBlank(source)) html = false;
        if (displayMath && !mathDelimiter && (trimmed == QStringLiteral("$$") || trimmed == QStringLiteral("\\]"))) displayMath = false;
        else if (displayMath && !openedMath && mathDelimiter && (trimmed == QStringLiteral("$$") || trimmed == QStringLiteral("\\]")))
            displayMath = false;
    }
    QStringList collapsed;
    bool previousBlank = false;
    for (int i = 0; i < out.size(); ++i) {
        const bool inRange = !limited || (i >= firstLine && i <= lastLine);
        const bool blank = fmtBlank(out.at(i));
        const bool protectedLine = protectedLines.at(i);
        if (inRange && blank && previousBlank && !protectedLine) continue;
        collapsed.append(out.at(i));
        previousBlank = blank && !protectedLine;
    }
    QString result = collapsed.join(QLatin1Char('\n'));
    if (text.endsWith(QLatin1Char('\n')) && !result.endsWith(QLatin1Char('\n')))
        result += QLatin1Char('\n');
    return result;
}

QString updateSectionNumbers(const QString& text, int firstLine, int lastLine)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    const bool limited = firstLine >= 0 && lastLine >= firstLine;
    int counters[7] = {};
    bool inFence = false;
    FmtFenceInfo opener;

    for (int i = 0; i < lines.size(); ++i) {
        const QString source = lines.at(i);
        if (inFence) {
            if (fmtClosesFence(source, opener))
                inFence = false;
            continue;
        }
        const FmtFenceInfo fence = fmtFenceInfo(source);
        if (fence.length > 0) {
            inFence = true;
            opener = fence;
            continue;
        }

        int contentStart = 0;
        int level = 0;
        if (!fmtHeadingInfo(source, contentStart, level))
            continue;
        const QString title = fmtStripHeadingNumber(source.mid(contentStart));
        const int hashesStart = source.indexOf(QLatin1Char('#'));
        if (level == 1) {
            for (int& counter : counters)
                counter = 0;
            if (!limited || (i >= firstLine && i <= lastLine))
                lines[i] = source.left(hashesStart + 1) + QLatin1Char(' ') + title;
            continue;
        }

        for (int missing = 2; missing < level; ++missing) {
            if (counters[missing] == 0)
                counters[missing] = 1;
        }
        ++counters[level];
        for (int deeper = level + 1; deeper <= 6; ++deeper)
            counters[deeper] = 0;

        if (!limited || (i >= firstLine && i <= lastLine)) {
            QStringList parts;
            for (int part = 2; part <= level; ++part)
                parts.append(QString::number(counters[part]));
            const QString number = parts.join(QLatin1Char('.'))
                + (level == 2 ? QStringLiteral(".") : QString());
            lines[i] = source.left(hashesStart + level) + QLatin1Char(' ') + number
                + QLatin1Char(' ') + title;
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString updateListNumbers(const QString& text, int firstLine, int lastLine)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    const bool limited = firstLine >= 0 && lastLine >= firstLine;
    QMap<int, int> nextNumbers;
    bool inFence = false;
    FmtFenceInfo opener;
    const auto clearAtOrDeeper = [&nextNumbers](int indent) {
        for (auto it = nextNumbers.begin(); it != nextNumbers.end();) {
            if (it.key() >= indent)
                it = nextNumbers.erase(it);
            else
                ++it;
        }
    };

    for (int i = 0; i < lines.size(); ++i) {
        const QString source = lines.at(i);
        if (inFence) {
            if (fmtClosesFence(source, opener))
                inFence = false;
            continue;
        }
        const FmtFenceInfo fence = fmtFenceInfo(source);
        if (fence.length > 0) {
            inFence = true;
            opener = fence;
            continue;
        }

        int indent = 0;
        int markerPosition = 0;
        QChar delimiter;
        int ignoredNumber = 0;
        if (fmtOrderedListInfo(source, indent, markerPosition, delimiter, ignoredNumber)) {
            clearAtOrDeeper(indent + 1);
            const int number = nextNumbers.value(indent, 1);
            nextNumbers[indent] = number + 1;
            if (!limited || (i >= firstLine && i <= lastLine)) {
                QString numbered = source;
                fmtNormalizeOrderedListNumber(numbered, number);
                lines[i] = numbered;
            }
            continue;
        }

        if (source.trimmed().isEmpty())
            continue; // Blank lines can occur in a loose list.

        int leading = 0;
        while (leading < source.size() && source.at(leading) == QLatin1Char(' '))
            ++leading;
        const bool unordered = leading + 1 < source.size()
            && (source.at(leading) == QLatin1Char('-') || source.at(leading) == QLatin1Char('*')
                || source.at(leading) == QLatin1Char('+'))
            && source.at(leading + 1).isSpace();
        if (unordered) {
            clearAtOrDeeper(leading);
        } else {
            // Indented prose may be a continuation; prose at the list's
            // indentation (or less) starts a new independent list run.
            clearAtOrDeeper(leading);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace TextTransforms
