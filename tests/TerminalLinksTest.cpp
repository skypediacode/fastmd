// Regression tests for the Terminal's host-side link authority. Links only
// Qt6::Core + src/TerminalLinks.cpp: every input here is what an untrusted page
// could send, and every decision is the one the host acts on.

#include "TerminalLinks.h"

#include <QDir>
#include <QFile>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUrl>

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

using TerminalLinks::LocalLink;
using TerminalLinks::Target;
using Kind = TerminalLinks::Target::Kind;

LocalLink parsed(const QString& text)
{
    LocalLink link;
    if (!TerminalLinks::parseLocal(text, &link))
        link.path = QStringLiteral("<rejected>");
    return link;
}

void touch(const QString& path)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write("x\n");
}

void testLineAndColumnSuffixes()
{
    LocalLink link = parsed(QStringLiteral("src/main.cpp:42"));
    CHECK(link.path == QLatin1String("src/main.cpp") && link.line == 42 && link.column == 0);
    link = parsed(QStringLiteral("src/main.cpp:42:15"));
    CHECK(link.line == 42 && link.column == 15);
    link = parsed(QStringLiteral("C:\\Projects\\FastMD\\src\\main.cpp:120:8"));
    CHECK(link.path == QLatin1String("C:\\Projects\\FastMD\\src\\main.cpp"));
    CHECK(link.line == 120 && link.column == 8);
    link = parsed(QStringLiteral("src/main.cpp(42)"));
    CHECK(link.path == QLatin1String("src/main.cpp") && link.line == 42);
    link = parsed(QStringLiteral("src/main.cpp(42,15)"));
    CHECK(link.line == 42 && link.column == 15);
    link = parsed(QStringLiteral("main.cpp(42, 15)"));
    CHECK(link.path == QLatin1String("main.cpp") && link.line == 42 && link.column == 15);
    link = parsed(QStringLiteral("\"x.py\", line 7"));
    CHECK(link.path == QLatin1String("x.py") && link.line == 7);
    link = parsed(QStringLiteral("\"C:\\My Projects\\notes.md\""));
    CHECK(link.path == QLatin1String("C:\\My Projects\\notes.md") && link.line == 0);
    link = parsed(QStringLiteral("\"C:\\My Projects\\notes.md\":3:4"));
    CHECK(link.line == 3 && link.column == 4);
    link = parsed(QStringLiteral("C:\\x"));
    CHECK(link.path == QLatin1String("C:\\x") && link.line == 0);
}

void testInvalidPositionsAreIgnored()
{
    LocalLink link = parsed(QStringLiteral("a/b.md:0"));
    CHECK(link.path == QLatin1String("a/b.md") && link.line == 0);
    link = parsed(QStringLiteral("a/b.md:0:5"));
    CHECK(link.line == 0 && link.column == 0);
    link = parsed(QStringLiteral("a/b.md:99999999999"));
    CHECK(link.line == 0);
}

