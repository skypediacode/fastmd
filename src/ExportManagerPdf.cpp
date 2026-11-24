#include "ExportManager.h"
#include "ThemedMessageBox.h"
#include "AppShutdown.h"
#include "PdfExportHost.h"

#include <QFile>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QSettings>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QMutex>
#include <QMutexLocker>

namespace {
bool promoteCompletedPdf(const QString& stagedPath, const QString& finalPath)
{
    QFile source(stagedPath);
    QSaveFile destination(finalPath);
    // Preserve the established ability to replace a writable PDF in a
    // directory where creating a sibling temporary file is not permitted.
    destination.setDirectWriteFallback(true);
    if (!source.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly))
        return false;

    constexpr qsizetype kChunkSize = 1024 * 1024;
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(kChunkSize);
        if ((chunk.isEmpty() && source.error() != QFileDevice::NoError)
            || destination.write(chunk) != chunk.size()) {
            destination.cancelWriting();
            return false;
        }
    }
    return destination.commit();
}

// waitForFinished() timing out leaves the child still running, and ~QProcess
// then kills it and waits with *no* timeout -- so a wedged headless browser or
// Pandoc turns an already-bad bounded stall into an unbounded one, on the UI
// thread, with the window not pumping messages. Always leave the scope with the
// child actually reaped.
bool waitForProcessBounded(QProcess& process, int startMs, int finishMs)
{
    if (!process.waitForStarted(startMs)) {
        process.kill();
        process.waitForFinished(2000);
        return false;
    }
    constexpr int pollMs = 200;
    int remaining = finishMs;
    while (remaining > 0) {
        if (process.waitForFinished(qMin(pollMs, remaining)))
            return true;
        if (AppShutdown::started())
            break;
        remaining -= pollMs;
    }
    process.kill();
    process.waitForFinished(2000);
    return false;
}

// Every export below launches headless Chrome/Edge against a brand-new,
// throwaway --user-data-dir (see the profile-dir comments at each call site),
// so unlike a browser the user already has open, there is no warm profile to
// inherit. Without these flags a fresh profile spends real, sometimes
// multi-second, network-bound time on Chrome's normal first-run setup --
// component-update pings, the variations/field-trial seed fetch, Safe
// Browsing list downloads, GAIA/sync bootstrap -- none of which a one-shot
// offline PDF/PNG render needs. This is the same flag set headless automation
// tooling (Puppeteer/Playwright) launches with by default for exactly this
// reason: it turns "spin up a browser from scratch" into "load one page and
// print it" with nothing else happening in the background.
QStringList fastHeadlessFlags()
{
    return {
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        QStringLiteral("--disable-background-networking"),
        QStringLiteral("--disable-component-update"),
        QStringLiteral("--disable-sync"),
        QStringLiteral("--disable-default-apps"),
        QStringLiteral("--disable-extensions"),
        QStringLiteral("--disable-backgrounding-occluded-windows"),
        QStringLiteral("--metrics-recording-only"),
        QStringLiteral("--mute-audio"),
    };
}

// A single FastMD-owned profile directory, reused across every headless
// export (PDF, mind map PDF, mind map PNG-for-DOCX) instead of a fresh
// QTemporaryDir wiped after each one. This is deliberately not the user's
// real Edge/Chrome profile -- reusing that would risk colliding with the
// singleton lock of a browser the user already has open, and would pull in
// their extensions/flags, making the export's rendering non-deterministic.
// A dedicated, minimal, FastMD-only profile still lets Chrome keep its disk
// cache warm between exports (only the first export in a while pays full
// cold-start cost) with none of that risk.
QString persistentBrowserProfileDir()
{
    static const QString dir = [] {
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (path.isEmpty())
            path = QDir::tempPath() + QStringLiteral("/FastMD");
        path += QStringLiteral("/ExportBrowserProfile");
        QDir().mkpath(path);
        return path;
    }();
    return dir;
}

// Only one headless process may point at persistentBrowserProfileDir() at a
// time (concurrent processes sharing one --user-data-dir risk profile lock
// contention/corruption), so every browser launch below holds this for the
// duration of the subprocess. Exports run on their own small thread pool
// (see ExportThreadPool.h), so this serializes just the browser-launch
// portion of at most a couple of concurrent exports -- still far cheaper
// than giving each one its own cold throwaway profile.
QMutex& browserProfileMutex()
{
    static QMutex m;
    return m;
}

