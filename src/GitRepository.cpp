#include "GitRepository.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace {

constexpr char kUnitSeparator = '\x1f';

GitFileState stateFromCode(QChar code, bool staged)
{
    switch (code.toLatin1()) {
    case 'A': return GitFileState::Added;
    case 'D': return GitFileState::Deleted;
    case 'R': return GitFileState::Renamed;
    case 'C': return GitFileState::Added;
    case '?': return GitFileState::Untracked;
    case 'U': return GitFileState::Conflicted;
    default:  break;
    }
    Q_UNUSED(staged);
    return GitFileState::Modified;
}

// Porcelain v1 marks a conflict with any of these XY pairs; a plain switch on
// one column would misread `AA` (both added) as a staged add.
bool isConflictPair(QChar x, QChar y)
{
    if (x == QLatin1Char('U') || y == QLatin1Char('U'))
        return true;
    if (x == QLatin1Char('A') && y == QLatin1Char('A'))
        return true;
    if (x == QLatin1Char('D') && y == QLatin1Char('D'))
        return true;
    return false;
}

QStringList splitZ(const QByteArray& data)
{
    QStringList records;
    int start = 0;
    while (start <= data.size()) {
        const int end = data.indexOf('\0', start);
        if (end < 0) {
            if (start < data.size())
                records.append(QString::fromUtf8(data.mid(start)));
            break;
        }
        records.append(QString::fromUtf8(data.mid(start, end - start)));
        start = end + 1;
    }
    return records;
}

QString fileNameForPath(const QString& path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

}

bool GitDiffSpec::matches(const GitDiffSpec& other) const
{
    return source == other.source
        && path == other.path
        && commitHash == other.commitHash
        && repoRoot.compare(other.repoRoot, Qt::CaseInsensitive) == 0;
}

QString GitDiffSpec::fileName() const
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

QString GitDiffSpec::tabTitle() const
{
    const QString name = fileName();
    switch (source) {
    case WorkingTree:
        return QStringLiteral("%1 (Working Tree)").arg(name);
    case Index:
        return QStringLiteral("%1 (Staged)").arg(name);
    case Commit:
        break;
    }
    // Short hashes are deliberately 6 characters: long enough to be unambiguous
    // in a tab title, short enough that the file name still fits.
    const QString shortCommit = commitHash.left(6);
    if (parentHash.isEmpty())
        return QStringLiteral("%1 (%2)").arg(name, shortCommit);
    return QStringLiteral("%1 (%2 ↔ %3)").arg(name, parentHash.left(6), shortCommit);
}

namespace GitRepository {

QString findRoot(const QString& startDir)
{
    if (startDir.isEmpty())
        return QString();

    QDir dir(startDir);
    if (!dir.exists())
        return QString();
    dir.makeAbsolute();

    // Source Control is scoped to the current workspace root only: a .git
    // marker in an ancestor directory must not attach this workspace to that
    // parent repository. A .git *file* (worktree, submodule) is as valid a
    // marker as a directory, so exists() rather than a directory test.
    if (QFileInfo::exists(dir.filePath(QStringLiteral(".git"))))
        return QDir::fromNativeSeparators(dir.absolutePath());
    return QString();
}

QVector<GitFileEntry> parseStatus(const QByteArray& data)
{
    QVector<GitFileEntry> entries;
    const QStringList records = splitZ(data);

    for (int i = 0; i < records.size(); ++i) {
        const QString& record = records.at(i);
        if (record.size() < 4)
            continue;

        const QChar x = record.at(0);
        const QChar y = record.at(1);
        QString path = record.mid(3);

        // With -z, a rename is two records: "R  <new>\0<old>\0". The arrow
        // form only appears without -z, which is exactly why we use -z.
        QString original;
        if ((x == QLatin1Char('R') || x == QLatin1Char('C')) && i + 1 < records.size()) {
            original = records.at(++i);
        }

        if (isConflictPair(x, y)) {
            GitFileEntry entry;
            entry.path = path;
            entry.originalPath = original;
            entry.state = GitFileState::Conflicted;
            entry.staged = false;
            entries.append(entry);
            continue;
        }

        if (x == QLatin1Char('?') && y == QLatin1Char('?')) {
            GitFileEntry entry;
            entry.path = path;
            entry.state = GitFileState::Untracked;
            entry.staged = false;
            entries.append(entry);
            continue;
        }

        if (x != QLatin1Char(' ') && x != QLatin1Char('?')) {
            GitFileEntry entry;
            entry.path = path;
            entry.originalPath = original;
            entry.state = stateFromCode(x, true);
            entry.staged = true;
            entries.append(entry);
        }
        if (y != QLatin1Char(' ') && y != QLatin1Char('?')) {
            GitFileEntry entry;
            entry.path = path;
            entry.state = stateFromCode(y, false);
            entry.staged = false;
            entries.append(entry);
        }
    }
    return entries;
}

GitChangeState changeState(const QVector<GitFileEntry>& entries)
{
    if (entries.isEmpty())
        return GitChangeState::Clean;
    for (const GitFileEntry& entry : entries) {
        if (entry.staged)
            return GitChangeState::HasStaged;
    }
    return GitChangeState::UnstagedOnly;
}

QString fileTypeForPath(const QString& path)
{
    const QString name = fileNameForPath(path);
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == name.size() - 1)
        return QString();
    return name.mid(dot + 1).toCaseFolded();
}

