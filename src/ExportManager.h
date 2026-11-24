#pragma once
#include <QString>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QSize>
#include <QVariant>

#include <functional>

#include "DocumentSpacing.h"
#include "HeaderFooter.h"
#include "MathRenderer.h"

class QWidget;

namespace FastMdDefaults {
inline constexpr int PreviewFontSize = 17;
inline constexpr int EditorFontSize = 16;
inline constexpr int MinimumPreviewFontSize = 6;
}

class ExportManager {
public:
    // The shared Markdown rendering preference.  This is deliberately resolved
    // by every renderer/export entry point instead of changing the document
    // source or relying on CSS whitespace handling.
    static bool renderSingleLineBreaksEnabled();
    struct PdfExportOptions {
        QPageSize pageSize = QPageSize(QPageSize::A4);
        QPageLayout::Orientation orientation = QPageLayout::Portrait;
        // Left/right kept tighter than top/bottom so pages use more of the
        // width (QMarginsF is left, top, right, bottom).
        QMarginsF marginsMm = QMarginsF(15.0, 18.0, 15.0, 18.0);
        int fontSize = 16;
        // Body font family for print and PDF (pdf/fontFamily); empty keeps the
        // preview stylesheet's own.
        QString fontFamily;
        // Document Spacing (document/* keys), shared with the Preview. The GUI
        // overwrites it from MainWindow's live value just before exporting or
        // printing; command-line export reads it from the settings.
        DocumentSpacing::Settings spacing;
        HeaderFooterOptions headerFooter;
    };

    // The persisted pdf/* Page Setup keys and their defaults. `value` is the
    // settings lookup: MainWindow passes its StartupSettings snapshot, command-
    // line export a read-only QSettings, so both resolve identical options.
    using SettingsLookup = std::function<QVariant(const QString& key, const QVariant& defaultValue)>;
    static PdfExportOptions pdfExportOptionsFromSettings(const SettingsLookup& value);

    static QString markdownToHtml(const QString& markdown,
                                  MathRenderMode mode = MathRenderMode::UnicodePreview,
                                  bool* mathDetected = nullptr,
                                  bool renderSingleLineBreaks = renderSingleLineBreaksEnabled());

    // Live-preview-only variant: same math/task-list/image-src post-processing as
    // markdownToHtml(), but block-level tags carry data-src-start/data-src-end
    // (1-based source line) attributes for Preview double-click source
    // highlighting. Not used by Export -- keeps that output byte-for-byte unchanged.
    static QString markdownToPreviewHtml(const QString& markdown, bool* mathDetected = nullptr,
                                         bool renderSingleLineBreaks = renderSingleLineBreaksEnabled());

    struct DocumentMetadata {
        QString title;
        QString author;
        QString description;
        QString keywords;

        bool isEmpty() const {
            return title.isEmpty() && author.isEmpty() && description.isEmpty() && keywords.isEmpty();
        }
    };

    // Location of a leading YAML front matter block. `lineCount` is what callers
    // that map HTML back to editor lines must add back after stripping (see
    // markdownToPreviewHtml); absent front matter leaves every field at 0.
    struct FrontMatter {
        QString block;      // YAML text between the fences, fences excluded
        int bodyOffset = 0; // index in the markdown where the document body starts
        int lineCount = 0;  // source lines the block occupies, fences included
    };

    static FrontMatter findYamlFrontMatter(const QString& markdown);
    static DocumentMetadata parseYamlFrontMatter(const QString& markdown);
    static QString stripYamlFrontMatter(const QString& markdown);

    static bool hasLabelledCodeBlock(const QString& bodyHtml);

    static QString buildFullHtml(const QString& bodyHtml, bool dark,
                                 int fontSize = FastMdDefaults::PreviewFontSize,
                                 bool isPrint = false, bool injectKatex = false,
                                 const QString& title = QString(),
                                 bool preserveHeadingSemantics = false,
                                 const DocumentMetadata& meta = DocumentMetadata());

