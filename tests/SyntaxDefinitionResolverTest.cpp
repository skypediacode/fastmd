#include "SyntaxDefinitionResolver.h"
#include "DocumentModel.h"
#include "FastMDFileTypes.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>

namespace {

bool require(bool condition, const QString& message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

QString definitionName(const QString& filePath, EditorMode mode)
{
    return SyntaxDefinitionResolver::resolve(filePath, mode).name();
}

bool isPlainText(const QString& filePath, EditorMode mode)
{
    return !SyntaxDefinitionResolver::resolve(filePath, mode).isValid();
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    ok &= require(definitionName(QStringLiteral("README.md"), EditorMode::Markdown) == QStringLiteral("FastMD Markdown"),
                  QStringLiteral("README.md in Markdown mode did not resolve to FastMD Markdown"));
    // Markdown mode always wins regardless of the file's own extension.
    ok &= require(definitionName(QStringLiteral("main.cpp"), EditorMode::Markdown) == QStringLiteral("FastMD Markdown"),
                  QStringLiteral("Markdown mode did not take priority over a non-Markdown extension"));

    ok &= require(definitionName(QStringLiteral("main.cpp"), EditorMode::PlainText) == QStringLiteral("C++"),
                  QStringLiteral("main.cpp did not resolve to C++"));
    ok &= require(definitionName(QStringLiteral("main.py"), EditorMode::PlainText) == QStringLiteral("Python"),
                  QStringLiteral("main.py did not resolve to Python"));
    ok &= require(definitionName(QStringLiteral("app.js"), EditorMode::PlainText) == QStringLiteral("JavaScript"),
                  QStringLiteral("app.js did not resolve to JavaScript"));
    // KSyntaxHighlighting ships a higher-priority "JSONC" definition (JSON plus
    // comments) that also claims *.json, so the shared Repository's own
    // conflict resolution picks it over the plain "JSON" definition -- it
    // still highlights standard JSON correctly, just under that name.
    ok &= require(definitionName(QStringLiteral("config.json"), EditorMode::PlainText) == QStringLiteral("JSONC"),
                  QStringLiteral("config.json did not resolve to a JSON-compatible definition"));
    ok &= require(definitionName(QStringLiteral("Dockerfile"), EditorMode::PlainText) == QStringLiteral("Dockerfile"),
                  QStringLiteral("Dockerfile did not resolve to Dockerfile"));
    ok &= require(definitionName(QStringLiteral("CMakeLists.txt"), EditorMode::PlainText) == QStringLiteral("CMake"),
                  QStringLiteral("CMakeLists.txt did not resolve to CMake"));
    ok &= require(definitionName(QStringLiteral("Makefile"), EditorMode::PlainText) == QStringLiteral("Makefile"),
                  QStringLiteral("Makefile did not resolve to Makefile"));

    ok &= require(isPlainText(QStringLiteral("notes.txt"), EditorMode::PlainText),
                  QStringLiteral("notes.txt unexpectedly resolved to a language definition"));
    ok &= require(isPlainText(QStringLiteral("notes.wxyz"), EditorMode::PlainText),
                  QStringLiteral("unknown extension unexpectedly resolved to a language definition"));
    ok &= require(isPlainText(QString(), EditorMode::PlainText),
                  QStringLiteral("untitled document (empty path) unexpectedly resolved to a language definition"));

    // Uppercase / mixed-case extensions still resolve.
    ok &= require(definitionName(QStringLiteral("MAIN.CPP"), EditorMode::PlainText) == QStringLiteral("C++"),
                  QStringLiteral("uppercase extension MAIN.CPP did not resolve to C++"));
    ok &= require(definitionName(QStringLiteral("Script.PY"), EditorMode::PlainText) == QStringLiteral("Python"),
                  QStringLiteral("mixed-case extension Script.PY did not resolve to Python"));

    // Save As / rename with a changed extension picks up the new language.
    ok &= require(definitionName(QStringLiteral("script.js"), EditorMode::PlainText) == QStringLiteral("JavaScript"),
                  QStringLiteral("renamed script.js did not resolve to JavaScript"));

    // Repeated resolution for the same path is stable (no caching state to
    // desync) and does not require recreating the shared Repository.
    const QString first = definitionName(QStringLiteral("main.rs"), EditorMode::PlainText);
    const QString second = definitionName(QStringLiteral("main.rs"), EditorMode::PlainText);
    ok &= require(first == second && first == QStringLiteral("Rust"),
                  QStringLiteral("repeated resolution of main.rs was not stable"));

    QTemporaryDir files;
    ok &= require(files.isValid(), QStringLiteral("could not create temporary test directory"));
    const QString validPath = files.filePath(QStringLiteral("valid.cpp"));
    ok &= require(writeFile(validPath, QByteArrayLiteral("plain UTF-8 text\n")),
                  QStringLiteral("could not write valid text test file"));
    ok &= require(SyntaxDefinitionResolver::isEditableTextFile(validPath),
                  QStringLiteral("valid UTF-8 source was rejected as editable text"));

    QByteArray trailingInvalid(64 * 1024, 'a');
    trailingInvalid.append("\nvalid text\n");
    trailingInvalid.append("\xC3\x28", 2);
    const QString trailingInvalidPath = files.filePath(QStringLiteral("trailing-invalid.cpp"));
    ok &= require(writeFile(trailingInvalidPath, trailingInvalid),
                  QStringLiteral("could not write trailing invalid UTF-8 test file"));
    ok &= require(!SyntaxDefinitionResolver::isEditableTextFile(trailingInvalidPath),
                  QStringLiteral("invalid UTF-8 after the scan chunk was accepted as editable text"));
    ok &= require(SyntaxDefinitionResolver::isLikelyEditableTextFile(trailingInvalidPath),
                  QStringLiteral("the bounded UI probe unexpectedly scanned beyond its first chunk"));

    const QString binaryPath = files.filePath(QStringLiteral("binary.cpp"));
    ok &= require(writeFile(binaryPath, QByteArray("source\0data", 11)),
                  QStringLiteral("could not write binary text-probe test file"));
    ok &= require(!SyntaxDefinitionResolver::isLikelyEditableTextFile(binaryPath),
                  QStringLiteral("the bounded UI probe accepted an immediate NUL byte"));

    // --- autoDetect(): the new detection entry point used by the
    // language-selection feature. It normalizes Markdown extensions to
    // FastMD's own definition and ".json" to plain "JSON" (unlike resolve(),
    // which keeps the repository's own JSONC priority for legacy callers).
    auto autoDetectName = [](const QString& path) {
        return SyntaxDefinitionResolver::autoDetect(path).name();
    };
    ok &= require(autoDetectName(QStringLiteral("README.md")) == QStringLiteral("FastMD Markdown"),
                  QStringLiteral("autoDetect did not resolve README.md to FastMD Markdown"));
    ok &= require(autoDetectName(QStringLiteral("notes.markdown")) == QStringLiteral("FastMD Markdown"),
                  QStringLiteral("autoDetect did not resolve .markdown to FastMD Markdown"));
    ok &= require(autoDetectName(QStringLiteral("config.json")) == QStringLiteral("JSON"),
                  QStringLiteral("autoDetect did not normalize .json to JSON"));
    ok &= require(autoDetectName(QStringLiteral("main.cpp")) == QStringLiteral("C++"),
                  QStringLiteral("autoDetect did not resolve main.cpp to C++"));
    ok &= require(!SyntaxDefinitionResolver::autoDetect(QStringLiteral("notes.txt")).isValid(),
                  QStringLiteral("autoDetect unexpectedly resolved notes.txt to a language"));
    ok &= require(!SyntaxDefinitionResolver::autoDetect(QString()).isValid(),
                  QStringLiteral("autoDetect unexpectedly resolved an empty path"));

    // --- "Open as Source Code" relies on recognizing that a plain filename lookup
    // for a Markdown file still yields a Markdown definition (both FastMD's
    // and the stock one advertise "*.md" at the same priority), so it must
    // drop that result rather than assume resolve() returns something
    // non-Markdown for PlainText mode.
    ok &= require(SyntaxDefinitionResolver::isMarkdownDefinition(
                      SyntaxDefinitionResolver::resolve(QStringLiteral("README.md"), EditorMode::PlainText)),
                  QStringLiteral("resolve(.md, PlainText) unexpectedly returned a non-Markdown definition"));

    // --- definitionForStoredName(): stable-name lookup + missing fallback.
    ok &= require(SyntaxDefinitionResolver::definitionForStoredName(QStringLiteral("Python")).isValid(),
                  QStringLiteral("definitionForStoredName did not find Python"));
    ok &= require(!SyntaxDefinitionResolver::definitionForStoredName(QStringLiteral("NoSuchLanguageXYZ")).isValid(),
                  QStringLiteral("definitionForStoredName unexpectedly found an unknown name"));
    ok &= require(!SyntaxDefinitionResolver::definitionForStoredName(QString()).isValid(),
                  QStringLiteral("definitionForStoredName unexpectedly resolved an empty name"));

    // --- availableDefinitions(): excludes hidden entries and the stock
    // Markdown duplicate (FastMD's own is offered separately by the popup).
    bool sawStockMarkdown = false;
    bool sawHidden = false;
    for (const auto& def : SyntaxDefinitionResolver::availableDefinitions()) {
        if (def.name().compare(QStringLiteral("Markdown"), Qt::CaseInsensitive) == 0)
            sawStockMarkdown = true;
        if (def.isHidden())
            sawHidden = true;
    }
    ok &= require(!sawStockMarkdown, QStringLiteral("availableDefinitions() included the stock Markdown duplicate"));
    ok &= require(!sawHidden, QStringLiteral("availableDefinitions() included a hidden definition"));

    // --- isMarkdownDefinition[Name]() and displayName() helpers.
    ok &= require(SyntaxDefinitionResolver::isMarkdownDefinitionName(QStringLiteral("FastMD Markdown")),
                  QStringLiteral("FastMD Markdown was not recognized as a Markdown definition name"));
    ok &= require(!SyntaxDefinitionResolver::isMarkdownDefinitionName(QStringLiteral("C++")),
                  QStringLiteral("C++ was incorrectly recognized as a Markdown definition name"));
    ok &= require(SyntaxDefinitionResolver::displayNameForStoredName(QString()) == QStringLiteral("Plain Text"),
                  QStringLiteral("empty stored name did not display as Plain Text"));
    ok &= require(SyntaxDefinitionResolver::displayNameForStoredName(QStringLiteral("FastMD Markdown")) == QStringLiteral("Markdown"),
                  QStringLiteral("FastMD Markdown did not display as Markdown"));

    // --- preferredSaveSuffix(): deterministic default extension per language.
    ok &= require(SyntaxDefinitionResolver::preferredSaveSuffix(QString()) == QStringLiteral(".txt"),
                  QStringLiteral("empty (Plain Text) did not prefer .txt"));
    ok &= require(SyntaxDefinitionResolver::preferredSaveSuffix(QStringLiteral("FastMD Markdown")) == QStringLiteral(".md"),
                  QStringLiteral("Markdown did not prefer .md"));
    ok &= require(SyntaxDefinitionResolver::preferredSaveSuffix(QStringLiteral("C++")) == QStringLiteral(".cpp"),
                  QStringLiteral("C++ did not prefer .cpp"));
    ok &= require(SyntaxDefinitionResolver::preferredSaveSuffix(QStringLiteral("Python")) == QStringLiteral(".py"),
                  QStringLiteral("Python did not prefer .py"));
    ok &= require(SyntaxDefinitionResolver::preferredSaveSuffix(QStringLiteral("JavaScript")) == QStringLiteral(".js"),
                  QStringLiteral("JavaScript did not prefer .js"));
    ok &= require(SyntaxDefinitionResolver::preferredSaveSuffix(QStringLiteral("JSON")) == QStringLiteral(".json"),
                  QStringLiteral("JSON did not prefer .json"));
    ok &= require(SyntaxDefinitionResolver::preferredSaveSuffix(QStringLiteral("Rust")) == QStringLiteral(".rs"),
                  QStringLiteral("Rust (uncommon language) did not fall back to its first safe repository suffix"));
    ok &= require(SyntaxDefinitionResolver::preferredSaveSuffix(QStringLiteral("NoSuchLanguageXYZ")) == QStringLiteral(".txt"),
                  QStringLiteral("unknown stored definition name did not fall back to .txt"));

    // --- fileNameMatchesDefinition(): exact-name / safe-suffix verbatim match.
    ok &= require(SyntaxDefinitionResolver::fileNameMatchesDefinition(QStringLiteral("Dockerfile"), QStringLiteral("Dockerfile")),
                  QStringLiteral("Dockerfile did not match its own exact-name definition"));
    ok &= require(SyntaxDefinitionResolver::fileNameMatchesDefinition(QStringLiteral("main.cpp"), QStringLiteral("C++")),
                  QStringLiteral("main.cpp did not match C++ by safe suffix"));
    ok &= require(!SyntaxDefinitionResolver::fileNameMatchesDefinition(QStringLiteral("main"), QStringLiteral("C++")),
                  QStringLiteral("extensionless 'main' incorrectly matched C++"));
    ok &= require(SyntaxDefinitionResolver::fileNameMatchesDefinition(QStringLiteral("README.md"), QStringLiteral("FastMD Markdown")),
                  QStringLiteral("README.md did not match Markdown by suffix"));

    // --- associableExtensions(): broad, safe, deduplicated, sorted list.
    const QStringList associable = SyntaxDefinitionResolver::associableExtensions();
    ok &= require(associable.contains(QStringLiteral(".cpp")), QStringLiteral("associableExtensions() missing .cpp"));
    ok &= require(associable.contains(QStringLiteral(".py")), QStringLiteral("associableExtensions() missing .py"));
    ok &= require(associable.contains(QStringLiteral(".js")), QStringLiteral("associableExtensions() missing .js"));
    ok &= require(associable.contains(QStringLiteral(".json")), QStringLiteral("associableExtensions() missing .json"));
    ok &= require(!associable.contains(QStringLiteral("dockerfile")),
                  QStringLiteral("associableExtensions() incorrectly included an exact filename"));
    for (const QString& ext : associable) {
        ok &= require(ext.startsWith(QLatin1Char('.')), QStringLiteral("associableExtensions() entry missing leading dot: ") + ext);
        ok &= require(ext == ext.toLower(), QStringLiteral("associableExtensions() entry not lowercase: ") + ext);
        ok &= require(!ext.mid(1).contains(QLatin1Char('.')), QStringLiteral("associableExtensions() included a compound suffix: ") + ext);
    }
    QStringList sortedCopy = associable;
    std::sort(sortedCopy.begin(), sortedCopy.end());
    ok &= require(sortedCopy == associable, QStringLiteral("associableExtensions() was not deterministically sorted"));
    ok &= require(QSet<QString>(associable.begin(), associable.end()).size() == associable.size(),
                  QStringLiteral("associableExtensions() contained duplicates"));

    // --- File-association narrow scope: FASTMD_WRITING_EXTENSIONS is the
    // single source of truth PreferencesDialog builds "Associate Markdown and
    // Text Files" from -- verify it is exactly the six documented extensions.
    {
        QStringList narrowScope;
#define FASTMD_TEST_APPEND_EXT(ext) narrowScope.append(QStringLiteral(ext));
        FASTMD_WRITING_EXTENSIONS(FASTMD_TEST_APPEND_EXT)
#undef FASTMD_TEST_APPEND_EXT
        const QStringList expected = {
            QStringLiteral("md"), QStringLiteral("markdown"),
            QStringLiteral("mdown"), QStringLiteral("mkd"),
            QStringLiteral("txt"), QStringLiteral("text"),
        };
        ok &= require(narrowScope == expected,
                      QStringLiteral("FASTMD_WRITING_EXTENSIONS did not produce the exact narrow association scope"));
    }

    return ok ? 0 : 1;
}
