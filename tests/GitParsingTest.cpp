// Focused regression tests for the Git tool window's pure parsing layer.
// Deliberately links only Qt6::Core + src/GitRepository.cpp -- no widgets, no
// QProcess, no repository on disk.

#include "GitRepository.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
// Guards the indexing that follows: a size mismatch must report one
// failure, not crash the whole run on QVector::at().
#define REQUIRE(cond) do { check((cond), #cond, __LINE__); if (!(cond)) return; } while (0)

QByteArray z(std::initializer_list<const char*> records)
{
    QByteArray data;
    for (const char* record : records) {
        data.append(record);
        data.append('\0');
    }
    return data;
}

const QChar US = QLatin1Char('\x1f');

// Deliberately NOT built with QString::arg(): "%1" followed by a year reads
// as placeholder %12, which silently produces the wrong record.
QString record(const QStringList& fields)
{
    return fields.join(US);
}

QByteArray joinLines(const QStringList& lines)
{
    return lines.join(QLatin1Char('\n')).toUtf8();
}

// ---------------------------------------------------------------------------

void testParseStatus()
{
    const QVector<GitFileEntry> entries = GitRepository::parseStatus(z({
        " M src/MainWindow.cpp",
        "A  src/GitRunner.cpp",
        " D docs/old.md",
        "?? notes.txt",
        "UU src/conflict.cpp",
        "MM src/both.cpp",
    }));

    // "MM" is staged *and* unstaged: two entries, like VS Code shows it.
    REQUIRE(entries.size() == 7);

    CHECK(entries.at(0).path == QLatin1String("src/MainWindow.cpp"));
    CHECK(entries.at(0).state == GitFileState::Modified);
    CHECK(!entries.at(0).staged);

    CHECK(entries.at(1).path == QLatin1String("src/GitRunner.cpp"));
    CHECK(entries.at(1).state == GitFileState::Added);
    CHECK(entries.at(1).staged);

    CHECK(entries.at(2).state == GitFileState::Deleted);
    CHECK(!entries.at(2).staged);

    CHECK(entries.at(3).state == GitFileState::Untracked);
    CHECK(!entries.at(3).staged);

    CHECK(entries.at(4).state == GitFileState::Conflicted);

    CHECK(entries.at(5).staged);
    CHECK(!entries.at(6).staged);
}

void testParseStatusRename()
{
    // With -z a rename is TWO records: "R  <new>\0<old>\0".
    const QVector<GitFileEntry> entries = GitRepository::parseStatus(z({
        "R  docs/new name.md",
        "docs/old name.md",
        " M src/\xc3\xa9\x63\x6c\x61ir.cpp",
    }));

    REQUIRE(entries.size() == 2);
    CHECK(entries.at(0).state == GitFileState::Renamed);
    CHECK(entries.at(0).path == QLatin1String("docs/new name.md"));
    CHECK(entries.at(0).originalPath == QLatin1String("docs/old name.md"));
    CHECK(entries.at(0).staged);

    // Non-ASCII survives because -z means git never quotes the path.
    CHECK(entries.at(1).path == QString::fromUtf8("src/\xc3\xa9\x63\x6c\x61ir.cpp"));
}

void testParseStatusConflictPairs()
{
    // AA and DD are conflicts, not "staged add" / "staged delete".
    const QVector<GitFileEntry> entries = GitRepository::parseStatus(z({
        "AA a.txt", "DD b.txt", "AU c.txt", "UD d.txt",
    }));
    REQUIRE(entries.size() == 4);
    for (const GitFileEntry& entry : entries)
        CHECK(entry.state == GitFileState::Conflicted);
}

