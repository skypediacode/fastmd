// Header/footer margin-box generation: token expansion, CSS escaping, every
// Header/Footer toggle combination, presets, and settings persistence. The
// rendered result is checked against real PDFs by tests/pdf/headerfootercases.py.

#include "ExportManager.h"
#include "HeaderFooter.h"
#include "StartupSettings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLocale>
#include <QVariantMap>

namespace {

int failures = 0;

void check(bool condition, const QString& message)
{
    if (!condition) {
        ++failures;
        qCritical().noquote() << "FAIL:" << message;
    }
}

void checkEqual(const QString& actual, const QString& expected, const QString& what)
{
    if (actual != expected) {
        ++failures;
        qCritical().noquote() << "FAIL:" << what << "\n  expected:" << expected << "\n  actual:  " << actual;
    }
}

HeaderFooterContext sampleContext()
{
    return HeaderFooter::makeContext(QStringLiteral("report.md"), QString(),
                                     QDateTime(QDate(2026, 10, 15), QTime(14, 30)));
}

HeaderFooterOptions options(bool headerOn, QStringList header, bool footerOn, QStringList footer)
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

const QString kEmptyTop = QStringLiteral("@top-center{content:\"\";}");
const QString kEmptyBottom = QStringLiteral("@bottom-center{content:\"\";}");

void testTokens()
{
    const HeaderFooterContext ctx = sampleContext();
    const QLocale locale = QLocale::system();
    const QString date = locale.toString(QDate(2026, 10, 15), QLocale::ShortFormat);
    const QString time = locale.toString(QTime(14, 30), QLocale::ShortFormat);

    checkEqual(HeaderFooter::expand(QStringLiteral("{file} | {title} | {date} {time} | {page}/{pages}"), ctx, 2, 7),
               QStringLiteral("report.md | report | %1 %2 | 2/7").arg(date, time), QStringLiteral("all tokens"));
    checkEqual(HeaderFooter::expand(QStringLiteral("{PAGE} of {Pages}"), ctx, 1, 3),
               QStringLiteral("1 of 3"), QStringLiteral("tokens are case-insensitive"));
    checkEqual(HeaderFooter::expand(QStringLiteral("{{page}} {{ }}"), ctx, 1, 3),
               QStringLiteral("{page} { }"), QStringLiteral("doubled braces are literal"));
    checkEqual(HeaderFooter::expand(QStringLiteral("{foo} {page {x{page}} a}b"), ctx, 4, 9),
               QStringLiteral("{foo} {page {x4} a}b"), QStringLiteral("unknown and unterminated tokens stay literal"));
    checkEqual(HeaderFooter::expand(QString(), ctx, 1, 1), QString(), QStringLiteral("empty template"));

    checkEqual(HeaderFooter::makeContext(QStringLiteral("notes.md"), QStringLiteral("  Quarterly Report ")).title,
               QStringLiteral("Quarterly Report"), QStringLiteral("front-matter title wins"));
    checkEqual(HeaderFooter::makeContext(QStringLiteral("notes.final.md"), QString()).title,
               QStringLiteral("notes.final"), QStringLiteral("title falls back to the base name"));
    checkEqual(HeaderFooter::makeContext(QStringLiteral("Untitled 3"), QString()).title,
               QStringLiteral("Untitled 3"), QStringLiteral("untitled tab name is its own title"));
}

void testCssContent()
{
    const HeaderFooterContext ctx = sampleContext();
    checkEqual(HeaderFooter::cssContent(QStringLiteral("Page {page} of {pages}"), ctx),
               QStringLiteral("\"Page \" counter(page) \" of \" counter(pages)"), QStringLiteral("page counters"));
    checkEqual(HeaderFooter::cssContent(QStringLiteral("{page}"), ctx),
               QStringLiteral("counter(page)"), QStringLiteral("a bare counter"));
    checkEqual(HeaderFooter::cssContent(QStringLiteral("{page}/{pages}"), ctx),
               QStringLiteral("counter(page) \"/\" counter(pages)"), QStringLiteral("adjacent counters"));
    checkEqual(HeaderFooter::cssContent(QString(), ctx), QStringLiteral("\"\""), QStringLiteral("empty content"));
    checkEqual(HeaderFooter::cssContent(QStringLiteral("{file} - {title}"), ctx),
               QStringLiteral("\"report.md - report\""), QStringLiteral("text tokens are baked in"));
    checkEqual(HeaderFooter::cssContent(QStringLiteral("{{page}}"), ctx),
               QStringLiteral("\"{page}\""), QStringLiteral("escaped braces never become a counter"));

    checkEqual(HeaderFooter::cssContent(QStringLiteral("Say \"hi\" \\ </style><script>"), ctx),
               QStringLiteral("\"Say \\22 hi\\22  \\\\ \\3C /style>\\3C script>\""),
               QStringLiteral("quotes, backslashes and < are escaped"));
    checkEqual(HeaderFooter::cssContent(QStringLiteral("a\nb\tc\rd") + QChar(0x2028) + QStringLiteral("e"), ctx),
               QStringLiteral("\"a b c d e\""), QStringLiteral("control characters become spaces"));
    checkEqual(HeaderFooter::cssContent(QStringLiteral("Tiếng Việt 日本語 ✓"), ctx),
               QStringLiteral("\"Tiếng Việt 日本語 ✓\""), QStringLiteral("Unicode passes through"));

    // A file name is user data too.
    const HeaderFooterContext hostile = HeaderFooter::makeContext(QStringLiteral("a\"b</style>.md"), QString());
    checkEqual(HeaderFooter::cssContent(QStringLiteral("{file}"), hostile),
               QStringLiteral("\"a\\22 b\\3C /style>.md\""), QStringLiteral("file names are escaped"));
}

void testToggleCombinations()
{
    const HeaderFooterContext ctx = sampleContext();
    const QStringList header { QStringLiteral("{file}"), QStringLiteral("Mid"), QStringLiteral("{date}") };
    const QStringList footer { QStringLiteral("L"), QStringLiteral("Page {page} of {pages}"), QStringLiteral("R") };

    // Off/off: nothing at all, whatever the slots hold.
    checkEqual(HeaderFooter::marginBoxCss(options(false, header, false, footer), ctx), QString(),
               QStringLiteral("both bands off"));

    const QString headerOnly = HeaderFooter::marginBoxCss(options(true, header, false, footer), ctx);
    check(headerOnly.startsWith(QStringLiteral("@top-left{content:\"report.md\";width:33.3%;text-align:left;vertical-align:middle;")),
          QStringLiteral("header-only: top-left box first, left-aligned, centred in the margin: ") + headerOnly);
    check(headerOnly.contains(QStringLiteral("@top-center{content:\"Mid\";width:33.3%;text-align:center;")),
          QStringLiteral("header-only: top-center box"));
    check(headerOnly.contains(QStringLiteral("@top-right{content:\"")) && headerOnly.contains(QStringLiteral("text-align:right")),
          QStringLiteral("header-only: top-right box"));
    check(headerOnly.endsWith(kEmptyBottom), QStringLiteral("header-only: bottom band claimed by an empty box"));
    check(!headerOnly.contains(QStringLiteral("@bottom-left")) && !headerOnly.contains(QStringLiteral("counter(")),
          QStringLiteral("header-only: disabled footer text never emitted"));
    check(!headerOnly.contains(kEmptyTop), QStringLiteral("header-only: no empty top box"));

    const QString footerOnly = HeaderFooter::marginBoxCss(options(false, header, true, footer), ctx);
    check(footerOnly.startsWith(kEmptyTop), QStringLiteral("footer-only: top band claimed by an empty box"));
    check(footerOnly.contains(QStringLiteral("@bottom-left{content:\"L\";width:33.3%;text-align:left;vertical-align:middle;")),
          QStringLiteral("footer-only: bottom-left box centred in the margin"));
    check(footerOnly.contains(QStringLiteral("@bottom-center{content:\"Page \" counter(page) \" of \" counter(pages);")),
          QStringLiteral("footer-only: page counters"));
    check(footerOnly.contains(QStringLiteral("@bottom-right{content:\"R\";")), QStringLiteral("footer-only: bottom-right"));
    check(!footerOnly.contains(QStringLiteral("report.md")) && !footerOnly.contains(QStringLiteral("Mid")),
          QStringLiteral("footer-only: disabled header text never emitted"));

    const QString both = HeaderFooter::marginBoxCss(options(true, header, true, footer), ctx);
    check(!both.contains(kEmptyTop) && !both.contains(kEmptyBottom), QStringLiteral("both: no suppressor boxes"));
    check(both.count(QStringLiteral("@top-")) == 3 && both.count(QStringLiteral("@bottom-")) == 3,
          QStringLiteral("both: six boxes"));
    check(both.indexOf(QStringLiteral("@top-")) < both.indexOf(QStringLiteral("@bottom-")),
          QStringLiteral("both: header boxes before footer boxes"));

    // An enabled band whose slots are blank still claims its band.
    const QString blank = HeaderFooter::marginBoxCss(
        options(true, { QStringLiteral("  "), QString(), QString() }, true, { QString(), QStringLiteral("{page}"), QString() }), ctx);
    check(blank.startsWith(kEmptyTop) && blank.contains(QStringLiteral("@bottom-center{content:counter(page);")),
          QStringLiteral("blank enabled header still suppresses the browser header: ") + blank);
    checkEqual(HeaderFooter::marginBoxCss(options(true, {}, true, {}), ctx), kEmptyTop + kEmptyBottom,
               QStringLiteral("both enabled and empty claims both bands"));

    // Only the populated slots of a band are emitted.
    const QString rightOnly = HeaderFooter::marginBoxCss(
        options(false, {}, true, { QString(), QString(), QStringLiteral("{page}") }), ctx);
    checkEqual(rightOnly, kEmptyTop + QStringLiteral("@bottom-right{content:counter(page);width:33.3%;text-align:right;"
                                                     "vertical-align:middle;font:9pt/1.25 'Segoe UI',sans-serif;color:#57606a;}"),
               QStringLiteral("single right slot"));
}

void testPageCss()
{
    ExportManager::PdfExportOptions page;
    const QString golden = QStringLiteral(
        "<style>@page{size:210.00mm 297.00mm;margin:18.00mm 15.00mm 18.00mm 15.00mm;}</style>");
    checkEqual(ExportManager::buildPageCss(page), golden, QStringLiteral("default page CSS is unchanged"));
    checkEqual(ExportManager::buildPageCss(page, sampleContext()), golden,
               QStringLiteral("disabled header/footer leaves the page CSS byte-identical"));

    page.headerFooter = options(false, { QStringLiteral("x") }, false, { QStringLiteral("{page}") });
    checkEqual(ExportManager::buildPageCss(page, sampleContext()), golden,
               QStringLiteral("disabled bands with text still leave the page CSS byte-identical"));

    page.pageSize = QPageSize(QPageSize::Letter);
    page.orientation = QPageLayout::Landscape;
    page.marginsMm = QMarginsF(5, 6, 7, 8);
    page.headerFooter = options(true, { QStringLiteral("%1 %2 </style>") }, true, { QString(), QStringLiteral("{page}") });
    const QString css = ExportManager::buildPageCss(page, sampleContext());
    check(css.startsWith(QStringLiteral("<style>@page{size:279.40mm 215.90mm;margin:6.00mm 7.00mm 8.00mm 5.00mm;@top-left{")),
          QStringLiteral("landscape geometry precedes the margin boxes: ") + css);
    check(css.endsWith(QStringLiteral("}}</style>")), QStringLiteral("margin boxes are closed inside @page"));
    check(css.count(QStringLiteral("</style>")) == 1, QStringLiteral("slot text cannot close the <style> element"));
    check(css.contains(QStringLiteral("\"%1 %2 \\3C /style>\"")), QStringLiteral("%n in slot text is not substituted"));
    check(css.count(u'{') == css.count(u'}'), QStringLiteral("braces balance"));
}

void testPresets()
{
    const QList<HeaderFooterPreset> presets = HeaderFooter::presets();
    check(presets.size() == 5, QStringLiteral("five presets"));
    for (int i = 0; i < presets.size(); ++i)
        check(HeaderFooter::presetIndex(presets.at(i).options) == i, QStringLiteral("preset %1 round-trips").arg(i));
    check(HeaderFooter::presetIndex(HeaderFooterOptions()) == 0, QStringLiteral("defaults are the None preset"));
    HeaderFooterOptions custom = presets.at(1).options;
    custom.footer[HeaderFooterOptions::Left] = QStringLiteral("x");
    check(HeaderFooter::presetIndex(custom) == -1, QStringLiteral("an edited preset is Custom"));
    check(!presets.at(0).options.isActive(), QStringLiteral("None is inactive"));
    check(presets.at(1).options.footerEnabled && !presets.at(1).options.headerEnabled,
          QStringLiteral("Page number is footer-only"));
}

void testSettings()
{
    QVariantMap store;
    const auto lookup = [&store](const QString& key, const QVariant& fallback) {
        return store.value(key, fallback);
    };

    const HeaderFooterOptions defaults = HeaderFooter::load(lookup);
    check(!defaults.headerEnabled && !defaults.footerEnabled, QStringLiteral("disabled by default"));
    check(defaults == HeaderFooterOptions(), QStringLiteral("empty store loads defaults"));
    check(!ExportManager::pdfExportOptionsFromSettings(lookup).headerFooter.isActive(),
          QStringLiteral("export options default to no header/footer"));

    const HeaderFooterOptions saved = options(true, { QStringLiteral("{file}"), QString(), QStringLiteral("Tiếng Việt") },
                                              false, { QString(), QStringLiteral("Page {page} of {pages}"), QString() });
    HeaderFooter::save(saved, [&store](const QString& key, const QVariant& value) { store.insert(key, value); });
    check(HeaderFooter::load(lookup) == saved, QStringLiteral("save/load round-trip"));
    check(ExportManager::pdfExportOptionsFromSettings(lookup).headerFooter == saved,
          QStringLiteral("Print, PDF export and CLI resolve the saved header/footer"));

    const QStringList keys = HeaderFooter::settingsKeys();
    check(keys.size() == 8, QStringLiteral("eight keys"));
    for (const QString& key : keys) {
        check(store.contains(key), QStringLiteral("save writes ") + key);
        check(StartupSettings::preloads(key), QStringLiteral("startup snapshot preloads ") + key);
    }

    store.insert(QStringLiteral("pdf/headerCenter"), QString(500, u'x'));
    check(HeaderFooter::load(lookup).header[HeaderFooterOptions::Center].size() == HeaderFooter::MaxSlotLength,
          QStringLiteral("hand-edited settings are capped at the slot limit"));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    testTokens();
    testCssContent();
    testToggleCombinations();
    testPageCss();
    testPresets();
    testSettings();
    if (failures) {
        qCritical().noquote() << failures << "header/footer check(s) failed";
        return 1;
    }
    qInfo().noquote() << "HeaderFooterTest: all checks passed";
    return 0;
}