bool isValidChangesSortMode(int value)
{
    return value >= static_cast<int>(GitChangesSortMode::Path)
        && value <= static_cast<int>(GitChangesSortMode::FileType);
}

GitChangesSortMode changesSortModeFromStoredValue(int value, GitChangesSortMode fallback)
{
    return isValidChangesSortMode(value) ? static_cast<GitChangesSortMode>(value) : fallback;
}

void sortEntries(QVector<GitFileEntry>& entries, GitChangesSortMode mode)
{
    const auto compareByName = [](const GitFileEntry& a, const GitFileEntry& b) {
        const int cmp = QString::compare(fileNameForPath(a.path), fileNameForPath(b.path),
                                         Qt::CaseInsensitive);
        return cmp != 0 ? cmp < 0 : QString::compare(a.path, b.path, Qt::CaseInsensitive) < 0;
    };

    switch (mode) {
    case GitChangesSortMode::Path:
        std::sort(entries.begin(), entries.end(), [](const GitFileEntry& a, const GitFileEntry& b) {
            return QString::compare(a.path, b.path, Qt::CaseInsensitive) < 0;
        });
        break;
    case GitChangesSortMode::Name:
        std::sort(entries.begin(), entries.end(), compareByName);
        break;
    case GitChangesSortMode::Status:
        std::sort(entries.begin(), entries.end(), [](const GitFileEntry& a, const GitFileEntry& b) {
            if (a.state != b.state)
                return static_cast<int>(a.state) < static_cast<int>(b.state);
            return QString::compare(a.path, b.path, Qt::CaseInsensitive) < 0;
        });
        break;
    case GitChangesSortMode::FileType:
        std::sort(entries.begin(), entries.end(), [&compareByName](const GitFileEntry& a,
                                                                     const GitFileEntry& b) {
            const int cmp = QString::compare(fileTypeForPath(a.path), fileTypeForPath(b.path),
                                             Qt::CaseInsensitive);
            return cmp != 0 ? cmp < 0 : compareByName(a, b);
        });
        break;
    }
}

bool isFileActionApplicable(GitFileAction action, const GitFileEntry& entry)
{
    if (entry.path.isEmpty())
        return false;

    switch (action) {
    case GitFileAction::Stage:
        return !entry.staged && entry.state != GitFileState::Conflicted;
    case GitFileAction::Unstage:
        return entry.staged;
    case GitFileAction::Discard:
        return !entry.staged && entry.state != GitFileState::Conflicted;
    case GitFileAction::Resolve:
        return entry.state == GitFileState::Conflicted;
    }
    return false;
}

QStringList resolveFileActionPaths(GitFileAction action, const GitFileEntry& clicked,
                                   bool clickedSelected,
                                   const QVector<GitFileEntry>& selectedEntries)
{
    const QVector<GitFileEntry> candidates = clickedSelected ? selectedEntries
                                                               : QVector<GitFileEntry>{ clicked };
    QStringList paths;
    QSet<QString> seen;
    for (const GitFileEntry& entry : candidates) {
        if (isFileActionApplicable(action, entry) && !seen.contains(entry.path)) {
            paths.append(entry.path);
            seen.insert(entry.path);
        }
    }
    return paths;
}

