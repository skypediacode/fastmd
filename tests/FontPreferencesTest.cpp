// Font preferences: family validation and quoting, the editor QSS rule, the
// preview/print body stack, the Vietnamese fallback for fonts without
// Vietnamese letters, settings keys, and that a default (empty) family leaves
// every generated stylesheet and export page byte-identical.

#include "ExportManager.h"
#include "FontPreferences.h"
#include "StartupSettings.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QDebug>
#include <QFile>
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

void testNormalizedFamily()
{
    using FontPreferences::normalizedFamily;
    checkEqual(normalizedFamily(QString()), QString(), "null family is the default");
    checkEqual(normalizedFamily(QStringLiteral("   ")), QString(), "blank family is the default");
    checkEqual(normalizedFamily(QStringLiteral("  Times New Roman ")), QStringLiteral("Times New Roman"),
               "surrounding whitespace is trimmed, inner spaces kept");
    checkEqual(normalizedFamily(QStringLiteral("Be Vietnam Pro")), QStringLiteral("Be Vietnam Pro"), "plain name");
    checkEqual(normalizedFamily(QStringLiteral("MS ゴシック")), QStringLiteral("MS ゴシック"), "non-ASCII name");
    checkEqual(normalizedFamily(QStringLiteral("Segoe UI Semibold-Italic (Test) & Co.")),
               QStringLiteral("Segoe UI Semibold-Italic (Test) & Co."), "harmless punctuation");

    const QStringList rejected = {
        QStringLiteral("Arial\"; } body { color: red"),
        QStringLiteral("Arial\\22 "),
        QStringLiteral("Arial; color: red"),
        QStringLiteral("Arial{"),
        QStringLiteral("Arial}"),
        QStringLiteral("</style><script>alert(1)</script>"),
        QStringLiteral("Arial>"),
        QStringLiteral("Line\nBreak"),
        QStringLiteral("Tab\tName"),
        QStringLiteral("Line") + QChar(0x2028) + QStringLiteral("Sep"),
        QString(129, u'A'),
    };
    for (const QString& name : rejected)
        checkEqual(normalizedFamily(name), QString(), QStringLiteral("unsafe name rejected: %1").arg(name));
    checkEqual(normalizedFamily(QString(128, u'A')), QString(128, u'A'), "128 characters is allowed");
}

void testQuotingAndRules()
{
    checkEqual(FontPreferences::quotedFamily(QStringLiteral("Fira Code")), QStringLiteral("\"Fira Code\""),
               "family is double-quoted");
    checkEqual(FontPreferences::quotedFamily(QStringLiteral("Bad\"Name")), QString(), "unsafe family is not quoted");

    checkEqual(FontPreferences::editorFamilyOrDefault(QString()), QStringLiteral("Consolas"), "editor default");
    checkEqual(FontPreferences::editorFamilyOrDefault(QStringLiteral("Bad;Name")), QStringLiteral("Consolas"),
               "unsafe editor family falls back to Consolas");
    checkEqual(FontPreferences::editorFamilyOrDefault(QStringLiteral("Cascadia Mono")),
               QStringLiteral("Cascadia Mono"), "editor family is a single family");

    checkEqual(FontPreferences::editorStyleRule(QString()), QString(), "default editor adds no QSS");
    const QString rule = FontPreferences::editorStyleRule(QStringLiteral("JetBrains Mono"));
    checkEqual(rule, QStringLiteral("\nMarkdownEditor { font-family: \"JetBrains Mono\"; }\n"),
               "editor rule targets MarkdownEditor with exactly one family");
    check(!rule.contains(u','), "editor rule never names a second family");

    checkEqual(FontPreferences::bodyFontStack(QString()), QString(), "default body adds no stack");
    checkEqual(FontPreferences::bodyFontStack(QStringLiteral("Georgia")),
               QStringLiteral("\"Georgia\",") + FontPreferences::PreviewBodyStack,
               "chosen family leads, the existing stack follows");
    checkEqual(FontPreferences::bodyFontCss(QString()), QString(), "default body adds no CSS");
    checkEqual(FontPreferences::bodyFontCss(QStringLiteral("Noto Serif")),
               QStringLiteral("body{font-family:\"Noto Serif\",") + FontPreferences::PreviewBodyStack
                   + QStringLiteral(";}"),
               "body rule");
    checkEqual(FontPreferences::bodyFontCss(QStringLiteral("x</style>")), QString(),
               "a name that could close <style> adds nothing");
}