void testCommitChangeStateTransitions()
{
    // The primary action must follow the status sequence a user sees after
    // editing, staging, then committing.
    const QVector<GitFileEntry> unstaged = GitRepository::parseStatus(z({
        " M notes.md", "?? draft.md",
    }));
    CHECK(GitRepository::changeState(unstaged) == GitChangeState::UnstagedOnly);

    const QVector<GitFileEntry> staged = GitRepository::parseStatus(z({
        "M  notes.md", "A  draft.md",
    }));
    CHECK(GitRepository::changeState(staged) == GitChangeState::HasStaged);

    CHECK(GitRepository::changeState({}) == GitChangeState::Clean);

    // A file with both index and worktree edits still commits only its index
    // version; it must never select the Stage All action.
    const QVector<GitFileEntry> mixed = GitRepository::parseStatus(z({ "MM notes.md" }));
    CHECK(GitRepository::changeState(mixed) == GitChangeState::HasStaged);
}

void testMultiFileActionTargetResolution()
{
    const GitFileEntry changed { QStringLiteral("fastmd.md"), {}, GitFileState::Modified, false };
    const GitFileEntry nested { QStringLiteral("docs/markdown-guide é.md"), {},
                                 GitFileState::Modified, false };
    const GitFileEntry staged { QStringLiteral("staged.md"), {}, GitFileState::Added, true };
    const GitFileEntry stagedNested { QStringLiteral("docs/staged guide.md"), {},
                                      GitFileState::Modified, true };
    const GitFileEntry conflict { QStringLiteral("conflict.md"), {}, GitFileState::Conflicted, false };

    // A clicked selected row batches all selected rows to which Stage applies,
    // retaining paths with spaces, Unicode and nested directories intact.
    CHECK(GitRepository::resolveFileActionPaths(GitFileAction::Stage, nested, true,
                                                { changed, nested, staged, conflict })
          == QStringList({ changed.path, nested.path }));
    // Unstage and discard apply the same rule with their own applicability.
    CHECK(GitRepository::resolveFileActionPaths(GitFileAction::Unstage, stagedNested, true,
                                                { changed, staged, stagedNested, conflict })
          == QStringList({ staged.path, stagedNested.path }));
    CHECK(GitRepository::resolveFileActionPaths(GitFileAction::Discard, nested, true,
                                                { changed, nested, staged, conflict })
          == QStringList({ changed.path, nested.path }));
    // Resolve is also a batchable per-file action, but only for conflicts.
    CHECK(GitRepository::resolveFileActionPaths(GitFileAction::Resolve, conflict, true,
                                                { changed, conflict, staged })
          == QStringList({ conflict.path }));

    // Clicking outside the selection remains a strict single-row operation.
    CHECK(GitRepository::resolveFileActionPaths(GitFileAction::Stage, nested, false,
                                                { changed, staged })
          == QStringList({ nested.path }));
    // One selected row remains identical to the existing one-file path.
    CHECK(GitRepository::resolveFileActionPaths(GitFileAction::Discard, changed, true,
                                                { changed })
          == QStringList({ changed.path }));
}

void testFileTypeSort()
{
    CHECK(GitRepository::fileTypeForPath(QStringLiteral("src/Main.CPP")) == QStringLiteral("cpp"));
    CHECK(GitRepository::fileTypeForPath(QStringLiteral("README")) == QString());
    CHECK(GitRepository::fileTypeForPath(QStringLiteral(".gitignore")) == QString());
    CHECK(GitRepository::fileTypeForPath(QStringLiteral("archive.tar.GZ")) == QStringLiteral("gz"));

    QVector<GitFileEntry> entries {
        { QStringLiteral("docs/zeta.MD") },
        { QStringLiteral("src/beta.cpp") },
        { QStringLiteral("src/Alpha.CPP") },
        { QStringLiteral("README") },
        { QStringLiteral(".gitignore") },
        { QStringLiteral("notes/guide.md") },
        { QStringLiteral("todo.txt") },
    };
    GitRepository::sortEntries(entries, GitChangesSortMode::FileType);
    QStringList paths;
    for (const GitFileEntry& entry : entries)
        paths.append(entry.path);
    // Extensionless files and dotfiles sort in the empty-type bucket first;
    // equal types use the existing case-insensitive filename, then path order.
    CHECK(paths == QStringList({ QStringLiteral(".gitignore"), QStringLiteral("README"),
                                 QStringLiteral("src/Alpha.CPP"), QStringLiteral("src/beta.cpp"),
                                 QStringLiteral("notes/guide.md"), QStringLiteral("docs/zeta.MD"),
                                 QStringLiteral("todo.txt") }));

    // These values are what the persisted git/changesSortMode setting restores.
    CHECK(GitRepository::isValidChangesSortMode(static_cast<int>(GitChangesSortMode::FileType)));
    CHECK(!GitRepository::isValidChangesSortMode(-1));
    CHECK(!GitRepository::isValidChangesSortMode(static_cast<int>(GitChangesSortMode::FileType) + 1));

    QTemporaryDir settingsDir;
    REQUIRE(settingsDir.isValid());
    QSettings settings(settingsDir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("git/changesSortMode"),
                      static_cast<int>(GitChangesSortMode::FileType));
    const int restored = settings.value(QStringLiteral("git/changesSortMode")).toInt();
    CHECK(GitRepository::changesSortModeFromStoredValue(restored)
          == GitChangesSortMode::FileType);
    CHECK(GitRepository::changesSortModeFromStoredValue(-1) == GitChangesSortMode::Path);
}

