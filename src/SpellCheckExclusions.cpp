#include "SpellCheckExclusions.h"

namespace SpellCheckExclusions {
namespace {

bool isFenceChar(QChar c)
{
    return c == QLatin1Char('`') || c == QLatin1Char('~');
}

int leadingSpaceWidth(const QString& line, int* firstNonSpace)
{
    int width = 0;
    int i = 0;
    for (; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char(' '))
            width += 1;
        else if (c == QLatin1Char('\t'))
            width += 4;
        else
            break;
    }
    if (firstNonSpace)
        *firstNonSpace = i;
    return width;
}

// Mirrors MarkdownHighlighter's fenceLineRe (^(```+|~~~+)(\S*)\s*$) so the two
// never disagree about where a fenced block starts and ends. Written as a scan
// rather than a QRegularExpression because this runs per line (Rule 13).
bool isFenceLine(const QString& line, QChar* fenceChar, int* fenceLength)
{
    if (line.isEmpty() || !isFenceChar(line.at(0)))
        return false;
    const QChar marker = line.at(0);
    int i = 0;
    while (i < line.size() && line.at(i) == marker)
        ++i;
    if (i < 3)
        return false;
    const int runLength = i;
    while (i < line.size() && !line.at(i).isSpace())
        ++i;
    for (int j = i; j < line.size(); ++j) {
        if (!line.at(j).isSpace())
            return false;
    }
    if (fenceChar)
        *fenceChar = marker;
    if (fenceLength)
        *fenceLength = runLength;
    return true;
}

bool isYamlDelimiter(const QString& line)
{
    const QString trimmed = line.trimmed();
    return trimmed == QLatin1String("---") || trimmed == QLatin1String("...");
}

int countDisplayMathMarkers(const QString& line)
{
    int count = 0;
    for (int i = 0; i + 1 < line.size(); ++i) {
        if (line.at(i) == QLatin1Char('$') && line.at(i + 1) == QLatin1Char('$')) {
            ++count;
            ++i;
        }
    }
    return count;
}

bool isReferenceDefinition(const QString& line, int firstNonSpace)
{
    if (firstNonSpace >= line.size() || line.at(firstNonSpace) != QLatin1Char('['))
        return false;
    for (int i = firstNonSpace + 1; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('\\')) {
            ++i;
            continue;
        }
        if (c == QLatin1Char(']'))
            return i + 1 < line.size() && line.at(i + 1) == QLatin1Char(':');
    }
    return false;
}

