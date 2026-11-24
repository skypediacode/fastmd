#include "MarkdownExtensions.h"
#include "MathDelimiters.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include "md4c-html.h"

namespace {

bool isFenceLine(const QString& line, QChar* character, int* length);

const QHash<QString, QString>& emojiNames()
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("rocket"), QStringLiteral("🚀")},
        {QStringLiteral("warning"), QStringLiteral("⚠️")},
        {QStringLiteral("smile"), QStringLiteral("😄")},
        {QStringLiteral("grinning"), QStringLiteral("😀")},
        {QStringLiteral("grin"), QStringLiteral("😁")},
        {QStringLiteral("joy"), QStringLiteral("😂")},
        {QStringLiteral("laughing"), QStringLiteral("😆")},
        {QStringLiteral("wink"), QStringLiteral("😉")},
        {QStringLiteral("heart"), QStringLiteral("❤️")},
        {QStringLiteral("+1"), QStringLiteral("👍")},
        {QStringLiteral("thumbsup"), QStringLiteral("👍")},
        {QStringLiteral("-1"), QStringLiteral("👎")},
        {QStringLiteral("thumbsdown"), QStringLiteral("👎")},
        {QStringLiteral("tada"), QStringLiteral("🎉")},
        {QStringLiteral("fire"), QStringLiteral("🔥")},
        {QStringLiteral("sparkles"), QStringLiteral("✨")},
        {QStringLiteral("star"), QStringLiteral("⭐")},
        {QStringLiteral("white_check_mark"), QStringLiteral("✅")},
        {QStringLiteral("x"), QStringLiteral("❌")},
        {QStringLiteral("eyes"), QStringLiteral("👀")},
        {QStringLiteral("thinking"), QStringLiteral("🤔")},
        {QStringLiteral("sob"), QStringLiteral("😭")},
        {QStringLiteral("cry"), QStringLiteral("😢")},
        {QStringLiteral("angry"), QStringLiteral("😠")},
        {QStringLiteral("confused"), QStringLiteral("😕")},
        {QStringLiteral("sunglasses"), QStringLiteral("😎")},
        {QStringLiteral("clap"), QStringLiteral("👏")},
        {QStringLiteral("pray"), QStringLiteral("🙏")},
        {QStringLiteral("wave"), QStringLiteral("👋")},
        {QStringLiteral("ok_hand"), QStringLiteral("👌")},
        {QStringLiteral("muscle"), QStringLiteral("💪")},
        {QStringLiteral("coffee"), QStringLiteral("☕")},
        {QStringLiteral("bug"), QStringLiteral("🐛")},
        {QStringLiteral("memo"), QStringLiteral("📝")},
        {QStringLiteral("book"), QStringLiteral("📖")},
        {QStringLiteral("bulb"), QStringLiteral("💡")},
        {QStringLiteral("gear"), QStringLiteral("⚙️")},
        {QStringLiteral("lock"), QStringLiteral("🔒")},
        {QStringLiteral("key"), QStringLiteral("🔑")},
        {QStringLiteral("folder"), QStringLiteral("📁")},
        {QStringLiteral("computer"), QStringLiteral("💻")},
        {QStringLiteral("phone"), QStringLiteral("📱")},
        {QStringLiteral("globe"), QStringLiteral("🌍")},
        {QStringLiteral("sunny"), QStringLiteral("☀️")},
        {QStringLiteral("crescent_moon"), QStringLiteral("🌙")},
        {QStringLiteral("construction"), QStringLiteral("🚧")},
        {QStringLiteral("package"), QStringLiteral("📦")},
        {QStringLiteral("email"), QStringLiteral("✉️")},
        {QStringLiteral("link"), QStringLiteral("🔗")}
    };
    return names;
}

QString token(int index)
{
    return QStringLiteral("@@FASTMD_EXT_%1@@").arg(index);
}

QString replaceDelimited(QString text, const QRegularExpression& expression,
                         const QString& opening, const QString& closing,
                         QStringList* snippets)
{
    QRegularExpressionMatchIterator it = expression.globalMatch(text);
    if (!it.hasNext())
        return text;

    QString result;
    int last = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += text.mid(last, match.capturedStart() - last);
        const int openingIndex = snippets->size();
        snippets->append(opening);
        result += token(openingIndex) + match.captured(1);
        const int closingIndex = snippets->size();
        snippets->append(closing);
        result += token(closingIndex);
        last = match.capturedEnd();
    }
    result += text.mid(last);
    return result;
}