// ---------------------------------------------------------------------------

void testParseBranchHeaderTracking()
{
    const GitBranchInfo info = GitRepository::parseBranchHeader(z({
        "# branch.oid 763ee2de40fd547aa02a7e00b7b37f1b5806b792",
        "# branch.head main",
        "# branch.upstream origin/main",
        "# branch.ab +2 -1",
        "1 .M N... 100644 100644 100644 aaa bbb src/x.cpp",
    }));

    CHECK(info.branch == QLatin1String("main"));
    CHECK(info.upstream == QLatin1String("origin/main"));
    CHECK(info.hasUpstream);
    CHECK(info.ahead == 2);
    CHECK(info.behind == 1);
    CHECK(!info.detached);
}

void testParseBranchHeaderNoUpstream()
{
    const GitBranchInfo info = GitRepository::parseBranchHeader(z({
        "# branch.oid 763ee2de40fd547aa02a7e00b7b37f1b5806b792",
        "# branch.head feature/new-thing",
    }));

    CHECK(info.branch == QLatin1String("feature/new-thing"));
    CHECK(!info.hasUpstream);
    CHECK(info.ahead == 0);
    CHECK(info.behind == 0);
    CHECK(!info.detached);
}

void testParseBranchHeaderDetached()
{
    const GitBranchInfo info = GitRepository::parseBranchHeader(z({
        "# branch.oid 763ee2de40fd547aa02a7e00b7b37f1b5806b792",
        "# branch.head (detached)",
    }));

    CHECK(info.detached);
    CHECK(!info.hasUpstream);
    // Falls back to the short oid so the header has something to show.
    CHECK(info.branch == QLatin1String("763ee2d"));
}

// ---------------------------------------------------------------------------

void testParseLog()
{
    QStringList lines;
    lines << record({ QStringLiteral("aaa111"), QStringLiteral("a11"),
                      QStringLiteral("Ada Lovelace"),
                      QStringLiteral("2026-08-26T09:25:22+07:00"),
                      QStringLiteral("fix: thing"), QStringLiteral("bbb222") })
          << record({ QStringLiteral("bbb222"), QStringLiteral("b22"),
                      QStringLiteral("Alan Turing"),
                      QStringLiteral("2026-08-24T23:59:13+07:00"),
                      QStringLiteral("merge: two"),
                      QStringLiteral("ccc333 ddd444") });

    const QVector<GitCommit> commits = GitRepository::parseLog(joinLines(lines));
    REQUIRE(commits.size() == 2);
    CHECK(commits.at(0).hash == QLatin1String("aaa111"));
    CHECK(commits.at(0).shortHash == QLatin1String("a11"));
    CHECK(commits.at(0).author == QLatin1String("Ada Lovelace"));
    CHECK(commits.at(0).subject == QLatin1String("fix: thing"));
    CHECK(commits.at(0).parents.size() == 1);
    CHECK(commits.at(0).date.isValid());

    CHECK(commits.at(1).parents.size() == 2);
    CHECK(commits.at(1).parents.at(1) == QLatin1String("ddd444"));
}