GitBranchInfo parseBranchHeader(const QByteArray& data)
{
    GitBranchInfo info;
    const QStringList records = splitZ(data);

    for (const QString& record : records) {
        if (!record.startsWith(QLatin1String("# branch.")))
            continue;

        if (record.startsWith(QLatin1String("# branch.head "))) {
            const QString head = record.mid(14).trimmed();
            if (head == QLatin1String("(detached)")) {
                info.detached = true;
            } else {
                info.branch = head;
            }
        } else if (record.startsWith(QLatin1String("# branch.upstream "))) {
            info.upstream = record.mid(18).trimmed();
            info.hasUpstream = !info.upstream.isEmpty();
        } else if (record.startsWith(QLatin1String("# branch.oid "))) {
            const QString oid = record.mid(13).trimmed();
            if (info.branch.isEmpty() && oid.size() >= 7)
                info.branch = oid.left(7);
        } else if (record.startsWith(QLatin1String("# branch.ab "))) {
            const QStringList parts = record.mid(12).trimmed().split(QLatin1Char(' '),
                                                                    Qt::SkipEmptyParts);
            for (const QString& part : parts) {
                if (part.startsWith(QLatin1Char('+')))
                    info.ahead = part.mid(1).toInt();
                else if (part.startsWith(QLatin1Char('-')))
                    info.behind = part.mid(1).toInt();
            }
        }
    }
    return info;
}

QVector<GitCommit> parseLog(const QByteArray& data)
{
    QVector<GitCommit> commits;
    const QString text = QString::fromUtf8(data);
    if (text.isEmpty())
        return commits;

    // A subject can contain anything except the separator, so split on the
    // separator first and only then worry about line boundaries.
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.trimmed().isEmpty())
            continue;
        const QStringList fields = line.split(QLatin1Char(kUnitSeparator));
        if (fields.size() < 5)
            continue;

        GitCommit commit;
        commit.hash      = fields.at(0).trimmed();
        commit.shortHash = fields.at(1).trimmed();
        commit.author    = fields.at(2);
        commit.date      = QDateTime::fromString(fields.at(3).trimmed(), Qt::ISODate);
        commit.subject   = fields.at(4);
        if (fields.size() >= 6) {
            commit.parents = fields.at(5).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
        if (commit.hash.isEmpty())
            continue;
        commits.append(commit);
    }
    return commits;
}

QVector<GitStashEntry> parseStashList(const QByteArray& data)
{
    QVector<GitStashEntry> stashes;
    const QString text = QString::fromUtf8(data);
    const QStringList lines = text.split(QLatin1Char('\n'));

    for (const QString& line : lines) {
        if (line.trimmed().isEmpty())
            continue;
        const QStringList fields = line.split(QLatin1Char(kUnitSeparator));
        if (fields.size() < 4)
            continue;

        GitStashEntry stash;
        stash.ref     = fields.at(0).trimmed();
        stash.hash    = fields.at(1).trimmed();
        stash.date    = QDateTime::fromString(fields.at(2).trimmed(), Qt::ISODate);
        stash.subject = fields.at(3);
        if (stash.ref.isEmpty())
            continue;
        stashes.append(stash);
    }
    return stashes;
}

QVector<GitBranchRef> parseBranchRefs(const QByteArray& data)
{
    QVector<GitBranchRef> local;
    QVector<GitBranchRef> remote;

    const QStringList lines =
        QString::fromUtf8(data).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        const QStringList fields = line.split(QLatin1Char('\x1f'));
        if (fields.size() < 2)
            continue;

        const QString fullName = fields.at(0);
        GitBranchRef ref;
        ref.name = fields.at(1);
        ref.remote = fullName.startsWith(QLatin1String("refs/remotes/"));
        // %(HEAD) is "*" for the checked-out branch and " " otherwise.
        ref.current = fields.size() > 2
            && fields.at(2).trimmed() == QLatin1String("*");
        if (ref.name.isEmpty())
            continue;
        // origin/HEAD is a symbolic alias for the remote's default branch, not
        // a branch of its own -- switching to it is never what the user meant.
        if (ref.remote && ref.name.endsWith(QLatin1String("/HEAD")))
            continue;

        (ref.remote ? remote : local).append(ref);
    }

    return local + remote;
}

