// Document Spacing: the three presets and their validation, the CSS each one
// emits, that a default emits nothing (default export HTML is unchanged), that
// every export entry point reads the same settings, and that preview.css keeps
// its defaults in the variable fallbacks while leaving headings, code blocks
// and `--space` alone.

#include "DocumentSpacing.h"
#include "ExportManager.h"
#include "StartupSettings.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
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

QString previewCss()
{
    QFile css(QStringLiteral(FASTMD_PREVIEW_CSS));
    check(css.open(QIODevice::ReadOnly), QStringLiteral("open %1").arg(css.fileName()));
    return QString::fromUtf8(css.readAll());
}

void testPresets()
{
    using namespace DocumentSpacing;
    const QList<int> line(LinePresets.begin(), LinePresets.end());
    const QList<int> paragraph(ParagraphPresets.begin(), ParagraphPresets.end());
    const QList<int> list(ListPresets.begin(), ListPresets.end());
    check(line == QList<int>({ 100, 115, 125, 140, 150, 175, 200 }), "line presets 1.0 .. 2.0");
    check(paragraph == QList<int>({ 0, 3, 6, 9, 12, 15, 18 }), "paragraph presets 0 .. 18 pt");
    check(list == QList<int>({ 0, 10, 25, 40, 60 }), "list presets 0 .. 0.6 em");
    check(isPreset(LinePresets, DefaultLine) && isPreset(ParagraphPresets, DefaultParagraph)
              && isPreset(ListPresets, DefaultList),
          "every default is a preset");
    check(DefaultLine == 150 && DefaultParagraph == 12 && DefaultList == 25, "defaults 1.5, 12 pt, 0.25 em");
    check(Settings().isDefault(), "a default Settings is default");
}

void testValidation()
{
    using namespace DocumentSpacing;
    check(normalizedLine(130) == DefaultLine && normalizedLine(-1) == DefaultLine && normalizedLine(155) == DefaultLine,
          "a non-preset line value is the default (1.55 is no longer a preset)");
    check(normalizedLine(115) == 115 && normalizedLine(100) == 100 && normalizedLine(200) == 200,
          "preset line values are kept");
    check(normalizedParagraph(13) == DefaultParagraph && normalizedParagraph(24) == DefaultParagraph
              && normalizedParagraph(0) == 0,
          "paragraph 0 is a preset, 13 and 24 are not");
    check(normalizedList(5) == DefaultList && normalizedList(0) == 0 && normalizedList(60) == 60,
          "list 0 is a preset, 5 is not");

    const auto lookup = [](const QVariantMap& map) {
        return [map](QLatin1StringView key) { return map.value(QString(key)); };
    };
    check(load(lookup({})).isDefault(), "missing keys (existing installs) are the default");
    const Settings read = load(lookup({ { QString(LineKey), QStringLiteral("125") },
                                        { QString(ParagraphKey), 0 },
                                        { QString(ListKey), QStringLiteral("0.4") } }));
    check(read.line == 125, "a string value as written by QSettings is read");
    check(read.paragraph == 0, "zero paragraph spacing is read, not mistaken for missing");
    check(read.list == DefaultList, "a non-integer string is the default");
    check(load(lookup({ { QString(LineKey), QStringLiteral("abc") } })).isDefault(), "garbage is the default");
    check(load(lookup({ { QString(LineKey), QStringLiteral("1; } body { color: red") } })).isDefault(),
          "a CSS-looking value is the default");
}