void testParseLogRootCommitAndPunctuation()
{
    QStringList lines;
    // No parents (root commit), and a subject full of punctuation that a
    // naive split on ':' or '|' would mangle.
    lines << record({ QStringLiteral("root01"), QStringLiteral("r01"),
                      QStringLiteral("Someone"),
                      QStringLiteral("2026-01-01T00:00:00+00:00"),
                      QString::fromUtf8("feat(x): a|b:c, \"d\" \xe2\x80\x94 e"),
                      QString() });

    const QVector<GitCommit> commits = GitRepository::parseLog(joinLines(lines));
    REQUIRE(commits.size() == 1);
    CHECK(commits.at(0).parents.isEmpty());
    CHECK(commits.at(0).subject == QString::fromUtf8("feat(x): a|b:c, \"d\" \xe2\x80\x94 e"));
}

// ---------------------------------------------------------------------------

void testParseStashList()
{
    QStringList lines;
    lines << record({ QStringLiteral("stash@{0}"), QStringLiteral("h0"),
                      QStringLiteral("2026-08-26T09:00:00+07:00"),
                      QStringLiteral("WIP on main: 763ee2d fix(menu): reduce") })
          << record({ QStringLiteral("stash@{1}"), QStringLiteral("h1"),
                      QStringLiteral("2026-08-25T09:00:00+07:00"),
                      QStringLiteral("On main: my message: with a colon") });

    const QVector<GitStashEntry> stashes = GitRepository::parseStashList(joinLines(lines));
    REQUIRE(stashes.size() == 2);
    CHECK(stashes.at(0).ref == QLatin1String("stash@{0}"));
    CHECK(stashes.at(0).subject == QLatin1String("WIP on main: 763ee2d fix(menu): reduce"));
    CHECK(stashes.at(1).subject == QLatin1String("On main: my message: with a colon"));
    CHECK(stashes.at(0).date.isValid());
}

// ---------------------------------------------------------------------------

void testParseBranchRefs()
{
    QStringList lines;
    lines << record({ QStringLiteral("refs/heads/main"), QStringLiteral("main"),
                      QStringLiteral(" ") })
          << record({ QStringLiteral("refs/heads/feature/git-ui"),
                      QStringLiteral("feature/git-ui"), QStringLiteral("*") })
          << record({ QStringLiteral("refs/remotes/origin/HEAD"),
                      QStringLiteral("origin/HEAD"), QStringLiteral(" ") })
          << record({ QStringLiteral("refs/remotes/origin/main"),
                      QStringLiteral("origin/main"), QStringLiteral(" ") });

    const QVector<GitBranchRef> branches = GitRepository::parseBranchRefs(joinLines(lines));
    // Locals first, then remotes; origin/HEAD is an alias and is dropped.
    REQUIRE(branches.size() == 3);
    CHECK(branches.at(0).name == QLatin1String("main"));
    CHECK(!branches.at(0).remote);
    CHECK(!branches.at(0).current);
    CHECK(branches.at(1).name == QLatin1String("feature/git-ui"));
    CHECK(branches.at(1).current);
    CHECK(branches.at(2).name == QLatin1String("origin/main"));
    CHECK(branches.at(2).remote);
    CHECK(!branches.at(2).current);
}

void testParseRemotes()
{
    const QByteArray data =
        "origin\thttps://example.com/a.git (fetch)\n"
        "origin\thttps://example.com/a.git (push)\n"
        "mirror\thttps://example.com/read.git (fetch)\n"
        "mirror\thttps://example.com/write.git (push)\n";

    const QVector<GitRemote> remotes = GitRepository::parseRemotes(data);
    REQUIRE(remotes.size() == 2);
    CHECK(remotes.at(0).name == QLatin1String("origin"));
    CHECK(remotes.at(0).fetchUrl == QLatin1String("https://example.com/a.git"));
    CHECK(remotes.at(0).pushUrl == remotes.at(0).fetchUrl);
    CHECK(remotes.at(1).fetchUrl == QLatin1String("https://example.com/read.git"));
    CHECK(remotes.at(1).pushUrl == QLatin1String("https://example.com/write.git"));
}

