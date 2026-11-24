#include "TerminalLinks.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSysInfo>

namespace TerminalLinks {
namespace {

constexpr int kMaxPosition = 10000000;
constexpr qsizetype kMaxPathLength = 32767;

int position(const QString& digits)
{
    bool ok = false;
    const int value = digits.toInt(&ok);
    return ok && value > 0 && value <= kMaxPosition ? value : 0;
}

bool isSeparator(QChar c)
{
    return c == u'/' || c == u'\\';
}

bool isAsciiLetter(QChar c)
{
    return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z');
}

// Windows maps these names to devices in every directory, with any extension
// and ignoring trailing dots and spaces: `nul.txt` and `CON ` are devices too.
bool isReservedDeviceName(QStringView component)
{
    QStringView base = component;
    if (const qsizetype dot = base.indexOf(u'.'); dot >= 0)
        base = base.left(dot);
    while (!base.isEmpty() && base.back() == u' ')
        base.chop(1);
    for (const char* name : { "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$" }) {
        if (base.compare(QLatin1String(name), Qt::CaseInsensitive) == 0)
            return true;
    }
    if (base.size() != 4)
        return false;
    const QStringView stem = base.left(3);
    if (stem.compare(u"COM", Qt::CaseInsensitive) != 0
        && stem.compare(u"LPT", Qt::CaseInsensitive) != 0)
        return false;
    const QChar digit = base.at(3);
    return (digit >= u'0' && digit <= u'9') || digit == QChar(0x00B9)
        || digit == QChar(0x00B2) || digit == QChar(0x00B3);
}

QString localPathFromFileUrl(const QUrl& url)
{
    if (!url.isValid())
        return {};
    const QString host = url.host();
    if (!host.isEmpty() && host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) != 0
        && host.compare(QSysInfo::machineHostName(), Qt::CaseInsensitive) != 0)
        return {};
    QUrl local(url);
    local.setHost(QString());
    QString path = local.toLocalFile();
    // MSYS shells (Git Bash) write /c/Users/... for C:/Users/...
    static const QRegularExpression msys(QStringLiteral("^/([A-Za-z])(/.*)?$"));
    if (const QRegularExpressionMatch match = msys.match(path); match.hasMatch()) {
        const QString rest = match.captured(2);
        path = match.captured(1).toUpper() + QLatin1Char(':')
            + (rest.isEmpty() ? QStringLiteral("/") : rest);
    }
    return path;
}

bool isUsableDirectory(const QString& path)
{
    return !path.isEmpty() && QDir::isAbsolutePath(path) && isSafeLocalPath(path)
        && QFileInfo(path).isDir();
}

Target locate(const LocalLink& link, const QString& reportedCwd, const QString& startDirectory)
{
    Target target;
    QString absolute = QDir::fromNativeSeparators(link.path);
    if (!QDir::isAbsolutePath(absolute)) {
        const QString base = isUsableDirectory(reportedCwd) ? reportedCwd
            : isUsableDirectory(startDirectory) ? startDirectory : QString();
        if (base.isEmpty())
            return target;
        absolute = QDir(base).absoluteFilePath(absolute);
    }
    absolute = QDir::cleanPath(absolute);
    if (!isSafeLocalPath(absolute))
        return target;

    const QFileInfo info(absolute);
    if (isShellShortcut(absolute) ? (info.exists() || info.isSymLink()) : info.isFile()) {
        target.kind = Target::Kind::File;
        target.line = link.line;
        target.column = link.column;
    } else if (info.isDir()) {
        target.kind = Target::Kind::Directory;
    } else {
        target.kind = Target::Kind::NotFound;
    }
    target.path = absolute;
    return target;
}

} // namespace

bool isSafeLocalPath(const QString& path)
{
    if (path.isEmpty() || path.size() > kMaxPathLength)
        return false;
    for (const QChar c : path) {
        const char16_t u = c.unicode();
        if (u < 0x20 || u == 0x7f || u == u'<' || u == u'>' || u == u'"' || u == u'|'
            || u == u'?' || u == u'*')
            return false;
    }
    // Leading separators: \\server\share and //server/share are network
    // shares, \\?\ and \\.\ the device namespace, and a single one is relative
    // to whatever drive the shell happens to be on.
    if (isSeparator(path.front()))
        return false;
    if (const qsizetype colon = path.indexOf(u':'); colon >= 0) {
        // Only a drive letter's colon, and only as C:\ or C:/ -- C:foo is
        // relative to that drive's hidden current directory, and a colon
        // anywhere else names an NTFS alternate data stream.
        if (colon != 1 || !isAsciiLetter(path.at(0)) || path.size() < 3
            || !isSeparator(path.at(2)) || path.indexOf(u':', 2) >= 0)
            return false;
    }
    qsizetype start = 0;
    for (qsizetype i = 0; i <= path.size(); ++i) {
        if (i < path.size() && !isSeparator(path.at(i)))
            continue;
        if (isReservedDeviceName(QStringView(path).mid(start, i - start)))
            return false;
        start = i + 1;
    }
    return true;
}

