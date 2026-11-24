// Produces the fixtures tests/pdf/headerfootercases.py checks:
//   <out>/<case>.pdf        -- ExportManager::exportPdf() (WebView2 PrintToPdf,
//                              headless-browser fallback), one per toggle case
//   <out>/print_<case>.html -- the print document File > Print hands to the
//                              browser, for a header/footer-enabled browser run
// Usage: PdfHeaderFooterTest <out-dir>

#include "ExportManager.h"
#include "HeaderFooter.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

struct Case {
    const char* name;
    HeaderFooterOptions options;
    QMarginsF marginsMm = QMarginsF(15.0, 18.0, 15.0, 18.0);
    bool untitled = false;
};

HeaderFooterOptions make(bool headerOn, QStringList header, bool footerOn, QStringList footer)
{
    HeaderFooterOptions o;
    o.headerEnabled = headerOn;
    o.footerEnabled = footerOn;
    for (int i = 0; i < HeaderFooterOptions::SlotCount; ++i) {
        o.header[i] = header.value(i);
        o.footer[i] = footer.value(i);
    }
    return o;
}

QString sampleMarkdown()
{
    QString md = QStringLiteral("---\ntitle: Header Footer Fixture\n---\n\n# Header & Footer fixture\n\n");
    for (int i = 1; i <= 34; ++i) {
        md += QStringLiteral("## Section %1\n\nLorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
                             "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
                             "veniam, quis nostrud exercitation ullamco laboris.\n\n").arg(i);
        if (i % 9 == 0)
            md += QStringLiteral("$$\\int_0^1 x^2\\,dx = \\frac{1}{3}$$\n\n");
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
        qCritical("usage: PdfHeaderFooterTest <out-dir>");
        return 2;
    }
    const QDir out(QString::fromLocal8Bit(argv[1]));
    QDir().mkpath(out.absolutePath());

    const QString markdown = sampleMarkdown();
    const QString docPath = out.filePath(QStringLiteral("hf-sample.md"));
    if (!writeText(docPath, markdown)) {
        qCritical("could not write the sample document");
        return 1;
    }

    const QString page = QStringLiteral("Page {page} of {pages}");
    const QList<Case> cases = {
        { "off", make(false, { QStringLiteral("{file}") }, false, { QString(), page }) },
        { "header", make(true, { QStringLiteral("{file}"), QStringLiteral("HDR-MID"), QStringLiteral("{title}") },
                         false, { QString(), page }) },
        { "footer", make(false, { QStringLiteral("{file}") }, true,
                         { QStringLiteral("FTR-LEFT"), page, QStringLiteral("P{page}/{pages}") }) },
        { "both", make(true, { QStringLiteral("{file}"), QString(), QStringLiteral("{title}") },
                       true, { QString(), page, QString() }) },
        { "escape", make(true, { QString(), QStringLiteral("Say \"hi\" \\ </style> {{page}} Tiếng Việt 日本語"), QString() },
                         true, { QStringLiteral("%1 %2"), QString(), QStringLiteral("{page}/{pages}") }) },
        { "untitled", make(true, { QStringLiteral("{file}") }, true, { QString(), page }),
          QMarginsF(15.0, 18.0, 15.0, 18.0), true },
        { "narrow", make(true, { QStringLiteral("Long header text that keeps going across its third of the page "
                                                "and wraps onto several lines {file}"), QString(), QString() },
                         true, { QString(), page }), QMarginsF(15.0, 6.0, 15.0, 6.0) },
    };

    const QString body = ExportManager::markdownToHtml(markdown, MathRenderMode::KatexOutput);
    int failures = 0;
    for (const Case& c : cases) {
        ExportManager::PdfExportOptions options;
        options.marginsMm = c.marginsMm;
        options.headerFooter = c.options;
        const QString name = QString::fromLatin1(c.name);

        QString error;
        const QString pdf = out.filePath(name + QStringLiteral(".pdf"));
        const bool ok = c.untitled
            ? ExportManager::exportPdf(markdown, pdf, QString(), options, false, nullptr, &error,
                                       QStringLiteral("Untitled 7"))
            : ExportManager::exportPdf(markdown, pdf, docPath, options, false, nullptr, &error);
        if (!ok) {
            ++failures;
            qCritical().noquote() << "export failed for" << name << ':' << error;
        }

        // Mirrors MainWindow::printDocument(): print shell, then the page CSS
        // before the last </head>. The title is the browser's own header text.
        QString html = ExportManager::buildPreviewHtml(body, false, 16, true, QStringLiteral("BROWSERTITLE"));
        const HeaderFooterContext context = HeaderFooter::makeContext(
            QStringLiteral("hf-sample.md"), ExportManager::parseYamlFrontMatter(markdown).title);
        html.insert(html.lastIndexOf(QStringLiteral("</head>")), ExportManager::buildPageCss(options, context));
        if (!writeText(out.filePath(QStringLiteral("print_") + name + QStringLiteral(".html")), html)) {
            ++failures;
            qCritical().noquote() << "could not write print HTML for" << name;
        }
    }
    return failures ? 1 : 0;
}