void testParseRemotesUrlWithSpace()
{
    // A local path remote can contain spaces; the " (fetch)" suffix must be
    // stripped from the right, never by splitting on whitespace.
    const QByteArray data =
        "local\tC:/My Repos/thing.git (fetch)\n"
        "local\tC:/My Repos/thing.git (push)\n";

    const QVector<GitRemote> remotes = GitRepository::parseRemotes(data);
    REQUIRE(remotes.size() == 1);
    CHECK(remotes.at(0).fetchUrl == QLatin1String("C:/My Repos/thing.git"));
}

void testRemoteNameValidation()
{
    CHECK(GitRepository::isValidRemoteName(QStringLiteral("origin")));
    CHECK(GitRepository::isValidRemoteName(QStringLiteral("up-stream_2")));
    CHECK(!GitRepository::isValidRemoteName(QString()));
    CHECK(!GitRepository::isValidRemoteName(QStringLiteral("has space")));
    CHECK(!GitRepository::isValidRemoteName(QStringLiteral("a/b")));
    CHECK(!GitRepository::isValidRemoteName(QStringLiteral("-rf")));
}

// ---------------------------------------------------------------------------

void testSideBySideInvariant(const QVector<GitDiffRow>& rows)
{
    // The invariant the whole widget rests on: every row describes both
    // panes, so the panes always hold the same number of lines.
    for (const GitDiffRow& row : rows) {
        CHECK(row.leftNo >= -1);
        CHECK(row.rightNo >= -1);
    }
}

void testToSideBySidePureAdd()
{
    const QByteArray diff =
        "diff --git a/f.txt b/f.txt\n"
        "index 111..222 100644\n"
        "--- a/f.txt\n"
        "+++ b/f.txt\n"
        "@@ -1,2 +1,4 @@\n"
        " one\n"
        "+two\n"
        "+three\n"
        " four\n";

    const QVector<GitDiffRow> rows = GitRepository::toSideBySide(diff);
    testSideBySideInvariant(rows);

    REQUIRE(rows.size() == 5);
    CHECK(rows.at(0).kind == GitDiffRow::HunkHeader);
    CHECK(rows.at(1).kind == GitDiffRow::Context);
    CHECK(rows.at(1).leftNo == 1 && rows.at(1).rightNo == 1);
}

void testToSideBySideReplaceUnequal()
{
    // 3 removed against 1 added: the right side must get two gap rows.
    const QByteArray diff =
        "@@ -10,3 +10,1 @@\n"
        "-alpha\n"
        "-beta\n"
        "-gamma\n"
        "+single\n";

    const QVector<GitDiffRow> rows = GitRepository::toSideBySide(diff);
    testSideBySideInvariant(rows);

    REQUIRE(rows.size() == 4);
    CHECK(rows.at(1).hasLeft() && rows.at(1).hasRight());
    CHECK(rows.at(1).leftText == QLatin1String("alpha"));
    CHECK(rows.at(1).rightText == QLatin1String("single"));

    CHECK(rows.at(2).hasLeft() && !rows.at(2).hasRight());
    CHECK(rows.at(3).hasLeft() && !rows.at(3).hasRight());

    // Left line numbers continue from the hunk's start.
    CHECK(rows.at(1).leftNo == 10);
    CHECK(rows.at(3).leftNo == 12);
    CHECK(rows.at(1).rightNo == 10);
}

void testToSideBySidePureDelete()
{
    const QByteArray diff =
        "@@ -5,2 +5,1 @@\n"
        " keep\n"
        "-gone\n";

    const QVector<GitDiffRow> rows = GitRepository::toSideBySide(diff);
    REQUIRE(rows.size() == 3);
    CHECK(rows.at(2).hasLeft());
    CHECK(!rows.at(2).hasRight());
    CHECK(rows.at(2).rightText.isEmpty());
}