    static QString buildPreviewHtml(const QString& bodyHtml, bool dark,
                                    int fontSize = FastMdDefaults::PreviewFontSize,
                                    bool isPrint = false,
                                    const QString& title = QString(),
                                    const DocumentMetadata& meta = DocumentMetadata(),
                                    const QString& bodyFontFamily = QString(),
                                    const DocumentSpacing::Settings& spacing = DocumentSpacing::Settings());

    // Lightweight interactive Markmap document. FastMD's HTML viewer loads the
    // runtime from its locally bundled preview assets; a normal browser falls
    // back to the same CDN-backed runtime pattern as Markmap's REPL export.
    static QString buildMindMapHtml(const QString& markdown, bool dark = false,
                                    const QString& title = QString());
    // Mind map built for rasterization: bundled runtime by file:// URL, no
    // toolbar, and no enter animation, so a headless screenshot is deterministic.
    // `documentDir` anchors document-relative image paths; pass the source
    // document's folder when it has one.
    static QString buildMindMapRenderHtml(const QString& markdown, bool dark = false,
                                          const QString& documentDir = QString(),
                                          bool transparentBackground = false);

    // <base> for a rendering written to temp storage, so the document's
    // relative images and links resolve from `documentDir` instead of the temp
    // folder. Empty when there is no document folder (untitled).
    static QString documentBaseTag(const QString& documentDir);

    // CSS @page rule (page size + margins, plus header/footer margin boxes)
    // honoring the shared Page Setup options -- used by both PDF export and
    // interactive printing (window.print()), so the two stay in sync.
    static QString buildPageCss(const PdfExportOptions& options,
                                const HeaderFooterContext& context = HeaderFooterContext());

    static bool exportHtml(const QString& markdown, const QString& filePath, bool darkMode = false,
                           const QString& bodyFontFamily = QString(),
                           const DocumentSpacing::Settings& spacing = DocumentSpacing::Settings());
    static bool exportPdf(const QString& markdown, const QString& filePath, const QString& docPath,
                          const PdfExportOptions& options, bool darkMode, QWidget* parent = nullptr,
                          QString* error = nullptr, const QString& documentName = QString());
    // Browser-backed PDF export for already-rendered standalone HTML such as
    // the Mind Map SVG. The caller owns the document's HTML and styling.
    static bool exportPdfHtml(const QString& html, const QString& filePath, QWidget* parent = nullptr);
    // Rasterize standalone HTML to PNG via the same headless browser. Needed
    // wherever an SVG has to reach a Pandoc-written format: Pandoc delegates SVG
    // to rsvg-convert, which FastMD does not bundle and which cannot render the
    // <foreignObject> elements Markmap uses for every node label.
    // `transparentBackground` keeps transparent page pixels transparent in the
    // browser screenshot instead of flattening them to Chromium's canvas color.
    static bool renderHtmlToPng(const QString& html, const QString& pngPath,
                                const QSize& viewport, int scaleFactor = 2,
                                QWidget* parent = nullptr,
                                bool transparentBackground = false);
    // Launches the headless export browser once against a trivial page, purely
    // to pay first-launch cost (profile init, disk/shader/code cache) against
    // the persistent export profile before the user's first real PDF/PNG
    // export. Call from a background startup task; safe to call unconditionally
    // (a no-op failure if no browser is installed). See ExportManagerPdf.cpp.
    static void warmExportBrowser();
    static bool exportDocx(const QString& markdown, const QString& filePath, const QString& docPath, QWidget* parent = nullptr);
    // Generic Pandoc-based export for additional formats (EPUB, plain text, RTF,
    // ODT, LaTeX, ...). `pandocFormat` is passed as Pandoc's `-t` writer name
    // when non-empty; otherwise Pandoc infers the writer from filePath's extension.
    // `formatLabel` is used in error dialog titles (e.g. "EPUB").
    static bool exportViaPandoc(const QString& markdown, const QString& filePath, const QString& docPath,
                                const QString& pandocFormat, const QString& formatLabel,
                                const QStringList& extraArgs = {}, QWidget* parent = nullptr);
    static QString pandocExecutablePath();
    static QString katexAssetsDir();
    static QString odtReferenceDocPath();
    static QString docxReferenceDocPath();
};
