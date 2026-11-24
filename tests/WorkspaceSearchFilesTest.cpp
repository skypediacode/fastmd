#include "WorkspaceSearchFiles.h"
#include "WorkspaceTextSearch.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

using namespace WorkspaceSearchFiles;

static bool g_ok = true;

static void expect(bool value, const char* what)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        g_ok = false;
    }
}

static void writeBytes(const QString& path, const QByteArray& data)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
}

static QByteArray readBytes(const QString& path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

// Replace "foo" with "bar" through exactly the path the panel uses for a file
// that is not open: load, replace, save.
static bool replaceInFile(const QString& path, int* count)
{
    LoadedText file;
    if (!load(path, &file, WorkspaceTextSearch::kMaxFileBytes))
        return false;
    const WorkspaceTextSearch::Query q{QStringLiteral("foo"), {}};
    QString out;
    *count = WorkspaceTextSearch::replaceAll(file.text, WorkspaceTextSearch::buildRegex(q), q,
                                             QStringLiteral("bar"), &out);
    file.text = out;
    return save(path, file);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString dir = tmp.path();
    int count = 0;

    struct Case {
        const char* name;
        QByteArray before;
        QByteArray after;
    };

    const QByteArray bom("\xEF\xBB\xBF", 3);
    const QList<Case> cases = {
        {"utf8 lf", "foo\nfoo\n", "bar\nbar\n"},
        {"utf8 crlf", "foo\r\nx foo\r\n", "bar\r\nx bar\r\n"},
        {"utf8 bom crlf", bom + "foo\r\nfoo", bom + "bar\r\nbar"},
        {"utf8 non-ascii", QStringLiteral("foo cửa\n").toUtf8(),
         QStringLiteral("bar cửa\n").toUtf8()},
        {"cr only", "foo\rfoo\r", "bar\rbar\r"},
    };
    for (const Case& c : cases) {
        const QString path = dir + "/case.txt";
        writeBytes(path, c.before);
        expect(replaceInFile(path, &count) && count >= 1, c.name);
        expect(readBytes(path) == c.after, c.name);
    }

    // UTF-16 LE / BE with BOM.
    {
        const QString path = dir + "/u16.txt";
        QByteArray le("\xFF\xFE", 2);
        const QString before = QStringLiteral("foo\r\nfoo\r\n");
        const QString after = QStringLiteral("bar\r\nbar\r\n");
        auto utf16le = [](const QString& s) {
            QByteArray b;
            for (QChar c : s) {
                b.append(char(c.unicode() & 0xFF));
                b.append(char(c.unicode() >> 8));
            }
            return b;
        };
        auto utf16be = [](const QString& s) {
            QByteArray b;
            for (QChar c : s) {
                b.append(char(c.unicode() >> 8));
                b.append(char(c.unicode() & 0xFF));
            }
            return b;
        };
        writeBytes(path, le + utf16le(before));
        expect(replaceInFile(path, &count) && count == 2, "utf16 le replace");
        expect(readBytes(path) == le + utf16le(after), "utf16 le preserved");

        const QByteArray be("\xFE\xFF", 2);
        writeBytes(path, be + utf16be(before));
        expect(replaceInFile(path, &count) && count == 2, "utf16 be replace");
        expect(readBytes(path) == be + utf16be(after), "utf16 be preserved");
    }

    // Files that must never be opened for replacement.
    {
        LoadedText file;
        writeBytes(dir + "/bin.dat", QByteArray("foo\0bar", 7));
        expect(!load(dir + "/bin.dat", &file, WorkspaceTextSearch::kMaxFileBytes),
               "binary file rejected");
        writeBytes(dir + "/latin1.txt", QByteArray("caf\xE9 foo"));
        expect(!load(dir + "/latin1.txt", &file, WorkspaceTextSearch::kMaxFileBytes),
               "undecodable file rejected");
        writeBytes(dir + "/big.txt", QByteArray(2048, 'a'));
        expect(!load(dir + "/big.txt", &file, 1024), "oversized file rejected");
        expect(!load(dir + "/missing.txt", &file, 1024), "missing file rejected");
    }

    if (g_ok)
        std::puts("WorkspaceSearchFilesTest: all checks passed");
    return g_ok ? 0 : 1;
}