// The persistent profile is a real Chrome profile, so it accumulates real
// browser state -- in particular a "did the last process exit cleanly"
// flag and a saved session/tab list. If any headless export is ever killed
// rather than exiting on its own (waitForProcessBounded's timeout, or the
// app closing mid-export), Chrome writes "exit_type":"Crashed" into
// Preferences, and every *subsequent* launch then tries to restore that
// saved session's tabs alongside the export's own page -- spawning extra
// renderer processes and hanging for minutes instead of finishing in well
// under a second. Because the hung launch also has to be killed, the flag
// never clears itself: once poisoned, every export stays poisoned. Wiping
// the saved session and forcing a clean exit-type before each launch keeps
// the profile's actual purpose (warm disk/shader/code cache) while making
// session restore -- which a one-shot offline render never wants -- never
// trigger. Cheap: this is a couple of small file operations, not a browser
// launch.
void resetProfileSessionState(const QString& profileDir)
{
    const QString defaultDir = profileDir + QStringLiteral("/Default");
    QDir(defaultDir + QStringLiteral("/Sessions")).removeRecursively();

    QFile prefs(defaultDir + QStringLiteral("/Preferences"));
    if (!prefs.exists() || !prefs.open(QIODevice::ReadWrite))
        return;
    const QByteArray data = prefs.readAll();
    QByteArray fixed = data;
    fixed.replace("\"exit_type\":\"Crashed\"", "\"exit_type\":\"Normal\"");
    fixed.replace("\"exited_cleanly\":false", "\"exited_cleanly\":true");
    if (fixed != data) {
        prefs.resize(0);
        prefs.write(fixed);
    }
}
// Command-line export has no window to show a dialog on, so every failure is
// also reported through `error` when the caller asks for it.
void reportPdfFailure(QWidget* parent, QString* error, const QString& message)
{
    if (error)
        *error = message;
    if (parent)
        themedWarning(parent, QObject::tr("PDF Export Failed"), message);
}
} // namespace

// ---------------------------------------------------------------------------
// Locate an installed Chrome or Edge on Windows for headless PDF printing.
static QString findBrowserExecutable()
{
    QStringList candidates;
    auto addEnv = [&](const char* var, const char* rel) {
        const QString base = qEnvironmentVariable(var);
        if (!base.isEmpty())
            candidates << base + QLatin1StringView(rel);
    };
    addEnv("ProgramFiles",      "/Google/Chrome/Application/chrome.exe");
    addEnv("ProgramFiles(x86)", "/Google/Chrome/Application/chrome.exe");
    addEnv("LocalAppData",      "/Google/Chrome/Application/chrome.exe");
    addEnv("ProgramFiles",      "/Microsoft/Edge/Application/msedge.exe");
    addEnv("ProgramFiles(x86)", "/Microsoft/Edge/Application/msedge.exe");
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }


    // Best-effort registry "App Paths" lookup.
    const char* keys[] = {
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe",
    };
    for (const char* k : keys) {
        QSettings reg(QString::fromLatin1(k), QSettings::NativeFormat);
        const QString path = reg.value(QStringLiteral(".")).toString();
        if (!path.isEmpty() && QFileInfo::exists(path))
            return path;
    }
    return {};
}

static QString exportDocumentTitle(const QString& docPath, const QString& filePath)
{
    const QString title = QFileInfo(docPath.isEmpty() ? filePath : docPath).completeBaseName();
    return title.isEmpty() ? QStringLiteral("FastMD") : title;
}

// Headless Chrome/Edge only ever picks up the document title (from <title>);
// Author, Subject and Keywords have no command-line or HTML equivalent. The
// rest of this block therefore appends a PDF incremental update that rewrites
// the /Info dictionary, which is what every viewer's Document Properties reads.
//
// Everything here works on raw bytes: the /Info dictionary Chrome writes can
// contain UTF-16BE literal strings, and round-tripping those through QString
// would silently re-encode every byte above 0x7F.

// UTF-16BE hex string: the only Info-dict encoding that needs no escaping and
// that every reader accepts for non-Latin text (PDF 32000-1 7.9.2.2).
static QByteArray encodePdfUtf16Hex(const QString& str)
{
    QByteArray hex("<FEFF");
    for (const QChar ch : str) {
        const ushort u = ch.unicode();
        hex += QByteArray::number(u, 16).rightJustified(4, '0').toUpper();
    }
    return hex + '>';
}

