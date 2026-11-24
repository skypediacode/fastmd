#include "SyntaxHighlightingRepository.h"

#include <QCoreApplication>
#include <QDebug>
#include <KSyntaxHighlighting/Definition>

namespace {

bool require(bool condition, const QString& message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto& repository = syntaxRepository();

    bool ok = true;
    ok &= require(repository.definitions().size() >= 400,
                  QStringLiteral("compiled KSyntaxHighlighting definition coverage is incomplete"));
    ok &= require(repository.definitionForFileName(QStringLiteral("file.cpp")).isValid(),
                  QStringLiteral("C++ definition was not available"));
    ok &= require(repository.definitionForFileName(QStringLiteral("file.py")).isValid(),
                  QStringLiteral("Python definition was not available"));
    ok &= require(repository.definitionForFileName(QStringLiteral("file.rs")).isValid(),
                  QStringLiteral("Rust definition was not available"));
    ok &= require(repository.definitionForName(QStringLiteral("FastMD Markdown")).isValid(),
                  QStringLiteral("FastMD Markdown definition was not available"));
    return ok ? 0 : 1;
}
