#include "WorkspaceTextSearch.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

using namespace WorkspaceTextSearch;

static bool g_ok = true;

static void expect(bool value, const char* what)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        g_ok = false;
    }
}

static void expectEq(const QString& actual, const QString& expected, const char* what)
{
    if (actual != expected) {
        std::fprintf(stderr, "FAIL: %s\n  expected: %s\n  actual:   %s\n", what,
                     qPrintable(expected), qPrintable(actual));
        g_ok = false;
    }
}

static QList<Match> find(const QString& text, const Query& query)
{
    QList<Match> out;
    searchText(text, buildRegex(query), kMaxTotalMatches, &out);
    return out;
}

static void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // --- replace preview parity: stored captures == real replacement -----
    {
        Query q{QStringLiteral("(foo)(bar)|content"), {false, false, true, true}};
        const QString text = QStringLiteral("foobar content Content CONTENT");
        const QList<Match> matches = find(text, q);
        QString all;
        replaceAll(text, buildRegex(q), q, QStringLiteral("$2$1x"), &all);
        QString viaPreview;
        qsizetype copied = 0;
        for (const Match& m : matches) {
            viaPreview += text.mid(copied, m.column - copied);
            viaPreview += expandReplacementFromCaptures(QStringLiteral("$2$1x"), m.captures, true, true);
            copied = m.column + m.length;
        }
        viaPreview += text.mid(copied);
        expectEq(viaPreview, all, "preview expansion equals replaceAll");
    }

    // --- options ---------------------------------------------------------
    {
        const QString text = QStringLiteral("Foo foo FOO food\nafoo foo");
        expect(find(text, {QStringLiteral("foo"), {}}).size() == 6, "insensitive plain");
        expect(find(text, {QStringLiteral("foo"), {true, false, false}}).size() == 4,
               "match case");
        const auto whole = find(text, {QStringLiteral("foo"), {false, true, false}});
        expect(whole.size() == 4, "whole word count");
        expect(whole.size() == 4 && whole[3].line == 1 && whole[3].column == 5,
               "whole word position");
        expect(find(QStringLiteral("a c++ b xc++"), {QStringLiteral("c++"), {false, true, false}})
                   .size() == 1, "whole word with non-word edge");
        expect(find(QStringLiteral("a.b axb"), {QStringLiteral("a.b"), {}}).size() == 1,
               "plain text is not a regex");
        expect(find(QStringLiteral("a.b axb"), {QStringLiteral("a.b"), {false, false, true}})
                   .size() == 2, "regex");
        expect(find(QStringLiteral("cat concat"), {QStringLiteral("cat"), {false, true, true}})
                   .size() == 1, "regex whole word");
    }

    // --- invalid / empty ---------------------------------------------------
    {
        QString error;
        const QRegularExpression bad =
            buildRegex({QStringLiteral("(["), {false, false, true}}, &error);
        expect(!bad.isValid() && !error.isEmpty(), "invalid regex reports an error");
        expect(find(QStringLiteral("(["), {QStringLiteral("(["), {false, false, true}}).isEmpty(),
               "invalid regex yields nothing");
        expect(buildRegex({QString(), {}}).pattern().isEmpty(), "empty query has no pattern");
        expect(find(QStringLiteral("abc"), {QString(), {}}).isEmpty(), "empty query finds nothing");
    }

    // --- zero-width, line anchors, unicode ---------------------------------
    {
        expect(find(QStringLiteral("abc\n\nabc"), {QStringLiteral("^"), {false, false, true}})
                   .isEmpty(), "zero-width matches are ignored");
        const auto anchored = find(QStringLiteral("x\nab\nab"),
                                   {QStringLiteral("^ab$"), {false, false, true}});
        expect(anchored.size() == 2 && anchored[1].line == 2, "anchors are per line");
        expect(find(QStringLiteral("Cửa hàng cửa"), {QStringLiteral("cửa"), {false, true, false}})
                   .size() == 2, "unicode whole word + case folding");
    }

    // --- previews ----------------------------------------------------------
    {
        const QString line = QStringLiteral("    ") + QString(80, QLatin1Char('x'))
            + QStringLiteral("NEEDLE") + QString(400, QLatin1Char('y'));
        const auto hits = find(line, {QStringLiteral("NEEDLE"), {true, false, false}});
        expect(hits.size() == 1, "long line match");
        if (hits.size() == 1) {
            const Match& m = hits[0];
            expectEq(m.preview.mid(m.previewStart, m.previewLength), QStringLiteral("NEEDLE"),
                     "preview keeps the match visible");
            expect(m.preview.startsWith(QChar(0x2026)) && m.preview.endsWith(QChar(0x2026)),
                   "preview is elided on both sides");
            expect(m.preview.size() < 200, "preview is bounded");
        }
        const auto indented = find(QStringLiteral("\t  hello world"),
                                   {QStringLiteral("world"), {}});
        expect(indented.size() == 1 && indented[0].preview == QStringLiteral("hello world"),
               "leading whitespace is trimmed");
    }

    // --- match cap -----------------------------------------------------------
    {
        QList<Match> out;
        const bool capped = searchText(QStringLiteral("a a a a a"),
                                       buildRegex({QStringLiteral("a"), {}}), 3, &out);
        expect(capped && out.size() == 3, "search stops at the limit");
    }

    // --- replacement ---------------------------------------------------------
    {
        const Query plain{QStringLiteral("foo"), {}};
        QString out;
        int n = replaceAll(QStringLiteral("foo Foo\nbar foo\n"), buildRegex(plain), plain,
                           QStringLiteral("$1"), &out);
        expect(n == 3, "replace all count");
        expectEq(out, QStringLiteral("$1 $1\nbar $1\n"), "plain replacement is literal");

        const Query rx{QStringLiteral("(\\w+)@(\\w+)"), {false, false, true}};
        n = replaceAll(QStringLiteral("a@b c@d"), buildRegex(rx), rx,
                       QStringLiteral("$2 at $1"), &out);
        expect(n == 2, "regex replace count");
        expectEq(out, QStringLiteral("b at a d at c"), "capture groups");

        n = replaceAll(QStringLiteral("no trailing newline foo"), buildRegex(plain), plain,
                       QStringLiteral("x"), &out);
        expectEq(out, QStringLiteral("no trailing newline x"), "no trailing newline added");
        n = replaceAll(QStringLiteral("\n\nfoo\n\n"), buildRegex(plain), plain,
                       QStringLiteral(""), &out);
        expectEq(out, QStringLiteral("\n\n\n\n"), "blank lines preserved");
    }

    // --- preserve case (same semantics as the document Find/Replace) ---------
    {
        Query pc{QStringLiteral("hello"), {}};
        pc.options.preserveCase = true;
        QString out;
        int n = replaceAll(QStringLiteral("hello Hello HELLO hElLo"), buildRegex(pc), pc,
                           QStringLiteral("wORld"), &out);
        expect(n == 4, "preserve case count");
        expectEq(out, QStringLiteral("world World WORLD wORld"), "preserve case replaceAll");

        expect(replaceOne(QStringLiteral("a Hello b"), 0, 2, 5, buildRegex(pc), pc,
                          QStringLiteral("world"), &out), "preserve case replaceOne");
        expectEq(out, QStringLiteral("a World b"), "preserve case replaceOne result");

        Query rx{QStringLiteral("(h)ello"), {false, false, true}};
        rx.options.preserveCase = true;
        replaceAll(QStringLiteral("HELLO"), buildRegex(rx), rx, QStringLiteral("$1i there"), &out);
        expectEq(out, QStringLiteral("HI THERE"), "preserve case after capture expansion");

        pc.options.preserveCase = false;
        replaceAll(QStringLiteral("HELLO"), buildRegex(pc), pc, QStringLiteral("world"), &out);
        expectEq(out, QStringLiteral("world"), "preserve case off is verbatim");
        Query other = pc;
        other.options.preserveCase = true;
        expect(other == pc, "preserve case does not change query identity");
    }

    // --- stale-match verification --------------------------------------------
    {
        const Query q{QStringLiteral("foo"), {}};
        const QRegularExpression re = buildRegex(q);
        const QString text = QStringLiteral("x\nab foo cd\ny");
        QString out;
        expect(replaceOne(text, 1, 3, 3, re, q, QStringLiteral("BAR"), &out), "replaceOne");
        expectEq(out, QStringLiteral("x\nab BAR cd\ny"), "replaceOne result");
        expect(!replaceOne(text, 1, 4, 3, re, q, QStringLiteral("BAR"), &out),
               "shifted offset is rejected");
        expect(!replaceOne(text, 1, 3, 2, re, q, QStringLiteral("BAR"), &out),
               "wrong length is rejected");
        expect(!replaceOne(text, 9, 0, 3, re, q, QStringLiteral("BAR"), &out),
               "missing line is rejected");
        expect(!replaceOne(QStringLiteral("x\nab fo cd"), 1, 3, 3, re, q,
                           QStringLiteral("BAR"), &out), "vanished match is rejected");
        const Query w{QStringLiteral("foo"), {false, true, false}};
        expect(!replaceOne(QStringLiteral("afoo"), 0, 1, 3, buildRegex(w), w,
                           QStringLiteral("BAR"), &out), "whole-word context is honoured");
    }

    // --- directory rules -----------------------------------------------------
    {
        expect(isSkippedDirectory(QStringLiteral(".git")), ".git skipped");
        expect(isSkippedDirectory(QStringLiteral("node_modules")), "node_modules skipped");
        expect(isSkippedDirectory(QStringLiteral("build-arm64")), "build-arm64 skipped");
        expect(!isSkippedDirectory(QStringLiteral("builder-notes")), "builder-notes kept");
        expect(!isSkippedDirectory(QStringLiteral("docs")), "docs kept");
    }

    // --- walk + run ------------------------------------------------------------
    {
        QTemporaryDir tmp;
        const QString root = tmp.path();
        writeFile(root + "/a.md", "hello needle\nsecond line\n");
        writeFile(root + "/sub/b.txt", "needle needle\n");
        writeFile(root + "/.git/config", "needle in git\n");
        writeFile(root + "/node_modules/x.js", "needle\n");
        writeFile(root + "/bin.dat", QByteArray("needle\0\1", 8));
        writeFile(root + "/open.md", "stale disk copy\n");

        const QStringList files = collectFiles(root, {});
        expect(files.size() == 4, "walk skips .git and node_modules");
        for (const QString& f : files)
            expect(!f.contains(QStringLiteral("/.git/")), "no .git file listed");

        const TextLoader loader = [](const QString& path, QString* text) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                return false;
            const QByteArray raw = f.readAll();
            if (raw.contains('\0'))
                return false;
            *text = QString::fromUtf8(raw);
            return true;
        };

        QHash<QString, QString> snapshots;
        snapshots.insert(pathKey(root + "/open.md"), QStringLiteral("unsaved needle here"));

        QHash<QString, int> byName;
        const Query q{QStringLiteral("needle"), {}};
        const RunSummary summary = run(root, buildRegex(q), snapshots, loader, {},
                                       [&](const FileResult& r) {
            byName.insert(QFileInfo(r.path).fileName(), int(r.matches.size()));
        });
        expect(summary.files == 3 && summary.matches == 4, "run totals");
        expect(byName.value("a.md") == 1 && byName.value("b.txt") == 2, "per-file counts");
        expect(byName.value("open.md") == 1, "open buffer is searched instead of the disk copy");
        expect(!byName.contains("bin.dat") && summary.skipped == 1, "binary file skipped");

        const Query stale{QStringLiteral("stale"), {}};
        int staleHits = 0;
        run(root, buildRegex(stale), snapshots, loader, {},
            [&](const FileResult&) { ++staleHits; });
        expect(staleHits == 0, "the stale disk copy is never searched");

        int cancelledHits = 0;
        const RunSummary cancelled = run(root, buildRegex(q), snapshots, loader,
                                         [] { return true; },
                                         [&](const FileResult&) { ++cancelledHits; });
        (void)cancelled;
        expect(cancelledHits == 0, "cancelled run reports nothing");

        int capHits = 0;
        const RunSummary capped = run(root, buildRegex(q), snapshots, loader, {},
                                      [&](const FileResult& r) { capHits += int(r.matches.size()); },
                                      2);
        expect(capped.capped && capHits <= 2, "run honours the match cap");
    }

    if (g_ok)
        std::puts("WorkspaceTextSearchTest: all checks passed");
    return g_ok ? 0 : 1;
}