// "1 0 obj" also occurs inside "21 0 obj", and object 1 is not guaranteed to be
// written first, so require a non-digit in front of the match.
static qsizetype findPdfObjectHeader(const QByteArray& pdf, int objNum, int genNum)
{
    const QByteArray header = QByteArray::number(objNum) + ' ' + QByteArray::number(genNum) + " obj";
    for (qsizetype at = pdf.indexOf(header); at >= 0; at = pdf.indexOf(header, at + 1)) {
        const char prev = at == 0 ? '\n' : pdf.at(at - 1);
        if (prev < '0' || prev > '9')
            return at;
    }
    return -1;
}

// Start of the value that follows `/<key>` in an Info dictionary, or -1 when the
// key is absent. Requires a name terminator so /Title does not match /TitleFoo.
static qsizetype findPdfInfoValue(const QByteArray& obj, const QByteArray& key)
{
    const QByteArray needle = QByteArray("/") + key;
    for (qsizetype at = obj.indexOf(needle); at >= 0; at = obj.indexOf(needle, at + 1)) {
        qsizetype pos = at + needle.size();
        while (pos < obj.size() && (obj.at(pos) == ' ' || obj.at(pos) == '\t'
                                    || obj.at(pos) == '\r' || obj.at(pos) == '\n')) {
            ++pos;
        }
        if (pos < obj.size() && (obj.at(pos) == '(' || obj.at(pos) == '<'))
            return pos;
    }
    return -1;
}

// One past the end of the string value starting at `pos`. Literal strings nest
// parentheses and escape them with a backslash, so a naive scan to the first
// ')' truncates any title that contains one.
static qsizetype pdfStringValueEnd(const QByteArray& obj, qsizetype pos)
{
    if (obj.at(pos) == '<') {
        if (pos + 1 < obj.size() && obj.at(pos + 1) == '<')
            return -1; // a dictionary, not a string -- leave it alone
        const qsizetype end = obj.indexOf('>', pos);
        return end < 0 ? -1 : end + 1;
    }
    int depth = 0;
    for (qsizetype i = pos; i < obj.size(); ++i) {
        const char c = obj.at(i);
        if (c == '\\') { ++i; continue; }
        if (c == '(') ++depth;
        else if (c == ')' && --depth == 0) return i + 1;
    }
    return -1;
}

