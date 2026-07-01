#pragma once
#include <QString>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>

class QWidget;

namespace FastMdDefaults {
inline constexpr int PreviewFontSize = 17;
inline constexpr int EditorFontSize = 16;
inline constexpr int MinimumPreviewFontSize = 6;
}

// Selects how math expressions are rendered by the Markdown pipeline.
//  - UnicodePreview: convert math to a lightweight Unicode/HTML fallback.
//    Used by the in-app QTextBrowser preview; fast and synchronous, no KaTeX.
//  - KatexOutput: preserve raw LaTeX math + delimiters so KaTeX auto-render can
//    typeset it in a real browser (Browser Preview, HTML export, PDF export).
enum class MathRenderMode {
    UnicodePreview,
    KatexOutput
};

class ExportManager {
public:
    struct PdfExportOptions {
        QPageSize pageSize = QPageSize(QPageSize::A4);
        QPageLayout::Orientation orientation = QPageLayout::Portrait;
        QMarginsF marginsMm = QMarginsF(20.0, 20.0, 20.0, 20.0);
        int fontSize = 16;
    };

    // Convert Markdown to an HTML body. When mode is KatexOutput, raw LaTeX math
    // (and its delimiters) is preserved instead of being converted to Unicode.
    // If mathDetected is non-null, it is set to true when math was found.
    static QString markdownToHtml(const QString& markdown,
                                  MathRenderMode mode = MathRenderMode::UnicodePreview,
                                  bool* mathDetected = nullptr);

    // Wrap a body in a full HTML document. When injectKatex is true, bundled
    // local KaTeX CSS/JS are referenced and auto-render is invoked on load.
    static QString buildFullHtml(const QString& bodyHtml, bool dark,
                                 int fontSize = FastMdDefaults::PreviewFontSize,
                                 bool isPrint = false, bool injectKatex = false,
                                 bool isPreview = false);

    // Export paths regenerate HTML from raw Markdown using KatexOutput so that
    // browser/export output never reuses the Unicode-converted preview body.
    static bool exportHtml(const QString& markdown, const QString& filePath, bool darkMode = false);
    static bool exportPdf(const QString& markdown, const QString& filePath, const QString& docPath, const PdfExportOptions& options, QWidget* parent = nullptr);

    // Export Markdown to DOCX via an external Pandoc process (never linked as a
    // library). Shows its own error dialogs via `parent` on failure.
    static bool exportDocx(const QString& markdown, const QString& filePath, const QString& docPath, QWidget* parent = nullptr);

    // Absolute path to the bundled Pandoc executable
    // (<app_dir>/tools/pandoc/pandoc.exe), or the system "pandoc" found on PATH.
    // Returns an empty string if neither is available.
    static QString pandocExecutablePath();

    // Absolute path to the bundled KaTeX asset directory (next to the executable),
    // or an empty string if the assets are missing.
    static QString katexAssetsDir();
};
