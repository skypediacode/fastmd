#include "CliExport.h"

#include "ExportManager.h"
#include "MainWindowTextIO.h"
#include "PdfExportHost.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace CliExport {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("CliExport", text);
}

QString nativePath(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

#ifdef Q_OS_WIN
HANDLE g_errorHandle = nullptr;

// FastMD.exe is a WIN32-subsystem program, so it starts with no console. Keep
// stderr when the caller redirected it to a file or pipe; otherwise borrow the
// launching terminal's console. Launched from Explorer there is neither, and
// the exit code is the only report.
void attachErrorOutput()
{
    HANDLE inherited = GetStdHandle(STD_ERROR_HANDLE);
    if (inherited && inherited != INVALID_HANDLE_VALUE && GetFileType(inherited) != FILE_TYPE_UNKNOWN) {
        g_errorHandle = inherited;
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;
    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    HANDLE console = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (console != INVALID_HANDLE_VALUE)
        g_errorHandle = console;
}

void writeError(const QString& message)
{
    if (!g_errorHandle)
        return;
    const QString line = QStringLiteral("FastMD: ") + message + QLatin1Char('\n');
    DWORD written = 0;
    DWORD mode = 0;
    if (GetConsoleMode(g_errorHandle, &mode)) {
        WriteConsoleW(g_errorHandle, line.utf16(), static_cast<DWORD>(line.size()), &written, nullptr);
    } else {
        const QByteArray utf8 = line.toUtf8();
        WriteFile(g_errorHandle, utf8.constData(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}
#else
void attachErrorOutput() {}

void writeError(const QString& message)
{
    std::fprintf(stderr, "FastMD: %s\n", qPrintable(message));
}
#endif

// Qt's own diagnostics (e.g. the WebView2 -> headless browser fallback note)
// would otherwise reach the borrowed console; the final error message already
// carries whatever the user needs.
void discardQtMessages(QtMsgType, const QMessageLogContext&, const QString&) {}

int fail(int exitCode, const QString& message)
{
    writeError(message);
    return exitCode;
}

int convert(const Arguments& arguments)
{
    const QFileInfo input(arguments.inputPath);
    if (!input.exists())
        return fail(ExitFileError, tr("Input file not found: %1").arg(nativePath(arguments.inputPath)));
    if (!input.isFile())
        return fail(ExitFileError, tr("Input is not a file: %1").arg(nativePath(arguments.inputPath)));

    QString markdown;
    switch (loadEditableTextFile(input.absoluteFilePath(), &markdown, nullptr)) {
    case EditableTextFileLoadResult::Success:
        break;
    case EditableTextFileLoadResult::NotEditable:
        return fail(ExitFileError, tr("Input is not a text file: %1").arg(nativePath(arguments.inputPath)));
    case EditableTextFileLoadResult::ReadError:
        return fail(ExitFileError, tr("Could not read input file: %1").arg(nativePath(arguments.inputPath)));
    }

    const QFileInfo output(arguments.outputPath);
    if (output.isDir())
        return fail(ExitFileError, tr("Output path is a folder: %1").arg(nativePath(arguments.outputPath)));
    if (output.exists() && !arguments.force) {
        return fail(ExitFileError, tr("Output file already exists: %1. Use --force to overwrite it.")
                                       .arg(nativePath(arguments.outputPath)));
    }
    if (!output.absoluteDir().exists()) {
        return fail(ExitFileError, tr("Output folder does not exist: %1")
                                       .arg(nativePath(output.absolutePath())));
    }

    const QSettings settings;
    const ExportManager::PdfExportOptions options = ExportManager::pdfExportOptionsFromSettings(
        [&settings](const QString& key, const QVariant& defaultValue) {
            return settings.value(key, defaultValue);
        });

    QString error;
    const bool exported = ExportManager::exportPdf(markdown, arguments.outputPath, input.absoluteFilePath(),
                                                   options, false, nullptr, &error);
    if (!exported) {
        return fail(ExitExportFailed, error.isEmpty() ? tr("PDF export failed.")
                                                      : tr("PDF export failed: %1").arg(error));
    }
    return ExitSuccess;
}

}

int run(const QStringList& arguments)
{
    attachErrorOutput();
    qInstallMessageHandler(discardQtMessages);

    const Arguments parsed = parseArguments(arguments, QDir::currentPath());
    if (!parsed.isValid())
        return fail(ExitUsageError, parsed.error + QLatin1Char('\n') + usageText());

    const int exitCode = convert(parsed);
    PdfExportHost::shutdown();
    return exitCode;
}

}