QVector<GitRemote> parseRemotes(const QByteArray& data)
{
    QVector<GitRemote> remotes;
    const QString text = QString::fromUtf8(data);

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        const int tab = line.indexOf(QLatin1Char('\t'));
        if (tab <= 0)
            continue;
        const QString name = line.left(tab);
        QString rest = line.mid(tab + 1);

        // "<url> (fetch)" -- a URL may contain spaces, so strip the suffix
        // from the right rather than splitting on whitespace.
        bool isPush = false;
        if (rest.endsWith(QLatin1String(" (push)"))) {
            isPush = true;
            rest.chop(7);
        } else if (rest.endsWith(QLatin1String(" (fetch)"))) {
            rest.chop(8);
        }
        const QString url = rest.trimmed();

        int index = -1;
        for (int i = 0; i < remotes.size(); ++i) {
            if (remotes.at(i).name == name) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            GitRemote remote;
            remote.name = name;
            remotes.append(remote);
            index = remotes.size() - 1;
        }
        if (isPush)
            remotes[index].pushUrl = url;
        else
            remotes[index].fetchUrl = url;
    }

    for (GitRemote& remote : remotes) {
        if (remote.pushUrl.isEmpty())
            remote.pushUrl = remote.fetchUrl;
        if (remote.fetchUrl.isEmpty())
            remote.fetchUrl = remote.pushUrl;
    }
    return remotes;
}

QVector<GitFileEntry> parseNameStatus(const QByteArray& data)
{
    QVector<GitFileEntry> entries;
    const QStringList records = splitZ(data);

    for (int i = 0; i < records.size(); ++i) {
        const QString code = records.at(i).trimmed();
        if (code.isEmpty())
            continue;

        const QChar letter = code.at(0);
        if (letter == QLatin1Char('R') || letter == QLatin1Char('C')) {
            if (i + 2 >= records.size())
                break;
            GitFileEntry entry;
            entry.originalPath = records.at(++i);
            entry.path = records.at(++i);
            entry.state = GitFileState::Renamed;
            entry.staged = true;
            entries.append(entry);
            continue;
        }
        if (i + 1 >= records.size())
            break;
        GitFileEntry entry;
        entry.path = records.at(++i);
        entry.state = stateFromCode(letter, true);
        entry.staged = true;
        entries.append(entry);
    }
    return entries;
}

QVector<GitDiffRow> toSideBySide(const QByteArray& unifiedDiff)
{
    QVector<GitDiffRow> rows;
    const QString text = QString::fromUtf8(unifiedDiff);
    if (text.trimmed().isEmpty())
        return rows;

    QStringList lines = text.split(QLatin1Char('\n'));
    // A trailing newline produces one empty trailing element that is not a
    // line of the diff. Every *other* empty line inside a hunk is a real
    // empty context line and must keep its line numbers moving.
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    QStringList pendingRemoved;
    QStringList pendingAdded;
    int leftNo = 0;
    int rightNo = 0;
    bool inHunk = false;

    // A run of '-' lines followed by a run of '+' lines is a replacement:
    // pair them row by row and pad the shorter side with gaps. This is the
    // whole "diff algorithm" -- git already did the hard part.
    auto flush = [&] {
        const int count = qMax(pendingRemoved.size(), pendingAdded.size());
        for (int i = 0; i < count; ++i) {
            GitDiffRow row;
            row.kind = GitDiffRow::Change;
            if (i < pendingRemoved.size()) {
                row.leftText = pendingRemoved.at(i);
                row.leftNo = ++leftNo;
            }
            if (i < pendingAdded.size()) {
                row.rightText = pendingAdded.at(i);
                row.rightNo = ++rightNo;
            }
            rows.append(row);
        }
        pendingRemoved.clear();
        pendingAdded.clear();
    };

    for (const QString& line : lines) {
        if (line.startsWith(QLatin1String("@@"))) {
            flush();
            inHunk = true;

            // @@ -a,b +c,d @@ optional section heading
            const int close = line.indexOf(QLatin1String("@@"), 2);
            const QString ranges = close > 0 ? line.mid(2, close - 2).trimmed() : QString();
            for (const QString& part : ranges.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                const QString numbers = part.mid(1).section(QLatin1Char(','), 0, 0);
                if (part.startsWith(QLatin1Char('-')))
                    leftNo = numbers.toInt() - 1;
                else if (part.startsWith(QLatin1Char('+')))
                    rightNo = numbers.toInt() - 1;
            }

            GitDiffRow row;
            row.kind = GitDiffRow::HunkHeader;
            row.leftText = line;
            row.rightText = line;
            rows.append(row);
            continue;
        }

        if (!inHunk) {
            if (line.startsWith(QLatin1String("Binary files")) ||
                line.startsWith(QLatin1String("GIT binary patch"))) {
                GitDiffRow row;
                row.kind = GitDiffRow::Notice;
                row.leftText = QStringLiteral("Binary file — no text diff available");
                row.rightText = row.leftText;
                rows.append(row);
                return rows;
            }
            // diff --git / index / --- / +++ headers carry nothing the
            // side-by-side view can show, and the title bar already names
            // both sides.
            continue;
        }

        if (line.startsWith(QLatin1Char('\\'))) {
            // "\ No newline at end of file"
            continue;
        }
        if (line.startsWith(QLatin1Char('-'))) {
            pendingRemoved.append(line.mid(1));
            continue;
        }
        if (line.startsWith(QLatin1Char('+'))) {
            pendingAdded.append(line.mid(1));
            continue;
        }
        flush();
        GitDiffRow row;
        row.kind = GitDiffRow::Context;
        // An empty context line can reach us as "" rather than " " when the
        // producer stripped trailing whitespace; mid(1) on "" is "" anyway.
        row.leftText = line.mid(1);
        row.rightText = row.leftText;
        row.leftNo = ++leftNo;
        row.rightNo = ++rightNo;
        rows.append(row);
    }
    flush();

    return rows;
}

