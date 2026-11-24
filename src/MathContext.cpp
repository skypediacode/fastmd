#include "MathContext.h"

#include <QTextCursor>
#include <QTextDocument>
#include <QVector>

namespace {

struct Span {
    int start;
    int end;
    MathContext::Kind kind;
};

bool escaped(const QString& text, int pos)
{
    int slashes = 0;
    for (int i = pos - 1; i >= 0 && text.at(i) == QLatin1Char('\\'); --i)
        ++slashes;
    return (slashes & 1) != 0;
}

int runLength(const QString& text, int pos, QChar ch)
{
    int end = pos;
    while (end < text.size() && text.at(end) == ch)
        ++end;
    return end - pos;
}

bool fenceAt(const QString& text, int start, int* fenceLength, QChar* fenceChar)
{
    int p = start;
    while (p < text.size() && p < start + 3 && text.at(p) == QLatin1Char(' '))
        ++p;
    if (p >= text.size() || (text.at(p) != QLatin1Char('`') && text.at(p) != QLatin1Char('~')))
        return false;
    const int length = runLength(text, p, text.at(p));
    if (length < 3)
        return false;
    *fenceLength = length;
    *fenceChar = text.at(p);
    return true;
}

QVector<Span> mathSpans(const QString& text)
{
    QVector<Span> spans;
    int i = 0;
    bool inFence = false;
    int fenceLength = 0;
    QChar fenceChar;

    while (i < text.size()) {
        const int lineEnd = text.indexOf(QLatin1Char('\n'), i);
        const int nextLine = lineEnd < 0 ? text.size() : lineEnd + 1;

        int candidateFenceLength = 0;
        QChar candidateFenceChar;
        if (fenceAt(text, i, &candidateFenceLength, &candidateFenceChar)) {
            if (!inFence) {
                inFence = true;
                fenceLength = candidateFenceLength;
                fenceChar = candidateFenceChar;
                i = nextLine;
                continue;
            }
            if (candidateFenceChar == fenceChar && candidateFenceLength >= fenceLength) {
                inFence = false;
                i = nextLine;
                continue;
            }
        }
        if (inFence) {
            i = nextLine;
            continue;
        }

        int continueAt = -1;
        for (int p = i; p < nextLine; ++p) {
            if (text.at(p) == QLatin1Char('`')) {
                const int ticks = runLength(text, p, QLatin1Char('`'));
                const QString closer(ticks, QLatin1Char('`'));
                const int close = text.indexOf(closer, p + ticks);
                if (close >= 0 && close < nextLine) {
                    p = close + ticks - 1;
                    continue;
                }
            }
            if (text.at(p) == QLatin1Char('$') && !escaped(text, p)) {
                const int dollars = runLength(text, p, QLatin1Char('$'));
                if (dollars == 2) {
                    int close = p + 2;
                    while (close + 1 < text.size()) {
                        close = text.indexOf(QStringLiteral("$$"), close);
                        if (close < 0)
                            break;
                        if (!escaped(text, close) && runLength(text, close, QLatin1Char('$')) == 2)
                            break;
                        ++close;
                    }
                    if (close >= 0) {
                        spans.append({p, close + 2, MathContext::Kind::Block});
                        continueAt = close + 2;
                        break;
                    }
                    continue;
                }
                if (dollars == 1) {
                    int close = p + 1;
                    while (close < nextLine) {
                        close = text.indexOf(QLatin1Char('$'), close);
                        if (close < 0 || close >= nextLine)
                            break;
                        if (!escaped(text, close) && runLength(text, close, QLatin1Char('$')) == 1)
                            break;
                        ++close;
                    }
                    if (close >= 0 && close < nextLine) {
                        spans.append({p, close + 1, MathContext::Kind::Inline});
                        p = close;
                    }
                    continue;
                }
            }
            if (text.at(p) == QLatin1Char('\\') && !escaped(text, p) && p + 1 < text.size()
                && (text.at(p + 1) == QLatin1Char('(') || text.at(p + 1) == QLatin1Char('['))) {
                const bool block = text.at(p + 1) == QLatin1Char('[');
                const QString closer = block ? QStringLiteral("\\]") : QStringLiteral("\\)");
                const int close = text.indexOf(closer, p + 2);
                if (close >= 0 && (block || close < nextLine) && !escaped(text, close)) {
                    spans.append({p, close + 2, block ? MathContext::Kind::Block : MathContext::Kind::Inline});
                    if (block) {
                        continueAt = close + 2;
                        break;
                    }
                    p = close + 1;
                }
            }
        }
        i = continueAt >= 0 ? continueAt : nextLine;
    }
    return spans;
}

} // namespace

namespace MathContext {

Result at(const QTextDocument* document, int start, int end)
{
    if (!document || start < 0 || end < start)
        return {};
    const QVector<Span> spans = mathSpans(document->toPlainText());
    for (const Span& span : spans) {
        if (start > span.start && end < span.end)
            return {span.kind, span.start, span.end};
    }
    return {};
}

Result at(const QTextCursor& cursor)
{
    return at(cursor.document(), cursor.selectionStart(), cursor.selectionEnd());
}

} // namespace MathContext
