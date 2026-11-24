#pragma once

#include <QString>
#include <QStringList>

// Pure parser for command-line PDF export:
//
//   FastMD.exe <input.md> -o|--output <output.pdf> [--force]
//
// Links Qt6::Core alone so CliExportArgumentsTest needs no Widgets or WebView2.
namespace CliExport {

enum ExitCode {
    ExitSuccess = 0,
    ExitExportFailed = 1,
    ExitUsageError = 2,
    ExitFileError = 3,
};

struct Arguments {
    // True whenever -o/--output appears, even when the rest is invalid: the
    // process must then never fall through to the GUI or single-instance path.
    bool requested = false;
    QString inputPath;  // absolute, resolved against the working directory
    QString outputPath; // absolute, resolved against the working directory
    bool force = false;
    QString error;      // non-empty means a usage error

    bool isValid() const { return requested && error.isEmpty(); }
};

// `arguments` includes argv[0], as QCoreApplication::arguments() returns it.
bool isRequested(const QStringList& arguments);
Arguments parseArguments(const QStringList& arguments, const QString& workingDirectory);
QString usageText();

}