QString replaceEmoji(QString text)
{
    static const QRegularExpression emojiRe(QStringLiteral(R"re(:(?![0-9]+:)([A-Za-z0-9_+\-]+):)re"));
    QRegularExpressionMatchIterator it = emojiRe.globalMatch(text);
    if (!it.hasNext())
        return text;

    QString result;
    int last = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += text.mid(last, match.capturedStart() - last);
        const auto found = emojiNames().constFind(match.captured(1));
        result += found == emojiNames().cend() ? match.captured(0) : found.value();
        last = match.capturedEnd();
    }
    result += text.mid(last);
    return result;
}

QString transformText(QString text, QStringList* snippets)
{
    // transformText() runs once per plain-text run of every line, so these must
    // stay static: constructing a QRegularExpression compiles (and JITs) a PCRE2
    // program, which dominated preview rendering on large documents.
    static const QRegularExpression markRe(
        QStringLiteral(R"re((?<![=])==([^=\r\n]+)==(?![=]))re"));
    static const QRegularExpression supRe(
        QStringLiteral(R"re((?<!\^)\^([^\^\r\n]+)\^(?!\^))re"));
    static const QRegularExpression subRe(
        QStringLiteral(R"re((?<!~)~([^~\r\n]+)~(?!~))re"));

    // Cheap character probes first: most lines contain none of these markers,
    // and skipping the match avoids walking the run three times.
    if (text.contains(QLatin1Char('=')))
        text = replaceDelimited(text, markRe,
                                QStringLiteral("<mark>"), QStringLiteral("</mark>"), snippets);
    if (text.contains(QLatin1Char('^')))
        text = replaceDelimited(text, supRe,
                                QStringLiteral("<sup>"), QStringLiteral("</sup>"), snippets);
    if (text.contains(QLatin1Char('~')))
        text = replaceDelimited(text, subRe,
                                QStringLiteral("<sub>"), QStringLiteral("</sub>"), snippets);
    return text.contains(QLatin1Char(':')) ? replaceEmoji(text) : text;
}

QString transformLine(const QString& line, QStringList* snippets)
{
    QString result;
    int position = 0;
    while (position < line.size()) {
        int start = -1;
        int end = -1;
        if (line.mid(position, 2) == QStringLiteral("[^")) {
            const int closingBracket = line.indexOf(QLatin1Char(']'), position + 2);
            if (closingBracket >= 0)
                end = closingBracket + 1;
        } else if (line.at(position) == QLatin1Char('`')) {
            int runLength = 1;
            while (position + runLength < line.size()
                   && line.at(position + runLength) == QLatin1Char('`'))
                ++runLength;
            const QString fence(runLength, QLatin1Char('`'));
            end = line.indexOf(fence, position + runLength);
            if (end >= 0)
                end += runLength;
        } else if (line.at(position) == QLatin1Char('$')
                   && (position == 0 || line.at(position - 1) != QLatin1Char('\\'))) {
            const bool display = position + 1 < line.size()
                && line.at(position + 1) == QLatin1Char('$');
            const QString delimiter = display ? QStringLiteral("$$") : QStringLiteral("$");
            const int contentStart = position + delimiter.size();
            int close = display ? contentStart
                : int(FastMdMath::findInlineDollarClose(line, position, line.size()));
            while (display && close < line.size()) {
                close = line.indexOf(delimiter, close);
                if (close < 0 || close == 0 || line.at(close - 1) != QLatin1Char('\\'))
                    break;
                ++close;
            }
            if (close >= contentStart)
                end = close + delimiter.size();
        } else if (line.mid(position, 2) == QStringLiteral("\\\\(")
                   || line.mid(position, 2) == QStringLiteral("\\\\[")) {
            const QString close = line.at(position + 1) == QLatin1Char('(')
                ? QStringLiteral("\\\\)") : QStringLiteral("\\\\]");
            const int closePos = line.indexOf(close, position + 2);
            if (closePos >= 0)
                end = closePos + close.size();
        }

        if (end > position) {
            result += line.mid(position, end - position);
            position = end;
        } else {
            start = position;
            while (position < line.size()) {
                const QChar ch = line.at(position);
                if (ch == QLatin1Char('`') || ch == QLatin1Char('$')
                    || (ch == QLatin1Char('\\') && position + 1 < line.size()
                        && (line.at(position + 1) == QLatin1Char('(')
                            || line.at(position + 1) == QLatin1Char('['))))
                    break;
                ++position;
            }
            if (position == start)
                ++position;
            result += transformText(line.mid(start, position - start), snippets);
        }
    }
    return result;
}

