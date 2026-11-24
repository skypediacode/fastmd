// Focused regression tests for the pure command-line PDF export argument
// parser. Deliberately links only Qt6::Core + src/CliExportArguments.cpp.

#include "CliExportArguments.h"

#include <QDir>
#include <QString>
#include <QStringList>

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

const QString kWorkingDir = QStringLiteral("C:/Work Dir");

CliExport::Arguments parse(QStringList arguments)
{
    arguments.prepend(QStringLiteral("FastMD.exe"));
    return CliExport::parseArguments(arguments, kWorkingDir);
}

void testNotRequested()
{
    const auto plain = parse({ QStringLiteral("abc.md") });
    CHECK(!plain.requested);
    CHECK(!plain.isValid());
    CHECK(plain.error.isEmpty());

    CHECK(!parse({ QStringLiteral("abc.md"), QStringLiteral("--force") }).requested);
    CHECK(!parse({ QStringLiteral("--new-window"), QStringLiteral("abc.md") }).requested);
    CHECK(!parse({}).requested);
    // argv[0] is never an option, whatever it is called.
    CHECK(!CliExport::isRequested({ QStringLiteral("-o") }));
}

void testBothSpellings()
{
    const auto shortForm = parse({ QStringLiteral("abc.md"), QStringLiteral("-o"), QStringLiteral("abc.pdf") });
    CHECK(shortForm.isValid());
    CHECK(shortForm.inputPath == QStringLiteral("C:/Work Dir/abc.md"));
    CHECK(shortForm.outputPath == QStringLiteral("C:/Work Dir/abc.pdf"));
    CHECK(!shortForm.force);

    const auto longForm = parse({ QStringLiteral("abc.md"), QStringLiteral("--output"), QStringLiteral("abc.pdf") });
    CHECK(longForm.isValid());
    CHECK(longForm.inputPath == shortForm.inputPath);
    CHECK(longForm.outputPath == shortForm.outputPath);
}

void testOptionOrder()
{
    const auto optionsFirst = parse({ QStringLiteral("--force"), QStringLiteral("-o"),
                                      QStringLiteral("out.pdf"), QStringLiteral("in.md") });
    CHECK(optionsFirst.isValid());
    CHECK(optionsFirst.force);
    CHECK(optionsFirst.inputPath == QStringLiteral("C:/Work Dir/in.md"));
    CHECK(optionsFirst.outputPath == QStringLiteral("C:/Work Dir/out.pdf"));

    const auto forceLast = parse({ QStringLiteral("in.md"), QStringLiteral("--output"),
                                   QStringLiteral("out.pdf"), QStringLiteral("--force") });
    CHECK(forceLast.isValid());
    CHECK(forceLast.force);
}

void testMissingValue()
{
    const auto atEnd = parse({ QStringLiteral("abc.md"), QStringLiteral("-o") });
    CHECK(atEnd.requested);
    CHECK(!atEnd.isValid());
    CHECK(atEnd.error.contains(QStringLiteral("-o")));

    const auto followedByOption = parse({ QStringLiteral("abc.md"), QStringLiteral("--output"),
                                          QStringLiteral("--force") });
    CHECK(followedByOption.requested);
    CHECK(!followedByOption.isValid());
    CHECK(followedByOption.error.contains(QStringLiteral("--output")));

    const auto empty = parse({ QStringLiteral("abc.md"), QStringLiteral("-o"), QString() });
    CHECK(!empty.isValid());

    const auto twice = parse({ QStringLiteral("abc.md"), QStringLiteral("-o"), QStringLiteral("a.pdf"),
                               QStringLiteral("-o"), QStringLiteral("b.pdf") });
    CHECK(!twice.isValid());
}

