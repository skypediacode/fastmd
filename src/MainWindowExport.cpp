#include "MainWindow.h"
#include "TabWidget.h"
#include "MarkdownEditor.h"
#include "ExportManager.h"
#include "HeaderFooter.h"
#include "HeaderFooterDialog.h"
#include "DocumentModel.h"
#include "IconHelper.h"
#include "ThemedMessageBox.h"
#include "WorkspaceFileOps.h"
#include "PreviewWidget.h"
#include "ImageViewer.h"
#include "ExportThreadPool.h"
#include "RatePrompt.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QPageSetupDialog>
#include <QPrinter>
#include <QDialog>
#include <QCoreApplication>
#include <QEventLoop>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPalette>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QStatusBar>
#include <QFutureWatcher>
#include <QtConcurrent>

#include <functional>

#ifdef Q_OS_WIN
// Defined in MainWindow.cpp; sets the native title bar to dark/light mode.
bool setWindowsTitleBarDarkMode(QWidget* window, bool dark, bool includeCaptionColor = true);
#endif

// ---------------------------------------------------------------------------
// Export "success" popup, reused by every doExport*() below.
// ---------------------------------------------------------------------------
class ExportSuccessDialog : public QDialog {
public:
    ExportSuccessDialog(const QString& filePath, const QString& typeName, QWidget* parent,
                        const QString& note = QString(),
                        std::function<void(const QString&)> openHandler = {})
        : QDialog(parent)
        , m_filePath(filePath)
        , m_openHandler(std::move(openHandler))
    {
        setWindowTitle(tr("Export %1").arg(typeName));
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

#ifdef Q_OS_WIN
        const bool isDarkTheme = palette().color(QPalette::Window).lightness() < 128;
        setWindowsTitleBarDarkMode(this, isDarkTheme);
#endif

        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(20, 20, 20, 16);
        mainLayout->setSpacing(16);

        auto* topLayout = new QHBoxLayout();
        topLayout->setContentsMargins(0, 0, 0, 0);
        topLayout->setSpacing(12);
        topLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        // Icon
        auto* iconLabel = new QLabel(this);
        QIcon icon = IconHelper::materialIcon(QChar(0xE86C), QColor("#4CAF50"));
        iconLabel->setPixmap(icon.pixmap(32, 32));
        topLayout->addWidget(iconLabel);

        // Text
        auto* textLabel = new QLabel(tr("%1 exported successfully.").arg(typeName), this);
        textLabel->setWordWrap(false);
        topLayout->addWidget(textLabel);

        mainLayout->addLayout(topLayout);

        if (!note.isEmpty()) {
            auto* noteLabel = new QLabel(note, this);
            noteLabel->setWordWrap(true);
            noteLabel->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
            mainLayout->addWidget(noteLabel);
        }

        // Buttons
        auto* btnLayout = new QHBoxLayout();
        btnLayout->setSpacing(8);
        btnLayout->setAlignment(Qt::AlignCenter);

        auto* openBtn = new QPushButton(tr("Open %1").arg(typeName), this);
        openBtn->setDefault(true);
        openBtn->setFixedWidth(96);

        auto* openFolderBtn = new QPushButton(tr("Open Folder"), this);
        openFolderBtn->setFixedWidth(96);

        auto* closeBtn = new QPushButton(tr("Close"), this);
        closeBtn->setFixedWidth(96);

        btnLayout->addWidget(openBtn);
        btnLayout->addWidget(openFolderBtn);
        btnLayout->addWidget(closeBtn);

        mainLayout->addLayout(btnLayout);

        setLayout(mainLayout);
        adjustSize();
        setFixedSize(size());

        connect(openBtn, &QPushButton::clicked, this, &ExportSuccessDialog::onOpenFile);
        connect(openFolderBtn, &QPushButton::clicked, this, &ExportSuccessDialog::onOpenFolder);
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    }

private:
    void onOpenFile()
    {
        if (m_openHandler)
            m_openHandler(m_filePath);
        else
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_filePath));
        accept();
    }

    void onOpenFolder()
    {
        WorkspaceFileOps::revealFileInExplorer(m_filePath);
        accept();
    }

    QString m_filePath;
    std::function<void(const QString&)> m_openHandler;
};

