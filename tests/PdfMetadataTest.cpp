// Verifies that PDF export writes the YAML front matter into the PDF's /Info
// dictionary. Headless Chrome/Edge only honors <title>, so ExportManagerPdf
// appends an incremental update for Author/Subject/Keywords -- this exercises
// that byte surgery against a real browser-produced PDF.

#include "ExportManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <QThread>

#include <future>

namespace {

bool require(bool condition, const QString& message)
{
    if (!condition)
        qCritical().noquote() << message;
    return condition;
}

// UTF-16BE hex string, matching what injectPdfMetadata writes.
QByteArray expectedHexValue(const QString& text)
{
    QByteArray hex("<FEFF");
    for (const QChar ch : text)
        hex += QByteArray::number(ch.unicode(), 16).rightJustified(4, '0').toUpper();
    return hex + '>';
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString samplePath = QStringLiteral(FASTMD_FIXTURES_DIR "/front-matter.md");
    QFile sample(samplePath);
    if (!require(sample.open(QIODevice::ReadOnly), QStringLiteral("tests/fixtures/front-matter.md is missing")))
        return 1;
    const QString markdown = QString::fromUtf8(sample.readAll());

    const ExportManager::DocumentMetadata meta = ExportManager::parseYamlFrontMatter(markdown);
    if (!require(!meta.isEmpty(), QStringLiteral("the sample has no parseable front matter")))
        return 1;

    QTemporaryDir dir;
    if (!require(dir.isValid(), QStringLiteral("could not create a temporary directory")))
        return 1;
    const QString pdfPath = dir.filePath(QStringLiteral("front-matter.pdf"));

    ExportManager::PdfExportOptions options;
    if (!ExportManager::exportPdf(markdown, pdfPath, samplePath, options, false)) {
        // The export pipeline requires an installed Chrome/Edge. Skipping is the
        // honest outcome on a machine without one -- but say so out loud.
        qWarning().noquote() << "SKIPPED: PDF export unavailable (no Chrome/Edge found).";
        return 0;
    }

    QFile pdf(pdfPath);
    if (!require(pdf.open(QIODevice::ReadOnly), QStringLiteral("could not read back the exported PDF")))
        return 1;
    const QByteArray data = pdf.readAll();

    bool ok = true;
    ok &= require(data.startsWith("%PDF-") && data.trimmed().endsWith("%%EOF"),
                  QStringLiteral("the rewritten PDF is not a well-formed PDF"));

    // The incremental update must be appended, leaving the browser's own
    // xref table and its trailer intact ahead of it.
    ok &= require(data.count("startxref") >= 2 && data.contains("/Prev "),
                  QStringLiteral("no incremental update was appended to the PDF"));

    struct { const char* key; QString value; } entries[] = {
        { "/Title",    meta.title },
        { "/Author",   meta.author },
        { "/Subject",  meta.description },
        { "/Keywords", meta.keywords },
    };
    for (const auto& entry : entries) {
        const QByteArray expected = QByteArray(entry.key) + " " + expectedHexValue(entry.value.trimmed());
        ok &= require(data.contains(expected),
                      QStringLiteral("exported PDF /Info is missing %1 as a UTF-16 hex string")
                          .arg(QString::fromLatin1(entry.key)));
    }

    // Relative raster and SVG images are resolved from the Markdown document's
    // directory by the browser export path. This uses self-contained fixtures
    // rather than a user sample file so it remains a stable regression test.
    const QString imageDocPath = dir.filePath(QStringLiteral("images.md"));
    QFile svg(dir.filePath(QStringLiteral("image.svg")));
    if (!require(svg.open(QIODevice::WriteOnly)
                     && svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\">"
                                  "<rect width=\"64\" height=\"64\" fill=\"#2e9eff\"/></svg>") > 0,
                 QStringLiteral("could not create local SVG PDF fixture"))) {
        return 1;
    }
    svg.close();
    QFile png(dir.filePath(QStringLiteral("image.png")));
    const QByteArray tinyPng = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/"
        "y2Y6AAAAAElFTkSuQmCC");
    if (!require(png.open(QIODevice::WriteOnly) && png.write(tinyPng) == tinyPng.size(),
                 QStringLiteral("could not create local PNG PDF fixture"))) {
        return 1;
    }
    png.close();
    const QString imageMarkdown = QStringLiteral("![](image.svg)\n\n![](image.png)\n");
    const QString imagePdfPath = dir.filePath(QStringLiteral("Untitled.pdf"));
    if (!require(ExportManager::exportPdf(imageMarkdown, imagePdfPath, imageDocPath, options, false),
                 QStringLiteral("PDF export failed for Markdown with local images"))) {
        return 1;
    }
    if (!require(QFileInfo::exists(imagePdfPath) && QFileInfo(imagePdfPath).size() > 0,
                 QStringLiteral("image PDF was not produced"))) {
        return 1;
    }

    // Broken local resources must not keep the headless browser alive. Missing,
    // corrupt, repeated, and absolute local references all degrade to the
    // browser's normal broken-image rendering while the PDF still completes.
    QFile corrupt(dir.filePath(QStringLiteral("corrupt.png")));
    if (!require(corrupt.open(QIODevice::WriteOnly) && corrupt.write("not an image") > 0,
                 QStringLiteral("could not create corrupt image PDF fixture"))) {
        return 1;
    }
    corrupt.close();
    const QString mixedMarkdown = QStringLiteral("![](image.svg)\n\n![](image.svg)\n\n![](<%1>)\n\n![](missing.png)\n\n![](corrupt.png)\n")
        .arg(QUrl::fromLocalFile(png.fileName()).toString());
    const QString mixedPdfPath = dir.filePath(QStringLiteral("mixed-images.pdf"));
    if (!require(ExportManager::exportPdf(mixedMarkdown, mixedPdfPath, imageDocPath, options, false),
                 QStringLiteral("PDF export did not complete with broken local images"))
        || !require(QFileInfo::exists(mixedPdfPath) && QFileInfo(mixedPdfPath).size() > 0,
                    QStringLiteral("broken-image PDF was not produced"))) {
        return 1;
    }

    // The rewrite must not have re-encoded the bytes it left alone.
    const qsizetype lastInfo = data.lastIndexOf("/Author");
    ok &= require(lastInfo > 0 && !data.mid(lastInfo).contains("\xC3\xBE\xC3\xBF"),
                  QStringLiteral("the /Info dictionary was double-encoded through Latin-1"));

    // Walk the appended table the way a reader does: the trailing startxref must
    // point at it, and its one entry must point at the rewritten object. A
    // miscalculated offset here is invisible in the raw bytes but breaks every
    // viewer, so resolve it rather than trusting the append arithmetic.
    const qsizetype finalStartxref = data.lastIndexOf("startxref");
    const qsizetype eof = data.indexOf("%%EOF", finalStartxref);
    bool parsed = false;
    const qlonglong xrefAt = finalStartxref < 0 || eof < 0
        ? -1
        : data.mid(finalStartxref + 9, eof - finalStartxref - 9).trimmed().toLongLong(&parsed);
    if (require(parsed && xrefAt > 0 && xrefAt < data.size()
                    && data.mid(xrefAt, 4) == "xref",
                QStringLiteral("the appended startxref does not point at the new xref table"))) {
        const QList<QByteArray> rows =
            data.mid(xrefAt + 5, 40).split('\n'); // subsection header + first entry
        const QList<QByteArray> header = rows.value(0).simplified().split(' ');
        const qlonglong objOffset = rows.value(1).left(10).toLongLong(&parsed);
        const QByteArray expectedHeader =
            header.value(0) + " " + QByteArray::number(0) + " obj";
        ok &= require(header.size() == 2 && header.value(1) == "1",
                      QStringLiteral("the new xref subsection is not a single-object subsection"));
        ok &= require(parsed && objOffset > 0 && objOffset < data.size()
                          && data.mid(objOffset, expectedHeader.size()) == expectedHeader,
                      QStringLiteral("the new xref entry does not point at the rewritten /Info object"));
    } else {
        ok = false;
    }

    // Force the WebView2 completion beyond its bounded wait. Its late staged
    // output must be isolated from the browser fallback's final destination,
    // and retiring the controller must leave the next export able to recreate
    // a clean host.
    const QString timedOutPath = dir.filePath(QStringLiteral("timed-out-webview.pdf"));
    qputenv("FASTMD_PDF_TEST_TIMEOUT_MS", QByteArrayLiteral("1"));
    qputenv("FASTMD_PDF_TEST_COMPLETION_DELAY_MS", QByteArrayLiteral("100"));
    ok &= require(ExportManager::exportPdf(QStringLiteral("# Timeout fallback\n\nStable output"),
                                            timedOutPath, samplePath, options, false),
                  QStringLiteral("PDF fallback after forced WebView2 timeout failed"));
    qunsetenv("FASTMD_PDF_TEST_TIMEOUT_MS");
    qunsetenv("FASTMD_PDF_TEST_COMPLETION_DELAY_MS");
    QFile timedOutFile(timedOutPath);
    ok &= require(timedOutFile.open(QIODevice::ReadOnly),
                  QStringLiteral("timed-out fallback PDF could not be opened"));
    const QByteArray timedOutBytes = timedOutFile.readAll();
    timedOutFile.close();
    QThread::msleep(250);
    ok &= require(timedOutFile.open(QIODevice::ReadOnly),
                  QStringLiteral("timed-out fallback PDF could not be reopened"));
    ok &= require(!timedOutBytes.isEmpty() && timedOutFile.readAll() == timedOutBytes,
                  QStringLiteral("late WebView2 completion changed the fallback destination"));

    // Two export tasks can be scheduled together by separate windows. The
    // shared WebView2 controller is single-threaded, so the host must serialize
    // them without mixing callbacks or output paths.
    const QString concurrentA = dir.filePath(QStringLiteral("concurrent-a.pdf"));
    const QString concurrentB = dir.filePath(QStringLiteral("concurrent-b.pdf"));
    auto exportA = std::async(std::launch::async, [&] {
        return ExportManager::exportPdf(QStringLiteral("# Export A\n\nAlpha"),
                                        concurrentA, samplePath, options, false);
    });
    auto exportB = std::async(std::launch::async, [&] {
        return ExportManager::exportPdf(QStringLiteral("# Export B\n\nBeta"),
                                        concurrentB, samplePath, options, false);
    });
    ok &= require(exportA.get() && exportB.get(),
                  QStringLiteral("concurrent PDF exports did not both complete"));
    ok &= require(QFileInfo(concurrentA).size() > 0 && QFileInfo(concurrentB).size() > 0,
                  QStringLiteral("concurrent PDF export produced an empty output"));

    return ok ? 0 : 1;
}