void testToSideBySideMultipleHunksAndNoNewline()
{
    const QByteArray diff =
        "@@ -1,1 +1,1 @@\n"
        "-a\n"
        "+b\n"
        "\\ No newline at end of file\n"
        "@@ -20,1 +20,1 @@\n"
        "-c\n"
        "+d\n";

    const QVector<GitDiffRow> rows = GitRepository::toSideBySide(diff);
    testSideBySideInvariant(rows);

    int headers = 0;
    for (const GitDiffRow& row : rows) {
        if (row.kind == GitDiffRow::HunkHeader)
            ++headers;
        // The "\ No newline" marker must never become a visible row.
        CHECK(!row.leftText.startsWith(QLatin1Char('\\')));
    }
    CHECK(headers == 2);
    REQUIRE(rows.size() == 4);

    // The second hunk restarts numbering from its own @@ range.
    CHECK(rows.at(3).leftNo == 20);
}

void testToSideBySideEmptyContextLine()
{
    // An empty context line must still advance both line counters.
    const QByteArray diff =
        "@@ -1,3 +1,3 @@\n"
        " one\n"
        "\n"
        "-three\n"
        "+THREE\n";

    const QVector<GitDiffRow> rows = GitRepository::toSideBySide(diff);
    REQUIRE(rows.size() == 4);
    CHECK(rows.at(2).kind == GitDiffRow::Context);
    CHECK(rows.at(2).leftNo == 2);
    CHECK(rows.at(3).leftNo == 3);
    CHECK(rows.at(3).rightNo == 3);
}

void testToSideBySideBinary()
{
    const QByteArray diff =
        "diff --git a/img.png b/img.png\n"
        "index 111..222 100644\n"
        "Binary files a/img.png and b/img.png differ\n";

    const QVector<GitDiffRow> rows = GitRepository::toSideBySide(diff);
    REQUIRE(rows.size() == 1);
    CHECK(rows.at(0).kind == GitDiffRow::Notice);
}

void testToSideBySideEmpty()
{
    CHECK(GitRepository::toSideBySide(QByteArray()).isEmpty());
    CHECK(GitRepository::toSideBySide("   \n  \n").isEmpty());
}

// ---------------------------------------------------------------------------

// The tab title is the only place a diff document identifies itself, so its
// shape (and the 6-character hashes) is worth pinning down.
void testDiffSpecTabTitles()
{
    GitDiffSpec working;
    working.repoRoot = QStringLiteral("E:/repo");
    working.path = QStringLiteral("src/ui/Editor.cpp");
    working.source = GitDiffSpec::WorkingTree;
    CHECK(working.fileName() == QStringLiteral("Editor.cpp"));
    CHECK(working.tabTitle() == QStringLiteral("Editor.cpp (Working Tree)"));
    CHECK(working.isValid());

    GitDiffSpec staged = working;
    staged.source = GitDiffSpec::Index;
    CHECK(staged.tabTitle() == QStringLiteral("Editor.cpp (Staged)"));
    // Same file, different comparison: these must not de-duplicate onto one tab.
    CHECK(!staged.matches(working));

    GitDiffSpec commit = working;
    commit.source = GitDiffSpec::Commit;
    commit.commitHash = QStringLiteral("b29ff6cabc1234567890");
    commit.parentHash = QStringLiteral("91ac3edeadbeef123456");
    CHECK(commit.tabTitle() == QString::fromUtf8("Editor.cpp (91ac3e \xE2\x86\x94 b29ff6)"));

    // A root commit has no parent to name on the left of the arrow.
    GitDiffSpec root = commit;
    root.parentHash.clear();
    CHECK(root.tabTitle() == QStringLiteral("Editor.cpp (b29ff6)"));
    // parentHash is not part of identity: the same commit is the same diff.
    CHECK(root.matches(commit));

    // Windows paths differ only by case; the same repository must match.
    GitDiffSpec other = working;
    other.repoRoot = QStringLiteral("e:/REPO");
    CHECK(other.matches(working));

    GitDiffSpec empty;
    CHECK(!empty.isValid());
}

// ---------------------------------------------------------------------------

