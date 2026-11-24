#include "CliExportArguments.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace CliExport {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("CliExport", text);
}

bool isOutputFlag(const QString& argument)
{
    return argument == QLatin1String("-o") || argument == QLatin1String("--output");
}

bool isGuiFlag(const QString& argument)
{
    return argument == QLatin1String("--new-window") || argument == QLatin1String("--notepad")
        || argument == QLatin1String("--restart");
}

QString resolvePath(const QString& workingDirectory, const QString& path)
{
    return QDir::cleanPath(QDir(workingDirectory).absoluteFilePath(path));
}

}

bool isRequested(const QStringList& arguments)
{
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        if (isOutputFlag(arguments.at(i)))
            return true;
    }
    return false;
}

Arguments parseArguments(const QStringList& arguments, const QString& workingDirectory)
{
    Arguments result;
    result.requested = isRequested(arguments);
    if (!result.requested)
        return result;

    QString input;
    QString output;
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        const QString& argument = arguments.at(i);
        if (isOutputFlag(argument)) {
            if (!output.isEmpty()) {
                result.error = tr("The output file was given more than once.");
                return result;
            }
            const bool hasValue = i + 1 < arguments.size()
                && !arguments.at(i + 1).isEmpty()
                && !arguments.at(i + 1).startsWith(QLatin1Char('-'));
            if (!hasValue) {
                result.error = tr("Missing output file after %1.").arg(argument);
                return result;
            }
            output = arguments.at(++i);
        } else if (argument == QLatin1String("--force")) {
            result.force = true;
        } else if (isGuiFlag(argument)) {
            result.error = tr("%1 cannot be combined with -o/--output.").arg(argument);
            return result;
        } else if (argument.size() > 1 && argument.startsWith(QLatin1Char('-'))) {
            result.error = tr("Unknown option %1.").arg(argument);
            return result;
        } else if (argument.isEmpty()) {
            result.error = tr("An empty argument is not a valid input file.");
            return result;
        } else if (!input.isEmpty()) {
            result.error = tr("Only one input file can be converted at a time.");
            return result;
        } else {
            input = argument;
        }
    }

    if (input.isEmpty()) {
        result.error = tr("No input file was given.");
        return result;
    }
    if (QFileInfo(output).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) != 0
        || QFileInfo(output).completeBaseName().isEmpty()) {
        result.error = tr("Unsupported output file %1. Only .pdf output is supported.").arg(output);
        return result;
    }

    result.inputPath = resolvePath(workingDirectory, input);
    result.outputPath = resolvePath(workingDirectory, output);
    return result;
}

QString usageText()
{
    return tr("Usage: FastMD.exe <input.md> -o <output.pdf> [--force]\n"
              "       FastMD.exe <input.md> --output <output.pdf> [--force]");
}

}