// ---------------------------------------------------------------------------
static void applyPdfExportOptions(QPrinter& printer, const ExportManager::PdfExportOptions& options)
{
    printer.setPageSize(options.pageSize);
    printer.setPageOrientation(options.orientation);
    printer.setPageMargins(options.marginsMm, QPageLayout::Millimeter);
}

// ---------------------------------------------------------------------------
static ExportManager::PdfExportOptions pdfExportOptionsFromPrinter(const QPrinter& printer, int fontSize)
{
    ExportManager::PdfExportOptions options;
    options.pageSize = printer.pageLayout().pageSize();
    options.orientation = printer.pageLayout().orientation();
    options.marginsMm = printer.pageLayout().margins(QPageLayout::Millimeter);
    options.fontSize = fontSize;
    return options;
}

// ---------------------------------------------------------------------------
static QString buildPlainTextPrintBody(const QString& text, const QFont& editorFont)
{
    QStringList families = editorFont.families();
    if (families.isEmpty() && !editorFont.family().isEmpty())
        families << editorFont.family();
    families << QStringLiteral("Consolas") << QStringLiteral("monospace");
    QStringList quoted;
    quoted.reserve(families.size());
    for (const QString& f : families)
        quoted << QStringLiteral("'%1'").arg(f);

    const int px = editorFont.pixelSize() > 0 ? editorFont.pixelSize() : editorFont.pointSize();

    return QStringLiteral(
        "<pre style=\"margin:0;white-space:pre-wrap;word-break:break-word;"
        "overflow-wrap:anywhere;font-family:%1;font-size:%2px;\">%3</pre>")
        .arg(quoted.join(QStringLiteral(",")), QString::number(px > 0 ? px : 14), text.toHtmlEscaped());
}

// The values {file}, {title}, {date} and {time} stand for in a header or
// footer: the tab's own name (so an untitled tab prints "Untitled 1") and, for
// Markdown, a front-matter title ahead of the base name.
static HeaderFooterContext headerFooterContextFor(const TabPage* pg, const QString& text, bool markdown)
{
    return HeaderFooter::makeContext(
        pg ? pg->model->displayName() : QString(),
        markdown ? ExportManager::parseYamlFrontMatter(text).title : QString());
}

// Browser preview and printing use an HTML file in the system temporary
// directory.  Keep a saved document's directory as the HTML base, otherwise
// Markdown image URLs such as "images/logo.png" resolve relative to the temp
// directory instead of the Markdown file.
static QString documentBaseTag(const QString& documentPath)
{
    return ExportManager::documentBaseTag(
        documentPath.isEmpty() ? QString() : QFileInfo(documentPath).absolutePath());
}

// ---------------------------------------------------------------------------
QString MainWindow::exportInitialPath(const QString& extension) const
{
    TabPage* pg = m_tabs->currentPage();
    const QString docPath = pg ? pg->model->filePath() : QString();

    // Saved document: same folder, same base name, swapped extension.
    if (!docPath.isEmpty()) {
        const QFileInfo fi(docPath);
        return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + extension;
    }

    // Unsaved: use the tab title in the default (or last used) export folder.
    const QString name = pg ? pg->model->displayName() : QStringLiteral("Untitled");
    QString dir = !m_defaultSaveFolder.isEmpty() ? m_defaultSaveFolder
                : m_lastExportFolder;
    return (dir.isEmpty() ? QString() : dir + QLatin1Char('/')) + name + extension;
}

