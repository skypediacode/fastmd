#pragma once

#include "DarkTheme.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QRect>
#include <QString>
#include <QWidget>

// Semantic palette shared by both themes. The application is one environment
// in which only the document surfaces stand apart:
//
//   Canvas   window background, menu bar, toolbar, tab strip, status, gaps
//   Surface  secondary regions (Workspace, Outline, Terminal); deliberately the
//            same color as Canvas -- only their thin border marks their extent
//   Editor   primary content surfaces (editor, preview): deeper in Dark, white
//            in Light
//   Hover..  hover / selection / focus / active states
//   Elevated menus, popups, dialogs, tooltips
//   Row*     list and tree rows: one soft hover, one accent-tinted selection
//            (text keeps its normal color), one neutral unfocused selection
//
// Stylesheets are written with @token@ placeholders that resolve() expands, and
// C++ code that paints chrome itself reads color(), so the two cannot drift.
namespace ThemePalette {

enum Role {
    Canvas, Editor, Preview, Surface, Elevated, Hover, Border, Active, ActiveHover,
    Pressed, InactiveSelection, Text, EditorText, TextSecondary, Disabled,
    Accent, AccentHover, AccentPressed, Selection,
    RowHover, RowSelected, RowSelectedInactive, RoleCount
};

inline QRgb rgb(Role role, bool dark)
{
    static constexpr QRgb darkTable[RoleCount] = {
        DarkTheme::Canvas, DarkTheme::Editor, DarkTheme::Editor, DarkTheme::Surface,
        DarkTheme::Elevated, DarkTheme::Hover, DarkTheme::Border, DarkTheme::Active,
        DarkTheme::ActiveHover, DarkTheme::Pressed, DarkTheme::InactiveSelection,
        DarkTheme::Text, DarkTheme::EditorText, DarkTheme::TextSecondary, DarkTheme::Disabled,
        DarkTheme::Accent, DarkTheme::AccentHover, DarkTheme::AccentPressed, DarkTheme::Selection,
        DarkTheme::Hover, DarkTheme::RowSelected, DarkTheme::InactiveSelection,
    };
    // The light canvas is a barely-off-white neutral, so only a faint step
    // separates it from the white document surfaces. Editor and Preview share
    // one white: they are the two primary content surfaces. Preview is also the
    // WebView2 clear color and preview.css --bg, which must stay in lock-step.
    static constexpr QRgb lightTable[RoleCount] = {
        0xFFF7F7F7, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFF7F7F7,
        0xFFFFFFFF, 0xFFEBEBEB, 0xFFDCDCDC, 0xFFE6E6E6,
        0xFFD9D9D9, 0xFFCCCCCC, 0xFFE4E4E4,
        0xFF303030, 0xFF222222, 0xFF686868, 0xFFA0A0A0,
        0xFF0078D4, 0xFF106EBE, 0xFF005A9E, 0xFF0078D4,
        0xFFEBEBEB, 0xFFD9E9F8, 0xFFE4E4E4,
    };
    return dark ? darkTable[role] : lightTable[role];
}

inline QColor color(Role role, bool dark) { return QColor::fromRgb(rgb(role, dark)); }

inline QString resolve(QString qss, bool dark)
{
    if (!qss.contains(QLatin1Char('@')))
        return qss;
    // Longer names first where one is a prefix of another (@editorText@ /
    // @editor@ are distinct tokens, but keep the order explicit anyway).
    struct Token { const char* name; Role role; };
    static const Token tokens[] = {
        {"@canvas@", Canvas}, {"@editorText@", EditorText}, {"@editor@", Editor},
        {"@preview@", Preview}, {"@surface@", Surface}, {"@elevated@", Elevated},
        {"@hover@", Hover}, {"@border@", Border}, {"@activeHover@", ActiveHover},
        {"@active@", Active}, {"@pressed@", Pressed},
        {"@inactiveSelection@", InactiveSelection}, {"@textSecondary@", TextSecondary},
        {"@text@", Text}, {"@disabled@", Disabled}, {"@accentHover@", AccentHover},
        {"@accentPressed@", AccentPressed}, {"@accent@", Accent}, {"@selection@", Selection},
        {"@rowHover@", RowHover}, {"@rowSelectedInactive@", RowSelectedInactive},
        {"@rowSelected@", RowSelected},
    };
    for (const Token& t : tokens)
        qss.replace(QLatin1String(t.name), color(t.role, dark).name());
    return qss;
}

}

