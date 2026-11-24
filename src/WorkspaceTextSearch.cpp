#include "WorkspaceTextSearch.h"
#include "PreserveCase.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace WorkspaceTextSearch {

namespace {

constexpr int kPreviewBefore = 24;
constexpr int kPreviewAfter = 120;
constexpr int kPreviewMax = 240;

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

bool cancelledNow(const std::function<bool()>& cancelled)
{
    return cancelled && cancelled();
}

}  // namespace

QRegularExpression buildRegex(const Query& query, QString* error)
{
    if (error)
        error->clear();
    if (query.text.isEmpty())
        return QRegularExpression();

    QString pattern;
    if (query.options.regex) {
        pattern = query.options.wholeWord
            ? QStringLiteral("\\b(?:") + query.text + QStringLiteral(")\\b")
            : query.text;
    } else {
        pattern = QRegularExpression::escape(query.text);
        if (query.options.wholeWord) {
            if (isWordChar(query.text.front()))
                pattern.prepend(QStringLiteral("(?<!\\w)"));
            if (isWordChar(query.text.back()))
                pattern.append(QStringLiteral("(?!\\w)"));
        }
    }

    QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!query.options.matchCase)
        flags |= QRegularExpression::CaseInsensitiveOption;
    const QRegularExpression re(pattern, flags);
    if (!re.isValid() && error)
        *error = re.errorString();
    return re;
}

Match makeMatch(const QString& line, int lineNumber, int column, int length)
{
    Match m;
    m.line = lineNumber;
    m.column = column;
    m.length = length;

    int lead = 0;
    while (lead < line.size() && line.at(lead).isSpace() && lead < column)
        ++lead;
    int start = std::max(lead, column - kPreviewBefore);
    int end = std::min<int>(line.size(), column + length + kPreviewAfter);
    end = std::min(end, start + kPreviewMax);
    end = std::max(end, std::min<int>(line.size(), column + 1));

    QString body = line.mid(start, end - start);
    for (QChar& c : body) {
        if (c == QLatin1Char('\t') || c == QLatin1Char('\r'))
            c = QLatin1Char(' ');
    }
    const QString prefix = start > lead ? QStringLiteral("…") : QString();
    const QString suffix = end < line.size() ? QStringLiteral("…") : QString();
    m.preview = prefix + body + suffix;
    m.previewStart = prefix.size() + (column - start);
    m.previewLength = std::min(length, end - column);
    return m;
}

bool searchText(const QString& text, const QRegularExpression& re, int limit,
                QList<Match>* out)
{
    if (!re.isValid() || !out || limit <= 0)
        return false;

    const QStringView view(text);
    qsizetype lineStart = 0;
    int lineNumber = 0;
    while (lineStart <= view.size()) {
        qsizetype lineEnd = view.indexOf(QLatin1Char('\n'), lineStart);
        if (lineEnd < 0)
            lineEnd = view.size();
        const QStringView line = view.mid(lineStart, lineEnd - lineStart);

        if (!line.isEmpty()) {
            QRegularExpressionMatchIterator it = re.globalMatch(line);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                if (m.capturedLength() == 0)
                    continue;
                if (out->size() >= limit)
                    return true;
                Match match = makeMatch(line.toString(), lineNumber,
                                        int(m.capturedStart()), int(m.capturedLength()));
                const int groups = std::min(re.captureCount(), 9);
                match.captures.reserve(groups + 1);
                for (int g = 0; g <= groups; ++g)
                    match.captures.append(m.captured(g));
                out->append(std::move(match));
            }
        }

        lineStart = lineEnd + 1;
        ++lineNumber;
    }
    return false;
}

namespace {

template <typename Captured>
QString expandCore(const QString& replacement, bool regexMode, bool preserveCase,
                   const QString& whole, Captured captured)
{
    QString out = replacement;
    if (regexMode && replacement.contains(QLatin1Char('$'))) {
        out.clear();
        out.reserve(replacement.size());
        for (int i = 0; i < replacement.size(); ++i) {
            const QChar c = replacement.at(i);
            if (c == QLatin1Char('$') && i + 1 < replacement.size()) {
                const QChar d = replacement.at(i + 1);
                if (d >= QLatin1Char('1') && d <= QLatin1Char('9')) {
                    out += captured(d.unicode() - '0');
                    ++i;
                    continue;
                }
            }
            out += c;
        }
    }
    return preserveCase ? applyPreservedCase(whole, out) : out;
}

}  // namespace

QString expandReplacement(const QString& replacement, const QRegularExpressionMatch& match,
                          bool regexMode, bool preserveCase)
{
    return expandCore(replacement, regexMode, preserveCase, match.captured(0),
                      [&](int n) { return match.captured(n); });
}

QString expandReplacementFromCaptures(const QString& replacement, const QStringList& captures,
                                      bool regexMode, bool preserveCase)
{
    return expandCore(replacement, regexMode, preserveCase,
                      captures.isEmpty() ? QString() : captures.first(),
                      [&](int n) { return n < captures.size() ? captures.at(n) : QString(); });
}

