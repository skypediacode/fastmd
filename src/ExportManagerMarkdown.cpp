#include "ExportManager.h"
#include "MathRenderer.h"

#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <functional>

#include "md4c-html.h"
#include "MarkdownToc.h"
#include "MarkdownExtensions.h"
#include "MarkdownAlignment.h"
#include "MarkdownPreviewRenderer.h"

namespace {

// Replaces every "<prefix><index>@@" placeholder in one linear scan. Doing it
// with one QString::replace() per snippet re-scans the whole body N times, which
// turns a math-heavy document's preview refresh quadratic.
QString substitutePlaceholders(const QString& text, const QString& prefix,
                               const QStringList& snippets, const QStringList& literalSnippets = {})
{
    if (snippets.isEmpty())
        return text;
    int cursor = text.indexOf(prefix);
    if (cursor < 0)
        return text;

    QString out;
    out.reserve(text.size() + 64);
    int last = 0;
    // MD4C is the authority for code blocks (including indentation and nested
    // containers). Restore source spelling where it classified a placeholder
    // as code; compatibility decoding must never rewrite a displayed example.
    const auto nextCodeStart = [&](int from) {
        int start = text.indexOf(QStringLiteral("<code"), from);
        while (start >= 0) {
            const int after = start + 5;
            if (after < text.size() && (text.at(after) == QLatin1Char('>') || text.at(after).isSpace()))
                return start;
            start = text.indexOf(QStringLiteral("<code"), after);
        }
        return -1;
    };
    int codeStart = literalSnippets.isEmpty() ? -1 : nextCodeStart(0);
    int codeEnd = codeStart < 0 ? -1 : text.indexOf(QStringLiteral("</code>"), codeStart);
    while (cursor >= 0) {
        while (codeStart >= 0 && codeEnd >= 0 && codeEnd < cursor) {
            codeStart = nextCodeStart(codeEnd + 7);
            codeEnd = codeStart < 0 ? -1 : text.indexOf(QStringLiteral("</code>"), codeStart);
        }
        const int digitsStart = cursor + prefix.size();
        int scan = digitsStart;
        while (scan < text.size() && text.at(scan).isDigit())
            ++scan;
        const bool closed = scan > digitsStart && scan + 1 < text.size()
            && text.at(scan) == QLatin1Char('@') && text.at(scan + 1) == QLatin1Char('@');
        bool ok = false;
        const int index = closed
            ? QStringView(text).mid(digitsStart, scan - digitsStart).toInt(&ok) : -1;
        if (closed && ok && index >= 0 && index < snippets.size()) {
            out += QStringView(text).mid(last, cursor - last);
            const bool inCode = codeStart >= 0 && codeStart < cursor && codeEnd > cursor;
            out += inCode && index < literalSnippets.size()
                ? literalSnippets.at(index).toHtmlEscaped() : snippets.at(index);
            last = scan + 2;
            cursor = text.indexOf(prefix, last);
        } else {
            // Not one of ours (or out of range): leave it in place verbatim.
            cursor = text.indexOf(prefix, cursor + prefix.size());
        }
    }
    out += QStringView(text).mid(last);
    return out;
}

static QString normalizeEscapedCalloutMarkers(QString markdown)
{
    // An escaped callout marker always contains the literal "\[!", so this probe
    // (a plain substring scan) replaces a multiline PCRE2 globalMatch over the
    // whole body on every refresh for a construct almost no document uses.
    if (!markdown.contains(QLatin1String("\\[!")))
        return markdown;

    static const QRegularExpression marker(
        QStringLiteral(R"(^([ \t]*>[ \t]*)\\\[!([A-Za-z]+)\\\])"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    static const QStringList supported = { QStringLiteral("NOTE"), QStringLiteral("TIP"),
        QStringLiteral("WARNING"), QStringLiteral("IMPORTANT"), QStringLiteral("CAUTION") };
    QString result;
    int last = 0;
    QRegularExpressionMatchIterator it = marker.globalMatch(markdown);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString type = match.captured(2).toUpper();
        if (!supported.contains(type))
            continue;
        result += QStringView(markdown).mid(last, match.capturedStart() - last);
        result += match.captured(1) + QStringLiteral("[!") + type + QStringLiteral("]");
        last = match.capturedEnd();
    }
    if (last == 0)
        return markdown;
    result += QStringView(markdown).mid(last);
    return result;
}

// FastMdMath collapses multi-line equations to one placeholder. Translate each
// resulting line through those collapses into the provenance supplied by the
// preceding preprocessing stage (TOC expansion in the preview pipeline).
static QVector<int> mapMathLineOrigins(int outputLineCount, const QVector<int>& inputOrigins,
                                       const QVector<MathLineShift>& shifts)
{
    QVector<int> origins;
    origins.reserve(outputLineCount);
    int shiftIndex = 0;
    int cumulative = 0;
    for (int line = 1; line <= outputLineCount; ++line) {
        while (shiftIndex < shifts.size() && shifts.at(shiftIndex).atLine < line)
            cumulative += shifts.at(shiftIndex++).deltaLines;
        const int inputLine = line + cumulative;
        origins.append(inputLine > 0 && inputLine <= inputOrigins.size()
            ? inputOrigins.at(inputLine - 1) : inputLine);
    }
    return origins;
}

static int lineCount(const QString& text)
{
    return text.count(QLatin1Char('\n')) + 1;
}

// `rendererMarkedLists` is set on the preview path, where MarkdownPreviewRenderer
// has already emitted data-fastmd-task-list / data-fastmd-loose-list from md4c's
// own block details. The loose-list half below re-splits the entire Markdown
// source and regex-matches every line to recompute the same thing, so it is
// skipped there; the export path (plain md_html) still needs it.
static QString normalizeTaskListHtml(QString html, const QString& markdown, bool interactive,
                                     bool rendererMarkedLists = false)
{
    // md4c emits a checkbox <input> for every task item, so a document without
    // one cannot produce a match below. Skipping spares a full rebuild of the
    // body on every preview refresh.
    if (!html.contains(QStringLiteral("checkbox")))
        return html;

    static const QRegularExpression checkedInputRe(
        QStringLiteral(R"re(<input\b(?=[^>]*\btype\s*=\s*"checkbox")(?=[^>]*\bchecked\b)[^>]*>)re"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression uncheckedInputRe(
        QStringLiteral(R"re(<input\b(?=[^>]*\btype\s*=\s*"checkbox")[^>]*>)re"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression taskListUlRe(
        QStringLiteral(R"re(<(ul|ol)([^>]*)class="([^"]*\btask-list\b[^"]*)"([^>]*)>)re"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression taskListLiRe(
        QStringLiteral(R"re(<li([^>]*)class="([^"]*\btask-list-item\b[^"]*)"([^>]*)>)re"),
        QRegularExpression::CaseInsensitiveOption);

    if (!interactive) {
        html.replace(checkedInputRe, QStringLiteral("<span class=\"task-list-marker\">☑&nbsp;</span>"));
        html.replace(uncheckedInputRe, QStringLiteral("<span class=\"task-list-marker\">☐&nbsp;</span>"));
    }
    html.replace(taskListUlRe, QStringLiteral("<\\1\\2class=\"\\3\" data-fastmd-task-list=\"1\"\\4>"));
    html.replace(taskListLiRe, QStringLiteral("<li\\1class=\"\\2\" data-fastmd-task-item=\"1\"\\3>"));

    if (rendererMarkedLists)
        return html;

    QList<bool> looseTaskItems;
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    static const QRegularExpression taskLineRe(
        QStringLiteral(R"(^[ \t]{0,3}(?:[-+*]|\d+[.)])[ \t]+\[[ xX]\])"));
    bool previousBlank = false;
    for (const QString& line : lines) {
        if (line.contains(QLatin1Char('[')) && taskLineRe.match(line).hasMatch())
            looseTaskItems.append(previousBlank);
        previousBlank = QStringView(line).trimmed().isEmpty();
    }

    // Linear equivalent of matching <(ul|ol)([^>]*)>(.*?)</\1>: for each list
    // opener, the block ends at the *earliest* closing tag of the same name
    // (the regex's lazy quantifier), and scanning resumes after it. PCRE2
    // backtracking over a multi-hundred-KB body made this the dominant cost of
    // refreshing a task-heavy document.
    static const QString itemMarker = QStringLiteral("data-fastmd-task-item=\"1\"");
    static const QString listMarker = QStringLiteral("data-fastmd-task-list=\"1\"");
    QString marked;
    marked.reserve(html.size() + 256);
    int last = 0;
    int taskIndex = 0;
    int cursor = 0;
    // Both openers are tracked incrementally: re-running indexOf() for each from
    // the cursor rescans the tail of the body once per list, which is quadratic
    // when a document uses only one of the two list kinds.
    int nextUl = html.indexOf(QStringLiteral("<ul"), 0, Qt::CaseSensitive);
    int nextOl = html.indexOf(QStringLiteral("<ol"), 0, Qt::CaseSensitive);
    while (cursor < html.size()) {
        if (nextUl >= 0 && nextUl < cursor)
            nextUl = html.indexOf(QStringLiteral("<ul"), cursor, Qt::CaseSensitive);
        if (nextOl >= 0 && nextOl < cursor)
            nextOl = html.indexOf(QStringLiteral("<ol"), cursor, Qt::CaseSensitive);

        int open = -1;
        QString closeTag;
        if (nextUl >= 0 && (nextOl < 0 || nextUl < nextOl)) {
            open = nextUl; closeTag = QStringLiteral("</ul>");
        } else if (nextOl >= 0) {
            open = nextOl; closeTag = QStringLiteral("</ol>");
        }
        if (open < 0)
            break;

        const int openEnd = html.indexOf(QLatin1Char('>'), open);
        if (openEnd < 0)
            break;
        const int closeAt = html.indexOf(closeTag, openEnd + 1);
        if (closeAt < 0)
            break;
        const int end = closeAt + closeTag.size();

        QString block = html.mid(open, end - open);
        const int taskItems = block.count(itemMarker);
        if (taskItems > 0 && !block.contains(listMarker)) {
            const int gt = block.indexOf(QLatin1Char('>'));
            if (gt >= 0)
                block.insert(gt, QStringLiteral(" data-fastmd-task-list=\"1\""));
        }
        bool loose = false;
        for (int i = 0; i < taskItems && taskIndex + i < looseTaskItems.size(); ++i)
            loose = loose || looseTaskItems.at(taskIndex + i);
        taskIndex += taskItems;
        if (taskItems > 0 && loose && !block.contains(QStringLiteral("data-fastmd-loose-list"))) {
            const int gt = block.indexOf(QLatin1Char('>'));
            if (gt >= 0)
                block.insert(gt, QStringLiteral(" data-fastmd-loose-list=\"1\""));
        }
        marked += QStringView(html).mid(last, open - last);
        marked += block;
        last = end;
        cursor = end;
    }
    if (last > 0)
        html = marked + html.mid(last);
    return html;
}

static QString transformCallouts(QString html)
{
    if (!html.contains(QStringLiteral("<blockquote"), Qt::CaseInsensitive))
        return html;
    std::function<QString(const QString&)> transform = [&transform](const QString& input) -> QString {
        QString out;
        int cursor = 0;
        while (cursor < input.size()) {
            const int open = input.indexOf(QStringLiteral("<blockquote"), cursor,
                                           Qt::CaseInsensitive);
            if (open < 0) { out += input.mid(cursor); break; }
            out += input.mid(cursor, open - cursor);
            const int openEnd = input.indexOf(QLatin1Char('>'), open);
            if (openEnd < 0) { out += input.mid(open); break; }
            int depth = 1;
            int scan = openEnd + 1;
            int close = -1;
            while (depth && scan < input.size()) {
                const int nextOpen = input.indexOf(QStringLiteral("<blockquote"), scan,
                                                    Qt::CaseInsensitive);
                const int nextClose = input.indexOf(QStringLiteral("</blockquote>"), scan,
                                                     Qt::CaseInsensitive);
                if (nextClose < 0) break;
                if (nextOpen >= 0 && nextOpen < nextClose) {
                    ++depth;
                    scan = input.indexOf(QLatin1Char('>'), nextOpen) + 1;
                } else {
                    --depth;
                    close = nextClose;
                    scan = nextClose + 13;
                }
            }
            if (close < 0) { out += input.mid(open); break; }
            QString inner = transform(input.mid(openEnd + 1, close - openEnd - 1));
            static const QRegularExpression marker(
                // The live preview renderer wraps every text run (including
                // the callout marker) in a source-line span.  Accept that
                // wrapper here as well as md_html's plain text so callouts
                // render identically in preview and export.  A soft break is
                // likewise a mapped empty span in the preview path.
                QStringLiteral(R"(^\s*<p\b[^>]*>\s*(?:<span\b[^>]*>\s*)?\[!([A-Za-z]+)\](?:\s*</span>)?(?:\s*</p>\s*|\s*(?:(?:<br\b[^>]*>\s*)|(?:<span\b[^>]*>\s*</span>\s*))?)?)"),
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch match = marker.match(inner);
            const QString type = match.hasMatch() ? match.captured(1).toUpper() : QString();
            static const QStringList supported = { QStringLiteral("NOTE"), QStringLiteral("TIP"),
                QStringLiteral("WARNING"), QStringLiteral("IMPORTANT"), QStringLiteral("CAUTION") };
            if (!supported.contains(type)) {
                out += input.mid(open, close + 13 - open);
            } else {
                QString openTag = input.mid(open, openEnd - open + 1);
                const QString classType = type.toLower();
                openTag.replace(QStringLiteral("<blockquote"),
                                QStringLiteral("<blockquote class=\"fastmd-callout fastmd-callout-%1\" data-callout-type=\"%1\"")
                                    .arg(classType), Qt::CaseInsensitive);
                const QString icon = type == QStringLiteral("NOTE") ? QStringLiteral("ⓘ")
                    : type == QStringLiteral("TIP") ? QStringLiteral("💡")
                    : type == QStringLiteral("WARNING") ? QStringLiteral("⚠")
                    : type == QStringLiteral("IMPORTANT") ? QStringLiteral("★") : QStringLiteral("⛔");
                const QString title = QStringLiteral("<div class=\"fastmd-callout-title\"><span class=\"fastmd-callout-icon\" aria-hidden=\"true\">%1</span><span>%2</span></div>").arg(icon, type);
                const bool markerParagraphClosed = match.captured(0).contains(QStringLiteral("</p>"), Qt::CaseInsensitive);
                out += openTag + title;
                if (!markerParagraphClosed)
                    out += QStringLiteral("<p>");
                out += inner.mid(match.capturedEnd());
                out += QStringLiteral("</blockquote>");
            }
            cursor = close + 13;
        }
        return out;
    };
    return transform(html);
}

// A <table> with border-radius + overflow:hidden doesn't reliably clip its own
// cell backgrounds/borders in Chromium/WebView2 -- table boxes are a special
// case for overflow clipping. Wrapping genuine content tables (MD4C's own
// tables always have a <thead>, unlike the small alignment tables math
// fallback rendering can emit) in a plain div lets THAT div own the rounding,
// which browsers clip reliably. The div's styling lives in preview.css /
// ExportManager::buildCss under ".table-wrap".
static QString formatNumericReferenceLinks(QString html, const QString& markdown)
{
    // This runs on every Markdown conversion, so avoid compiling/scanning
    // regexes for the overwhelmingly common document with no definitions.
    if (!markdown.contains(QStringLiteral("]:")))
        return html;

    static const QRegularExpression definition(
        QStringLiteral(R"(^ {0,3}\[([1-9][0-9]*)\]:[ \t]*(?:<[^>\r\n]+>|\S+))"),
        QRegularExpression::MultilineOption);
    QSet<QString> labels;
    QRegularExpressionMatchIterator definitions = definition.globalMatch(markdown);
    while (definitions.hasNext())
        labels.insert(definitions.next().captured(1));
    if (labels.isEmpty())
        return html;
    // Limit the presentation change to shortcut references such as [1], not
    // an unrelated inline link whose visible text happens to be a number.
    static const QRegularExpression citationUse(
        QStringLiteral(R"((?<!!)\[([1-9][0-9]*)\](?![ \t]*(?:\(|:)))"));
    QSet<QString> referencedLabels;
    QRegularExpressionMatchIterator uses = citationUse.globalMatch(markdown);
    while (uses.hasNext())
        referencedLabels.insert(uses.next().captured(1));
    labels.intersect(referencedLabels);
    if (labels.isEmpty())
        return html;

    static const QRegularExpression anchor(
        QStringLiteral(R"(<a\b([^>]*)>(.*?)</a>)"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tags(QStringLiteral(R"(<[^>]+>)"));
    QString formatted;
    int last = 0;
    QRegularExpressionMatchIterator anchors = anchor.globalMatch(html);
    while (anchors.hasNext()) {
        const QRegularExpressionMatch match = anchors.next();
        const QString label = QString(match.captured(2)).remove(tags).trimmed();
        if (!labels.contains(label))
            continue;
        if (formatted.isEmpty())
            formatted.reserve(html.size() + 24 * labels.size());
        formatted += QStringView(html).mid(last, match.capturedStart() - last);
        formatted += QStringLiteral("<sup class=\"reference-link\"><a")
            + match.captured(1) + QStringLiteral(">[") + match.captured(2)
            + QStringLiteral("]</a></sup>");
        last = match.capturedEnd();
    }
    if (formatted.isEmpty())
        return html;
    formatted += QStringView(html).mid(last);
    return formatted;
}

static QString wrapContentTables(const QString& html)
{
    static const QString openTag = QStringLiteral("<table");
    static const QString closeTag = QStringLiteral("</table>");
    static const QString theadMarker = QStringLiteral("<thead>");
    static const QString wrapOpen = QStringLiteral("<div class=\"table-wrap\">");
    static const QString wrapClose = QStringLiteral("</div>");

    if (!html.contains(openTag))
        return html;

    QString out;
    out.reserve(html.size() + 256);
    int pos = 0;
    while (true) {
        // "<table" (not "<table>"): the live preview's <table> tags carry
        // data-src-start/end attributes for source-line mapping.
        const int start = html.indexOf(openTag, pos);
        if (start < 0) { out += html.mid(pos); break; }
        const int closeAt = html.indexOf(closeTag, start);
        if (closeAt < 0) { out += html.mid(pos); break; }
        const int end = closeAt + closeTag.size();
        out += html.mid(pos, start - pos);
        const QString block = html.mid(start, end - start);
        if (block.contains(theadMarker))
            out += wrapOpen + block + wrapClose;
        else
            out += block;
        pos = end;
    }
    return out;
}

// Shared by markdownToHtml() and markdownToPreviewHtml(): substitutes back the math
// snippets FastMdMath::preprocess() tokenized out, normalizes task-list markup, and
// decodes percent-escaped local image paths so they resolve as files. `result` is
// the raw HTML md4c produced (either via md_html() or MarkdownPreviewRenderer).

// Alignment wrappers are authored as blank-line separated HTML divs:
//
//     <div style="text-align: center;">
//
//     ...markdown...
//
//     </div>
//
// md4c already renders that correctly on its own -- the div passes through
// verbatim and the blank-line separated body is parsed as Markdown -- so the
// export path (ExportManager::markdownToHtml) needs no help and must not pay
// for any of this. The live preview is the exception: MarkdownPreviewRenderer
// wraps every raw HTML block in its own <div class="fastmd-raw-html"> source
// mapping container, which closes immediately and swallows the alignment div
// before it can reach the content.
//
// So for the preview only, swap the two wrapper lines for one-line paragraph
// sentinels that the renderer maps like any other paragraph, then restore them
// after parsing. Line count is preserved 1:1, so every data-src-* stays exact.
//
// Only a fully blank-line separated, un-indented pair is tokenized: anything
// else is not a CommonMark HTML block boundary (4+ spaces of indent is a code
// block), so the sentinel would not survive as its own paragraph and would
// leak into the rendered page.
static QString protectAlignmentWrappers(const QString& markdown)
{
    if (!markdown.contains(QLatin1String("<div")))
        return markdown;

    static const QRegularExpression splitter(QStringLiteral(R"(\r?\n)"));
    static const QRegularExpression openAlignment(MarkdownAlignment::openPattern(),
                                                  QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression openAnyDiv(MarkdownAlignment::anyOpenPattern(),
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression closeDiv(MarkdownAlignment::closePattern(),
                                             QRegularExpression::CaseInsensitiveOption);

    QStringList lines = markdown.split(splitter, Qt::KeepEmptyParts);
    const auto isolated = [&lines](int index) {
        const auto blank = [&lines](int i) {
            return i < 0 || i >= lines.size() || lines.at(i).trimmed().isEmpty();
        };
        return blank(index - 1) && blank(index + 1);
    };

    struct Pending { int line; QString alignment; };
    QVector<Pending> stack;
    bool changed = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        if (!line.contains(QLatin1Char('<')))
            continue;
        const QRegularExpressionMatch alignment = openAlignment.match(line);
        if (alignment.hasMatch()) {
            stack.append({i, alignment.captured(1).toLower()});
        } else if (openAnyDiv.match(line).hasMatch()) {
            stack.append({i, QString()});
        } else if (closeDiv.match(line).hasMatch() && !stack.isEmpty()) {
            const Pending open = stack.takeLast();
            // Both wrapper lines must survive md4c as standalone paragraphs;
            // tokenizing only one half would emit an unbalanced div.
            if (!open.alignment.isEmpty() && isolated(open.line) && isolated(i)) {
                lines[open.line] = QStringLiteral("@@FASTMD_ALIGN_START_%1@@").arg(open.alignment);
                lines[i] = QStringLiteral("@@FASTMD_ALIGN_END@@");
                changed = true;
            }
        }
    }
    if (!changed)
        return markdown;
    const QString lineEnding = markdown.contains(QLatin1String("\r\n"))
        ? QStringLiteral("\r\n") : QStringLiteral("\n");
    return lines.join(lineEnding);
}

static QString restoreAlignmentWrappers(QString html)
{
    if (!html.contains(QLatin1String("@@FASTMD_ALIGN_")))
        return html;
    // The preview renderer puts the sentinel's text inside a source-mapped
    // span. Replacing the whole paragraph keeps the generated div a single
    // top-level element, which preview.js's incremental block diff requires.
    static const QRegularExpression open(
        QStringLiteral(R"(<p\b[^>]*>\s*(?:<span\b[^>]*>\s*)?@@FASTMD_ALIGN_START_(left|center|right|justify)@@(?:\s*</span>)?\s*</p>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression close(
        QStringLiteral(R"(<p\b[^>]*>\s*(?:<span\b[^>]*>\s*)?@@FASTMD_ALIGN_END@@(?:\s*</span>)?\s*</p>)"),
        QRegularExpression::CaseInsensitiveOption);
    html.replace(open, QStringLiteral(R"(<div style="text-align: \1;">)"));
    html.replace(close, QStringLiteral("</div>"));
    return html;
}

static QString postProcessMarkdownHtml(QString result, const QStringList& mathSnippets,
                                       const QStringList& extensionSnippets, const QString& markdown,
                                       bool interactiveTasks = false,
                                       bool rendererMarkedLists = false,
                                       const QStringList& literalMathSnippets = {})
{
    // A standalone $$...$$ block renders to a block-level element (table),
    // which md4c wraps in its own <p>. QTextBrowser's limited HTML engine
    // can't render a block element nested inside a <p>, so unwrap it. Only the
    // UnicodePreview path produces <table> snippets, so this stays rare; the
    // bulk substitution below is a single linear pass.
    // One static regex and one linear pass, not a freshly compiled PCRE2 program
    // plus a whole-body replace() per snippet (Rules 13 and 14). Nothing to do at
    // all unless some snippet is a block-level table, which only the
    // UnicodePreview mode ever produces — the live preview never reaches this.
    bool anyTableSnippet = false;
    for (const QString& snippet : mathSnippets) {
        if (snippet.startsWith(QStringLiteral("<table"))) {
            anyTableSnippet = true;
            break;
        }
    }
    if (anyTableSnippet) {
        static const QRegularExpression wrappedMathRe(
            QStringLiteral(R"(<p>\s*@@FASTMD_MATH_(\d+)@@\s*</p>)"));
        QString unwrapped;
        int last = 0;
        QRegularExpressionMatchIterator it = wrappedMathRe.globalMatch(result);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            bool ok = false;
            const int index = match.captured(1).toInt(&ok);
            if (!ok || index < 0 || index >= mathSnippets.size()
                || !mathSnippets.at(index).startsWith(QStringLiteral("<table")))
                continue;
            if (last == 0)
                unwrapped.reserve(result.size());
            unwrapped += QStringView(result).mid(last, match.capturedStart() - last);
            unwrapped += mathSnippets.at(index);
            last = match.capturedEnd();
        }
        if (last > 0) {
            unwrapped += QStringView(result).mid(last);
            result = unwrapped;
        }
    }
    result = substitutePlaceholders(result, QStringLiteral("@@FASTMD_MATH_"), mathSnippets,
                                    literalMathSnippets);
    result = MarkdownExtensions::restore(result, extensionSnippets);
    result = normalizeTaskListHtml(result, markdown, interactiveTasks, rendererMarkedLists);
    result = formatNumericReferenceLinks(result, markdown);
    result = transformCallouts(result);
    result = wrapContentTables(result);
    result = restoreAlignmentWrappers(result);

    // Decode URL-encoded paths in src="..." so QTextDocument/QTextBrowser can find local files with spaces
    if (!result.contains(QStringLiteral("src=\"")))
        return result;
    // Built in one linear pass. The previous version called replace() at an index
    // for every match while a globalMatch iterator held the pre-replacement
    // subject, tracking a running offset by hand — O(n*m) and easy to get wrong.
    static const QRegularExpression srcRe(QStringLiteral("src=\"([^\"]+)\""));
    QRegularExpressionMatchIterator it = srcRe.globalMatch(result);
    QString decodedResult;
    int last = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString srcUrl = match.captured(1);
        if (!srcUrl.contains('%'))
            continue;
        const QString decoded = QUrl::fromPercentEncoding(srcUrl.toUtf8());
        if (decoded == srcUrl)
            continue;
        if (last == 0)
            decodedResult.reserve(result.size());
        decodedResult += QStringView(result).mid(last, match.capturedStart(1) - last);
        decodedResult += decoded;
        last = match.capturedEnd(1);
    }
    if (last > 0) {
        decodedResult += QStringView(result).mid(last);
        return decodedResult;
    }

    return result;
}

// Auto-fix links and images that have spaces or brackets in their unquoted paths
// e.g., ![alt](C:/path with space[1].png) -> ![alt](<C:/path with space[1].png>)
static QString fixUnquotedLinkPaths(QString md)
{
    static const QRegularExpression re(
        QStringLiteral(R"((!?\[[^\]\n]*\])\(([^)<>\"\n]*(?:\s+|\[|\])[^)<>\"\n]*)\))"));
    if (!md.contains(QLatin1Char('[')))
        return md;
    md.replace(re, QStringLiteral("\\1(<\\2>)"));
    return md;
}

// Inject heading IDs sequentially into the generated HTML h1-h6 tags
static QString injectHeadingIds(QString html, const QStringList& slugs)
{
    if (slugs.isEmpty())
        return html;
    static const QRegularExpression hTagRe(QStringLiteral("<h([1-6])(\\b[^>]*)>"),
                                           QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = hTagRe.globalMatch(html);

    QString result;
    result.reserve(html.size() + slugs.size() * 20);
    int lastPos = 0;
    int slugIdx = 0;

    while (it.hasNext() && slugIdx < slugs.size()) {
        QRegularExpressionMatch match = it.next();
        int start = match.capturedStart();
        int end = match.capturedEnd();

        result.append(html.mid(lastPos, start - lastPos));

        QString level = match.captured(1);
        QString attrs = match.captured(2);

        if (!attrs.contains(QStringLiteral("id="))) {
            result.append(QStringLiteral("<h%1 id=\"%2\"%3>").arg(level, slugs.at(slugIdx), attrs));
        } else {
            result.append(match.captured(0));
        }

        lastPos = end;
        slugIdx++;
    }
    result.append(html.mid(lastPos));
    return result;
}

} // namespace

bool ExportManager::renderSingleLineBreaksEnabled()
{
    QSettings settings;
    return settings.value(QStringLiteral("editor/renderSingleLineBreaks"), true).toBool();
}

// ---------------------------------------------------------------------------
QString ExportManager::markdownToHtml(const QString& markdown, MathRenderMode mode, bool* mathDetected,
                                      bool renderSingleLineBreaks)
{
    const QString cleanMarkdown = stripYamlFrontMatter(markdown);
    const MarkdownToc::Result toc = MarkdownToc::analyze(cleanMarkdown);
    QStringList mathSnippets;
    QStringList literalMathSnippets;
    const QString normalizedMarkdown = normalizeEscapedCalloutMarkers(toc.expandedMarkdown);
    const QString mathPreprocessed = FastMdMath::preprocess(normalizedMarkdown, mode, &mathSnippets,
                                                          nullptr, &literalMathSnippets);
    QStringList extensionSnippets;
    const QString preprocessed = fixUnquotedLinkPaths(
        MarkdownExtensions::preprocess(mathPreprocessed, &extensionSnippets));
    if (mathDetected)
        *mathDetected = !mathSnippets.isEmpty();
    QByteArray utf8 = preprocessed.toUtf8();
    QString result;

    auto cb = [](const MD_CHAR* out, MD_SIZE sz, void* ud) {
        static_cast<QString*>(ud)->append(QString::fromUtf8(out, static_cast<int>(sz)));
    };

    unsigned flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;
    if (renderSingleLineBreaks)
        flags |= MD_FLAG_HARD_SOFT_BREAKS;
    md_html(utf8.constData(), static_cast<MD_SIZE>(utf8.size()), cb, &result, flags, 0);

    QString html = postProcessMarkdownHtml(result, mathSnippets, extensionSnippets, cleanMarkdown,
                                          false, false, literalMathSnippets);
    return injectHeadingIds(html, toc.slugs);
}

// ---------------------------------------------------------------------------
QString ExportManager::markdownToPreviewHtml(const QString& markdown, bool* mathDetected,
                                             bool renderSingleLineBreaks)
{
    const FrontMatter frontMatter = findYamlFrontMatter(markdown);
    const QString cleanMarkdown = frontMatter.lineCount == 0
        ? markdown : markdown.mid(frontMatter.bodyOffset);
    const QString alignmentProtected = protectAlignmentWrappers(cleanMarkdown);
    const MarkdownToc::Result toc = MarkdownToc::analyze(alignmentProtected);
    QStringList mathSnippets;
    QStringList literalMathSnippets;
    QVector<MathLineShift> lineShifts;
    const QString normalizedMarkdown = normalizeEscapedCalloutMarkers(toc.expandedMarkdown);
    const QString mathPreprocessed = FastMdMath::preprocess(normalizedMarkdown,
        MathRenderMode::KatexOutput, &mathSnippets, &lineShifts, &literalMathSnippets);
    // Carry original source provenance through each line-changing preprocessing
    // pass, including stripped front matter, TOC expansion, and math collapse.
    QVector<int> tocLineOrigins = toc.expandedLineOrigins;
    if (frontMatter.lineCount > 0) {
        for (int& line : tocLineOrigins)
            line += frontMatter.lineCount;
    }
    QVector<int> mathLineOrigins = mapMathLineOrigins(lineCount(mathPreprocessed), tocLineOrigins,
                                                       lineShifts);
    QStringList extensionSnippets;
    QVector<int> finalLineOrigins;
    const QString preprocessed = fixUnquotedLinkPaths(
        MarkdownExtensions::preprocess(mathPreprocessed, &extensionSnippets,
                                       &mathLineOrigins, &finalLineOrigins, true));
    if (mathDetected)
        *mathDetected = !mathSnippets.isEmpty();
    QByteArray utf8 = preprocessed.toUtf8();

    unsigned flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;
    if (renderSingleLineBreaks)
        flags |= MD_FLAG_HARD_SOFT_BREAKS;
    QString result = MarkdownPreviewRenderer::render(utf8, flags, {}, finalLineOrigins);

    QString html = postProcessMarkdownHtml(result, mathSnippets, extensionSnippets, cleanMarkdown,
                                          true, true, literalMathSnippets);
    return injectHeadingIds(html, toc.slugs);
}