void testInputCount()
{
    const auto none = parse({ QStringLiteral("-o"), QStringLiteral("abc.pdf") });
    CHECK(none.requested);
    CHECK(!none.isValid());

    const auto two = parse({ QStringLiteral("a.md"), QStringLiteral("b.md"), QStringLiteral("-o"),
                             QStringLiteral("abc.pdf") });
    CHECK(two.requested);
    CHECK(!two.isValid());
}

void testOutputExtension()
{
    CHECK(!parse({ QStringLiteral("a.md"), QStringLiteral("-o"), QStringLiteral("a.html") }).isValid());
    CHECK(!parse({ QStringLiteral("a.md"), QStringLiteral("-o"), QStringLiteral("a") }).isValid());
    CHECK(!parse({ QStringLiteral("a.md"), QStringLiteral("-o"), QStringLiteral("a.pdf.txt") }).isValid());
    CHECK(!parse({ QStringLiteral("a.md"), QStringLiteral("-o"), QStringLiteral(".pdf") }).isValid());
    CHECK(parse({ QStringLiteral("a.md"), QStringLiteral("-o"), QStringLiteral("a.PDF") }).isValid());
    CHECK(parse({ QStringLiteral("a.md"), QStringLiteral("-o"), QStringLiteral("a.Pdf") }).isValid());
}

void testConflictingFlags()
{
    for (const char* flag : { "--new-window", "--notepad", "--restart" }) {
        const auto parsed = parse({ QString::fromLatin1(flag), QStringLiteral("a.md"), QStringLiteral("-o"),
                                    QStringLiteral("a.pdf") });
        CHECK(parsed.requested);
        CHECK(!parsed.isValid());
        CHECK(parsed.error.contains(QString::fromLatin1(flag)));
    }
    const auto unknown = parse({ QStringLiteral("a.md"), QStringLiteral("-o"), QStringLiteral("a.pdf"),
                                 QStringLiteral("--landscape") });
    CHECK(!unknown.isValid());
}

void testPaths()
{
    const auto spaces = parse({ QStringLiteral("My Notes/read me.md"), QStringLiteral("-o"),
                                QStringLiteral("../Out Dir/read me.pdf") });
    CHECK(spaces.isValid());
    CHECK(spaces.inputPath == QStringLiteral("C:/Work Dir/My Notes/read me.md"));
    CHECK(spaces.outputPath == QStringLiteral("C:/Out Dir/read me.pdf"));

    const auto absolute = parse({ QStringLiteral("D:/docs/a.md"), QStringLiteral("-o"),
                                  QStringLiteral("E:/out/a.pdf") });
    CHECK(absolute.isValid());
    CHECK(absolute.inputPath == QStringLiteral("D:/docs/a.md"));
    CHECK(absolute.outputPath == QStringLiteral("E:/out/a.pdf"));

    const auto backslashes = parse({ QStringLiteral("D:\\docs\\a.md"), QStringLiteral("-o"),
                                     QStringLiteral("sub\\a.pdf") });
    CHECK(backslashes.isValid());
    CHECK(QDir::fromNativeSeparators(backslashes.inputPath) == QStringLiteral("D:/docs/a.md"));
    CHECK(QDir::fromNativeSeparators(backslashes.outputPath) == QStringLiteral("C:/Work Dir/sub/a.pdf"));

    const auto unicode = parse({ QString::fromUtf8("\xE6\x96\x87\xE6\xA1\xA3 \xC3\xA9.md"), QStringLiteral("-o"),
                                 QString::fromUtf8("\xE6\x96\x87\xE6\xA1\xA3 \xC3\xA9.pdf") });
    CHECK(unicode.isValid());
    CHECK(unicode.outputPath == QStringLiteral("C:/Work Dir/") + QString::fromUtf8("\xE6\x96\x87\xE6\xA1\xA3 \xC3\xA9.pdf"));
}

} // namespace

int main()
{
    testNotRequested();
    testBothSpellings();
    testOptionOrder();
    testMissingValue();
    testInputCount();
    testOutputExtension();
    testConflictingFlags();
    testPaths();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