void testVietnameseFallback()
{
    const QString range = QStringLiteral("U+01A0-01A1,U+1EBF");
    const QString faces = FontPreferences::vietnameseFallbackFaces(range);
    checkEqual(FontPreferences::vietnameseFallbackFaces(QString()), QString(), "no range, no faces");
    check(faces.count(QStringLiteral("@font-face{")) == 4, "regular, bold, italic and bold italic faces");
    check(faces.count(QStringLiteral("font-family:\"FastMD Vietnamese\"")) == 4, "every face is the fallback family");
    check(faces.count(QStringLiteral("unicode-range:U+01A0-01A1,U+1EBF;")) == 4, "every face covers only the range");
    for (const char* local : { "local(\"Segoe UI\")", "local(\"Segoe UI Bold\")", "local(\"Segoe UI Italic\")",
                               "local(\"Segoe UI Bold Italic\")" })
        check(faces.contains(QLatin1StringView(local)), QStringLiteral("face %1").arg(QLatin1StringView(local)));
    check(!faces.contains(u'<') && !faces.contains(u'\n'), "faces are safe inside <style> and JSON");

    checkEqual(FontPreferences::bodyFontStack(QStringLiteral("Georgia"), true),
               QStringLiteral("\"FastMD Vietnamese\",\"Georgia\",") + FontPreferences::PreviewBodyStack,
               "the fallback face leads the stack");
    checkEqual(FontPreferences::bodyFontStack(QString(), true), QString(), "no fallback without a chosen family");
    checkEqual(FontPreferences::bodyFontCss(QStringLiteral("Georgia"), range),
               faces + QStringLiteral("body{font-family:\"FastMD Vietnamese\",\"Georgia\",")
                   + FontPreferences::PreviewBodyStack + QStringLiteral(";}"),
               "the faces precede the body rule");
    checkEqual(FontPreferences::bodyFontCss(QStringLiteral("Georgia"), QString()),
               FontPreferences::bodyFontCss(QStringLiteral("Georgia")), "an empty range adds nothing");
    checkEqual(FontPreferences::bodyFontCss(QString(), range), QString(), "default body adds no faces");

    using FontPreferences::missingVietnameseRange;
    checkEqual(missingVietnameseRange(QString()), QString(), "the default family needs no fallback");
    checkEqual(missingVietnameseRange(QStringLiteral("No Such Font Family 12345")), QString(),
               "a family that is not installed needs no fallback");
    // Fonts every Windows 10/11 install ships; skipped elsewhere.
    if (QFontDatabase::hasFamily(QStringLiteral("Georgia"))) {
        const QString georgia = missingVietnameseRange(QStringLiteral("Georgia"));
        check(georgia.contains(QStringLiteral("U+1EBF")) || georgia.contains(QStringLiteral("-1EBF"))
                  || georgia.contains(QStringLiteral("U+1EA0-")),
              QStringLiteral("Georgia lacks ế: %1").arg(georgia));
        check(georgia.startsWith(QStringLiteral("U+")) && !georgia.contains(QStringLiteral("00EA")),
              QStringLiteral("Georgia keeps the letters it has (ê): %1").arg(georgia));
        checkEqual(missingVietnameseRange(QStringLiteral(" Georgia ")), georgia, "the range is per family, cached");
    }
    for (const char* family : { "Times New Roman", "Segoe UI", "Arial", "Cambria" }) {
        const QString name = QString::fromLatin1(family);
        if (QFontDatabase::hasFamily(name))
            checkEqual(missingVietnameseRange(name), QString(), QStringLiteral("%1 has every Vietnamese letter").arg(name));
    }
}

void testStackMatchesPreviewCss()
{
    QFile css(QStringLiteral(FASTMD_PREVIEW_CSS));
    check(css.open(QIODevice::ReadOnly), QStringLiteral("open %1").arg(css.fileName()));
    const QString text = QString::fromUtf8(css.readAll());
    check(text.contains(QStringLiteral("font-family: ") + FontPreferences::PreviewBodyStack + u';'),
          "PreviewBodyStack matches preview.css's body font-family");

    const QString legacy = ExportManager::buildFullHtml(QStringLiteral("<p>x</p>"), false);
    check(legacy.contains(FontPreferences::PreviewBodyStack),
          "PreviewBodyStack matches ExportManager's legacy body font-family");
}