QString blockquoteContent(const QString& line)
{
    int pos = 0;
    while (pos < line.size() && (line.at(pos) == QLatin1Char(' ') || line.at(pos) == QLatin1Char('\t')))
        ++pos;
    bool quoted = false;
    while (pos < line.size() && line.at(pos) == QLatin1Char('>')) {
        quoted = true;
        ++pos;
        while (pos < line.size() && (line.at(pos) == QLatin1Char(' ') || line.at(pos) == QLatin1Char('\t')))
            ++pos;
    }
    return quoted ? line.mid(pos) : line;
}

QString renderMarkdownFragment(const QString& markdown)
{
    QByteArray utf8 = markdown.toUtf8();
    QString html;
    auto cb = [](const MD_CHAR* out, MD_SIZE size, void* userData) {
        static_cast<QString*>(userData)->append(QString::fromUtf8(out, static_cast<int>(size)));
    };
    md_html(utf8.constData(), MD_SIZE(utf8.size()), cb, &html,
            MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS, 0);
    return html;
}

QString renderInline(const QString& markdown)
{
    QString html = renderMarkdownFragment(markdown).trimmed();
    if (html.startsWith(QStringLiteral("<p>")) && html.endsWith(QStringLiteral("</p>")))
        html = html.mid(3, html.size() - 7);
    return html;
}

QString appendFootnoteBacklinks(QString bodyHtml, const QString& id, int referenceCount)
{
    QString backlinks = QStringLiteral("<span class=\"footnote-backlinks\">");
    for (int occurrence = 1; occurrence <= referenceCount; ++occurrence) {
        const QString marker = referenceCount == 1
            ? QStringLiteral("↩")
            : QStringLiteral("↩<sup>%1</sup>").arg(occurrence);
        backlinks += QStringLiteral(" <a class=\"footnote-backref\" href=\"#fnref-%1-%2\" aria-label=\"Back to reference %2\" title=\"Back to reference %2\">%3</a>")
            .arg(id, QString::number(occurrence), marker);
    }
    backlinks += QStringLiteral("</span>");

    // Put navigation after the final rendered block, so multi-paragraph notes
    // and lists read naturally and backlinks never interrupt their content.
    static const QRegularExpression finalBlockEnd(
        QStringLiteral(R"(</(p|ul|ol|pre|blockquote|table|div)>\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = finalBlockEnd.match(bodyHtml);
    if (match.hasMatch()) {
        const QString tag = match.captured(1).toLower();
        if (tag == QStringLiteral("p"))
            bodyHtml.insert(match.capturedStart(), backlinks);
        else
            bodyHtml.insert(match.capturedEnd(), backlinks);
    } else {
        bodyHtml += backlinks;
    }
    return bodyHtml;
}

bool isFootnoteDefinition(const QString& line, QString* id, QString* body)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s{0,3}\[\^([A-Za-z0-9][A-Za-z0-9_.-]*)\]:\s*(.*)$)"));
    const auto match = re.match(line);
    if (!match.hasMatch())
        return false;
    *id = match.captured(1);
    *body = match.captured(2);
    return true;
}

bool isDefinitionLine(const QString& line, QString* body)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s{0,3}:\s+(.+)$)"));
    const auto match = re.match(line);
    if (!match.hasMatch())
        return false;
    *body = match.captured(1);
    return true;
}

