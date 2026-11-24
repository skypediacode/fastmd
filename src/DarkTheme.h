#pragma once

#include <QColor>
#include <QString>

// Raw values of the dark theme (near-neutral blacks separated by small
// luminance steps). ThemePalette.h maps them, together with the light values,
// onto the semantic roles both stylesheets and C++ painting use.
namespace DarkTheme {

inline constexpr QRgb Canvas = 0xFF191A1B;        // application environment: chrome, gaps
inline constexpr QRgb Editor = 0xFF121314;        // primary content surfaces (editor, preview)
inline constexpr QRgb Surface = Canvas;           // secondary regions (workspace, outline, terminal)
inline constexpr QRgb Elevated = 0xFF202122;      // menus, popups, dialogs, tooltips
inline constexpr QRgb Hover = 0xFF242526;         // hover on Surface rows
inline constexpr QRgb Border = 0xFF2A2B2C;        // separators and outlines
inline constexpr QRgb Active = 0xFF313233;        // buttons / strong active surface
inline constexpr QRgb ActiveHover = 0xFF3A3B3C;   // hover on Active, strong outlines
inline constexpr QRgb Pressed = 0xFF434445;
inline constexpr QRgb InactiveSelection = 0xFF2C2D2E;
inline constexpr QRgb Text = 0xFFBFBFBF;
inline constexpr QRgb EditorText = 0xFFBBBEBF;
inline constexpr QRgb TextSecondary = 0xFF8C8C8C;
inline constexpr QRgb Disabled = 0xFF606162;
inline constexpr QRgb Accent = 0xFF3994BC;
inline constexpr QRgb AccentHover = 0xFF4AA3CC;
inline constexpr QRgb AccentPressed = 0xFF2D7A9D;
inline constexpr QRgb Selection = 0xFF276782;
inline constexpr QRgb RowSelected = 0xFF1F3B49;   // Accent tinted into Canvas

inline QColor color(QRgb rgb) { return QColor::fromRgb(rgb); }

}
