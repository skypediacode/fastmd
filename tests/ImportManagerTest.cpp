#include "ImportManager.h"
#include "WorkspaceFileOps.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QProcess>
#include <QSet>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

bool require(bool condition, const QString& message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

void touch(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write("x");
}

}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    const QString filter = ImportManager::importFileFilter();
    for (const QString& extension : { QStringLiteral("*.doc"), QStringLiteral("*.docx"),
                                     QStringLiteral("*.pptx"), QStringLiteral("*.xlsx"),
                                     QStringLiteral("*.odt"), QStringLiteral("*.rtf"),
                                     QStringLiteral("*.epub"), QStringLiteral("*.csv"),
                                     QStringLiteral("*.pdf") }) {
        ok &= require(filter.contains(extension),
                      QStringLiteral("import filter missing %1").arg(extension));
    }
    ok &= require(ImportManager::converterPath().endsWith(QStringLiteral("/tools/anydoc/fastmd-anydoc.exe")),
                  QStringLiteral("converter must resolve only beside FastMD"));

    // The filter is generated from supportedExtensions(), so the two can never
    // drift -- every advertised pattern must have a matching list entry.
    for (const QString& extension : ImportManager::supportedExtensions()) {
        ok &= require(filter.contains(QStringLiteral("*.") + extension),
                      QStringLiteral("import filter missing *.%1").arg(extension));
    }
    ok &= require(ImportManager::documentFilterEntry().count(QLatin1Char('*'))
                      == ImportManager::supportedExtensions().size(),
                  QStringLiteral("document filter entry must list every supported extension once"));

    // Insert > Content from File routes on this predicate: a supported document
    // goes through AnyDoc, everything else is read as text. Extension, not
    // content -- .rtf and .csv are text but must stay on the converter path.
    for (const QString& suffix : { QStringLiteral("docx"), QStringLiteral("DOCX"),
                                   QStringLiteral("Pdf"), QStringLiteral("rtf"),
                                   QStringLiteral("csv") }) {
        ok &= require(ImportManager::handlesExtension(suffix),
                      QStringLiteral("%1 must route to the converter").arg(suffix));
    }
    for (const QString& suffix : { QStringLiteral("md"), QStringLiteral("markdown"),
                                   QStringLiteral("txt"), QStringLiteral("json"),
                                   QStringLiteral("yaml"), QStringLiteral("cpp"),
                                   QStringLiteral("log"), QStringLiteral("docxx"),
                                   QStringLiteral("") }) {
        ok &= require(!ImportManager::handlesExtension(suffix),
                      QStringLiteral("'%1' must be inserted as raw text").arg(suffix));
    }

    // A scratch destination is unique, does not exist yet (the converter refuses
    // an existing output), and removeTemporary() takes its folder with it.
    const QString scratch = ImportManager::temporaryDestinationFor(QStringLiteral("C:/docs/Report.docx"));
    ok &= require(!scratch.isEmpty() && !QFileInfo::exists(scratch),
                  QStringLiteral("scratch destination must not exist yet"));
    ok &= require(QFileInfo(scratch).fileName() == QStringLiteral("Report.md"),
                  QStringLiteral("scratch destination must keep the source base name"));
    const QString scratchAgain = ImportManager::temporaryDestinationFor(QStringLiteral("C:/docs/Report.docx"));
    ok &= require(scratch != scratchAgain, QStringLiteral("scratch destinations must be unique"));
    ImportManager::removeTemporary(scratchAgain);
    const QString scratchFolder = QFileInfo(scratch).absolutePath();
    ok &= require(QFileInfo::exists(scratchFolder), QStringLiteral("scratch folder must be created"));
    touch(scratch);
    ImportManager::removeTemporary(scratch);
    ok &= require(!QFileInfo::exists(scratchFolder),
                  QStringLiteral("removeTemporary must delete the scratch folder"));

    // The destination is derived from the source with no dialog, lands beside
    // the source document, and never collides with an existing file.
    QTemporaryDir dir;
    ok &= require(dir.isValid(), QStringLiteral("could not create temp dir"));
    const QDir root(dir.path());
    const QString source = root.filePath(QStringLiteral("Report.docx"));
    touch(source);

    const QString first = ImportManager::destinationFor(source);
    ok &= require(QFileInfo(first).fileName() == QStringLiteral("Report.md"),
                  QStringLiteral("expected Report.md, got %1").arg(QFileInfo(first).fileName()));
    ok &= require(QFileInfo(first).absolutePath() == QFileInfo(source).absolutePath(),
                  QStringLiteral("destination must sit beside the source document"));

    touch(first);
    const QString second = ImportManager::destinationFor(source);
    ok &= require(QFileInfo(second).fileName() == QStringLiteral("Report (2).md"),
                  QStringLiteral("expected Report (2).md, got %1").arg(QFileInfo(second).fileName()));

    touch(second);
    const QString third = ImportManager::destinationFor(source);
    ok &= require(QFileInfo(third).fileName() == QStringLiteral("Report (3).md"),
                  QStringLiteral("expected Report (3).md, got %1").arg(QFileInfo(third).fileName()));

    // A dotted source name keeps its full base, and never overwrites.
    const QString dotted = root.filePath(QStringLiteral("Q1.2026.final.docx"));
    touch(dotted);
    ok &= require(QFileInfo(ImportManager::destinationFor(dotted)).fileName()
                      == QStringLiteral("Q1.2026.final.md"),
                  QStringLiteral("expected Q1.2026.final.md"));

    ok &= require(!QFileInfo::exists(ImportManager::destinationFor(source)),
                  QStringLiteral("destination must never be an existing file"));

    const QString copySourceDir = root.filePath(QStringLiteral("copy-source"));
    const QString copyDestDir = root.filePath(QStringLiteral("copy-destination"));
    QDir().mkpath(copySourceDir);
    QDir().mkpath(copyDestDir);
    touch(QDir(copySourceDir).filePath(QStringLiteral("note.md")));
    const QString copiedDir = WorkspaceFileOps::copyToDirectory(copySourceDir, copyDestDir);
    ok &= require(QFileInfo::exists(QDir(copiedDir).filePath(QStringLiteral("note.md"))),
                  QStringLiteral("recursive workspace copy did not preserve a file"));

    const QString nestedCopySourceDir = QDir(copySourceDir).filePath(QStringLiteral("nested"));
    QDir().mkpath(nestedCopySourceDir);
    touch(QDir(nestedCopySourceDir).filePath(QStringLiteral("child.md")));
    const QString copiedDirCollision = WorkspaceFileOps::copyToDirectory(copySourceDir, copyDestDir);
    ok &= require(QFileInfo(copiedDirCollision).fileName() == QStringLiteral("copy-source (2)"),
                  QStringLiteral("workspace folder copy did not resolve a duplicate name"));
    ok &= require(QFileInfo::exists(QDir(copiedDirCollision).filePath(QStringLiteral("nested/child.md"))),
                  QStringLiteral("recursive workspace copy did not preserve nested files"));

    const QString copySourceFile = root.filePath(QStringLiteral("copy-source.md"));
    touch(copySourceFile);
    const QString copiedFile = WorkspaceFileOps::copyToDirectory(copySourceFile, copyDestDir);
    const QString copiedFileCollision = WorkspaceFileOps::copyToDirectory(copySourceFile, copyDestDir);
    ok &= require(QFileInfo(copiedFile).fileName() == QStringLiteral("copy-source.md"),
                  QStringLiteral("workspace file copy did not preserve its original name"));
    ok &= require(QFileInfo(copiedFileCollision).fileName() == QStringLiteral("copy-source (2).md"),
                  QStringLiteral("workspace file copy did not preserve its duplicate-name convention"));

    WorkspaceFileOps::WorkspaceCopyCancellation cancelled;
    cancelled.cancel();
    const QString cancelledCopy = WorkspaceFileOps::copyToDirectory(
        copySourceDir, copyDestDir, &cancelled);
    ok &= require(cancelledCopy.isEmpty(),
                  QStringLiteral("a cancelled workspace copy unexpectedly completed"));

    const QString concurrentDest = root.filePath(QStringLiteral("concurrent-destination"));
    const QString concurrentSourceA = root.filePath(QStringLiteral("source-a/note.md"));
    const QString concurrentSourceB = root.filePath(QStringLiteral("source-b/note.md"));
    QDir().mkpath(QFileInfo(concurrentSourceA).absolutePath());
    QDir().mkpath(QFileInfo(concurrentSourceB).absolutePath());
    {
        QFile a(concurrentSourceA);
        QFile b(concurrentSourceB);
        if (!a.open(QIODevice::WriteOnly) || !b.open(QIODevice::WriteOnly))
            return 1;
        a.write("alpha");
        b.write("beta");
    }
    QDir().mkpath(concurrentDest);
    auto copyA = std::async(std::launch::async, [&] {
        return WorkspaceFileOps::copyToDirectory(concurrentSourceA, concurrentDest);
    });
    auto copyB = std::async(std::launch::async, [&] {
        return WorkspaceFileOps::copyToDirectory(concurrentSourceB, concurrentDest);
    });
    const QString copiedA = copyA.get();
    const QString copiedB = copyB.get();
    ok &= require(!copiedA.isEmpty() && !copiedB.isEmpty() && copiedA != copiedB,
                  QStringLiteral("concurrent copies did not reserve distinct destinations"));
    QFile copiedFileA(copiedA);
    QFile copiedFileB(copiedB);
    ok &= require(copiedFileA.open(QIODevice::ReadOnly),
                  QStringLiteral("first concurrent copy could not be opened"));
    ok &= require(copiedFileB.open(QIODevice::ReadOnly),
                  QStringLiteral("second concurrent copy could not be opened"));
    const QSet<QByteArray> copiedContents { copiedFileA.readAll(), copiedFileB.readAll() };
    ok &= require(copiedContents == QSet<QByteArray>{QByteArrayLiteral("alpha"), QByteArrayLiteral("beta")},
                  QStringLiteral("a concurrent copy overwrote another copy's contents"));