bool isPlainDefinitionTerm(const QString& line)
{
    const QString term = line.trimmed();
    if (term.isEmpty() || term.startsWith(QLatin1Char(':')) || term.startsWith(QLatin1Char('#')) || term.startsWith(QLatin1Char('>'))
        || term.startsWith(QLatin1Char('-')) || term.startsWith(QLatin1Char('*'))
        || term.startsWith(QLatin1Char('+')) || term.endsWith(QLatin1Char(':')))
        return false;
    return !term.contains(QStringLiteral("```")) && !term.contains(QStringLiteral("~~~"));
}

QStringList lowerExtensions(const QString& markdown, QStringList* snippets,
                            const QVector<int>* inputLineOrigins,
                            QVector<int>* outputLineOrigins, bool sourceMapping)
{
    const QStringList lines = markdown.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    QStringList output;
    output.reserve(lines.size() + 8);
    QHash<QString, QString> footnotes;
    QStringList footnoteOrder;
    QHash<QString, int> referenceCounts;
    QHash<QString, int> footnoteOrigins;
    QSet<QString> referenced;
    QChar fenceCharacter;
    int fenceLength = 0;
    auto originForInputLine = [&](int line) {
        return inputLineOrigins && line >= 0 && line < inputLineOrigins->size()
            ? inputLineOrigins->at(line) : line + 1;
    };
    auto appendOutput = [&](const QString& text, int origin) {
        output.append(text);
        if (outputLineOrigins)
            outputLineOrigins->append(origin);
    };

    for (int i = 0; i < lines.size();) {
        const QString& line = lines.at(i);
        QChar lineFence;
        int lineFenceLength = 0;
        if (isFenceLine(line, &lineFence, &lineFenceLength)) {
            if (fenceLength == 0) { fenceCharacter = lineFence; fenceLength = lineFenceLength; }
            else if (lineFence == fenceCharacter && lineFenceLength >= fenceLength) fenceLength = 0;
            appendOutput(line, originForInputLine(i));
            ++i;
            continue;
        }
        if (fenceLength != 0) { appendOutput(line, originForInputLine(i)); ++i; continue; }

        // MD4C's <div> HTML blocks end at a blank line, not at </div>.
        // FastMD's standalone Page Break must therefore be isolated on both
        // sides before parsing. These blanks exist only in parser input; give
        // them the marker's provenance so following source lines never drift.
        // Fences above remain opaque, and four-space indentation remains code.
        if (MarkdownExtensions::isPageBreakLine(line)) {
            const int origin = originForInputLine(i);
            if (!output.isEmpty() && !output.last().trimmed().isEmpty())
                appendOutput(QString(), origin);
            appendOutput(line, origin);
            if (i + 1 < lines.size() && !lines.at(i + 1).trimmed().isEmpty())
                appendOutput(QString(), origin);
            ++i;
            continue;
        }

        QString id, body;
        if (isFootnoteDefinition(line, &id, &body)) {
            footnoteOrigins[id] = originForInputLine(i);
            footnotes[id] = body;
            ++i;
            while (i < lines.size()) {
                const QString continuation = lines.at(i);
                QString ignoredId, ignoredBody;
                if (isFootnoteDefinition(continuation, &ignoredId, &ignoredBody)) break;
                if (continuation.trimmed().isEmpty()) { body += QLatin1Char('\n'); ++i; continue; }
                int spaces = 0;
                while (spaces < continuation.size() && continuation.at(spaces) == QLatin1Char(' ')) ++spaces;
                if (spaces < 4) break;
                body += QLatin1Char('\n') + continuation.mid(4);
                ++i;
            }
            footnotes[id] = body;
            continue;
        }

        QString definition;
        // A definition list can only open on a term line that directly follows a
        // blank line. Testing those two cheap conditions before the forward scan
        // keeps ordinary prose linear: otherwise every line of a long paragraph
        // re-scans the rest of that paragraph.
        const bool couldOpenDefinitionList =
            (i == 0 || lines.at(i - 1).trimmed().isEmpty()) && isPlainDefinitionTerm(line);
        int firstDefinitionLine = i + 1;
        if (couldOpenDefinitionList) {
            while (firstDefinitionLine < lines.size() && !lines.at(firstDefinitionLine).trimmed().isEmpty()
                   && isPlainDefinitionTerm(lines.at(firstDefinitionLine)))
                ++firstDefinitionLine;
        }
        if (couldOpenDefinitionList && firstDefinitionLine < lines.size()
            && isDefinitionLine(lines.at(firstDefinitionLine), &definition)) {
            const int definitionStart = i;
            QStringList terms;
            QVector<int> termOrigins;
            terms.append(line.trimmed());
            termOrigins.append(originForInputLine(i));
            ++i;
            while (i < firstDefinitionLine) {
                terms.append(lines.at(i).trimmed());
                termOrigins.append(originForInputLine(i));
                ++i;
            }
            QStringList definitions;
            QVector<int> definitionOrigins;
            definitions.append(definition);
            definitionOrigins.append(originForInputLine(firstDefinitionLine));
            ++i;
            auto appendContinuation = [&]() {
                while (i < lines.size() && !lines.at(i).trimmed().isEmpty()) {
                    const QString continuation = lines.at(i);
                    int spaces = 0;
                    while (spaces < continuation.size() && continuation.at(spaces) == QLatin1Char(' ')) ++spaces;
                    if (spaces < 4) break;
                    definitions.last() += QLatin1Char('\n') + continuation.mid(4);
                    ++i;
                }
            };
            appendContinuation();
            while (i < lines.size()) {
                if (isPlainDefinitionTerm(lines.at(i)) && i + 1 < lines.size()
                    && isDefinitionLine(lines.at(i + 1), &definition)) {
                    terms.append(lines.at(i).trimmed());
                    termOrigins.append(originForInputLine(i));
                    ++i;
                    definitions.append(definition);
                    definitionOrigins.append(originForInputLine(i));
                    ++i;
                    appendContinuation();
                } else if (i + 1 < lines.size() && isDefinitionLine(lines.at(i), &definition)) {
                    definitions.append(definition);
                    definitionOrigins.append(originForInputLine(i));
                    ++i;
                    appendContinuation();
                } else break;
            }
            QString block = sourceMapping
                ? QStringLiteral("<dl data-src-start=\"%1\">\n").arg(originForInputLine(definitionStart))
                : QStringLiteral("<dl>\n");
            for (int term = 0; term < terms.size(); ++term) {
                block += sourceMapping
                    ? QStringLiteral("<dt data-src-line=\"%1\">%2</dt>\n")
                        .arg(termOrigins.at(term)).arg(renderInline(terms.at(term)))
                    : QStringLiteral("<dt>%1</dt>\n").arg(renderInline(terms.at(term)));
            }
            for (int def = 0; def < definitions.size(); ++def) {
                block += sourceMapping
                    ? QStringLiteral("<dd data-src-line=\"%1\">%2</dd>\n")
                        .arg(definitionOrigins.at(def)).arg(renderMarkdownFragment(definitions.at(def)))
                    : QStringLiteral("<dd>%1</dd>\n").arg(renderMarkdownFragment(definitions.at(def)));
            }
            block += QStringLiteral("</dl>");
            appendOutput(block, originForInputLine(definitionStart));
            continue;
        }

        appendOutput(line, originForInputLine(i));
        ++i;
    }

    // Resolve references in source order. The scanner is line-oriented and skips
    // fenced code; inline-code spans are copied byte-for-byte.
    static const QRegularExpression refRe(QStringLiteral(R"(\[\^([A-Za-z0-9][A-Za-z0-9_.-]*)\])"));
    bool inOutputFence = false;
    QChar outputFenceCharacter;
    int outputFenceLength = 0;
    // Nothing in this pass does anything when the document defines no footnotes:
    // every rewrite is conditional on the reference naming a known one.
    for (int line = 0; !footnotes.isEmpty() && line < output.size(); ++line) {
        QChar fenceLineCharacter;
        int fenceLineLength = 0;
        if (isFenceLine(output.at(line), &fenceLineCharacter, &fenceLineLength)) {
            if (!inOutputFence) {
                inOutputFence = true;
                outputFenceCharacter = fenceLineCharacter;
                outputFenceLength = fenceLineLength;
            } else if (fenceLineCharacter == outputFenceCharacter && fenceLineLength >= outputFenceLength) {
                inOutputFence = false;
            }
            continue;
        }
        if (inOutputFence || output.at(line).startsWith(QStringLiteral("<dl>"))) continue;
        const QString text = output.at(line);
        // A reference always contains "[^"; without this probe every line paid a
        // regex match per character.
        if (!text.contains(QLatin1String("[^"))) continue;
        QString transformed;
        transformed.reserve(text.size() + 64);
        int pos = 0;
        bool inCode = false;
        // The match is hoisted out of the per-character walk: re-matching from
        // every position made this O(line length^2) in PCRE2 calls. A cached
        // match stays valid until the cursor passes it.
        QRegularExpressionMatch match = refRe.match(text, 0);
        while (pos < text.size()) {
            if (text.at(pos) == QLatin1Char('`')) { inCode = !inCode; transformed += text.at(pos++); continue; }
            if (!inCode) {
                if (match.hasMatch() && match.capturedStart() < pos)
                    match = refRe.match(text, pos);
                if (match.hasMatch() && footnotes.contains(match.captured(1))) {
                    const QString noteId = match.captured(1);
                    if (!referenced.contains(noteId)) { referenced.insert(noteId); footnoteOrder.append(noteId); }
                    const int number = footnoteOrder.indexOf(noteId) + 1;
                    const int occurrence = ++referenceCounts[noteId];
                    transformed += text.mid(pos, match.capturedStart() - pos);
                    transformed += QStringLiteral("<sup id=\"fnref-%1-%2\"><a href=\"#fn-%1\">[%3]</a></sup>")
                        .arg(noteId, QString::number(occurrence), QString::number(number));
                    pos = match.capturedEnd();
                    continue;
                }
            }
            transformed += text.at(pos++);
        }
        output[line] = transformed;
    }

    if (!footnoteOrder.isEmpty()) {
        const int firstOrigin = footnoteOrigins.value(footnoteOrder.first(), 1);
        appendOutput(QString(), firstOrigin);
        appendOutput(QStringLiteral("<section class=\"footnotes\"><ol>"), firstOrigin);
        for (int index = 0; index < footnoteOrder.size(); ++index) {
            const QString id = footnoteOrder.at(index);
            const QString body = appendFootnoteBacklinks(
                renderMarkdownFragment(footnotes.value(id)), id, referenceCounts.value(id));
            const QString footnoteItem = sourceMapping
                ? QStringLiteral("<li id=\"fn-%1\" data-src-start=\"%2\">%3</li>")
                    .arg(id, QString::number(footnoteOrigins.value(id, firstOrigin)), body)
                : QStringLiteral("<li id=\"fn-%1\">%2</li>").arg(id, body);
            appendOutput(footnoteItem,
                         footnoteOrigins.value(id, firstOrigin));
        }
        appendOutput(QStringLiteral("</ol></section>"),
                     footnoteOrigins.value(footnoteOrder.last(), firstOrigin));
    }

    // Returned as lines, not as one joined body: the only caller immediately
    // splits it again, and joining + re-splitting is two extra whole-document
    // passes per refresh. A few emitted blocks (the <dl> lists) contain their own
    // newlines, so they are flattened here — this is exactly what the old
    // join('\n').split('\n', KeepEmptyParts) round trip produced.
    QStringList flattened;
    flattened.reserve(output.size());
    QVector<int> flattenedOrigins;
    flattenedOrigins.reserve(output.size());
    for (int index = 0; index < output.size(); ++index) {
        const QString& element = output.at(index);
        const int origin = outputLineOrigins && index < outputLineOrigins->size()
            ? outputLineOrigins->at(index) : index + 1;
        if (element.contains(QLatin1Char('\n'))) {
            const QStringList parts = element.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
            flattened += parts;
            for (int part = 0; part < parts.size(); ++part)
                flattenedOrigins.append(origin);
        } else {
            flattened.append(element);
            flattenedOrigins.append(origin);
        }
    }
    if (outputLineOrigins)
        *outputLineOrigins = flattenedOrigins;
    return flattened;
}

