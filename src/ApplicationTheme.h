#pragma once

#include <QPalette>
#include <QString>

namespace ApplicationTheme {
bool apply(const QString& theme);
bool isDark();
// The application palette apply() installs for a light or dark theme.
QPalette palette(bool dark);
// The application stylesheet apply() installs for a light or dark theme.
QString styleSheet(bool dark);
// The Editor font family (empty: the built-in Consolas). Set it before the
// first apply() so startup builds the stylesheet once; applyEditorFontFamily()
// changes it later and reinstalls the stylesheet only if the family changed.
void setEditorFontFamily(const QString& family);
QString editorFontFamily();
bool applyEditorFontFamily(const QString& family);
}