void testUnsafePathsAreRejected()
{
    using TerminalLinks::isSafeLocalPath;
    CHECK(isSafeLocalPath(QStringLiteral("C:\\Users\\Example\\notes.md")));
    CHECK(isSafeLocalPath(QStringLiteral("C:/Projects/FastMD/docs/guide.md")));
    CHECK(isSafeLocalPath(QStringLiteral("../docs/guide.md")));
    CHECK(isSafeLocalPath(QString::fromUtf8("docs/\xE6\x96\x87\xE6\xA1\xA3/\xC3\xA9t\xC3\xA9.md")));
    CHECK(!isSafeLocalPath(QString()));
    CHECK(!isSafeLocalPath(QStringLiteral("\\\\server\\share\\notes.md")));
    CHECK(!isSafeLocalPath(QStringLiteral("//server/share/notes.md")));
    CHECK(!isSafeLocalPath(QStringLiteral("\\\\?\\C:\\x.md")));
    CHECK(!isSafeLocalPath(QStringLiteral("\\\\.\\PhysicalDrive0")));
    CHECK(!isSafeLocalPath(QStringLiteral("\\Windows\\x.md")));
    CHECK(!isSafeLocalPath(QStringLiteral("C:foo.md")));
    CHECK(!isSafeLocalPath(QStringLiteral("notes.md:secret")));
    CHECK(!isSafeLocalPath(QStringLiteral("C:\\x\\notes.md:secret:$DATA")));
    CHECK(!isSafeLocalPath(QStringLiteral("C:\\x\\NUL")));
    CHECK(!isSafeLocalPath(QStringLiteral("con.txt")));
    CHECK(!isSafeLocalPath(QStringLiteral("dir/COM1.log")));
    CHECK(!isSafeLocalPath(QStringLiteral("lpt9")));
    CHECK(!isSafeLocalPath(QStringLiteral("CONOUT$")));
    CHECK(isSafeLocalPath(QStringLiteral("console.md")));
    CHECK(isSafeLocalPath(QStringLiteral("COM10.md")));
    CHECK(!isSafeLocalPath(QStringLiteral("a\nb.md")));
    CHECK(!isSafeLocalPath(QString(QStringLiteral("a")) + QChar(0) + QStringLiteral("b.md")));
    CHECK(!isSafeLocalPath(QStringLiteral("a/b?.md")));
    CHECK(!isSafeLocalPath(QStringLiteral("a/<b>.md")));
    CHECK(parsed(QStringLiteral("\\\\evil.example\\share\\a.md")).path == QLatin1String("<rejected>"));
    CHECK(parsed(QStringLiteral("\"unterminated.md")).path == QLatin1String("<rejected>"));
}

void testSchemeAllowlist()
{
    const QString none;
    Target target = TerminalLinks::resolve(QStringLiteral("https://example.com/p?q=1#frag"), none, none);
    CHECK(target.kind == Kind::Web);
    CHECK(target.url.query() == QLatin1String("q=1") && target.url.fragment() == QLatin1String("frag"));
    CHECK(TerminalLinks::resolve(QStringLiteral("http://localhost:3000"), none, none).kind == Kind::Web);
    CHECK(TerminalLinks::resolve(QStringLiteral("mailto:someone@example.com"), none, none).kind == Kind::Web);
    for (const char* rejected : {
             "javascript:alert(1)", "vbscript:msgbox(1)", "ms-settings:privacy",
             "search-ms:query=x", "ms-msdt:/id", "data:text/html,<b>x</b>",
             "ftp://example.com/x", "http://", "https:///path", "mailto:",
             "about:blank", "shell:startup" }) {
        CHECK(TerminalLinks::resolve(QString::fromLatin1(rejected), none, none).kind == Kind::Invalid);
    }
}

void testFileUrls(const QString& root)
{
    const QString file = root + QStringLiteral("/a b.md");
    touch(file);
    const QString url = QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded);
    Target target = TerminalLinks::resolve(url, QString(), QString());
    CHECK(target.kind == Kind::File && target.path == QDir::cleanPath(file));

    const QString hosted = QStringLiteral("file://localhost/") + QDir(root).absolutePath()
        + QStringLiteral("/a%20b.md");
    CHECK(TerminalLinks::resolve(hosted, QString(), QString()).kind == Kind::File);
    const QString ownHost = QStringLiteral("file://") + QSysInfo::machineHostName() + QLatin1Char('/')
        + QDir(root).absolutePath() + QStringLiteral("/a%20b.md");
    CHECK(TerminalLinks::resolve(ownHost, QString(), QString()).kind == Kind::File);
    CHECK(TerminalLinks::resolve(QStringLiteral("file://evil.example/share/a.md"),
                                 QString(), QString()).kind == Kind::Invalid);

    // Git Bash spells C:/Users as /c/Users.
    const QString absolute = QDir(root).absolutePath();
    const QString msys = QStringLiteral("file:///") + absolute.at(0).toLower()
        + absolute.mid(2) + QStringLiteral("/a%20b.md");
    target = TerminalLinks::resolve(msys, QString(), QString());
    CHECK(target.kind == Kind::File);
}