static bool injectPdfMetadata(const QString& pdfPath, const ExportManager::DocumentMetadata& meta)
{
    if (meta.isEmpty())
        return true;

    QFile file(pdfPath);
    if (!file.open(QIODevice::ReadWrite))
        return false;

    const QByteArray pdfData = file.readAll();
    if (pdfData.isEmpty())
        return false;

    const qsizetype startxrefIdx = pdfData.lastIndexOf("startxref");
    const qsizetype eofIdx = startxrefIdx < 0 ? -1 : pdfData.indexOf("%%EOF", startxrefIdx);
    if (startxrefIdx < 0 || eofIdx < 0)
        return false;

    bool ok = false;
    const qsizetype offsetStart = startxrefIdx + qsizetype(sizeof("startxref") - 1);
    const qlonglong prevXref =
        pdfData.mid(offsetStart, eofIdx - offsetStart).trimmed().toLongLong(&ok);
    if (!ok)
        return false;

    // Chrome/Edge emit a classic xref table + trailer, tagged or not. A
    // cross-reference stream would need a different rewrite, so bail instead;
    // checking where startxref points rules one out even when the bytes
    // "trailer" happen to occur inside compressed stream data.
    const qsizetype trailerIdx = pdfData.lastIndexOf("trailer", startxrefIdx);
    if (trailerIdx < 0 || prevXref < 0 || prevXref >= trailerIdx
        || pdfData.mid(prevXref, 4) != "xref")
        return false;

    const QString trailerBlock =
        QString::fromLatin1(pdfData.mid(trailerIdx, startxrefIdx - trailerIdx));

    static const QRegularExpression infoRefRx(QStringLiteral(R"(/Info\s+(\d+)\s+(\d+)\s+R)"));
    const auto infoMatch = infoRefRx.match(trailerBlock);
    if (!infoMatch.hasMatch())
        return false;
    const int infoObjNum = infoMatch.captured(1).toInt();
    const int infoGenNum = infoMatch.captured(2).toInt();

    static const QRegularExpression rootRefRx(QStringLiteral(R"(/Root\s+(\d+\s+\d+\s+R))"));
    const auto rootMatch = rootRefRx.match(trailerBlock);
    if (!rootMatch.hasMatch())
        return false;
    const QByteArray rootRef = rootMatch.captured(1).simplified().toLatin1();

    static const QRegularExpression sizeRx(QStringLiteral(R"(/Size\s+(\d+))"));
    const auto sizeMatch = sizeRx.match(trailerBlock);
    if (!sizeMatch.hasMatch())
        return false;
    const int pdfSize = sizeMatch.captured(1).toInt();

    const qsizetype infoObjIdx = findPdfObjectHeader(pdfData, infoObjNum, infoGenNum);
    const qsizetype infoObjEnd = infoObjIdx < 0 ? -1 : pdfData.indexOf("endobj", infoObjIdx);
    if (infoObjIdx < 0 || infoObjEnd < 0)
        return false;

    QByteArray infoObj =
        pdfData.mid(infoObjIdx, infoObjEnd + qsizetype(sizeof("endobj") - 1) - infoObjIdx);
    const qsizetype dictEnd = infoObj.indexOf(">>");
    if (dictEnd < 0)
        return false;

    auto setKey = [&](const QByteArray& key, const QString& value) {
        const QString trimmed = value.trimmed();
        if (trimmed.isEmpty())
            return;
        const QByteArray encoded = encodePdfUtf16Hex(trimmed);
        const qsizetype valueStart = findPdfInfoValue(infoObj, key);
        const qsizetype valueEnd = valueStart < 0 ? -1 : pdfStringValueEnd(infoObj, valueStart);
        if (valueEnd > 0)
            infoObj.replace(valueStart, valueEnd - valueStart, encoded);
        else
            infoObj.insert(infoObj.indexOf(">>"), "/" + key + " " + encoded + "\n");
    };

    setKey("Title", meta.title);
    setKey("Author", meta.author);
    setKey("Subject", meta.description);
    setKey("Keywords", meta.keywords);

    const qlonglong newObjOffset = pdfData.size() + 1; // the leading '\n' below
    QByteArray appendData = "\n" + infoObj + "\n";
    const qlonglong newXrefOffset = pdfData.size() + appendData.size();

    // A one-entry xref subsection for the rewritten /Info object, chained to the
    // document's existing table via /Prev. Entries are exactly 20 bytes.
    appendData += "xref\n";
    appendData += QByteArray::number(infoObjNum) + " 1\n";
    appendData += QByteArray::number(newObjOffset).rightJustified(10, '0') + " 00000 n \n";
    appendData += "trailer\n<</Size " + QByteArray::number(pdfSize)
                + "\n/Root " + rootRef
                + "\n/Info " + QByteArray::number(infoObjNum) + " " + QByteArray::number(infoGenNum)
                + " R\n/Prev " + QByteArray::number(prevXref) + ">>\n";
    appendData += "startxref\n" + QByteArray::number(newXrefOffset) + "\n%%EOF\n";

    return file.seek(pdfData.size())
        && file.write(appendData) == appendData.size()
        && file.flush();
}

// The page to print is written into the render's own temp folder, never beside
// the Markdown file, so export needs only read access to the source folder.
// Its <base> keeps the document's relative images resolving from that folder.
// Fragment-only links ("#id", footnotes, [TOC]) stay in-document PDF links:
// Chromium matches them against the base URL, not the file's own location.
static QString pdfDocumentBaseTag(const QString& docPath)
{
    return ExportManager::documentBaseTag(
        docPath.isEmpty() ? QString() : QFileInfo(docPath).absolutePath());
}

// Chromium repeats a bookmark's title ("SetupSetup") whenever a heading starts
// a page: the glyphs overflow the headings' 1.3 line box by a hair, that sliver
// is painted (and clipped away) at the foot of the previous page, and the
// outline collects the text from both pages. Clipping only the box's top edge
// removes the sliver without moving or cutting any visible ink.
static constexpr QLatin1StringView kPdfHeadingCss(
    "<style>h1,h2,h3,h4,h5,h6{clip-path:inset(0 -100vw -100vh -100vw);}</style>");

// Chromium builds the PDF outline from rendered h1-h6, and asking for it costs
// a tagged PDF. A document without headings keeps the untagged output it had.
static bool hasHeadingElement(const QString& bodyHtml)
{
    static const QRegularExpression headingRx(QStringLiteral("<h[1-6][\\s>]"),
                                              QRegularExpression::CaseInsensitiveOption);
    return headingRx.match(bodyHtml).hasMatch();
}

static bool writeRenderHtml(const QString& path, const QString& html)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(html.toUtf8()) >= 0 && file.flush();
}

