#include "SyntaxDefinitionResolver.h"
#include "SyntaxHighlightingRepository.h"
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QSet>
#include <QStringDecoder>
#include <algorithm>

namespace SyntaxDefinitionResolver {

namespace {

const QStringList& markdownExtensions()
{
    static const QStringList extensions = {
        QStringLiteral("md"), QStringLiteral("markdown"),
        QStringLiteral("mdown"), QStringLiteral("mkd"),
    };
    return extensions;
}

bool isMarkdownExtension(const QString& suffix)
{
    return markdownExtensions().contains(suffix.toLower());
}

// A "safe simple extension" pattern matches exactly one dot-suffix, e.g.
// "*.cpp" -- not an exact filename ("Dockerfile"), not a further wildcard
// ("Config.*"), and not a compound suffix with an embedded dot ("*.fastq.gz").
bool isSafeSimpleExtensionPattern(const QString& pattern)
{
    if (!pattern.startsWith(QStringLiteral("*.")))
        return false;
    const QString rest = pattern.mid(2);
    if (rest.isEmpty())
        return false;
    return !rest.contains(QLatin1Char('.')) && !rest.contains(QLatin1Char('*'))
        && !rest.contains(QLatin1Char('?')) && !rest.contains(QLatin1Char('['))
        && !rest.contains(QLatin1Char(']'));
}

QString suffixFromSafePattern(const QString& pattern)
{
    return QStringLiteral(".") + pattern.mid(2).toLower();
}

const QHash<QString, QString>& explicitSaveSuffixes()
{
    static const QHash<QString, QString> map = {
        { QStringLiteral("c++"), QStringLiteral(".cpp") },
        { QStringLiteral("python"), QStringLiteral(".py") },
        { QStringLiteral("javascript"), QStringLiteral(".js") },
        { QStringLiteral("json"), QStringLiteral(".json") },
    };
    return map;
}

} // namespace

QString fastMdMarkdownDefinitionName()
{
    return QStringLiteral("FastMD Markdown");
}

KSyntaxHighlighting::Definition resolve(const QString& filePath, EditorMode mode)
{
    if (mode == EditorMode::Markdown)
        return syntaxRepository().definitionForName(fastMdMarkdownDefinitionName());

    const QString fileName = QFileInfo(filePath).fileName();
    if (fileName.isEmpty())
        return {};

    auto& repo = syntaxRepository();
    KSyntaxHighlighting::Definition def = repo.definitionForFileName(fileName);
    if (!def.isValid() && fileName != fileName.toLower())
        def = repo.definitionForFileName(fileName.toLower());
    return def;
}

KSyntaxHighlighting::Definition autoDetect(const QString& filePath)
{
    const QFileInfo info(filePath);
    const QString fileName = info.fileName();
    if (fileName.isEmpty())
        return {};

    if (isMarkdownExtension(info.suffix()))
        return syntaxRepository().definitionForName(fastMdMarkdownDefinitionName());

    if (info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0) {
        auto& repo = syntaxRepository();
        KSyntaxHighlighting::Definition json = repo.definitionForName(QStringLiteral("JSON"));
        if (json.isValid())
            return json;
    }

    return resolve(filePath, EditorMode::PlainText);
}

KSyntaxHighlighting::Definition definitionForStoredName(const QString& name)
{
    if (name.isEmpty())
        return {};
    return syntaxRepository().definitionForName(name);
}

QVector<KSyntaxHighlighting::Definition> availableDefinitions()
{
    QVector<KSyntaxHighlighting::Definition> result;
    const auto all = syntaxRepository().definitions();
    result.reserve(all.size());
    for (const auto& def : all) {
        if (!def.isValid() || def.isHidden())
            continue;
        if (isMarkdownDefinition(def) && def.name() != fastMdMarkdownDefinitionName())
            continue; // suppress the stock duplicate; FastMD's own is offered separately
        result.append(def);
    }
    return result;
}

bool isMarkdownDefinition(const KSyntaxHighlighting::Definition& definition)
{
    return definition.isValid() && isMarkdownDefinitionName(definition.name());
}

bool isMarkdownDefinitionName(const QString& name)
{
    return name.compare(QStringLiteral("Markdown"), Qt::CaseInsensitive) == 0
        || name.compare(fastMdMarkdownDefinitionName(), Qt::CaseInsensitive) == 0;
}

QString displayName(const KSyntaxHighlighting::Definition& definition)
{
    if (!definition.isValid())
        return QStringLiteral("Plain Text");
    if (isMarkdownDefinition(definition))
        return QStringLiteral("Markdown");
    return definition.translatedName();
}

QString displayNameForStoredName(const QString& name)
{
    if (name.isEmpty())
        return QStringLiteral("Plain Text");
    if (isMarkdownDefinitionName(name))
        return QStringLiteral("Markdown");
    return displayName(definitionForStoredName(name));
}

QString preferredSaveSuffix(const QString& definitionName)
{
    if (definitionName.isEmpty())
        return QStringLiteral(".txt");
    if (isMarkdownDefinitionName(definitionName))
        return QStringLiteral(".md");

    const auto& explicitSuffixes = explicitSaveSuffixes();
    const auto explicitIt = explicitSuffixes.constFind(definitionName.toLower());
    if (explicitIt != explicitSuffixes.constEnd())
        return explicitIt.value();

    const KSyntaxHighlighting::Definition definition = definitionForStoredName(definitionName);
    if (!definition.isValid())
        return QStringLiteral(".txt");

    // First safe simple pattern in repository order. suffixFromSafePattern()
    // already lowercases, so a definition advertising both "*.C" and "*.c"
    // yields the same suffix either way.
    for (const QString& pattern : definition.extensions()) {
        if (isSafeSimpleExtensionPattern(pattern))
            return suffixFromSafePattern(pattern);
    }
    return QStringLiteral(".txt");
}

bool fileNameMatchesDefinition(const QString& fileName, const QString& definitionName)
{
    if (fileName.isEmpty())
        return false;
    if (isMarkdownDefinitionName(definitionName))
        return isMarkdownExtension(QFileInfo(fileName).suffix());

    const KSyntaxHighlighting::Definition definition = definitionForStoredName(definitionName);
    if (!definition.isValid())
        return false;

    for (const QString& pattern : definition.extensions()) {
        if (isSafeSimpleExtensionPattern(pattern)) {
            if (fileName.endsWith(suffixFromSafePattern(pattern), Qt::CaseInsensitive))
                return true;
            continue;
        }
        // Exact filename / other verbatim patterns (Dockerfile, Makefile,
        // CMakeLists.txt, etc.) -- compare the whole name case-sensitively,
        // matching how Repository::definitionForFileName treats them.
        if (!pattern.contains(QLatin1Char('*')) && !pattern.contains(QLatin1Char('?'))
            && pattern.compare(fileName, Qt::CaseSensitive) == 0) {
            return true;
        }
    }
    return false;
}

QStringList associableExtensions()
{
    QSet<QString> unique;
    for (const auto& def : syntaxRepository().definitions()) {
        if (!def.isValid() || def.isHidden())
            continue;
        for (const QString& pattern : def.extensions()) {
            if (!isSafeSimpleExtensionPattern(pattern))
                continue;
            unique.insert(suffixFromSafePattern(pattern));
        }
    }
    QStringList result(unique.begin(), unique.end());
    std::sort(result.begin(), result.end());
    return result;
}

QString saveFilterPatterns(const QString& definitionName)
{
    if (definitionName.isEmpty() || isMarkdownDefinitionName(definitionName))
        return {};
    const KSyntaxHighlighting::Definition definition = definitionForStoredName(definitionName);
    if (!definition.isValid())
        return {};

    QStringList patterns;
    for (const QString& pattern : definition.extensions()) {
        if (isSafeSimpleExtensionPattern(pattern))
            patterns.append(pattern);
    }
    return patterns.join(QLatin1Char(' '));
}

bool isEditableTextFile(const QString& filePath)
{
    const QFileInfo info(filePath);
    if (!info.isFile())
        return false;

    const auto definition = resolve(filePath, EditorMode::PlainText);
    static QMimeDatabase mimeDatabase;
    const auto mimeType = mimeDatabase.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
    const bool isTextMimeType = mimeType.name().startsWith(QStringLiteral("text/"))
        || mimeType.inherits(QStringLiteral("text/plain"));
    if (!definition.isValid() && !isTextMimeType)
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QStringDecoder decoder(QStringDecoder::Utf8);
    constexpr qint64 chunkSize = 64 * 1024;
    for (;;) {
        const QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty())
            break;

        if (chunk.contains('\0'))
            return false;

        const QString decoded = decoder.decode(chunk);
        Q_UNUSED(decoded);
        if (decoder.hasError())
            return false;
    }

    if (file.error() != QFile::NoError)
        return false;

    const auto finalResult = decoder.finalize();
    return finalResult.error == QStringConverter::FinalizeResultError::NoError
        && !decoder.hasError();
}

bool isLikelyEditableTextFile(const QString& filePath)
{
    const QFileInfo info(filePath);
    if (!info.isFile())
        return false;

    const auto definition = resolve(filePath, EditorMode::PlainText);
    static QMimeDatabase mimeDatabase;
    const auto mimeType = mimeDatabase.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
    if (!definition.isValid() && !mimeType.name().startsWith(QStringLiteral("text/"))
        && !mimeType.inherits(QStringLiteral("text/plain"))) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    constexpr qint64 kProbeSize = 64 * 1024;
    const QByteArray sample = file.read(kProbeSize);
    if (sample.contains('\0') || file.error() != QFile::NoError)
        return false;
    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder.decode(sample);
    return !decoder.hasError();
}

} // namespace SyntaxDefinitionResolver