bool startsUrl(const QString& line, int i)
{
    if (i > 0) {
        const QChar prev = line.at(i - 1);
        if (prev.isLetterOrNumber())
            return false;
    }
    static const char* const schemes[] = { "http://", "https://", "ftp://", "ftps://",
                                            "mailto:", "file://", "www." };
    for (const char* scheme : schemes) {
        if (QStringView(line).sliced(i).startsWith(QLatin1String(scheme), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

int endOfUrl(const QString& line, int i)
{
    while (i < line.size()) {
        const QChar c = line.at(i);
        if (c.isSpace() || c == QLatin1Char('<') || c == QLatin1Char('>')
            || c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('"'))
            break;
        ++i;
    }
    return i;
}

// A ":name:" emoji shortcode. Digit-only bodies are rejected so clock times
// ("10:30:45") stay prose, matching the syntax definition's (?![0-9]+:) guard.
int endOfEmojiShortcode(const QString& line, int i)
{
    bool sawLetter = false;
    int j = i + 1;
    for (; j < line.size(); ++j) {
        const QChar c = line.at(j);
        if (c == QLatin1Char(':'))
            break;
        const bool allowed = c.isLetterOrNumber() || c == QLatin1Char('_')
                             || c == QLatin1Char('+') || c == QLatin1Char('-');
        if (!allowed)
            return -1;
        if (!c.isDigit())
            sawLetter = true;
    }
    if (j >= line.size() || j == i + 1 || !sawLetter)
        return -1;
    return j + 1;
}

void closeRun(QVector<Span>& spans, const QString& line, int from, int to)
{
    while (from < to && line.at(from).isSpace())
        ++from;
    while (to > from && line.at(to - 1).isSpace())
        --to;
    if (to > from)
        spans.append({ from, to - from });
}

} // namespace

LineState advance(const QString& line, LineState state, int lineNumber)
{
    QChar fenceChar;
    int fenceLength = 0;
    const bool fenceLine = isFenceLine(line, &fenceChar, &fenceLength);

    if (state.inFence) {
        if (fenceLine) {
            state.inFence = false;
            state.fenceChar = QChar();
            state.fenceLength = 0;
        }
        return state;
    }

    if (state.inFrontMatter) {
        if (isYamlDelimiter(line))
            state.inFrontMatter = false;
        return state;
    }

    if (fenceLine) {
        state.inFence = true;
        state.fenceChar = fenceChar;
        state.fenceLength = fenceLength;
        state.inMathBlock = false;
        return state;
    }

    if (lineNumber == 0 && line.trimmed() == QLatin1String("---")) {
        state.inFrontMatter = true;
        return state;
    }

    if (countDisplayMathMarkers(line) % 2 != 0)
        state.inMathBlock = !state.inMathBlock;

    return state;
}

QVector<Span> proseSpans(const QString& line, const LineState& state)
{
    QVector<Span> spans;
    if (line.isEmpty())
        return spans;

    if (state.inFence || state.inFrontMatter || state.inMathBlock)
        return spans;

    if (isFenceLine(line, nullptr, nullptr))
        return spans;

    int firstNonSpace = 0;
    if (leadingSpaceWidth(line, &firstNonSpace) >= 4)
        return spans;

    if (firstNonSpace >= line.size())
        return spans;

    if (line.trimmed() == QLatin1String("---"))
        return spans;

    if (isReferenceDefinition(line, firstNonSpace))
        return spans;

    int runStart = firstNonSpace;
    int i = firstNonSpace;
    const int n = line.size();

    while (i < n) {
        const QChar c = line.at(i);
        int skipTo = -1;

        if (c == QLatin1Char('\\') && i + 1 < n) {
            const QChar next = line.at(i + 1);
            if (next == QLatin1Char('(') || next == QLatin1Char('[')) {
                // \( ... \) and \[ ... \] LaTeX math.
                const QChar closer = next == QLatin1Char('(') ? QLatin1Char(')') : QLatin1Char(']');
                int j = i + 2;
                while (j + 1 < n && !(line.at(j) == QLatin1Char('\\') && line.at(j + 1) == closer))
                    ++j;
                skipTo = (j + 1 < n) ? j + 2 : n;
            } else if (next.isLetter()) {
                int j = i + 1;
                while (j < n && line.at(j).isLetter())
                    ++j;
                skipTo = j;
            } else {
                skipTo = i + 2;
            }
        } else if (c == QLatin1Char('`')) {
            int runLength = 0;
            while (i + runLength < n && line.at(i + runLength) == QLatin1Char('`'))
                ++runLength;
            int j = i + runLength;
            int closeAt = -1;
            while (j < n) {
                if (line.at(j) == QLatin1Char('`')) {
                    int closeLength = 0;
                    while (j + closeLength < n && line.at(j + closeLength) == QLatin1Char('`'))
                        ++closeLength;
                    if (closeLength == runLength) {
                        closeAt = j + closeLength;
                        break;
                    }
                    j += closeLength;
                    continue;
                }
                ++j;
            }
            skipTo = closeAt > 0 ? closeAt : n;
        } else if (c == QLatin1Char('$')) {
            const bool display = (i + 1 < n && line.at(i + 1) == QLatin1Char('$'));
            const int markerLength = display ? 2 : 1;
            int j = i + markerLength;
            int closeAt = -1;
            while (j < n) {
                if (line.at(j) == QLatin1Char('\\')) {
                    j += 2;
                    continue;
                }
                if (line.at(j) == QLatin1Char('$')) {
                    if (!display) {
                        closeAt = j + 1;
                        break;
                    }
                    if (j + 1 < n && line.at(j + 1) == QLatin1Char('$')) {
                        closeAt = j + 2;
                        break;
                    }
                }
                ++j;
            }
            // An unmatched '$' is currency, not math - never swallow the rest
            // of the line for it.
            skipTo = closeAt > 0 ? closeAt : i + markerLength;
        } else if (c == QLatin1Char('<')) {
            if (i + 1 < n) {
                const QChar next = line.at(i + 1);
                if (next.isLetter() || next == QLatin1Char('/') || next == QLatin1Char('!')) {
                    int j = line.indexOf(QLatin1Char('>'), i + 1);
                    skipTo = j >= 0 ? j + 1 : n;
                }
            }
        } else if (startsUrl(line, i)) {
            skipTo = endOfUrl(line, i);
        } else if (c == QLatin1Char(']') && i + 1 < n && line.at(i + 1) == QLatin1Char('(')) {
            int depth = 0;
            int j = i + 1;
            for (; j < n; ++j) {
                const QChar d = line.at(j);
                if (d == QLatin1Char('\\')) {
                    ++j;
                    continue;
                }
                if (d == QLatin1Char('('))
                    ++depth;
                else if (d == QLatin1Char(')')) {
                    if (--depth == 0) {
                        ++j;
                        break;
                    }
                }
            }
            skipTo = j;
        } else if (c == QLatin1Char('[') && i + 1 < n && line.at(i + 1) == QLatin1Char('^')) {
            const int j = line.indexOf(QLatin1Char(']'), i + 2);
            skipTo = j >= 0 ? j + 1 : n;
        } else if (c == QLatin1Char(':')) {
            const int end = endOfEmojiShortcode(line, i);
            if (end > 0)
                skipTo = end;
        }

        if (skipTo > i) {
            closeRun(spans, line, runStart, i);
            i = skipTo;
            runStart = i;
            continue;
        }
        ++i;
    }

    closeRun(spans, line, runStart, n);
    return spans;
}

QVector<Span> plainTextSpans(const QString& line)
{
    QVector<Span> spans;
    closeRun(spans, line, 0, line.size());
    return spans;
}

} // namespace SpellCheckExclusions
