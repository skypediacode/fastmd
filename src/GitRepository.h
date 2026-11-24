#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

// Pure parsing and layout for the Git tool window. No Qt Widgets and no
// QProcess here on purpose: everything below is a total function of its input
// bytes, which is what makes GitParsingTest possible.

enum class GitFileState {
    Modified,
    Added,
    Deleted,
    Renamed,
    Untracked,
    Conflicted
};

struct GitFileEntry {
    QString      path;          // repo-relative, forward slashes
    QString      originalPath;  // rename source, otherwise empty
    GitFileState state = GitFileState::Modified;
    bool         staged = false;
};

// The primary commit action depends on the index, not on the number of rows:
// a file modified in both places produces one staged and one unstaged entry.
enum class GitChangeState { Clean, UnstagedOnly, HasStaged };

// Ordering within each Changes group. Keep values stable: Source Control
// persists this enum as an integer in git/changesSortMode.
enum class GitChangesSortMode { Path, Name, Status, FileType };

// The per-file operations exposed by the Changes list.  Keeping the
// applicability rules next to the status model lets every UI entry point use
// identical multi-selection semantics without duplicating staged/conflict
// checks.
enum class GitFileAction { Stage, Unstage, Discard, Resolve };

struct GitBranchInfo {
    QString branch;
    QString upstream;
    int     ahead = 0;
    int     behind = 0;
    bool    detached = false;
    bool    hasUpstream = false;
};

struct GitCommit {
    QString     hash;
    QString     shortHash;
    QString     author;
    QString     subject;
    QDateTime   date;
    QStringList parents;

    // Filled by assignLanes().
    int          lane = 0;
    QVector<int> lanesBefore;   // lanes occupied on the edge above this row
    QVector<int> lanesAfter;    // lanes occupied on the edge below this row
};

struct GitStashEntry {
    QString   ref;      // stash@{0}
    QString   hash;
    QString   subject;
    QDateTime date;
};

// One entry of the branch selector's list. Deliberately minimal: this is a
// lightweight branch *switcher*, not branch management, so nothing here
// describes tracking configuration, upstream state, or commit counts.
struct GitBranchRef {
    QString name;            // short form: "main", "origin/dev"
    bool    remote = false;
    bool    current = false; // the checked-out branch (git's "*")
};

struct GitRemote {
    QString name;
    QString fetchUrl;
    QString pushUrl;
};

struct GitDiffRow {
    // One row describes both panes. A Change row tints each side from
    // whether that side has a line at all: present on the left means
    // removed, present on the right means added, absent means a gap. That
    // keeps a replaced pair (removed + added on the same row) expressible
    // without a second enum.
    enum Kind { Context, Change, HunkHeader, Notice };

    Kind    kind = Context;
    int     leftNo = -1;
    int     rightNo = -1;
    QString leftText;
    QString rightText;

    bool hasLeft() const  { return leftNo >= 0; }
    bool hasRight() const { return rightNo >= 0; }
};

// Identifies one diff document: which repository, which file, and which pair of
// versions. This is the whole identity of a Git diff tab -- it carries no file
// path, because a diff is not the file (see DocumentModel::setDiffSpec).
struct GitDiffSpec {
    enum Source { WorkingTree, Index, Commit };

    QString repoRoot;     // absolute
    QString path;         // repo-relative, forward slashes
    Source  source = WorkingTree;
    QString commitHash;   // Commit only
    QString parentHash;   // Commit only; empty for a root commit

    bool isValid() const { return !repoRoot.isEmpty() && !path.isEmpty(); }
    bool matches(const GitDiffSpec& other) const;

    // "Editor.cpp (Working Tree)" / "(Staged)" / "(91ac3e <-> b29ff6)".
    QString tabTitle() const;
    QString fileName() const;
};

namespace GitRepository {

// Checks startDir itself for a .git entry (directory or the file a
// worktree/submodule leaves behind). Returns an empty string when there is
// none. Deliberately does not walk up to ancestor directories: Source
// Control is scoped to the current workspace root, and a .git in a parent
// folder must not attach the workspace to that unrelated repository.
QString findRoot(const QString& startDir);

// `status --porcelain=v1 -z --untracked-files=all`. A file that is modified
// both in the index and in the worktree yields two entries, one staged and
// one not, exactly as VS Code shows it.
QVector<GitFileEntry> parseStatus(const QByteArray& data);

GitChangeState changeState(const QVector<GitFileEntry>& entries);

// The final suffix of a file name, case-folded for sorting. A leading dot is
// not an extension: .gitignore and extensionless files both return empty.
QString fileTypeForPath(const QString& path);
bool isValidChangesSortMode(int value);
GitChangesSortMode changesSortModeFromStoredValue(
    int value, GitChangesSortMode fallback = GitChangesSortMode::Path);
void sortEntries(QVector<GitFileEntry>& entries, GitChangesSortMode mode);

// A row action uses the selected rows only when its clicked row belongs to
// that selection. Invalid rows are skipped, paths stay in selection order and
// are de-duplicated. Otherwise the result is the clicked row alone when that
// operation is valid.
bool isFileActionApplicable(GitFileAction action, const GitFileEntry& entry);
QStringList resolveFileActionPaths(GitFileAction action, const GitFileEntry& clicked,
                                   bool clickedSelected,
                                   const QVector<GitFileEntry>& selectedEntries);

// `status --porcelain=v2 --branch -z`, header lines only.
GitBranchInfo parseBranchHeader(const QByteArray& data);

// `log --pretty=format:%H<US>%h<US>%an<US>%aI<US>%s<US>%P`
QVector<GitCommit> parseLog(const QByteArray& data);

// `stash list --pretty=format:%gd<US>%H<US>%aI<US>%gs`
QVector<GitStashEntry> parseStashList(const QByteArray& data);

// `for-each-ref --format=%(refname)<US>%(refname:short)<US>%(HEAD)
//  refs/heads refs/remotes`. Local branches come first, each group sorted the
// way git emitted it. A remote's symbolic HEAD (origin/HEAD) is dropped: it is
// an alias, not a branch a user would switch to.
QVector<GitBranchRef> parseBranchRefs(const QByteArray& data);

// `remote -v`, folding the (fetch)/(push) pair into one record.
QVector<GitRemote> parseRemotes(const QByteArray& data);

// `show --name-status -z --format=` for one commit.
QVector<GitFileEntry> parseNameStatus(const QByteArray& data);

// Converts a unified diff into aligned side-by-side rows. The returned rows
// always describe both sides, so the two panes hold the same line count.
QVector<GitDiffRow> toSideBySide(const QByteArray& unifiedDiff);

// Compact graph lane assignment. Fills lane/lanesBefore/lanesAfter.
void assignLanes(QVector<GitCommit>& commits, int maxLanes = 6);

bool isValidRemoteName(const QString& name);

QChar statusLetter(GitFileState state);

}