bool replacementForMatch(const QString& line, int column, int length,
                         const QRegularExpression& re, const Query& query,
                         const QString& replacement, QString* expanded)
{
    if (!re.isValid() || column < 0 || length <= 0 || column + length > line.size())
        return false;
    const QRegularExpressionMatch m = re.match(
        line, column, QRegularExpression::NormalMatch,
        QRegularExpression::AnchorAtOffsetMatchOption);
    if (!m.hasMatch() || m.capturedStart() != column || m.capturedLength() != length)
        return false;
    if (expanded)
        *expanded = expandReplacement(replacement, m, query.options.regex,
                                      query.options.preserveCase);
    return true;
}

int replaceAll(const QString& text, const QRegularExpression& re, const Query& query,
               const QString& replacement, QString* result)
{
    if (!re.isValid()) {
        if (result)
            *result = text;
        return 0;
    }

    QString out;
    out.reserve(text.size());
    int count = 0;
    qsizetype lineStart = 0;
    while (lineStart <= text.size()) {
        qsizetype lineEnd = text.indexOf(QLatin1Char('\n'), lineStart);
        const bool last = lineEnd < 0;
        if (last)
            lineEnd = text.size();
        const QStringView line = QStringView(text).mid(lineStart, lineEnd - lineStart);

        qsizetype copied = 0;
        QRegularExpressionMatchIterator it = re.globalMatch(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (m.capturedLength() == 0)
                continue;
            out += line.mid(copied, m.capturedStart() - copied);
            out += expandReplacement(replacement, m, query.options.regex,
                                     query.options.preserveCase);
            copied = m.capturedEnd();
            ++count;
        }
        out += line.mid(copied);
        if (last)
            break;
        out += QLatin1Char('\n');
        lineStart = lineEnd + 1;
    }
    if (result)
        *result = out;
    return count;
}

bool replaceOne(const QString& text, int line, int column, int length,
                const QRegularExpression& re, const Query& query,
                const QString& replacement, QString* result)
{
    qsizetype lineStart = 0;
    for (int i = 0; i < line; ++i) {
        const qsizetype next = text.indexOf(QLatin1Char('\n'), lineStart);
        if (next < 0)
            return false;
        lineStart = next + 1;
    }
    qsizetype lineEnd = text.indexOf(QLatin1Char('\n'), lineStart);
    if (lineEnd < 0)
        lineEnd = text.size();

    const QString lineText = text.mid(lineStart, lineEnd - lineStart);
    QString expanded;
    if (!replacementForMatch(lineText, column, length, re, query, replacement, &expanded))
        return false;
    if (result) {
        *result = text;
        result->replace(lineStart + column, length, expanded);
    }
    return true;
}

bool isSkippedDirectory(const QString& name)
{
    static const QStringList exact = {
        QStringLiteral(".git"), QStringLiteral(".hg"), QStringLiteral(".svn"),
        QStringLiteral("node_modules"), QStringLiteral(".vs"), QStringLiteral("dist"),
        QStringLiteral("out"), QStringLiteral("build"),
    };
    for (const QString& skipped : exact) {
        if (name.compare(skipped, Qt::CaseInsensitive) == 0)
            return true;
    }
    return name.startsWith(QStringLiteral("build-"), Qt::CaseInsensitive)
        || name.startsWith(QStringLiteral("build_"), Qt::CaseInsensitive);
}

QStringList collectFiles(const QString& root, const std::function<bool()>& cancelled)
{
    QStringList files;
    QStringList pending{QDir::cleanPath(root)};
    while (!pending.isEmpty()) {
        if (cancelledNow(cancelled))
            break;
        const QString directory = pending.takeLast();
        const QFileInfoList entries = QDir(directory).entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Hidden,
            QDir::Name | QDir::IgnoreCase);

        QStringList subdirectories;
        for (const QFileInfo& entry : entries) {
            if (entry.isDir()) {
                if (!isSkippedDirectory(entry.fileName()) && !entry.isJunction())
                    subdirectories.append(entry.absoluteFilePath());
            } else if (entry.isFile()) {
                files.append(entry.absoluteFilePath());
            }
        }
        for (auto it = subdirectories.crbegin(); it != subdirectories.crend(); ++it)
            pending.append(*it);
    }
    return files;
}

QString pathKey(const QString& path)
{
    const QString cleaned = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    return cleaned.toLower();
#else
    return cleaned;
#endif
}

RunSummary run(const QString& root, const QRegularExpression& re,
               const QHash<QString, QString>& snapshots, const TextLoader& loader,
               const std::function<bool()>& cancelled,
               const std::function<void(const FileResult&)>& onFile, int maxTotalMatches)
{
    RunSummary summary;
    if (!re.isValid())
        return summary;

    const QStringList files = collectFiles(root, cancelled);
    for (const QString& path : files) {
        if (cancelledNow(cancelled)) {
            summary.cancelled = true;
            break;
        }

        QString text;
        const auto snapshot = snapshots.constFind(pathKey(path));
        if (snapshot != snapshots.constEnd()) {
            text = *snapshot;
        } else if (!loader || !loader(path, &text)) {
            ++summary.skipped;
            continue;
        }

        FileResult result;
        const int room = maxTotalMatches - summary.matches;
        const bool hitLimit = searchText(text, re, room, &result.matches);
        if (!result.matches.isEmpty()) {
            result.path = path;
            summary.matches += int(result.matches.size());
            ++summary.files;
            if (onFile)
                onFile(result);
        }
        if (hitLimit || summary.matches >= maxTotalMatches) {
            summary.capped = true;
            break;
        }
    }
    return summary;
}

}  // namespace WorkspaceTextSearch
