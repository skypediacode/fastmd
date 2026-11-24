#include "SyntaxHighlightingRepository.h"
#include "StartupProfiler.h"
#include <QCoreApplication>
#include <QDir>

namespace {

// Repository::addCustomSearchPath() appends "/syntax" and "/themes" itself,
// so every path passed here must be a base directory, not a syntax/ or
// themes/ subfolder.
void addSearchPaths(KSyntaxHighlighting::Repository& repo)
{
#ifdef FASTMD_KSYNTAXHIGHLIGHTING_DATA_DIR
    repo.addCustomSearchPath(QStringLiteral(FASTMD_KSYNTAXHIGHLIGHTING_DATA_DIR));
#else
    repo.addCustomSearchPath(QCoreApplication::applicationDirPath() + QStringLiteral("/ksyntaxhighlighting"));
#endif
}

} // namespace

KSyntaxHighlighting::Repository& syntaxRepository()
{
    static KSyntaxHighlighting::Repository repository;
    static const bool initialized = [] {
        addSearchPaths(repository);
        FASTMD_MARK("syntax repository ready");
        return true;
    }();
    (void)initialized;
    return repository;
}