void testCss()
{
    using namespace DocumentSpacing;
    checkEqual(lineValue({}), QString(), "default line emits nothing");
    checkEqual(paragraphValue({}), QString(), "default paragraph emits nothing");
    checkEqual(listValue({}), QString(), "default list emits nothing");
    checkEqual(rootCss({}), QString(), "default settings emit no CSS");

    checkEqual(lineValue({ 100, 12, 25 }), QStringLiteral("1.0"), "1.0");
    checkEqual(lineValue({ 115, 12, 25 }), QStringLiteral("1.15"), "1.15");
    checkEqual(lineValue({ 125, 12, 25 }), QStringLiteral("1.25"), "1.25");
    checkEqual(lineValue({ 140, 12, 25 }), QStringLiteral("1.4"), "1.4");
    checkEqual(lineValue({ 175, 12, 25 }), QStringLiteral("1.75"), "1.75");
    checkEqual(lineValue({ 200, 12, 25 }), QStringLiteral("2.0"), "2.0");
    checkEqual(paragraphValue({ 150, 0, 25 }), QStringLiteral("0pt"), "0pt");
    checkEqual(paragraphValue({ 150, 18, 25 }), QStringLiteral("18pt"), "18pt");
    checkEqual(listValue({ 150, 12, 0 }), QStringLiteral("0.0em"), "0em");
    checkEqual(listValue({ 150, 12, 10 }), QStringLiteral("0.1em"), "0.1em");
    checkEqual(listValue({ 150, 12, 40 }), QStringLiteral("0.4em"), "0.4em");
    checkEqual(listValue({ 150, 12, 60 }), QStringLiteral("0.6em"), "0.6em");

    checkEqual(rootCss({ 100, 0, 0 }), QStringLiteral(":root{--lh:1.0;--pspace:0pt;--li-gap:0.0em;}"),
               "all three at their minimum, zero spacing included");
    checkEqual(rootCss({ 175, 12, 25 }), QStringLiteral(":root{--lh:1.75;}"), "only the changed variable is emitted");
    checkEqual(rootCss({ 150, 6, 25 }), QStringLiteral(":root{--pspace:6pt;}"), "paragraph alone");
    checkEqual(rootCss({ 150, 12, 40 }), QStringLiteral(":root{--li-gap:0.4em;}"), "list alone");
    checkEqual(rootCss({ 999, -4, 7 }), QString(), "unknown values are normalized to the default first");
    check(!rootCss({ 100, 0, 0 }).contains(QStringLiteral("--space")), "--space is never set");
}

void testSettingsKeys()
{
    using namespace DocumentSpacing;
    check(StartupSettings::preloads(LineKey), "document/lineSpacing is in kStartupKeys");
    check(StartupSettings::preloads(ParagraphKey), "document/paragraphSpacing is in kStartupKeys");
    check(StartupSettings::preloads(ListKey), "document/listSpacing is in kStartupKeys");
    check(LineKey != ParagraphKey && ParagraphKey != ListKey && LineKey != ListKey, "three independent keys");

    const auto lookup = [](const QVariantMap& map) {
        return [map](const QString& key, const QVariant& defaultValue) {
            return map.contains(key) ? map.value(key) : defaultValue;
        };
    };
    check(ExportManager::pdfExportOptionsFromSettings(lookup({})).spacing.isDefault(),
          "command-line export of an existing install uses the default");
    const ExportManager::PdfExportOptions options = ExportManager::pdfExportOptionsFromSettings(lookup({
        { QStringLiteral("document/lineSpacing"), QStringLiteral("115") },
        { QStringLiteral("document/paragraphSpacing"), QStringLiteral("0") },
        { QStringLiteral("document/listSpacing"), QStringLiteral("10") },
    }));
    check(options.spacing == DocumentSpacing::Settings({ 115, 0, 10 }), "PDF options read all three document/* keys");
    check(ExportManager::pdfExportOptionsFromSettings(lookup({
              { QStringLiteral("document/lineSpacing"), QStringLiteral("133") } })).spacing.isDefault(),
          "an invalid stored value is the default for command-line export");
}

void testPreviewHtml()
{
    const QString body = QStringLiteral("<p>Hello</p><ul><li>a</li></ul>");
    const QString byDefault = ExportManager::buildPreviewHtml(body, false, 16, true, QStringLiteral("t"));
    checkEqual(ExportManager::buildPreviewHtml(body, false, 16, true, QStringLiteral("t"), {}, QString(),
                                               DocumentSpacing::Settings()),
               byDefault, "default spacing produces byte-identical HTML");
    checkEqual(ExportManager::buildPreviewHtml(body, false, 16, true, QStringLiteral("t"), {}, QString(),
                                               DocumentSpacing::Settings{ 777, 99, 3 }),
               byDefault, "invalid spacing produces byte-identical HTML");

    if (!byDefault.contains(QStringLiteral("data-fastmd-standalone"))) {
        qWarning().noquote() << "SKIP: preview assets not beside the test executable;"
                                " buildPreviewHtml fell back to buildFullHtml";
        return;
    }
    const DocumentSpacing::Settings spacing{ 115, 0, 0 };
    const QString rule = DocumentSpacing::rootCss(spacing);
    // The three export surfaces share one builder; print and screen differ only
    // by the isPrint flag, so check both.
    for (const bool print : { true, false }) {
        const QString plain = ExportManager::buildPreviewHtml(body, false, 16, print, QStringLiteral("t"));
        const QString custom = ExportManager::buildPreviewHtml(body, false, 16, print, QStringLiteral("t"), {},
                                                               QString(), spacing);
        check(custom.count(rule) == 1, QStringLiteral("the spacing rule appears once (print=%1)").arg(print));
        QString stripped = custom;
        stripped.remove(stripped.indexOf(rule), rule.size());
        checkEqual(stripped, plain, QStringLiteral("the spacing rule is the only difference (print=%1)").arg(print));
        const int ruleAt = custom.indexOf(rule);
        check(ruleAt > custom.indexOf(QStringLiteral(":root{--fs:16px;")), "the rule follows preview.css and the theme vars");
        check(ruleAt < custom.indexOf(QStringLiteral("</style>"), ruleAt), "the rule is inside <style>");
    }

    const ExportManager::DocumentMetadata meta;
    const QString withFont = ExportManager::buildPreviewHtml(body, false, 16, true, QStringLiteral("t"), meta,
                                                             QStringLiteral("Times New Roman"), spacing);
    check(withFont.contains(rule) && withFont.contains(QStringLiteral("Times New Roman")),
          "spacing and font family combine");
}

