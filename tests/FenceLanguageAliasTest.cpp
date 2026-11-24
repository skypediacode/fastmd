#include "FenceLanguageAliases.h"
#include "SyntaxHighlightingRepository.h"

#include <KSyntaxHighlighting/Definition>

#include <QCoreApplication>
#include <QDebug>

namespace {

bool require(bool condition, const QString& message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

// Tag -> the KSyntaxHighlighting definition name it must highlight with. These
// match the highlight.js language each tag names, so the editor and the
// preview color a fence the same way.
const struct {
    const char* tag;
    const char* definition;
} kExpected[] = {
    { "rb", "Ruby" },
    { "RUBY", "Ruby" },
    { "rs", "Rust" },
    { "kt", "Kotlin" },
    { "lua", "Lua" },
    { "perl", "Perl" },
    { "tex", "LaTeX" },
    { "tsx", "TypeScript React (TSX)" },
    { "jsx", "JavaScript React (JSX)" },
    { "mjs", "JavaScript" },
    { "diff", "Diff" },
    { "haskell", "Haskell" },
    { "elixir", "Elixir" },
    { "make", "Makefile" },
    { "docker", "Dockerfile" },
    { "cpp", "C++" },
    { "bash", "Bash" },
    { "ps1", "PowerShell" },
    { "js", "JavaScript" },
};

// Tags that must stay unhighlighted. Each one would resolve to the wrong
// language if the editor fell back to matching highlight.js aliases against
// file extensions (fs -> Forth, ps -> PostScript, cls -> LaTeX, ...).
const char* const kUnmapped[] = {
    "fs", "ps", "pp", "q", "cls", "nc", "hsp", "mak", "prolog", "ex",
    "markdown", "text", "nosuchlanguage", "",
};

}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    for (const auto& entry : kExpected) {
        const QString fileName = FenceLanguageAliases::fileNameForTag(QString::fromLatin1(entry.tag));
        ok &= require(!fileName.isEmpty(),
                      QStringLiteral("no file name for tag '%1'").arg(QString::fromLatin1(entry.tag)));
        const KSyntaxHighlighting::Definition definition =
            syntaxRepository().definitionForFileName(fileName);
        ok &= require(definition.isValid() && definition.name() == QString::fromLatin1(entry.definition),
                      QStringLiteral("tag '%1' (%2) resolved to '%3', expected '%4'")
                          .arg(QString::fromLatin1(entry.tag), fileName,
                               definition.isValid() ? definition.name() : QStringLiteral("<none>"),
                               QString::fromLatin1(entry.definition)));
    }

    // A mistyped extension would silently leave that tag unhighlighted.
    const QStringList tags = FenceLanguageAliases::tags();
    ok &= require(!tags.isEmpty(), QStringLiteral("alias table is empty"));
    for (const QString& tag : tags) {
        ok &= require(tag == tag.toLower(), QStringLiteral("tag '%1' is not lower-case").arg(tag));
        const QString fileName = FenceLanguageAliases::fileNameForTag(tag);
        ok &= require(syntaxRepository().definitionForFileName(fileName).isValid(),
                      QStringLiteral("tag '%1' (%2) has no definition").arg(tag, fileName));
    }

    for (const char* tag : kUnmapped) {
        const QString name = FenceLanguageAliases::fileNameForTag(QString::fromLatin1(tag));
        ok &= require(name.isEmpty(),
                      QStringLiteral("tag '%1' must stay unhighlighted, got file name '%2'")
                          .arg(QString::fromLatin1(tag), name));
    }

    return ok ? 0 : 1;
}
