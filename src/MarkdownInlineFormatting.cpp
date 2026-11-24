#include "MarkdownInlineFormatting.h"

#include <QTextCursor>
#include <QTextDocument>
#include <QRegularExpression>

namespace MarkdownInlineFormatting {
namespace {

const QString kOpen = QStringLiteral("<mark>");
const QString kClose = QStringLiteral("</mark>");

bool hasExactWrapper(const QString& source, int start, int end)
{
    return end - start >= kOpen.size() + kClose.size()
        && source.mid(start, kOpen.size()).compare(kOpen, Qt::CaseInsensitive) == 0
        && source.mid(end - kClose.size(), kClose.size()).compare(kClose, Qt::CaseInsensitive) == 0;
}

bool hasAdjacentWrapper(const QString& source, int start, int end)
{
    return start >= kOpen.size() && end + kClose.size() <= source.size()
        && source.mid(start - kOpen.size(), kOpen.size()).compare(kOpen, Qt::CaseInsensitive) == 0
        && source.mid(end, kClose.size()).compare(kClose, Qt::CaseInsensitive) == 0;
}

void expandCodeSpan(const QString& source, int* start, int* end)
{
    for (int i = 0; i < source.size(); ++i) {
        if (source.at(i) != QLatin1Char('`'))
            continue;
        int run = 1;
        while (i + run < source.size() && source.at(i + run) == QLatin1Char('`'))
            ++run;
        const QString delimiter(run, QLatin1Char('`'));
        const int close = source.indexOf(delimiter, i + run);
        if (close < 0)
            break;
        if (*start >= i + run && *end <= close) {
            *start = i;
            *end = close + run;
            return;
        }
        i = close + run - 1;
    }
}

// A raw inline tag before a list marker stops Markdown from recognizing that
// marker. If a selection includes it, retain the marker and mark each selected
// item body independently instead of turning the list into paragraph text.
bool wrapSelectionPreservingListMarkers(QTextCursor& cursor, const QString& source,
                                        int start, int end)
{
    static const QRegularExpression listPrefix(
        QStringLiteral(R"(^\s*(?:[-+*]|\d+[.)])\s+)"));
    bool containsMarker = false;
    int lineStart = start <= 0 ? 0 : source.lastIndexOf(QLatin1Char('\n'), start - 1) + 1;
    while (lineStart <= end) {
        const int lineEndAt = source.indexOf(QLatin1Char('\n'), lineStart);
        const int lineEnd = lineEndAt < 0 ? source.size() : lineEndAt;
        const int from = qMax(start, lineStart);
        const int to = qMin(end, lineEnd);
        const QRegularExpressionMatch match = listPrefix.match(source.mid(lineStart, lineEnd - lineStart));
        if (match.hasMatch() && from <= lineStart + match.capturedLength() && to > lineStart + match.capturedLength()) {
            containsMarker = true;
            break;
        }
        if (lineEndAt < 0)
            break;
        lineStart = lineEndAt + 1;
    }
    if (!containsMarker)
        return false;

    QString replacement;
    replacement.reserve(end - start + 32);
    int cursorPos = start;
    lineStart = start <= 0 ? 0 : source.lastIndexOf(QLatin1Char('\n'), start - 1) + 1;
    while (lineStart <= end) {
        const int lineEndAt = source.indexOf(QLatin1Char('\n'), lineStart);
        const int lineEnd = lineEndAt < 0 ? source.size() : lineEndAt;
        const int from = qMax(start, lineStart);
        const int to = qMin(end, lineEnd);
        if (from < to) {
            replacement += source.mid(cursorPos, from - cursorPos);
            const QRegularExpressionMatch match = listPrefix.match(source.mid(lineStart, lineEnd - lineStart));
            const int prefixEnd = match.hasMatch() ? lineStart + match.capturedLength() : -1;
            if (prefixEnd >= from && prefixEnd < to) {
                replacement += source.mid(from, prefixEnd - from);
                replacement += kOpen + source.mid(prefixEnd, to - prefixEnd) + kClose;
            } else {
                replacement += kOpen + source.mid(from, to - from) + kClose;
            }
            cursorPos = to;
        }
        if (lineEndAt < 0 || lineEndAt >= end)
            break;
        lineStart = lineEndAt + 1;
    }
    replacement += source.mid(cursorPos, end - cursorPos);
    cursor.beginEditBlock();
    cursor.insertText(replacement);
    cursor.setPosition(start);
    cursor.setPosition(start + replacement.size(), QTextCursor::KeepAnchor);
    cursor.endEditBlock();
    return true;
}

} // namespace

void toggleMark(QTextCursor& cursor)
{
    QTextDocument* document = cursor.document();
    if (!document)
        return;

    const QString source = document->toPlainText();
    int start = cursor.selectionStart();
    int end = cursor.selectionEnd();
    if (start == end) {
        cursor.beginEditBlock();
        cursor.insertText(kOpen + kClose);
        cursor.setPosition(start + kOpen.size());
        cursor.endEditBlock();
        return;
    }

    if (hasExactWrapper(source, start, end)) {
        const QString content = source.mid(start + kOpen.size(),
                                           end - start - kOpen.size() - kClose.size());
        cursor.beginEditBlock();
        cursor.insertText(content);
        cursor.setPosition(start);
        cursor.setPosition(start + content.size(), QTextCursor::KeepAnchor);
        cursor.endEditBlock();
        return;
    }

    if (hasAdjacentWrapper(source, start, end)) {
        const QString content = source.mid(start, end - start);
        QTextCursor edit(document);
        edit.setPosition(start - kOpen.size());
        edit.setPosition(end + kClose.size(), QTextCursor::KeepAnchor);
        edit.beginEditBlock();
        edit.insertText(content);
        edit.endEditBlock();
        cursor.setPosition(start - kOpen.size());
        cursor.setPosition(start - kOpen.size() + content.size(), QTextCursor::KeepAnchor);
        return;
    }

    expandCodeSpan(source, &start, &end);
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    if (wrapSelectionPreservingListMarkers(cursor, source, start, end))
        return;

    const QString content = source.mid(start, end - start);
    cursor.beginEditBlock();
    cursor.insertText(kOpen + content + kClose);
    cursor.setPosition(start + kOpen.size());
    cursor.setPosition(start + kOpen.size() + content.size(), QTextCursor::KeepAnchor);
    cursor.endEditBlock();
}

} // namespace MarkdownInlineFormatting