void testStylesheet()
{
    const QString css = previewCss();
    check(css.contains(QStringLiteral("--space: 16px;")), "--space keeps its value");
    check(css.contains(QStringLiteral("line-height: var(--lh, 1.5);")), "body line-height defaults to 1.5");
    check(css.contains(QStringLiteral("p { margin: 0 0 var(--pspace, var(--space)); }")), "paragraph gap");
    check(css.contains(QStringLiteral("li { margin: var(--li-gap, 0.25em) 0; }")), "tight list item gap");
    check(css.contains(QStringLiteral("li > p { margin-bottom: calc(var(--li-gap, 0.25em) * 2); }")),
          "loose list gap is twice the item gap");
    check(css.contains(QStringLiteral("li > ul, li > ol { margin: var(--li-gap, 0.25em) 0 calc(var(--li-gap, 0.25em) * 1.4); }")),
          "nested list gaps keep their 1 / 1.4 ratio");
    check(css.contains(QStringLiteral("line-height: min(var(--lh, 1.5), 1.5);")), "table line-height is capped at 1.5");
    check(!css.contains(QStringLiteral("line-height: 1.55")), "the old 1.55 default is gone");
    check(css.contains(QStringLiteral("padding: calc(0.15em * min(1, (var(--lh, 1.5) - 1) * 2)) 0.4em;"))
              && css.contains(QStringLiteral("padding: calc(0.1em * min(1, (var(--lh, 1.5) - 1) * 2)) 0.45em;")),
          "inline code and kbd padding is exactly their old value at 1.5 and above");

    const int headingsAt = css.indexOf(QStringLiteral("/* Headings"));
    const int paragraphsAt = css.indexOf(QStringLiteral("/* Paragraphs"));
    check(headingsAt >= 0 && paragraphsAt > headingsAt, "found the heading block");
    const QString headings = css.mid(headingsAt, paragraphsAt - headingsAt);
    for (const char* variable : { "--lh", "--pspace", "--li-gap" })
        check(!headings.contains(QLatin1StringView(variable)), QStringLiteral("headings never use %1").arg(QLatin1StringView(variable)));
    check(headings.contains(QStringLiteral("line-height: 1.25;")), "headings keep line-height 1.25");
    check(headings.contains(QStringLiteral("h1 { --mt: calc(var(--fs) * 2.545); --mb: calc(var(--fs) * 0.727);"))
              && headings.contains(QStringLiteral("h6 { --mt: calc(var(--fs) * 1.09); --mb: calc(var(--fs) * 0.364);")),
          "heading margin ratios are unchanged");
    check(css.contains(QStringLiteral("  overflow-x: auto;\n  line-height: 1.5;\n  font-size: 0.94em;"))
              || css.contains(QStringLiteral("  overflow-x: auto;\r\n  line-height: 1.5;\r\n  font-size: 0.94em;")),
          "code blocks keep line-height 1.5");
    check(css.count(QStringLiteral("var(--space)")) >= 10, "block cards still use --space");
}

}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    testPresets();
    testValidation();
    testCss();
    testSettingsKeys();
    testPreviewHtml();
    testStylesheet();
    if (failures) {
        qCritical().noquote() << failures << "failure(s)";
        return 1;
    }
    qInfo().noquote() << "DocumentSpacingTest: all checks passed";
    return 0;
}