// Geometry of the card system. A card's border is painted by whoever owns the
// space around its content (a splitter for its panes, the Workspace panel for
// its two sections, QSS for the Terminal); the content itself sits Inset px
// inside the card edge, which clears the rounded corner so square child
// widgets -- including native WebView2 windows, which nothing can paint over --
// never cover the arc.
namespace ThemeCard {

// The radius scale follows a component's size and role, not one value
// everywhere:
//   ControlRadius  compact controls up to ~30px: fields, rows, menu items,
//                  small icon buttons, tooltips
//   ButtonRadius   the 36-40px chrome controls on the canvas: main toolbar
//                  buttons, the tab row's "+", the command palette box, the
//                  preview's outline button
//   Radius         surfaces: the cards
//   TabRadius      the document tab pills
//   OverlayRadius  floating surfaces (menus, popups, overlays), whose 4px items
//                  sit 4px inside them and so stay concentric
// QSS repeats these values as 4px / 8px / 10px / 8px / 8px.
inline constexpr int ControlRadius = 4;
inline constexpr int ButtonRadius = 8;
inline constexpr int Radius = 10;
inline constexpr int TabRadius = 8;
inline constexpr int OverlayRadius = 8;
// Inset clears the Radius arc and its antialiased stroke: a square child's
// corner (Inset, Inset) lies inside the inner edge of the border, with room
// for a native WebView2 window rounding half a device pixel at 150%.
inline constexpr int Inset = 4;
static_assert((Radius - Inset) * 1.41422 + 1.0 / 3.0 < Radius - 1,
              "a square child Inset px inside a card must not reach its border");
// The document tabs are self-contained TabRadius pills above both the Editor and
// the Preview card: they start flush with the top edge of the Workspace column
// (TabTopMargin, QSS margin-top on QTabBar#documentTabBar::tab) and sit TabGap
// above the cards (margin-bottom).
inline constexpr int TabTopMargin = 0;
inline constexpr int TabGap = 4;
// Horizontal margin of a list/tree row highlight inside its viewport, so the
// rounded fill never touches the card border or the scrollbar.
inline constexpr int RowInset = 4;
// Canvas visible between two adjacent cards: just enough that two borders do
// not merge into one separator.
inline constexpr int Gap = 4;
// Handle width for a splitter whose panes are cards it paints itself: the
// visible gap plus each neighbour's card edge.
inline constexpr int CardSplitterHandle = Gap + 2 * Inset;
// A splitter whose panes paint their own cards has a handle only Gap wide; it
// reaches this far into each neighbour's card edge so every gap shares the
// same CardSplitterHandle-wide drag target.
inline constexpr int HitSlop = Inset;

inline constexpr const char* RoleProperty = "fastmdCardRole";

inline void setRole(QWidget* widget, ThemePalette::Role role)
{
    widget->setProperty(RoleProperty, int(role));
}

// A bordered, rounded box: a card, or the current document tab. The 1px stroke
// sits on the half-pixel inside rect so it covers exactly the edge rows.
inline void paintRounded(QPainter& painter, const QRect& box, const QColor& fill,
                         const QColor& border, qreal radius)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
    painter.restore();
}

inline void paint(QPainter& painter, const QRect& card, ThemePalette::Role fill, bool dark)
{
    paintRounded(painter, card, ThemePalette::color(fill, dark),
                 ThemePalette::color(ThemePalette::Border, dark), Radius);
}

// The shared list/tree row background: a rounded fill RowInset px inside the
// row's horizontal extent. Returns false when the row needs no fill.
inline bool paintRow(QPainter& painter, const QRect& row, bool dark, bool selected,
                     bool hovered, bool active = true)
{
    if (!selected && !hovered)
        return false;
    const ThemePalette::Role role = !selected ? ThemePalette::RowHover
        : active ? ThemePalette::RowSelected : ThemePalette::RowSelectedInactive;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ThemePalette::color(role, dark));
    painter.drawRoundedRect(QRectF(row.adjusted(RowInset, 1, -RowInset, -1)),
                            ControlRadius, ControlRadius);
    painter.restore();
    return true;
}

}