void testRelativeResolutionAndCwdPolicy(const QString& root)
{
    QDir dir(root);
    dir.mkpath(QStringLiteral("start/src"));
    dir.mkpath(QStringLiteral("cwd/src"));
    dir.mkpath(QStringLiteral("start/docs"));
    touch(root + QStringLiteral("/start/src/main.cpp"));
    touch(root + QStringLiteral("/start/only-in-start.md"));
    touch(root + QStringLiteral("/cwd/src/main.cpp"));
    const QString start = root + QStringLiteral("/start");
    const QString cwd = root + QStringLiteral("/cwd");

    Target target = TerminalLinks::resolve(QStringLiteral("src/main.cpp:42:15"), QString(), start);
    CHECK(target.kind == Kind::File);
    CHECK(target.path == QDir::cleanPath(start + QStringLiteral("/src/main.cpp")));
    CHECK(target.line == 42 && target.column == 15);

    // A reported cwd wins outright: the start directory is not a fallback.
    target = TerminalLinks::resolve(QStringLiteral("src/main.cpp"), cwd, start);
    CHECK(target.path == QDir::cleanPath(cwd + QStringLiteral("/src/main.cpp")));
    target = TerminalLinks::resolve(QStringLiteral("only-in-start.md"), cwd, start);
    CHECK(target.kind == Kind::NotFound
          && target.path == QDir::cleanPath(cwd + QStringLiteral("/only-in-start.md")));

    // An unusable report is no report.
    CHECK(TerminalLinks::resolve(QStringLiteral("./only-in-start.md"),
                                 root + QStringLiteral("/missing"), start).kind == Kind::File);
    CHECK(TerminalLinks::resolve(QStringLiteral("./only-in-start.md"),
                                 QStringLiteral("relative/dir"), start).kind == Kind::File);
    CHECK(TerminalLinks::resolve(QStringLiteral("./only-in-start.md"),
                                 QStringLiteral("\\\\server\\share"), start).kind == Kind::File);

    // No usable base at all: relative links do nothing rather than guess.
    CHECK(TerminalLinks::resolve(QStringLiteral("src/main.cpp"), QString(), QString()).kind
          == Kind::Invalid);

    CHECK(TerminalLinks::resolve(QStringLiteral("./docs"), QString(), start).kind
          == Kind::Directory);
    CHECK(TerminalLinks::resolve(QStringLiteral("../start/docs"), QString(),
                                 cwd).kind == Kind::Directory);
    CHECK(TerminalLinks::resolve(start, QString(), QString()).kind == Kind::Directory);
    CHECK(TerminalLinks::resolve(QStringLiteral("src/missing.cpp:3"), QString(), start).kind
          == Kind::NotFound);
    CHECK(TerminalLinks::resolve(QStringLiteral("C:\\definitely\\not\\here.md"),
                                 QString(), QString()).kind == Kind::NotFound);
}