void assignLanes(QVector<GitCommit>& commits, int maxLanes)
{
    if (maxLanes < 1)
        maxLanes = 1;

    // lanes[i] is the hash the i-th column is currently waiting to draw.
    QVector<QString> lanes;

    auto laneOf = [&lanes](const QString& hash) {
        for (int i = 0; i < lanes.size(); ++i) {
            if (lanes.at(i) == hash)
                return i;
        }
        return -1;
    };
    auto occupied = [&lanes, maxLanes] {
        QVector<int> result;
        for (int i = 0; i < lanes.size(); ++i) {
            if (!lanes.at(i).isEmpty())
                result.append(qMin(i, maxLanes - 1));
        }
        return result;
    };

    for (GitCommit& commit : commits) {
        int lane = laneOf(commit.hash);
        if (lane < 0) {
            lane = lanes.indexOf(QString());
            if (lane < 0) {
                lanes.append(QString());
                lane = lanes.size() - 1;
            }
        }

        commit.lanesBefore = occupied();
        commit.lane = qMin(lane, maxLanes - 1);

        // Any other lane also waiting for this commit merges into it here.
        for (int i = 0; i < lanes.size(); ++i) {
            if (i != lane && lanes.at(i) == commit.hash)
                lanes[i].clear();
        }

        lanes[lane] = commit.parents.value(0);
        for (int p = 1; p < commit.parents.size(); ++p) {
            const QString& parent = commit.parents.at(p);
            if (laneOf(parent) >= 0)
                continue;
            int free = lanes.indexOf(QString());
            if (free < 0) {
                lanes.append(QString());
                free = lanes.size() - 1;
            }
            lanes[free] = parent;
        }

        while (!lanes.isEmpty() && lanes.last().isEmpty())
            lanes.removeLast();

        commit.lanesAfter = occupied();
    }
}

bool isValidRemoteName(const QString& name)
{
    if (name.isEmpty())
        return false;
    if (name.startsWith(QLatin1Char('-')) || name.startsWith(QLatin1Char('.')))
        return false;
    for (const QChar c : name) {
        if (c.isSpace())
            return false;
        if (c == QLatin1Char('/') || c == QLatin1Char('\\') ||
            c == QLatin1Char(':') || c == QLatin1Char('?') ||
            c == QLatin1Char('*') || c == QLatin1Char('[') ||
            c == QLatin1Char('^') || c == QLatin1Char('~'))
            return false;
    }
    return true;
}

QChar statusLetter(GitFileState state)
{
    switch (state) {
    case GitFileState::Modified:   return QLatin1Char('M');
    case GitFileState::Added:      return QLatin1Char('A');
    case GitFileState::Deleted:    return QLatin1Char('D');
    case GitFileState::Renamed:    return QLatin1Char('R');
    case GitFileState::Untracked:  return QLatin1Char('U');
    case GitFileState::Conflicted: return QLatin1Char('!');
    }
    return QLatin1Char('M');
}

}