// Render a PDF by driving an installed Chrome/Edge in headless mode. This
// produces a properly authored PDF that renders identically across all viewers
// (Edge, Chrome, Adobe, Foxit) and supports the full CSS stylesheet, unlike the
// limited QTextDocument path. Used whenever a browser is available.
static bool exportPdfViaBrowser(const QString& bodyHtml, const QString& filePath,
                                const QString& docPath, const ExportManager::PdfExportOptions& options,
                                const QString& browser, bool dark,
                                const ExportManager::DocumentMetadata& meta,
                                const HeaderFooterContext& pageContext, QWidget* parent,
                                QString* error)
{
    QTemporaryDir outputDir(QDir::tempPath() + QStringLiteral("/fastmd_pdf_browser_XXXXXX"));
    if (!outputDir.isValid()) {
        if (error)
            *error = QObject::tr("Could not create temporary PDF output storage.");
        return false;
    }
    const QString stagedPdf = outputDir.filePath(QStringLiteral("output.pdf"));

    // Build a self-referencing temporary HTML file.
    QString fullHtml = ExportManager::buildPreviewHtml(
        bodyHtml, dark, options.fontSize, true, exportDocumentTitle(docPath, filePath), meta,
        options.fontFamily, options.spacing);
    // Must use lastIndexOf, not indexOf: buildPreviewHtml embeds offline asset
    // bundles (e.g. mermaid.min.js) inline in <head>, and those minified
    // bundles can themselves contain a literal "</head>" substring well
    // before the document's real closing tag -- indexOf() would match that
    // spurious occurrence first and inject the @page CSS inside a JS string,
    // where it has no effect. The real closing tag is always the last
    // occurrence, since every embedded asset is substituted into the
    // template before its own literal "</head>".
    fullHtml.insert(fullHtml.lastIndexOf(QStringLiteral("</head>")),
                    ExportManager::buildPageCss(options, pageContext) + kPdfHeadingCss
                        + pdfDocumentBaseTag(docPath));

    const QString tempHtml = outputDir.filePath(QStringLiteral("document.html"));
    if (!writeRenderHtml(tempHtml, fullHtml)) {
        reportPdfFailure(parent, error,
                         QObject::tr("Could not create a temporary HTML file for PDF export in %1.")
                             .arg(QDir::toNativeSeparators(outputDir.path())));
        return false;
    }

    QProcess proc;
    QStringList args;
    args << QStringLiteral("--headless=new")
         << QStringLiteral("--disable-gpu")
         // The generated HTML is a local file whose <base> is the Markdown
         // document's folder. Explicitly permit its file: images (and SVGs
         // that refer to other local assets) instead of relying on
         // browser-version-specific same-file-origin behavior.
         << QStringLiteral("--allow-file-access-from-files")
         << QStringLiteral("--no-pdf-header-footer")
         << QStringLiteral("--generate-pdf-document-outline")
         << QStringLiteral("--user-data-dir=") + QDir::toNativeSeparators(persistentBrowserProfileDir())
         << QStringLiteral("--print-to-pdf=") + QDir::toNativeSeparators(stagedPdf)
         << QStringLiteral("--virtual-time-budget=10000")
         << QUrl::fromLocalFile(tempHtml).toString(QUrl::FullyEncoded);
    args += fastHeadlessFlags();

    // Serialize on the shared persistent profile dir -- see browserProfileMutex().
    QMutexLocker profileLock(&browserProfileMutex());
    resetProfileSessionState(persistentBrowserProfileDir());
    proc.start(browser, args);
    bool ok = waitForProcessBounded(proc, 10000, 60000);
    profileLock.unlock();

    // The browser may keep the file briefly; confirm a non-empty PDF was produced.
    const bool produced = QFileInfo::exists(stagedPdf) && QFileInfo(stagedPdf).size() > 0;

    if (!ok || !produced) {
        reportPdfFailure(parent, error,
                         QObject::tr("The browser failed to generate the PDF.\n\n%1")
                             .arg(QString::fromLocal8Bit(proc.readAllStandardError()).trimmed()));
        return false;
    }
    // A PDF that the /Info rewrite could not parse is still a valid, complete
    // export -- only Author/Subject/Keywords are missing -- so this never fails
    // the export, but it must not fail silently either.
    if (!injectPdfMetadata(stagedPdf, meta))
        qWarning("FastMD: could not write document properties into staged PDF output");
    if (!promoteCompletedPdf(stagedPdf, filePath)) {
        if (error)
            *error = QObject::tr("Could not write the completed PDF to its destination.");
        return false;
    }
    return true;
}