void testParseNameStatus()
{
    const QVector<GitFileEntry> entries = GitRepository::parseNameStatus(z({
        "M", "src/MainWindow.cpp",
        "A", "src/GitWindow.cpp",
        "D", "docs/gone.md",
        "R100", "docs/old.md", "docs/new.md",
    }));

    REQUIRE(entries.size() == 4);
    CHECK(entries.at(0).state == GitFileState::Modified);
    CHECK(entries.at(1).state == GitFileState::Added);
    CHECK(entries.at(2).state == GitFileState::Deleted);
    CHECK(entries.at(3).state == GitFileState::Renamed);
    CHECK(entries.at(3).originalPath == QLatin1String("docs/old.md"));
    CHECK(entries.at(3).path == QLatin1String("docs/new.md"));
}

// ---------------------------------------------------------------------------

GitCommit makeCommit(const QString& hash, const QStringList& parents)
{
    GitCommit commit;
    commit.hash = hash;
    commit.shortHash = hash;
    commit.parents = parents;
    return commit;
}

void testAssignLanesLinear()
{
    QVector<GitCommit> commits {
        makeCommit(QStringLiteral("c"), { QStringLiteral("b") }),
        makeCommit(QStringLiteral("b"), { QStringLiteral("a") }),
        makeCommit(QStringLiteral("a"), {}),
    };
    GitRepository::assignLanes(commits);

    for (const GitCommit& commit : commits)
        CHECK(commit.lane == 0);
    // The root commit closes the only lane.
    CHECK(commits.last().lanesAfter.isEmpty());
}

void testAssignLanesMerge()
{
    //  m  (merge of a and b)
    //  |\
    //  a b
    //  |/
    //  r
    QVector<GitCommit> commits {
        makeCommit(QStringLiteral("m"), { QStringLiteral("a"), QStringLiteral("b") }),
        makeCommit(QStringLiteral("a"), { QStringLiteral("r") }),
        makeCommit(QStringLiteral("b"), { QStringLiteral("r") }),
        makeCommit(QStringLiteral("r"), {}),
    };
    GitRepository::assignLanes(commits);

    REQUIRE(commits.size() == 4);
    CHECK(commits.at(0).lane == 0);
    // The merge opens a second lane for its extra parent.
    CHECK(commits.at(0).lanesAfter.size() == 2);
    CHECK(commits.at(1).lane == 0);
    CHECK(commits.at(2).lane == 1);
    // Both branches rejoin at the root, so only one lane survives it.
    CHECK(commits.at(3).lane == 0);
    CHECK(commits.at(3).lanesAfter.isEmpty());
}

void testAssignLanesOverflowIsClamped()
{
    // Ten independent roots would want ten lanes; maxLanes must clamp the
    // reported column without crashing or losing rows.
    QVector<GitCommit> commits;
    for (int i = 0; i < 10; ++i)
        commits.append(makeCommit(QStringLiteral("h%1").arg(i), {}));

    GitRepository::assignLanes(commits, 3);
    REQUIRE(commits.size() == 10);
    for (const GitCommit& commit : commits) {
        CHECK(commit.lane >= 0);
        CHECK(commit.lane < 3);
        for (int lane : commit.lanesAfter)
            CHECK(lane < 3);
    }
}

}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testParseStatus();
    testParseStatusRename();
    testParseStatusConflictPairs();
    testCommitChangeStateTransitions();
    testMultiFileActionTargetResolution();
    testFileTypeSort();

    testParseBranchHeaderTracking();
    testParseBranchHeaderNoUpstream();
    testParseBranchHeaderDetached();

    testParseLog();
    testParseLogRootCommitAndPunctuation();

    testParseStashList();

    testParseRemotes();
    testParseRemotesUrlWithSpace();
    testParseBranchRefs();
    testRemoteNameValidation();

    testToSideBySidePureAdd();
    testToSideBySideReplaceUnequal();
    testToSideBySidePureDelete();
    testToSideBySideMultipleHunksAndNoNewline();
    testToSideBySideEmptyContextLine();
    testToSideBySideBinary();
    testToSideBySideEmpty();

    testDiffSpecTabTitles();

    testParseNameStatus();

    testAssignLanesLinear();
    testAssignLanesMerge();
    testAssignLanesOverflowIsClamped();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
