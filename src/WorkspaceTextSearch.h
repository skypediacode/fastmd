#pragma once

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <functional>

// Pure engine behind "Search in Files": matching, previews, replacement and the
// workspace walk. Qt Core only (no Widgets, no document model) so it runs on a
// worker thread and tests/WorkspaceTextSearchTest.cpp can drive it directly.
//
// Matching is line based: a match never spans a line break, and `^`/`$` anchor
// to a line. Text is the editor's own representation -- '\n' separated.
namespace WorkspaceTextSearch {

struct Options {
    bool matchCase = false;
    bool wholeWord = false;
    bool regex = false;
    // Replacement-side only: it never changes what matches, so it is left out
    // of equality and toggling it does not rerun the search.
    bool preserveCase = false;
    bool operator==(const Options& o) const
    {
        return matchCase == o.matchCase && wholeWord == o.wholeWord && regex == o.regex;
    }
};

struct Query {
    QString text;
    Options options;
    bool operator==(const Query& o) const { return text == o.text && options == o.options; }
};

struct Match {
    int line = 0;      // 0-based
    int column = 0;    // UTF-16 offset in the line
    int length = 0;
    QString preview;   // trimmed, elided line text
    int previewStart = 0;
    int previewLength = 0;
    // Group 0 (the matched text) then groups 1..9, kept so a Replace preview can
    // run the real expansion without the source line. Display-only: navigation
    // and replacement still use line/column/length and re-verify them.
    QStringList captures;
};

struct FileResult {
    QString path;
    QList<Match> matches;
};

constexpr int kMaxTotalMatches = 10000;
constexpr qint64 kMaxFileBytes = 5 * 1024 * 1024;

// The pattern is compiled once per query. Whole Word in regex mode wraps the
// pattern the way FindController does; in plain mode the anchors are only
// applied on the sides where the term starts/ends with a word character, so
// "c++" is still findable as a whole word.
QRegularExpression buildRegex(const Query& query, QString* error = nullptr);

// Appends the matches of `re` in `text`. Zero-length matches are ignored.
// Stops once `out` holds `limit` entries; returns true when it stopped early.
bool searchText(const QString& text, const QRegularExpression& re, int limit,
                QList<Match>* out);

Match makeMatch(const QString& line, int lineNumber, int column, int length);

// `$1`..`$9` are expanded in regex mode only -- the same rule as the document
// Find/Replace's FindController. Preserve Case is then applied to the expanded
// text from the casing of the whole match (applyPreservedCase, shared).
QString expandReplacement(const QString& replacement, const QRegularExpressionMatch& match,
                          bool regexMode, bool preserveCase = false);

// Same expansion from stored captures (Match::captures); expandReplacement()
// above is this function's only other caller-side twin, so a preview can never
// disagree with a real replacement.
QString expandReplacementFromCaptures(const QString& replacement, const QStringList& captures,
                                      bool regexMode, bool preserveCase);

// Re-checks a stored match against the *current* line text. Returns false when
// the line no longer holds exactly that match, so a stale offset can never be
// replaced. On success `replacement` is the text to insert.
bool replacementForMatch(const QString& line, int column, int length,
                         const QRegularExpression& re, const Query& query,
                         const QString& replacement, QString* expanded);

// Replaces every match in `text`. Returns the number of replacements.
int replaceAll(const QString& text, const QRegularExpression& re, const Query& query,
               const QString& replacement, QString* result);

// Replaces one stored match after re-verifying it. False when it is stale.
bool replaceOne(const QString& text, int line, int column, int length,
                const QRegularExpression& re, const Query& query,
                const QString& replacement, QString* result);

// Directories never searched. Everything else, hidden files included, is.
bool isSkippedDirectory(const QString& name);
// Every regular file below `root` outside skipped directories. Symlinks and
// junctions are neither followed nor listed, so the walk cannot loop.
QStringList collectFiles(const QString& root, const std::function<bool()>& cancelled);

// Key under which an open document's snapshot is matched to a walked file.
QString pathKey(const QString& path);

struct RunSummary {
    int files = 0;
    int matches = 0;
    int skipped = 0;
    bool capped = false;
    bool cancelled = false;
};

using TextLoader = std::function<bool(const QString& path, QString* text)>;

// Walks `root` and reports each file that matches. A file open in an editor is
// searched through its snapshot (`snapshots`, keyed by pathKey()) instead of the
// disk copy, dirty or not. `loader` returns false for a file that is not text.
RunSummary run(const QString& root, const QRegularExpression& re,
               const QHash<QString, QString>& snapshots, const TextLoader& loader,
               const std::function<bool()>& cancelled,
               const std::function<void(const FileResult&)>& onFile,
               int maxTotalMatches = kMaxTotalMatches);

}  // namespace WorkspaceTextSearch
