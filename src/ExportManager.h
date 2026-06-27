#pragma once
#include <QString>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>

class QWidget;

namespace FastMdDefaults {
inline constexpr int PreviewFontSize = 16;
inline constexpr int EditorFontSize = 15;
inline constexpr int MinimumPreviewFontSize = 6;
}

class ExportManager {
public:
    struct PdfExportOptions {
        QPageSize pageSize = QPageSize(QPageSize::A4);
        QPageLayout::Orientation orientation = QPageLayout::Portrait;
        QMarginsF marginsMm = QMarginsF(20.0, 20.0, 20.0, 20.0);
        int fontSize = 12;
    };

    static QString markdownToHtml(const QString& markdown);
    static QString buildFullHtml(const QString& bodyHtml, bool dark, int fontSize = FastMdDefaults::PreviewFontSize, bool isPrint = false);

    static bool exportHtml(const QString& bodyHtml, const QString& filePath, bool darkMode = false);
    static bool exportPdf(const QString& bodyHtml, const QString& filePath, const QString& docPath, const PdfExportOptions& options, QWidget* parent = nullptr);
};