bool isFenceLine(const QString& line, QChar* character, int* length)
{
    const QString trimmed = line.trimmed();
    if (trimmed.size() < 3 || (trimmed.front() != QLatin1Char('`') && trimmed.front() != QLatin1Char('~')))
        return false;
    const QChar c = trimmed.front();
    int count = 0;
    while (count < trimmed.size() && trimmed.at(count) == c)
        ++count;
    if (count < 3)
        return false;
    *character = c;
    *length = count;
    return true;
}

} // namespace

namespace MarkdownExtensions {

QString preprocess(const QString& markdown, QStringList* htmlSnippets,
                   const QVector<int>* inputLineOrigins,
                   QVector<int>* outputLineOrigins, bool sourceMapping)
{
    if (!htmlSnippets)
        return markdown;
    htmlSnippets->clear();
    QVector<int> loweredOrigins;
    QVector<int>* loweredOriginsOut = outputLineOrigins ? &loweredOrigins : nullptr;
    const QStringList lines = lowerExtensions(markdown, htmlSnippets,
                                              outputLineOrigins ? inputLineOrigins : nullptr,
                                              loweredOriginsOut, sourceMapping);
    QStringList output;
    output.reserve(lines.size());
    QChar fenceCharacter;
    int fenceLength = 0;
    QString displayMathClose;
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString& line = lines.at(lineIndex);
        QChar lineFence;
        int lineFenceLength = 0;
        if (isFenceLine(line, &lineFence, &lineFenceLength)) {
            if (fenceLength == 0) {
                fenceCharacter = lineFence;
                fenceLength = lineFenceLength;
            } else if (lineFence == fenceCharacter && lineFenceLength >= fenceLength) {
                fenceLength = 0;
            }
            output.append(line);
        } else if (fenceLength != 0) {
            output.append(line);
        } else if (!displayMathClose.isEmpty()) {
            output.append(line);
            if (blockquoteContent(line).trimmed() == displayMathClose)
                displayMathClose.clear();
        } else {
            const QString mathLine = blockquoteContent(line).trimmed();
            if (mathLine == QStringLiteral("$$")) {
                output.append(line);
                displayMathClose = QStringLiteral("$$");
            } else if (mathLine == QStringLiteral("\\\\[")) {
                output.append(line);
                displayMathClose = QStringLiteral("\\\\]");
            } else {
                output.append(transformLine(line, htmlSnippets));
            }
        }
    }
    if (outputLineOrigins)
        *outputLineOrigins = loweredOrigins;
    return output.join(QLatin1Char('\n'));
}