// The page underlines an unquoted drive path through its spaces
// ("D:/Lucy Cua Nam Wife Chip/MinhMaiAnh/"); the host must open exactly that,
// and settle for the longest existing prefix when the run overshot.
void testUnquotedPathsWithSpaces(const QString& root)
{
    QDir dir(root);
    dir.mkpath(QStringLiteral("My Notes/Sub Dir/toan11"));
    touch(root + QStringLiteral("/My Notes/Sub Dir/toan11/bai tap.md"));
    touch(root + QStringLiteral("/My Notes/Sub Dir/tool.exe"));
    const QString base = root + QStringLiteral("/My Notes/Sub Dir");
    const QString native = QDir::toNativeSeparators(base);

    Target target = TerminalLinks::resolve(base + QLatin1Char('/'), QString(), QString());
    CHECK(target.kind == Kind::Directory && target.path == QDir::cleanPath(base));
    target = TerminalLinks::resolve(native, QString(), QString());
    CHECK(target.kind == Kind::Directory && target.path == QDir::cleanPath(base));
    target = TerminalLinks::resolve(native + QStringLiteral("\\toan11\\bai tap.md:12:4"),
                                    QString(), QString());
    CHECK(target.kind == Kind::File && target.line == 12 && target.column == 4);
    CHECK(target.path == QDir::cleanPath(base + QStringLiteral("/toan11/bai tap.md")));

    // An existing executable resolves as a file; MainWindow only reveals it.
    target = TerminalLinks::resolve(native + QStringLiteral("\\tool.exe"), QString(), QString());
    CHECK(target.kind == Kind::File);

    // "PS C:\My Notes\Sub Dir> and more" style overshoot: longest prefix wins.
    target = TerminalLinks::resolve(native + QStringLiteral(" is gone"), QString(), QString());
    CHECK(target.kind == Kind::Directory && target.path == QDir::cleanPath(base));
    target = TerminalLinks::resolve(root + QStringLiteral("/Nope Here/x.md"), QString(), QString());
    CHECK(target.kind == Kind::NotFound
          && target.path == QDir::cleanPath(root + QStringLiteral("/Nope Here/x.md")));
    // Quoted text says exactly where the path ends: no prefix guessing.
    CHECK(TerminalLinks::resolve(QLatin1Char('"') + native + QStringLiteral(" is gone\""),
                                 QString(), QString()).kind == Kind::NotFound);

    // A reported cwd with spaces (PowerShell's OSC 9;9) anchors relative links.
    target = TerminalLinks::resolve(QStringLiteral("toan11/"), native, root);
    CHECK(target.kind == Kind::Directory
          && target.path == QDir::cleanPath(base + QStringLiteral("/toan11")));
    target = TerminalLinks::resolve(QStringLiteral("toan11\\bai tap.md:3"), native, root);
    CHECK(target.kind == Kind::File && target.line == 3);
}

// The page now underlines bare file names and backslash-relative paths as
// typed in PowerShell; each must open from the reported cwd, and a missing one
// must report the absolute path it was looked for at rather than just failing.
void testBareNamesAndNestedRelativePaths(const QString& root)
{
    QDir dir(root);
    dir.mkpath(QStringLiteral("Bare Cwd Ở Đây/toan11"));
    dir.mkpath(QStringLiteral("bare-start"));
    const QString cwd = root + QStringLiteral("/Bare Cwd Ở Đây");
    const QString start = root + QStringLiteral("/bare-start");
    const QString native = QDir::toNativeSeparators(cwd);
    touch(cwd + QStringLiteral("/README.md"));
    touch(cwd + QStringLiteral("/ung-dung.md"));
    touch(cwd + QStringLiteral("/toan11/on-tap.md"));

    struct Case { const char* text; const char* path; int line; int column; };
    for (const Case& c : {
             Case{ "README.md", "/README.md", 0, 0 },
             Case{ "README.md:12", "/README.md", 12, 0 },
             Case{ "ung-dung.md", "/ung-dung.md", 0, 0 },
             Case{ "toan11\\on-tap.md", "/toan11/on-tap.md", 0, 0 },
             Case{ "toan11\\on-tap.md:24:5", "/toan11/on-tap.md", 24, 5 },
             Case{ "toan11/on-tap.md(24,5)", "/toan11/on-tap.md", 24, 5 },
             Case{ "./toan11/on-tap.md", "/toan11/on-tap.md", 0, 0 },
             Case{ ".\\toan11\\on-tap.md:3", "/toan11/on-tap.md", 3, 0 },
         }) {
        const Target target = TerminalLinks::resolve(QString::fromLatin1(c.text), native, start);
        CHECK(target.kind == Kind::File);
        CHECK(target.path == QDir::cleanPath(cwd + QString::fromLatin1(c.path)));
        CHECK(target.line == c.line && target.column == c.column);
    }

    // Absolute, unquoted, spaces and non-ASCII in the directory.
    Target target = TerminalLinks::resolve(native + QStringLiteral("\\toan11\\on-tap.md:24:5"),
                                           QString(), QString());
    CHECK(target.kind == Kind::File && target.line == 24 && target.column == 5);
    CHECK(target.path == QDir::cleanPath(cwd + QStringLiteral("/toan11/on-tap.md")));
    target = TerminalLinks::resolve(native + QStringLiteral("\\ung-dung.md"), QString(), QString());
    CHECK(target.kind == Kind::File);

    // `name.ext:N` once read as a URL with scheme "name.ext" and was refused.
    touch(cwd + QStringLiteral("/main.c"));
    target = TerminalLinks::resolve(QStringLiteral("main.c:3:5"), native, start);
    CHECK(target.kind == Kind::File && target.line == 3 && target.column == 5);
    // A real scheme with a number is then just a missing local file, never Web.
    CHECK(TerminalLinks::resolve(QStringLiteral("javascript:1"), native, start).kind
          == Kind::NotFound);

    // Underlined but absent: NotFound, naming the cwd-based location.
    target = TerminalLinks::resolve(QStringLiteral("toan11\\missing.md:24:5"), native, start);
    CHECK(target.kind == Kind::NotFound);
    CHECK(target.path == QDir::cleanPath(cwd + QStringLiteral("/toan11/missing.md")));
    target = TerminalLinks::resolve(native + QStringLiteral("\\no such file.md"), QString(), QString());
    CHECK(target.kind == Kind::NotFound
          && target.path == QDir::cleanPath(cwd + QStringLiteral("/no such file.md")));
    // A bare name only in the start directory is not found once a cwd is reported.
    touch(start + QStringLiteral("/start-only.md"));
    CHECK(TerminalLinks::resolve(QStringLiteral("start-only.md"), native, start).kind
          == Kind::NotFound);
    CHECK(TerminalLinks::resolve(QStringLiteral("start-only.md"), QString(), start).kind
          == Kind::File);
}