void testSettingsKeys()
{
    check(StartupSettings::preloads(FontPreferences::EditorKey), "editor/fontFamily is in kStartupKeys");
    check(StartupSettings::preloads(FontPreferences::PreviewKey), "preview/fontFamily is in kStartupKeys");
    check(StartupSettings::preloads(FontPreferences::PrintKey), "pdf/fontFamily is in kStartupKeys");
    check(FontPreferences::EditorKey != FontPreferences::PreviewKey
              && FontPreferences::PreviewKey != FontPreferences::PrintKey
              && FontPreferences::EditorKey != FontPreferences::PrintKey,
          "three independent keys");

    const auto lookup = [](const QVariantMap& map) {
        return [map](const QString& key, const QVariant& defaultValue) {
            return map.contains(key) ? map.value(key) : defaultValue;
        };
    };
    checkEqual(ExportManager::pdfExportOptionsFromSettings(lookup({})).fontFamily, QString(),
               "missing pdf/fontFamily is the default (existing installs)");
    const QVariantMap others{
        { QStringLiteral("editor/fontFamily"), QStringLiteral("Fira Code") },
        { QStringLiteral("preview/fontFamily"), QStringLiteral("Georgia") },
    };
    checkEqual(ExportManager::pdfExportOptionsFromSettings(lookup(others)).fontFamily, QString(),
               "Editor and Preview fonts do not leak into Print/PDF");
    checkEqual(ExportManager::pdfExportOptionsFromSettings(
                   lookup({ { QStringLiteral("pdf/fontFamily"), QStringLiteral(" Cambria ") } })).fontFamily,
               QStringLiteral("Cambria"), "pdf/fontFamily is read and normalized");
    checkEqual(ExportManager::pdfExportOptionsFromSettings(
                   lookup({ { QStringLiteral("pdf/fontFamily"), QStringLiteral("a;b") } })).fontFamily,
               QString(), "an unsafe stored pdf/fontFamily falls back to the default");
}

void testPreviewHtml()
{
    const QString body = QStringLiteral("<p>Tiếng Việt</p><pre><code>x</code></pre>");
    const QString byDefault = ExportManager::buildPreviewHtml(body, false, 16, true, QStringLiteral("t"));
    checkEqual(ExportManager::buildPreviewHtml(body, false, 16, true, QStringLiteral("t"), {}, QString()),
               byDefault, "an empty family produces byte-identical HTML");
    checkEqual(ExportManager::buildPreviewHtml(body, false, 16, true, QStringLiteral("t"), {},
                                               QStringLiteral("bad\"name")),
               byDefault, "an unsafe family produces byte-identical HTML");

    if (!byDefault.contains(QStringLiteral("data-fastmd-standalone"))) {
        qWarning().noquote() << "SKIP: preview assets not beside the test executable;"
                                " buildPreviewHtml fell back to buildFullHtml";
        return;
    }
    const QString rule = FontPreferences::bodyFontCss(QStringLiteral("Times New Roman"));
    const QString custom = ExportManager::buildPreviewHtml(body, false, 16, true, QStringLiteral("t"), {},
                                                           QStringLiteral("Times New Roman"));
    check(custom.count(rule) == 1, "the body rule appears once");
    QString stripped = custom;
    stripped.remove(stripped.indexOf(rule), rule.size());
    checkEqual(stripped, byDefault, "the body rule is the only difference");
    const int ruleAt = custom.indexOf(rule);
    check(ruleAt > custom.indexOf(QStringLiteral(":root{--fs:16px;")), "the body rule follows preview.css and the theme vars");
    check(ruleAt < custom.indexOf(QStringLiteral("</style>"), ruleAt), "the body rule is inside <style>");

    if (QFontDatabase::hasFamily(QStringLiteral("Georgia"))) {
        const QString georgia = ExportManager::buildPreviewHtml(body, false, 16, true, QStringLiteral("t"), {},
                                                                QStringLiteral("Georgia"));
        const QString guarded = FontPreferences::bodyFontCss(
            QStringLiteral("Georgia"), FontPreferences::missingVietnameseRange(QStringLiteral("Georgia")));
        check(georgia.count(guarded) == 1, "a Georgia export carries the Vietnamese fallback faces and stack");
        check(custom.count(QStringLiteral("@font-face{font-family:\"FastMD Vietnamese\"")) == 0,
              "a Times New Roman export has no fallback faces");
    }
}

}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    testNormalizedFamily();
    testQuotingAndRules();
    testVietnameseFallback();
    testStackMatchesPreviewCss();
    testSettingsKeys();
    testPreviewHtml();
    if (failures) {
        qCritical().noquote() << failures << "failure(s)";
        return 1;
    }
    qInfo().noquote() << "FontPreferencesTest: all checks passed";
    return 0;
}