// Render a PDF through the app's own WebView2 (see PdfExportHost), instead of
// launching a second, throwaway Chromium-family process. This is now the
// primary PDF export path -- see exportPdf() below -- with exportPdfViaBrowser()
// above kept only as a fallback for the rare case where the installed
// WebView2 Runtime cannot create an environment (exportPdfViaBrowser requires
// nothing WebView2 does not already require for the live preview, so that
// fallback should in practice never be reached on a machine where FastMD's
// preview itself works).
static bool exportPdfViaWebView2(const QString& bodyHtml, const QString& filePath,
                                 const QString& docPath, const ExportManager::PdfExportOptions& options,
                                 bool dark, const ExportManager::DocumentMetadata& meta,
                                 const HeaderFooterContext& pageContext,
                                 QWidget* parent, QString* hostError)
{
    QTemporaryDir outputDir(QDir::tempPath() + QStringLiteral("/fastmd_pdf_webview_XXXXXX"));
    if (!outputDir.isValid()) {
        if (hostError)
            *hostError = QStringLiteral("Could not create temporary PDF output storage.");
        return false;
    }
    const QString stagedPdf = outputDir.filePath(QStringLiteral("output.pdf"));

    QString fullHtml = ExportManager::buildPreviewHtml(
        bodyHtml, dark, options.fontSize, true, exportDocumentTitle(docPath, filePath), meta,
        options.fontFamily, options.spacing);
    // See the identical comment in exportPdfViaBrowser(): must be lastIndexOf.
    fullHtml.insert(fullHtml.lastIndexOf(QStringLiteral("</head>")),
                    ExportManager::buildPageCss(options, pageContext) + kPdfHeadingCss
                        + pdfDocumentBaseTag(docPath));

    const QString tempHtml = outputDir.filePath(QStringLiteral("document.html"));
    if (!writeRenderHtml(tempHtml, fullHtml)) {
        const QString message = QObject::tr("Could not create a temporary HTML file for PDF export in %1.")
                                    .arg(QDir::toNativeSeparators(outputDir.path()));
        if (hostError)
            *hostError = message;
        if (parent)
            themedWarning(parent, QObject::tr("PDF Export Failed"), message);
        return false;
    }

    const QString navigateUrl = QUrl::fromLocalFile(tempHtml).toString(QUrl::FullyEncoded);
    QString error;
    if (!PdfExportHost::renderToPdf(navigateUrl, stagedPdf, options, hasHeadingElement(bodyHtml), &error)) {
        if (hostError)
            *hostError = error;
        return false;
    }

    if (!injectPdfMetadata(stagedPdf, meta))
        qWarning("FastMD: could not write document properties into staged PDF output");
    if (!promoteCompletedPdf(stagedPdf, filePath)) {
        if (hostError)
            *hostError = QStringLiteral("Could not promote the completed PDF to its destination.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Confirms the destination can be created/overwritten before a browser process
// is ever started, so a locked target fails immediately with a clear message
// instead of after a headless print run.
static bool pdfDestinationIsWritable(const QString& filePath, QWidget* parent, QString* error)
{
    QFile file(filePath);
    bool writable = false;
    if (file.exists()) {
        if (file.open(QIODevice::ReadWrite)) {
            file.close();
            writable = true;
        }
    } else if (file.open(QIODevice::WriteOnly)) {
        file.close();
        file.remove();
        writable = true;
    }
    if (!writable) {
        reportPdfFailure(parent, error,
                         QObject::tr("Could not write to the destination file. The file may be open or locked by another application."));
    }
    return writable;
}

// Pays the persistent export profile's first-launch cost (profile init,
// disk/shader/code cache) against a trivial page at startup, in the
// background, so a user's first real PDF/PNG export doesn't hit it instead --
// see the profile-dir comments above. Best-effort: no browser installed, or
// the warm-up itself timing out, is silently ignored, since a real export
// will simply pay the cost (and try again) at that point.
void ExportManager::warmExportBrowser()
{
    const QString browser = findBrowserExecutable();
    if (browser.isEmpty())
        return;

    QTemporaryFile tempPdf(QDir::tempPath() + QStringLiteral("/fastmd_warm_XXXXXX.pdf"));
    tempPdf.setAutoRemove(false); // proc writes the file after this handle closes
    if (!tempPdf.open())
        return;
    const QString pdfPath = tempPdf.fileName();
    tempPdf.close();

    QProcess proc;
    QStringList args;
    args << QStringLiteral("--headless=new")
         << QStringLiteral("--disable-gpu")
         << QStringLiteral("--no-pdf-header-footer")
         << QStringLiteral("--user-data-dir=") + QDir::toNativeSeparators(persistentBrowserProfileDir())
         << QStringLiteral("--print-to-pdf=") + QDir::toNativeSeparators(pdfPath)
         << QStringLiteral("--virtual-time-budget=1000")
         << QStringLiteral("about:blank");
    args += fastHeadlessFlags();

    {
        QMutexLocker profileLock(&browserProfileMutex());
        resetProfileSessionState(persistentBrowserProfileDir());
        proc.start(browser, args);
        waitForProcessBounded(proc, 10000, 60000);
    }

    QFile::remove(pdfPath);
}

bool ExportManager::exportPdf(const QString& markdown, const QString& filePath, const QString& docPath,
                              const PdfExportOptions& options, bool dark, QWidget* parent,
                              QString* error, const QString& documentName)
{
    if (!pdfDestinationIsWritable(filePath, parent, error))
        return false;

    bool mathDetected = false;
    const DocumentMetadata meta = parseYamlFrontMatter(markdown);
    const QString katexBody = markdownToHtml(markdown, MathRenderMode::KatexOutput, &mathDetected);

    if (mathDetected && katexAssetsDir().isEmpty()) {
        reportPdfFailure(parent, error,
                         QObject::tr("Bundled KaTeX math assets were not found next to the application."));
        return false;
    }

    // An untitled tab has no docPath; its tab name stands in before the
    // destination's own name does.
    const QString pageFileName = !docPath.isEmpty() ? QFileInfo(docPath).fileName()
        : !documentName.isEmpty()                   ? documentName
                                                    : QFileInfo(filePath).fileName();
    const HeaderFooterContext pageContext = HeaderFooter::makeContext(pageFileName, meta.title);

    // PDF is rendered through FastMD's own WebView2 (see PdfExportHost) --
    // the same shell/CSS/KaTeX/highlight.js/Mermaid bundle the live preview
    // uses, so exported PDFs keep 1:1 visual parity with Preview while
    // reusing one warm environment across every export instead of launching
    // a second Chromium-family process per file. Only falls back to a
    // one-shot headless Chrome/Edge if WebView2 itself could not be used --
    // in practice that should never happen on a machine where FastMD's own
    // Preview already works, since both need the same Evergreen Runtime.
    QString webViewError;
    bool useWebView2 = true;
#ifdef FASTMD_PDF_TEST_HOOKS
    useWebView2 = !qEnvironmentVariableIsSet("FASTMD_PDF_TEST_FORCE_BROWSER");
#endif
    if (useWebView2 && exportPdfViaWebView2(katexBody, filePath, docPath, options, dark, meta, pageContext,
                                            parent, &webViewError))
        return true;
    if (AppShutdown::started()) {
        if (error)
            *error = QObject::tr("PDF generation was cancelled during shutdown.");
        return false;
    }
    qWarning("FastMD: WebView2 PDF export failed (%s), falling back to headless browser",
             qPrintable(webViewError));

    const QString browser = findBrowserExecutable();
    if (!browser.isEmpty())
        return exportPdfViaBrowser(katexBody, filePath, docPath, options, browser, dark, meta,
                                   pageContext, parent, error);

    reportPdfFailure(parent, error,
                     QObject::tr("PDF export requires the browser rendering pipeline.\n\n%1").arg(webViewError));
    return false;
}

bool ExportManager::exportPdfHtml(const QString& html, const QString& filePath, QWidget* parent)
{
    if (!pdfDestinationIsWritable(filePath, parent, nullptr))
        return false;

    const QString browser = findBrowserExecutable();
    if (browser.isEmpty()) {
        if (parent)
            themedWarning(parent, QObject::tr("PDF Export Failed"),
                          QObject::tr("Microsoft Edge or Google Chrome could not be found."));
        return false;
    }

    // buildMindMapRenderHtml() already carries the document folder as its
    // <base>, so the page and the staged PDF both stay in temp storage and the
    // source folder needs only read access.
    QTemporaryDir renderDir(QDir::tempPath() + QStringLiteral("/fastmd_mindmap_XXXXXX"));
    const QString tempHtml = renderDir.filePath(QStringLiteral("mindmap.html"));
    const QString stagedPdf = renderDir.filePath(QStringLiteral("output.pdf"));
    if (!renderDir.isValid() || !writeRenderHtml(tempHtml, html)) {
        if (parent)
            themedWarning(parent, QObject::tr("PDF Export Failed"),
                          QObject::tr("Could not create a temporary HTML file for PDF export."));
        return false;
    }

    QProcess proc;
    QStringList args {
        QStringLiteral("--headless=new"),
        QStringLiteral("--disable-gpu"),
        QStringLiteral("--allow-file-access-from-files"),
        QStringLiteral("--no-pdf-header-footer"),
        QStringLiteral("--user-data-dir=") + QDir::toNativeSeparators(persistentBrowserProfileDir()),
        QStringLiteral("--print-to-pdf=") + QDir::toNativeSeparators(stagedPdf),
        QStringLiteral("--virtual-time-budget=10000"),
        QUrl::fromLocalFile(tempHtml).toString(QUrl::FullyEncoded),
    };
    args += fastHeadlessFlags();
    QMutexLocker profileLock(&browserProfileMutex());
    resetProfileSessionState(persistentBrowserProfileDir());
    proc.start(browser, args);
    bool ok = waitForProcessBounded(proc, 10000, 60000);
    profileLock.unlock();
    const bool produced = QFileInfo::exists(stagedPdf) && QFileInfo(stagedPdf).size() > 0;
    if (!ok || !produced) {
        if (parent)
            themedWarning(parent, QObject::tr("PDF Export Failed"),
                          QObject::tr("The browser failed to generate the PDF.\n\n%1")
                              .arg(QString::fromLocal8Bit(proc.readAllStandardError()).trimmed()));
        return false;
    }
    if (!promoteCompletedPdf(stagedPdf, filePath)) {
        if (parent)
            themedWarning(parent, QObject::tr("PDF Export Failed"),
                          QObject::tr("Could not write the completed PDF to its destination."));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Rasterize standalone HTML to a PNG with the same headless browser the PDF path
// uses. Needed because Pandoc's DOCX/ODT writers hand SVG off to rsvg-convert,
// which FastMD does not bundle and which drops <foreignObject> -- the element
// Markmap renders every node label into.
bool ExportManager::renderHtmlToPng(const QString& html, const QString& pngPath,
                                    const QSize& viewport, int scaleFactor, QWidget* parent,
                                    bool transparentBackground)
{
    const QString browser = findBrowserExecutable();
    if (browser.isEmpty()) {
        if (parent)
            themedWarning(parent, QObject::tr("Export Failed"),
                          QObject::tr("Microsoft Edge or Google Chrome could not be found."));
        return false;
    }

    QTemporaryFile tempHtml(QDir(QFileInfo(pngPath).absolutePath())
                                .filePath(QStringLiteral(".fastmd_render_XXXXXX.html")));
    tempHtml.setAutoRemove(true);
    if (!tempHtml.open() || tempHtml.write(html.toUtf8()) < 0) {
        if (parent)
            themedWarning(parent, QObject::tr("Export Failed"),
                          QObject::tr("Could not create a temporary HTML file for image rendering."));
        return false;
    }
    tempHtml.close();

    QFile::remove(pngPath);

    const QSize size = viewport.isEmpty() ? QSize(1200, 800) : viewport;
    QProcess proc;
    QStringList arguments {
        QStringLiteral("--headless=new"),
        QStringLiteral("--disable-gpu"),
        QStringLiteral("--allow-file-access-from-files"),
        QStringLiteral("--hide-scrollbars"),
        QStringLiteral("--user-data-dir=") + QDir::toNativeSeparators(persistentBrowserProfileDir()),
        QStringLiteral("--screenshot=") + QDir::toNativeSeparators(pngPath),
        QStringLiteral("--window-size=%1,%2").arg(size.width()).arg(size.height()),
        QStringLiteral("--force-device-scale-factor=%1").arg(qBound(1, scaleFactor, 4)),
        QStringLiteral("--virtual-time-budget=10000"),
        QUrl::fromLocalFile(tempHtml.fileName()).toString(),
    };
    if (transparentBackground)
        arguments.insert(arguments.size() - 1, QStringLiteral("--default-background-color=00000000"));
    arguments += fastHeadlessFlags();
    QMutexLocker profileLock(&browserProfileMutex());
    resetProfileSessionState(persistentBrowserProfileDir());
    proc.start(browser, arguments);
    bool ok = waitForProcessBounded(proc, 10000, 60000);
    profileLock.unlock();
    const bool produced = QFileInfo::exists(pngPath) && QFileInfo(pngPath).size() > 0;
    if (!ok || !produced) {
        if (parent)
            themedWarning(parent, QObject::tr("Export Failed"),
                          QObject::tr("The browser failed to render the image.\n\n%1")
                              .arg(QString::fromLocal8Bit(proc.readAllStandardError()).trimmed()));
        return false;
    }
    return true;
}