void testFileUrlPositions(const QString& root)
{
    touch(root + QStringLiteral("/pos.md"));
    const QString url = QUrl::fromLocalFile(root + QStringLiteral("/pos.md")).toString();
    Target target = TerminalLinks::resolve(url + QStringLiteral(":7:3"), QString(), QString());
    CHECK(target.kind == Kind::File && target.line == 7 && target.column == 3);
    CHECK(target.path == QDir::cleanPath(root + QStringLiteral("/pos.md")));
    target = TerminalLinks::resolve(url + QStringLiteral(":9"), QString(), QString());
    CHECK(target.kind == Kind::File && target.line == 9 && target.column == 0);
    CHECK(TerminalLinks::resolve(url + QStringLiteral(":x"), QString(), QString()).kind
          == Kind::Invalid);
}

void testShortcutsAreClassified()
{
    CHECK(TerminalLinks::isShellShortcut(QStringLiteral("C:/x/App.lnk")));
    CHECK(TerminalLinks::isShellShortcut(QStringLiteral("C:/x/site.URL")));
    CHECK(TerminalLinks::isShellShortcut(QStringLiteral("a.appref-ms")));
    CHECK(!TerminalLinks::isShellShortcut(QStringLiteral("C:/x/readme.md")));
    CHECK(!TerminalLinks::isShellShortcut(QStringLiteral("C:/x/run.ps1")));
}

} // namespace

int main()
{
    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::printf("FAIL: could not create a temporary directory\n");
        return 1;
    }
    const QString root = QDir(temp.path()).absolutePath();

    testLineAndColumnSuffixes();
    testInvalidPositionsAreIgnored();
    testUnsafePathsAreRejected();
    testSchemeAllowlist();
    testFileUrls(root);
    testRelativeResolutionAndCwdPolicy(root);
    testUnquotedPathsWithSpaces(root);
    testBareNamesAndNestedRelativePaths(root);
    testFileUrlPositions(root);
    testShortcutsAreClassified();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