namespace {

// Export duration varies substantially by format and by the installed
// browser/Pandoc runtime.  These are deliberately only estimates: the
// progress indicator is explanatory feedback, not a measurement of exporter
// work.  Once the estimate is exhausted the bar stays at 95% until the real
// task completes, so it never falsely reports completion.
int estimatedExportDurationMs(const QString& formatLabel)
{
    if (formatLabel == QStringLiteral("PDF"))
        return 3000;
    if (formatLabel == QStringLiteral("DOCX"))
        return 600;
    if (formatLabel == QStringLiteral("EPUB"))
        return 6000;
    if (formatLabel == QStringLiteral("HTML"))
        return 200;
    return 4000;
}

}

void MainWindow::runExportTask(const QString& formatLabel, const QString& filePath,
                               std::function<bool()> task, const QString& successNote)
{
    // Windows file names are case-insensitive. Use an absolute cleaned key so
    // Save As paths such as .\\report.pdf and report.pdf cannot race each other.
    const QString key = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath()).toCaseFolded();
    if (m_exportDestinations.contains(key)) {
        setExportStatus(tr("Export already running"), 4000);
        return;
    }

    m_exportDestinations.insert(key);
    ++m_runningExportCount;
    // Show feedback before scheduling the worker. A queued 500 ms activity
    // delay made the UI appear unresponsive while exports were already under
    // way, especially during their expensive setup phase.
    setExportStatus(tr("Exporting %1… 0%").arg(formatLabel));

    // The percentage is intentionally shown in the existing left-hand
    // status-bar slot, alongside the export text. It is simulated feedback:
    // advance linearly to 95%, then wait there for the real exporter.
    auto* progressTimer = new QTimer(this);
    QElapsedTimer progressClock;
    progressClock.start();
    const int expectedDuration = estimatedExportDurationMs(formatLabel);
    connect(progressTimer, &QTimer::timeout, this,
            [this, formatLabel, progressClock, expectedDuration]() {
        const int rawProgress = qMin(95,
            qRound(progressClock.elapsed() * 95.0 / expectedDuration));
        const int simulatedProgress = (rawProgress / 5) * 5;
        setExportStatus(tr("Exporting %1… %2%")
                            .arg(formatLabel)
                            .arg(simulatedProgress));
    });
    progressTimer->start(100);

    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this,
            [this, watcher, progressTimer, key, formatLabel, filePath, successNote]() {
        const bool ok = watcher->result();
        progressTimer->stop();
        progressTimer->deleteLater();
        watcher->deleteLater();
        m_exportDestinations.remove(key);
        --m_runningExportCount;
        if (m_runningExportCount > 0) {
            setExportStatus(tr("Exporting %n file(s)…", nullptr, m_runningExportCount));
            return;
        }
        if (ok) {
            // 100% is deliberately written only after the worker has really
            // finished and immediately before the completion dialog appears.
            setExportStatus(tr("Exporting %1… 100%").arg(formatLabel));
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (m_showExportSuccessDialog) {
                // Keep the established completion affordance (Open / Open Folder)
                // while the work itself remains entirely non-modal and off-thread.
                const bool openInternally = formatLabel == QStringLiteral("PDF")
                    || formatLabel == QStringLiteral("HTML");
                ExportSuccessDialog dlg(filePath, formatLabel, this, successNote,
                    openInternally ? std::function<void(const QString&)>([this](const QString& path) {
                        openFilePath(path);
                    }) : std::function<void(const QString&)>());
                dlg.exec();
            }
            setExportStatus(tr("%1 exported successfully").arg(formatLabel), 4000);
            RatePrompt::afterSuccessfulExport(this);
        } else {
            setExportStatus(tr("%1 export failed").arg(formatLabel), 6000);
            themedWarning(this, tr("Export Failed"),
                          tr("%1 export failed. The destination may be locked, or a required browser, Pandoc, or image resource may be unavailable.")
                              .arg(formatLabel));
        }
    });
    // Off the global QThreadPool (shared with large-document preview
    // conversion and Workspace search) so a slow export subprocess can't
    // starve those latency-sensitive tasks of a worker slot. See
    // ExportThreadPool.h.
    watcher->setFuture(QtConcurrent::run(&exportThreadPool(), std::move(task)));
}

