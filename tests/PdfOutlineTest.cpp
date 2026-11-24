// Produces the fixtures tests/pdf/outlinecases.py checks:
//   <out>/<variant>_<case>.pdf -- ExportManager::exportPdf() output, where
//   <variant> is the export path the test hooks force:
//     webview    -- the default WebView2 DevTools print (with outline)
//     printtopdf -- WebView2 PrintToPdf, the path used when the DevTools print
//                   is unavailable (no outline; the reference for unchanged
//                   page geometry and layout)
//     browser    -- the headless Chrome/Edge fallback
// Usage: PdfOutlineTest <out-dir>

#include "ExportManager.h"
#include "HeaderFooter.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>

namespace {

QString outlineMarkdown()
{
    return QStringLiteral(
        "---\ntitle: Outline Fixture\nauthor: Nguyễn Văn A\n---\n\n"
        "# Intro & overview\n\nShort paragraph.\n\n"
        "### Skipped level\n\nUnder the H1 without an H2.\n\n"
        "## Tiếng Việt: Định nghĩa và ví dụ\n\nNội dung.\n\n"
        "## Duplicate\n\nFirst.\n\n"
        "## Duplicate\n\nSecond.\n\n"
        "```bash\n# fenced, not a heading\n```\n\n"
        "    # indented code, not a heading\n\n"
        "<!--\n# commented out, not a heading\n-->\n\n"
        "Setext heading\n--------------\n\n"
        "## Special (chars) & 1 < 2 C:\\\\path \"quotes\"\n\n"
        "## Inline *emphasis* and `code`\n\n"
        "## A long heading that keeps going well past the width of any bookmarks pane, "
        "so a viewer has to truncate or wrap it somewhere along the way\n\n"
        "<div style=\"break-after: page;\"></div>\n\n"
        "# Second page\n\n"
        "#### Deep H4\n\n##### Deeper H5\n\n###### Deepest H6\n\n"
        "## Back to H2\n\n"
        "> ## Quoted heading\n\n"
        "> [!NOTE]\n> ### Callout heading\n\n"
        "- ## List heading\n\n"
        "## Area $\\pi r^2$\n\n"
        "[TOC]\n\nEnd.\n");
}

QString plainMarkdown()
{
    return QStringLiteral("Just a paragraph, no headings.\n\n```\n# code\n```\n");
}

QString longMarkdown()
{
    QString md = QStringLiteral("# Long document\n\n");
    for (int i = 1; i <= 30; ++i) {
        md += QStringLiteral("## Section %1\n\nLorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
                             "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
                             "veniam, quis nostrud exercitation ullamco laboris.\n\n").arg(i);
    }
    return md;
}

bool writeText(const QString& path, const QString& text)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(text.toUtf8()) >= 0;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        qCritical("usage: PdfOutlineTest <out-dir>");
        return 2;
    }
    const QDir out(QString::fromLocal8Bit(argv[1]));
    QDir().mkpath(out.absolutePath());

    struct Case {
        const char* name;
        QString markdown;
        ExportManager::PdfExportOptions options;
    };
    ExportManager::PdfExportOptions defaults;
    ExportManager::PdfExportOptions landscape;
    landscape.pageSize = QPageSize(QPageSize::A4);
    landscape.orientation = QPageLayout::Landscape;
    landscape.marginsMm = QMarginsF(12.0, 20.0, 25.0, 16.0);
    landscape.headerFooter.headerEnabled = true;
    landscape.headerFooter.footerEnabled = true;
    landscape.headerFooter.header[0] = QStringLiteral("{file}");
    landscape.headerFooter.footer[1] = QStringLiteral("Page {page} of {pages}");
    const QList<Case> cases = {
        { "outline", outlineMarkdown(), defaults },
        { "plain", plainMarkdown(), defaults },
        { "landscape", longMarkdown(), landscape },
    };

    struct Variant {
        const char* name;
        const char* hook;
    };
    const Variant variants[] = {
        { "webview", nullptr },
        { "printtopdf", "FASTMD_PDF_TEST_NO_DEVTOOLS_PRINT" },
        { "browser", "FASTMD_PDF_TEST_FORCE_BROWSER" },
    };

    int failures = 0;
    for (const Case& c : cases) {
        const QString name = QString::fromLatin1(c.name);
        const QString docPath = out.filePath(name + QStringLiteral(".md"));
        if (!writeText(docPath, c.markdown)) {
            qCritical().noquote() << "could not write" << docPath;
            return 1;
        }
        for (const Variant& v : variants) {
            if (v.hook)
                qputenv(v.hook, QByteArrayLiteral("1"));
            QString error;
            const QString pdf = out.filePath(QString::fromLatin1(v.name) + QLatin1Char('_') + name
                                             + QStringLiteral(".pdf"));
            if (!ExportManager::exportPdf(c.markdown, pdf, docPath, c.options, false, nullptr, &error)) {
                ++failures;
                qCritical().noquote() << "export failed for" << v.name << name << ':' << error;
            }
            if (v.hook)
                qunsetenv(v.hook);
        }
    }
    return failures ? 1 : 0;
}