#ifdef Q_OS_WIN
    const QString junctionSource = root.filePath(QStringLiteral("junction-source"));
    const QString junctionTarget = root.filePath(QStringLiteral("junction-target"));
    const QString junctionPath = QDir(junctionSource).filePath(QStringLiteral("outside"));
    QDir().mkpath(junctionSource);
    QDir().mkpath(junctionTarget);
    touch(QDir(junctionTarget).filePath(QStringLiteral("external.md")));
    QProcess mklink;
    mklink.start(QStringLiteral("cmd.exe"), { QStringLiteral("/c"), QStringLiteral("mklink"),
        QStringLiteral("/J"), QDir::toNativeSeparators(junctionPath),
        QDir::toNativeSeparators(junctionTarget) });
    mklink.waitForFinished(5000);
    ok &= require(mklink.exitCode() == 0, QStringLiteral("test junction could not be created"));
    const QString junctionCopy = WorkspaceFileOps::copyToDirectory(junctionSource, copyDestDir);
    ok &= require(junctionCopy.isEmpty(),
                  QStringLiteral("workspace copy unexpectedly traversed an NTFS junction"));

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    ok &= require(CreatePipe(&readPipe, &writePipe, nullptr, 0) != FALSE,
                  QStringLiteral("cancellation test pipe could not be created"));
    WorkspaceFileOps::WorkspaceCopyCancellation ioCancellation;
    std::promise<void> readStarted;
    std::atomic<DWORD> readError{ERROR_SUCCESS};
    std::thread blockedReader([&] {
        if (!ioCancellation.beginBlockingOperation()) {
            readError.store(ERROR_CANCELLED);
            readStarted.set_value();
            return;
        }
        readStarted.set_value();
        char byte = 0;
        DWORD read = 0;
        if (!ReadFile(readPipe, &byte, 1, &read, nullptr))
            readError.store(GetLastError());
        ioCancellation.endBlockingOperation();
    });
    readStarted.get_future().wait();
    const auto cancelStarted = std::chrono::steady_clock::now();
    ioCancellation.cancel();
    blockedReader.join();
    const auto cancelElapsed = std::chrono::steady_clock::now() - cancelStarted;
    CloseHandle(readPipe);
    CloseHandle(writePipe);
    ok &= require(readError.load() == ERROR_OPERATION_ABORTED,
                  QStringLiteral("cancellation did not interrupt synchronous filesystem I/O"));
    ok &= require(cancelElapsed < std::chrono::seconds(2),
                  QStringLiteral("synchronous filesystem cancellation was not bounded"));
#endif

    return ok ? 0 : 1;
}