namespace {
// Markmap's autoFit centers the map in the viewport, so a fixed render canvas
// usually surrounds it with a wide empty border that Word carries into the
// document as part of the image and renders as a mostly-blank page. Crop back to
// the drawn content plus a uniform margin; the uncropped image survives any
// failure. One row-major pass — scanning column-by-column instead walks the
// whole image per column, which on a 3200x2000 render is millions of cache
// misses for the same bounding box.
void trimImageBorder(const QString& pngPath, int margin = 24)
{
    QImage image(pngPath);
    if (image.isNull()) return;
    image = image.convertToFormat(QImage::Format_ARGB32);

    const QRgb background = image.pixel(0, 0);
    int top = -1, bottom = -1, left = image.width(), right = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        int x = 0;
        while (x < image.width() && line[x] == background) ++x;
        if (x == image.width()) continue;
        int end = image.width() - 1;
        while (end > x && line[end] == background) --end;
        if (top < 0) top = y;
        bottom = y;
        left = qMin(left, x);
        right = qMax(right, end);
    }
    if (top < 0 || right <= left || bottom <= top) return; // Nothing was drawn.

    const QRect content = QRect(QPoint(left, top), QPoint(right, bottom))
        .adjusted(-margin, -margin, margin, margin)
        .intersected(image.rect());
    if (content == image.rect()) return;
    const QImage cropped = image.copy(content);
    if (!cropped.isNull())
        cropped.save(pngPath, "PNG");
}
}

void MainWindow::exportMindMapHtml(const QString& markdown)
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Mind Map HTML"), exportInitialPath(QStringLiteral(".html")),
        tr("HTML (*.html *.htm);;All Files (*)"));
    if (path.isEmpty()) return;
    m_lastExportFolder = QFileInfo(path).absolutePath();
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    runExportTask(QStringLiteral("HTML"), path, [markdown, path, dark]() {
        const QString html = ExportManager::buildMindMapHtml(markdown, dark);
        QSaveFile file(path);
        return !html.isEmpty() && file.open(QIODevice::WriteOnly)
            && file.write(html.toUtf8()) >= 0 && file.commit();
    });
}

void MainWindow::exportMindMapPdf(const QString& markdownSource)
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Mind Map PDF"), exportInitialPath(QStringLiteral(".pdf")), tr("PDF (*.pdf);;All Files (*)"));
    if (path.isEmpty()) return;
    m_lastExportFolder = QFileInfo(path).absolutePath();
    TabPage* pg = m_tabs->currentPage();
    const QString docPath = pg ? pg->model->filePath() : QString();
    // Re-rendered rather than printed from the preview's serialized SVG: Markmap
    // bakes each node's measured <foreignObject> size into the clone as a fixed
    // width, so any page that does not reproduce those exact metrics overflows
    // it -- and a foreignObject clips, which is what cut the quadratic formula
    // off mid-glyph.
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QString documentDir = docPath.isEmpty() ? QString() : QFileInfo(docPath).absolutePath();
    runExportTask(QStringLiteral("PDF"), path, [markdownSource, path, dark, documentDir]() {
        return ExportManager::exportPdfHtml(
            ExportManager::buildMindMapRenderHtml(markdownSource, dark, documentDir), path);
    });
}