bool parseLocal(const QString& text, LocalLink* out)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > kMaxPathLength)
        return false;

    LocalLink link;
    const QChar first = trimmed.front();
    if (first == u'"' || first == u'\'') {
        const qsizetype close = trimmed.indexOf(first, 1);
        if (close < 0)
            return false;
        link.path = trimmed.mid(1, close - 1);
        static const QRegularExpression quotedSuffix(QStringLiteral(
            R"(^(?::(\d+)(?::(\d+))?|\((\d+)(?:,\s*(\d+))?\)|,\s*line\s+(\d+))?$)"));
        const QRegularExpressionMatch match = quotedSuffix.match(trimmed.mid(close + 1));
        if (!match.hasMatch())
            return false;
        link.line = position(match.captured(1) + match.captured(3) + match.captured(5));
        link.column = position(match.captured(2) + match.captured(4));
    } else {
        static const QRegularExpression suffixed(QStringLiteral(
            R"(^(.+?)(?::(\d+)(?::(\d+))?|\((\d+)(?:,\s*(\d+))?\))?$)"));
        const QRegularExpressionMatch match = suffixed.match(trimmed);
        if (!match.hasMatch())
            return false;
        link.path = match.captured(1);
        link.line = position(match.captured(2) + match.captured(4));
        link.column = position(match.captured(3) + match.captured(5));
    }
    if (link.line == 0)
        link.column = 0;
    if (!isSafeLocalPath(link.path))
        return false;
    if (out)
        *out = link;
    return true;
}

bool isAllowedWebUrl(const QUrl& url)
{
    if (!url.isValid())
        return false;
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
        return !url.host().isEmpty();
    return scheme == QLatin1String("mailto") && !url.path().isEmpty();
}

bool isShellShortcut(const QString& path)
{
    static const QStringList suffixes{
        QStringLiteral("lnk"), QStringLiteral("url"), QStringLiteral("pif"),
        QStringLiteral("scf"), QStringLiteral("appref-ms"), QStringLiteral("website"),
        QStringLiteral("library-ms"), QStringLiteral("searchconnector-ms"),
        QStringLiteral("settingcontent-ms"),
    };
    return suffixes.contains(QFileInfo(path).suffix(), Qt::CaseInsensitive);
}

Target resolve(const QString& text, const QString& reportedCwd,
               const QString& startDirectory)
{
    Target target;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > kMaxPathLength)
        return target;

    // Two or more letters: a one-letter "scheme" is a drive (C:\...).
    static const QRegularExpression scheme(QStringLiteral("^([A-Za-z][A-Za-z0-9+.-]+):"));
    // `README.md:12` is a file and a position, not a URL whose scheme is
    // "README.md" -- '.' is legal in a scheme.
    static const QRegularExpression positionOnly(QStringLiteral(R"(^\d+(?::\d+)?$)"));
    LocalLink link;
    if (const QRegularExpressionMatch match = scheme.match(trimmed);
        match.hasMatch()
        && !positionOnly.matchView(QStringView(trimmed).mid(match.capturedEnd())).hasMatch()) {
        const QUrl url(trimmed);
        if (match.captured(1).compare(QStringLiteral("file"), Qt::CaseInsensitive) != 0) {
            if (isAllowedWebUrl(url)) {
                target.kind = Target::Kind::Web;
                target.url = url;
            }
            return target;
        }
        link.path = localPathFromFileUrl(url);
        // A colon past the drive is never part of a safe path, so a trailing
        // :line[:col] on a file URL is unambiguous.
        static const QRegularExpression fileUrlPosition(
            QStringLiteral(R"(^(.+?):(\d+)(?::(\d+))?$)"));
        if (const QRegularExpressionMatch suffix = fileUrlPosition.match(link.path);
            suffix.hasMatch() && suffix.capturedStart(2) > 2) {
            link.path = suffix.captured(1);
            link.line = position(suffix.captured(2));
            link.column = link.line ? position(suffix.captured(3)) : 0;
        }
        if (!isSafeLocalPath(link.path))
            return target;
        return locate(link, reportedCwd, startDirectory);
    } else if (!parseLocal(trimmed, &link)) {
        return target;
    }

    target = locate(link, reportedCwd, startDirectory);
    if (target.kind != Target::Kind::NotFound || trimmed.front() == u'"' || trimmed.front() == u'\'')
        return target;
    // Unquoted output cannot say where a path with spaces ends, so the page
    // underlines the longest plausible run ("C:\My Docs\a.md") and the host
    // settles for the longest prefix ending at a space that exists.
    constexpr int kMaxPrefixes = 32;
    int tried = 0;
    for (qsizetype space = link.path.lastIndexOf(u' '); space > 0 && tried < kMaxPrefixes;
         space = link.path.lastIndexOf(u' ', space - 1), ++tried) {
        LocalLink prefix;
        prefix.path = link.path.left(space);
        if (!isSafeLocalPath(prefix.path))
            continue;
        const Target candidate = locate(prefix, reportedCwd, startDirectory);
        if (candidate.kind == Target::Kind::File || candidate.kind == Target::Kind::Directory)
            return candidate;
    }
    return target;
}

} // namespace TerminalLinks