QString restore(QString html, const QStringList& htmlSnippets)
{
    // One linear scan rather than one full-body replace() per snippet, which is
    // quadratic on documents with many highlight/sub/sup/emoji spans.
    if (htmlSnippets.isEmpty())
        return html;
    static const QString prefix = QStringLiteral("@@FASTMD_EXT_");
    int cursor = html.indexOf(prefix);
    if (cursor < 0)
        return html;

    QString out;
    out.reserve(html.size() + 64);
    int last = 0;
    while (cursor >= 0) {
        const int digitsStart = cursor + prefix.size();
        int scan = digitsStart;
        while (scan < html.size() && html.at(scan).isDigit())
            ++scan;
        const bool closed = scan > digitsStart && scan + 1 < html.size()
            && html.at(scan) == QLatin1Char('@') && html.at(scan + 1) == QLatin1Char('@');
        bool ok = false;
        const int index = closed
            ? QStringView(html).mid(digitsStart, scan - digitsStart).toInt(&ok) : -1;
        if (closed && ok && index >= 0 && index < htmlSnippets.size()) {
            out += QStringView(html).mid(last, cursor - last);
            out += htmlSnippets.at(index);
            last = scan + 2;
            cursor = html.indexOf(prefix, last);
        } else {
            cursor = html.indexOf(prefix, cursor + prefix.size());
        }
    }
    out += QStringView(html).mid(last);
    return out;
}

bool isPageBreakLine(const QString& line)
{
    if (!line.contains(QLatin1String("break-after"), Qt::CaseInsensitive))
        return false;
    static const QRegularExpression pageBreakLine(
        QStringLiteral(R"(^ {0,3}<div\b(?=[^>]*\bstyle\s*=\s*["'][^"']*\bbreak-after\s*:\s*page\s*(?=;|["']))[^>]*>[ \t]*</div\s*>[ \t\r]*$)"),
        QRegularExpression::CaseInsensitiveOption);
    return pageBreakLine.match(line).hasMatch();
}

} // namespace MarkdownExtensions
