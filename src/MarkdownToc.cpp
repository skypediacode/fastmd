#include "MarkdownToc.h"

#include <QMutex>
#include <QRegularExpression>
#include <QSet>

namespace MarkdownToc {
namespace {

bool isPlaceholder(const QString& line)
{
    const QString trimmed = line.trimmed();
    return trimmed == QStringLiteral("[TOC]");
}

// Compiled once: cleanTitle()/slugify() run per heading, and building a
// QRegularExpression compiles a PCRE2 program each time.
static const QRegularExpression& inlineLinkRe()
{
    static const QRegularExpression re(QStringLiteral(R"(\[([^\]]+)\]\([^)]+\))"));
    return re;
}

QString cleanTitle(QString title)
{
    if (!title.contains(QLatin1Char('[')))
        return title.trimmed();
    static const QRegularExpression imageRe(QStringLiteral(R"(!\[[^\]]*\]\([^)]+\))"));
    title.replace(inlineLinkRe(), QStringLiteral("\\1"));
    title.replace(imageRe, QString());
    return title.trimmed();
}

QString stripBlockquotePrefix(const QString& line)
{
    int pos = 0;
    int indentation = 0;
    while (pos < line.size() && (line.at(pos) == QLatin1Char(' ') || line.at(pos) == QLatin1Char('\t'))
           && indentation < 4) {
        ++pos;
        ++indentation;
    }
    if (indentation >= 4 || pos >= line.size() || line.at(pos) != QLatin1Char('>'))
        return line;
    while (pos < line.size() && line.at(pos) == QLatin1Char('>')) {
        ++pos;
        while (pos < line.size() && (line.at(pos) == QLatin1Char(' ') || line.at(pos) == QLatin1Char('\t')))
            ++pos;
    }
    return line.mid(pos);
}

QString slugify(const QString& text, QSet<QString>& usedSlugs)
{
    static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
    QString plain = text;
    if (plain.contains(QLatin1Char('<')))
        plain.remove(tagRe);
    if (plain.contains(QLatin1Char('[')))
        plain.replace(inlineLinkRe(), QStringLiteral("\\1"));
    plain.remove(QLatin1Char('*'));
    plain.remove(QLatin1Char('_'));
    plain.remove(QLatin1Char('~'));
    plain.remove(QLatin1Char('`'));

    QString slug;
    slug.reserve(plain.size());
    bool lastWasHyphen = false;
    for (const QChar ch : plain) {
        if (ch.isLetterOrNumber()) {
            slug += ch.toLower();
            lastWasHyphen = false;
        } else if (ch == '-' || ch == '_') {
            slug += ch;
            lastWasHyphen = false;
        } else if (!slug.isEmpty() && !lastWasHyphen) {
            slug += QLatin1Char('-');
            lastWasHyphen = true;
        }
    }
    if (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    if (slug.isEmpty())
        slug = QStringLiteral("section");

    QString uniqueSlug = slug;
    int counter = 1;
    while (usedSlugs.contains(uniqueSlug))
        uniqueSlug = slug + QStringLiteral("-%1").arg(counter++);
    usedSlugs.insert(uniqueSlug);
    return uniqueSlug;
}

} // namespace

static Result analyzeUncached(const QString& markdown)
{
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    static const QRegularExpression headingRe(QStringLiteral(R"(^(#{1,6})\s+(.+)$)"));

    Result result;
    result.expandedMarkdown = markdown;
    result.expandedLineOrigins.reserve(lines.size());
    for (int i = 0; i < lines.size(); ++i)
        result.expandedLineOrigins.append(i + 1);
    QSet<QString> usedSlugs;
    QString codeFence;
    bool inCodeBlock = false;

    for (int lineNumber = 0; lineNumber < lines.size(); ++lineNumber) {
        const QString& line = lines.at(lineNumber);
        const QString trimmed = line.trimmed();

        if (!inCodeBlock) {
            if (trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~"))) {
                inCodeBlock = true;
                codeFence = trimmed.left(3);
            }
        } else if (trimmed.startsWith(codeFence)) {
            inCodeBlock = false;
        }

        if (inCodeBlock)
            continue;

        if (isPlaceholder(line))
            result.placeholderLineNumbers.append(lineNumber);

        const QRegularExpressionMatch match = headingRe.match(stripBlockquotePrefix(line));
        if (!match.hasMatch())
            continue;

        const int level = match.captured(1).length();
        QString rawTitle = match.captured(2).trimmed();
        while (rawTitle.endsWith(QLatin1Char('#')))
            rawTitle.chop(1);
        rawTitle = rawTitle.trimmed();
        if (rawTitle.isEmpty())
            continue;

        const QString slug = slugify(rawTitle, usedSlugs);
        result.headings.append({ level, cleanTitle(rawTitle), slug });
        result.slugs.append(slug);
    }

    if (result.headings.isEmpty())
        return result;

    int minimumLevel = 6;
    for (const Heading& heading : result.headings)
        minimumLevel = qMin(minimumLevel, heading.level);

    QStringList tocLines;
    tocLines.reserve(result.headings.size());
    for (const Heading& heading : result.headings) {
        const QString indent((heading.level - minimumLevel) * 2, QLatin1Char(' '));
        tocLines.append(QStringLiteral("%1- [%2](#%3)").arg(indent, heading.title, heading.slug));
    }
    result.tocMarkdown = tocLines.join(QLatin1Char('\n'));

    if (result.placeholderLineNumbers.isEmpty())
        return result;

    QStringList expandedLines = lines;
    QVector<int> expandedOrigins;
    expandedOrigins.reserve(lines.size() + result.headings.size());
    int placeholderIndex = 0;
    for (int line = 0; line < lines.size(); ++line) {
        const bool isPlaceholder = placeholderIndex < result.placeholderLineNumbers.size()
            && result.placeholderLineNumbers.at(placeholderIndex) == line;
        if (!isPlaceholder) {
            expandedOrigins.append(line + 1);
            continue;
        }
        ++placeholderIndex;
        expandedLines[line] = result.tocMarkdown;
        const int generatedLines = result.tocMarkdown.count(QLatin1Char('\n')) + 1;
        for (int n = 0; n < generatedLines; ++n)
            expandedOrigins.append(line + 1);
    }
    result.expandedMarkdown = expandedLines.join(QLatin1Char('\n'));
    result.expandedLineOrigins = expandedOrigins;
    return result;
}

// One-entry memo over analyzeUncached(). The same source is analyzed repeatedly
// and unchanged: once per preview refresh, again on every Edit-menu open via
// hasHeadings(), and again for TOC insertion and export. Guarded by a mutex
// because the preview conversion runs on a QtConcurrent worker while the UI
// thread can be opening the Edit menu. The comparison is a memcmp where the miss
// costs a full pass over every line, so even a cold compare is worth it.
Result analyze(const QString& markdown)
{
    static QMutex cacheMutex;
    static QString cachedSource;
    static Result cachedResult;

    {
        const QMutexLocker locker(&cacheMutex);
        if (!cachedSource.isNull() && cachedSource == markdown)
            return cachedResult;
    }

    Result result = analyzeUncached(markdown);

    {
        const QMutexLocker locker(&cacheMutex);
        cachedSource = markdown;
        cachedResult = result;
    }
    return result;
}

QString render(const Result& result, const QString& lineEnding)
{
    if (result.tocMarkdown.isEmpty())
        return {};
    QString rendered = result.tocMarkdown;
    return rendered.replace(QLatin1Char('\n'), lineEnding);
}

bool hasHeadings(const QString& markdown)
{
    return !analyze(markdown).headings.isEmpty();
}

} // namespace MarkdownToc