void MainWindow::exportMindMapDocx(const QString& markdownSource)
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Mind Map DOCX"), exportInitialPath(QStringLiteral(".docx")),
        tr("Word Document (*.docx);;All Files (*)"));
    if (path.isEmpty()) return;
    m_lastExportFolder = QFileInfo(path).absolutePath();

    // Rendered to PNG instead of handing Pandoc the preview's SVG, because
    // Pandoc's SVG converter cannot render Markmap's foreignObject labels.
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    TabPage* pg = m_tabs->currentPage();
    const QString sourcePath = pg ? pg->model->filePath() : QString();
    const QString documentDir = sourcePath.isEmpty() ? QString()
                                                     : QFileInfo(sourcePath).absolutePath();
    runExportTask(QStringLiteral("DOCX"), path, [markdownSource, path, dark, documentDir]() {
        QTemporaryDir mapDir;
        if (!mapDir.isValid())
            return false;
        const QString pngPath = mapDir.filePath(QStringLiteral("mindmap.png"));
        if (!ExportManager::renderHtmlToPng(
                ExportManager::buildMindMapRenderHtml(markdownSource, dark, documentDir,
                                                       true /* transparentBackground */),
                pngPath, QSize(1600, 1000), 2, nullptr,
                true /* transparentBackground */)) {
            return false;
        }
        trimImageBorder(pngPath);
        const QString markdown = QStringLiteral("![](%1)\n").arg(QFileInfo(pngPath).fileName());
        return ExportManager::exportDocx(markdown, path, pngPath);
    });
}

// ---------------------------------------------------------------------------
void MainWindow::doExportHtml()
{
    if (m_activePreview && m_activePreview->isMindMap()) {
        if (m_activeEditor)
            exportMindMapHtml(m_activeEditor->toPlainText());
        return;
    }
    if (!m_activeEditor) return;
    const QString markdown = m_activeEditor->toPlainText();
    if (markdown.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export HTML"), exportInitialPath(QStringLiteral(".html")),
        tr("HTML (*.html *.htm);;All Files (*)"));
    if (path.isEmpty()) return;
    m_lastExportFolder = QFileInfo(path).absolutePath();
    // Regenerate from raw markdown via the KaTeX path — never the preview body.
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    runExportTask(QStringLiteral("HTML"), path, [markdown, path, dark, font = m_previewFontFamily,
                                                      spacing = m_documentSpacing]() {
        return ExportManager::exportHtml(markdown, path, dark, font, spacing);
    });
}

// ---------------------------------------------------------------------------
void MainWindow::doExportPdf()
{
    if (m_activePreview && m_activePreview->isMindMap()) {
        // Rendered fresh from the Markdown -- see exportMindMapPdf().
        if (m_activeEditor)
            exportMindMapPdf(m_activeEditor->toPlainText());
        return;
    }
    if (!m_activeEditor) return;
    const QString markdown = m_activeEditor->toPlainText();
    if (markdown.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export PDF"), exportInitialPath(QStringLiteral(".pdf")),
        tr("PDF (*.pdf);;All Files (*)"));
    if (path.isEmpty()) return;
    m_lastExportFolder = QFileInfo(path).absolutePath();

    TabPage* pg = m_tabs->currentPage();
    QString docPath = pg ? pg->model->filePath() : QString();

    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QString documentName = pg ? pg->model->displayName() : QString();
    ExportManager::PdfExportOptions options = m_pdfExportOptions;
    options.spacing = m_documentSpacing;
    runExportTask(QStringLiteral("PDF"), path, [markdown, path, docPath, options, dark, documentName]() {
        return ExportManager::exportPdf(markdown, path, docPath, options, dark, nullptr, nullptr,
                                        documentName);
    });
}

// ---------------------------------------------------------------------------
void MainWindow::doExportDocx()
{
    if (m_activePreview && m_activePreview->isMindMap()) {
        // Rendered fresh from the Markdown, so no round trip through the
        // preview's serialized SVG is needed -- see exportMindMapDocx().
        if (m_activeEditor)
            exportMindMapDocx(m_activeEditor->toPlainText());
        return;
    }
    if (!m_activeEditor) return;
    const QString markdown = m_activeEditor->toPlainText();
    if (markdown.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export DOCX"), exportInitialPath(QStringLiteral(".docx")),
        tr("Word Document (*.docx);;All Files (*)"));
    if (path.isEmpty()) return;
    m_lastExportFolder = QFileInfo(path).absolutePath();

    TabPage* pg = m_tabs->currentPage();
    QString docPath = pg ? pg->model->filePath() : QString();

    runExportTask(QStringLiteral("DOCX"), path, [markdown, path, docPath]() {
        return ExportManager::exportDocx(markdown, path, docPath);
    });
}

