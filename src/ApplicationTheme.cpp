#include "ApplicationTheme.h"
#include "DarkTheme.h"
#include "FontPreferences.h"
#include "Stylesheet.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyle>

namespace ApplicationTheme {

namespace {
QString& editorFamilyStorage()
{
    static QString family;
    return family;
}
}

QPalette palette(bool dark)
{
    QPalette palette = QApplication::style()->standardPalette();
    if (dark) {
        palette.setColor(QPalette::Window, DarkTheme::color(DarkTheme::Elevated));
        palette.setColor(QPalette::WindowText, DarkTheme::color(DarkTheme::Text));
        palette.setColor(QPalette::Base, DarkTheme::color(DarkTheme::Editor));
        palette.setColor(QPalette::AlternateBase, DarkTheme::color(DarkTheme::Surface));
        palette.setColor(QPalette::Text, DarkTheme::color(DarkTheme::Text));
        palette.setColor(QPalette::Button, DarkTheme::color(DarkTheme::Active));
        palette.setColor(QPalette::ButtonText, DarkTheme::color(DarkTheme::Text));
        palette.setColor(QPalette::Highlight, DarkTheme::color(DarkTheme::Selection));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    }
    return palette;
}

QString styleSheet(bool dark)
{
    const QString base = dark ? AppStyle::dark() : AppStyle::light();
    const QString editorRule = FontPreferences::editorStyleRule(editorFamilyStorage());
    return editorRule.isEmpty() ? base : base + editorRule;
}

void setEditorFontFamily(const QString& family)
{
    editorFamilyStorage() = FontPreferences::normalizedFamily(family);
}

QString editorFontFamily()
{
    return editorFamilyStorage();
}

bool applyEditorFontFamily(const QString& family)
{
    const QString normalized = FontPreferences::normalizedFamily(family);
    if (normalized == editorFamilyStorage())
        return false;
    editorFamilyStorage() = normalized;
    // Same stylesheet the theme installed, with only the editor rule changed;
    // the palette is untouched. Before the first apply() there is nothing to
    // refresh: apply() will pick the family up.
    if (qApp->property("fastmd.applicationTheme").isValid())
        qApp->setStyleSheet(styleSheet(isDark()));
    return true;
}

bool apply(const QString& theme)
{
    bool dark = theme == QStringLiteral("dark");
    if (theme == QStringLiteral("system"))
        dark = QApplication::palette().color(QPalette::Window).lightness() < 128;

    // Palette first: setStyleSheet() repolishes every live widget, and a
    // stylesheet-styled widget fixes the palette current at that moment onto
    // itself and its children, which a later setPalette() never reaches.
    QApplication::setPalette(palette(dark));
    qApp->setStyleSheet(styleSheet(dark));
    qApp->setProperty("fastmd.applicationTheme", theme);
    qApp->setProperty("fastmd.applicationThemeDark", dark);
    return dark;
}

bool isDark()
{
    return qApp->property("fastmd.applicationThemeDark").toBool();
}

}