// ---------------------------------------------------------------------------
void MainWindow::doExportViaPandoc(const QString& extension, const QString& filter,
                                   const QString& dialogTitle, const QString& pandocFormat,
                                   const QString& formatLabel, const QStringList& extraArgs,
                                   const QString& successNote)
{
    if (!m_activeEditor) return;
    const QString markdown = m_activeEditor->toPlainText();
    if (markdown.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(
        this, dialogTitle, exportInitialPath(extension), filter);
    if (path.isEmpty()) return;
    m_lastExportFolder = QFileInfo(path).absolutePath();

    TabPage* pg = m_tabs->currentPage();
    QString docPath = pg ? pg->model->filePath() : QString();

    runExportTask(formatLabel, path, [markdown, path, docPath, pandocFormat, formatLabel, extraArgs]() {
        return ExportManager::exportViaPandoc(markdown, path, docPath, pandocFormat, formatLabel, extraArgs);
    }, successNote);
}

// ---------------------------------------------------------------------------
void MainWindow::doExportEpub()
{
    // Pandoc otherwise auto-generates a blank title page (no <title> metadata is
    // set) as the first spine entry, which readers show as an empty leading page.
    doExportViaPandoc(QStringLiteral(".epub"), tr("EPUB (*.epub);;All Files (*)"),
                      tr("Export EPUB"), QString(), QStringLiteral("EPUB"),
                      {QStringLiteral("--epub-title-page=false")});
}

// ---------------------------------------------------------------------------
void MainWindow::doExportPlainText()
{
    doExportViaPandoc(QStringLiteral(".txt"), tr("Plain Text (*.txt);;All Files (*)"),
                      tr("Export Plain Text"), QStringLiteral("plain"), QStringLiteral("TXT"));
}

// ---------------------------------------------------------------------------
void MainWindow::doExportRtf()
{
    // Pandoc's "rtf" writer emits a body-only RTF fragment (no {\rtf1...} file
    // header/font/color tables) unless --standalone is passed — without it the
    // output is not a valid, self-contained RTF document.
    doExportViaPandoc(QStringLiteral(".rtf"), tr("Rich Text Format (*.rtf);;All Files (*)"),
                      tr("Export Rich Text"), QString(), QStringLiteral("RTF"),
                      {QStringLiteral("--standalone")});
}

// ---------------------------------------------------------------------------
void MainWindow::doExportOdt()
{
    doExportViaPandoc(QStringLiteral(".odt"), tr("OpenDocument Text (*.odt);;All Files (*)"),
                      tr("Export OpenDocument"), QString(), QStringLiteral("ODT"));
}

// ---------------------------------------------------------------------------
void MainWindow::doExportLatex()
{
    // --standalone wraps the body in a full \documentclass{...}...\begin{document}
    // preamble (amsmath/amssymb, hyperref, etc.) -- without it Pandoc emits only
    // the body fragment, which cannot be compiled on its own. The mainfont
    // variable only takes effect when compiled with XeLaTeX/LuaLaTeX (the
    // \ifPDFTeX branch in Pandoc's template ignores it under pdfLaTeX); it is
    // required for Vietnamese and other non-ASCII text to render correctly,
    // since pdfLaTeX + Latin Modern cannot encode those glyphs at all.
    doExportViaPandoc(QStringLiteral(".tex"), tr("LaTeX (*.tex);;All Files (*)"),
                      tr("Export LaTeX"), QString(), QStringLiteral("LaTeX"),
                      {QStringLiteral("--standalone"),
                       QStringLiteral("-V"), QStringLiteral("mainfont=Segoe UI"),
                       QStringLiteral("-V"), QStringLiteral("monofont=Consolas"),
                       QStringLiteral("-V"), QStringLiteral("colorlinks=true"),
                       QStringLiteral("-V"), QStringLiteral("linkcolor=FastMDLink"),
                       QStringLiteral("-V"), QStringLiteral("urlcolor=FastMDLink"),
                       QStringLiteral("-V"), QStringLiteral("toccolor=FastMDLink")},
                      tr("Compile with XeLaTeX or LuaLaTeX (not pdfLaTeX) for the "
                         "FastMD sans-serif fonts and correct Unicode/Vietnamese text."));
}

// ---------------------------------------------------------------------------
void MainWindow::editPdfPageSetup()
{
    QPrinter printer(QPrinter::HighResolution);
    applyPdfExportOptions(printer, m_pdfExportOptions);

    QPageSetupDialog dlg(&printer, this);
    dlg.setWindowTitle(tr("Page Setup"));
    if (dlg.exec() != QDialog::Accepted)
        return;

    const HeaderFooterOptions headerFooter = m_pdfExportOptions.headerFooter;
    const QString fontFamily = m_pdfExportOptions.fontFamily;
    m_pdfExportOptions = pdfExportOptionsFromPrinter(printer, m_pdfExportOptions.fontSize);
    m_pdfExportOptions.headerFooter = headerFooter;
    m_pdfExportOptions.fontFamily = fontFamily;
    writeSettings();
}

// ---------------------------------------------------------------------------
void MainWindow::editHeaderFooter()
{
    TabPage* pg = m_tabs->currentPage();
    const bool markdown = pg && pg->model->editorMode() == EditorMode::Markdown;
    const QString text = m_activeEditor && pg && !pg->model->isViewer()
        ? m_activeEditor->toPlainText() : QString();
    HeaderFooterDialog dlg(m_pdfExportOptions, headerFooterContextFor(pg, text, markdown), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_pdfExportOptions.headerFooter = dlg.options();
    writeSettings();
}

// ---------------------------------------------------------------------------
void MainWindow::printDocument()
{
    if (!m_activeEditor) return;

    TabPage* pg = m_tabs->currentPage();
    if (pg && pg->model->editorMode() == EditorMode::Image) {
        if (pg->imageViewer)
            pg->imageViewer->print();
        return;
    }
    if (pg && (pg->model->editorMode() == EditorMode::Pdf || pg->model->editorMode() == EditorMode::Html
               || pg->model->editorMode() == EditorMode::Svg)) {
        pg->preview->printReadOnlyFile();
        return;
    }
    const bool plainTextMode = pg && pg->model->editorMode() == EditorMode::PlainText;
    const QString docTitle = pg ? pg->model->displayName() : QString();

    // 1. Generate the HTML. Markdown mode renders through the normal parser;
    // Plain Text mode wraps the raw, HTML-escaped editor text in a <pre> block
    // so it prints exactly as typed. Both share the same WebView2/browser
    // print pipeline below.
    QString fullHtml;
    if (plainTextMode) {
        const QString body = buildPlainTextPrintBody(m_activeEditor->toPlainText(), m_activeEditor->font());
        fullHtml = ExportManager::buildFullHtml(body, false, 16, true, false, docTitle, true);
    } else {
        const QString markdown = m_activeEditor->toPlainText();
        const QString body = ExportManager::markdownToHtml(markdown, MathRenderMode::KatexOutput);
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        fullHtml = ExportManager::buildPreviewHtml(body, dark, 16, true, docTitle, ExportManager::parseYamlFrontMatter(markdown),
                                                   m_pdfExportOptions.fontFamily, m_documentSpacing);
    }

    // buildFullHtml() falls back to a literal "FastMD" <title> when none is given,
    // which Chrome's native print header/footer overlay then renders verbatim in
    // its title slot. A regular space doesn't work here -- Chrome trims the
    // title before checking for emptiness, so a whitespace-only title is treated
    // as blank and it falls back to showing the temp file's name instead. A
    // zero-width space (U+200B) is not classified as whitespace by that trim,
    // so document.title stays non-empty (no URL/filename fallback) while
    // rendering no visible glyph. This document.title is only ever seen by
    // this ephemeral print HTML, never shown to the user otherwise.
    if (docTitle.trimmed().isEmpty()) {
        static const QRegularExpression titleTagRe(QStringLiteral("<title>[^<]*</title>"));
        fullHtml.replace(titleTagRe, QStringLiteral("<title>​</title>"));
    }

    // The print document itself lives under QDir::tempPath(), so make relative
    // Markdown resources resolve from the saved document's directory.
    const QString baseTag = documentBaseTag(pg ? pg->model->filePath() : QString());
    if (!baseTag.isEmpty())
        fullHtml.insert(fullHtml.lastIndexOf(QStringLiteral("</head>")), baseTag);

    // Apply the configured size and margins to every printed page. Must use
    // lastIndexOf, not indexOf: buildPreviewHtml/buildFullHtml embed offline
    // asset bundles (e.g. mermaid.min.js) inline in <head>, and those minified
    // bundles can themselves contain a literal "</head>" substring (e.g.
    // internal HTML-string-building helper code) well before the document's
    // real closing tag. indexOf() would match that spurious occurrence first
    // and inject the @page CSS inside a JS string, where it has no effect --
    // this is exactly why Page Setup margins were silently ignored on
    // Mermaid-containing documents like samples/promptAll-test.md. The real
    // closing tag is always the last occurrence, since every embedded asset
    // is substituted into the template before its own literal "</head>".
    const int headIdx = fullHtml.lastIndexOf(QStringLiteral("</head>"));
    const QString pageCss = ExportManager::buildPageCss(
        m_pdfExportOptions, headerFooterContextFor(pg, m_activeEditor->toPlainText(), !plainTextMode));
    if (headIdx != -1) {
        fullHtml.insert(headIdx, pageCss);
    } else {
        fullHtml.append(pageCss);
    }

    // 2. Wait for the standalone preview shell to finish asynchronous Mermaid,
    // KaTeX, and syntax rendering before opening the browser print dialog.
    int bodyEndIdx = fullHtml.lastIndexOf(QStringLiteral("</body>"));
    QString printScript = QStringLiteral(
        "<script>(function(){function p(){window.print();}"
        "if(window.fastmdPreviewRendered)p();"
        "else window.addEventListener('fastmd-preview-rendered',p,{once:true});"
        "})();</script>\n");
    if (bodyEndIdx != -1) {
        fullHtml.insert(bodyEndIdx, printScript);
    } else {
        fullHtml.append(printScript);
    }

    // 3. Write to temporary file
    QString tempPath = QDir::tempPath() + QStringLiteral("/fastmd_print_preview.html");
    QFile file(tempPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << fullHtml;
        file.close();

        // 4. Open in the user's default browser
        QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
    }
}

// ---------------------------------------------------------------------------
void MainWindow::doPreviewBrowser()
{
    if (!m_activeEditor) return;
    // Regenerate from raw markdown via the KaTeX path so the browser renders real
    // LaTeX math, not the in-app Unicode fallback.
    const QString markdown = m_activeEditor->toPlainText();
    QString tempPath = QDir::tempPath() + QStringLiteral("/fastmd_preview.html");
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    if (ExportManager::exportHtml(markdown, tempPath, dark, m_previewFontFamily, m_documentSpacing)) {
        TabPage* pg = m_tabs->currentPage();
        const QString baseTag = documentBaseTag(pg ? pg->model->filePath() : QString());
        if (!baseTag.isEmpty()) {
            QFile file(tempPath);
            if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
                QString fullHtml = QString::fromUtf8(file.readAll());
                fullHtml.insert(fullHtml.lastIndexOf(QStringLiteral("</head>")), baseTag);
                file.resize(0);
                file.seek(0);
                QTextStream out(&file);
                out.setEncoding(QStringConverter::Utf8);
                out << fullHtml;
            }
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
    }
}
