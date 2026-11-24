#pragma once

#include "GitRepository.h"
#include "DarkTheme.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>

namespace GitUiLayout {

// Shared leading inset for file-row content on the Changes and Graph
// surfaces. Keeping this here prevents the two delegates from drifting apart.
constexpr int kFileRowLeadingInset = 4;

// Trailing inset for file-row content, matching GitFileDelegate's
// option.rect.adjusted(..., -6, 0). Shared so the group row's count badge
// can be positioned against the exact same right edge the file rows use.
constexpr int kFileRowTrailingInset = 6;

// Small left breathing room for the graph's first lane.
constexpr int kGraphLaneOriginInset = 20;

// Shared left offset for Graph header and row content.
constexpr int kGraphContentInset = 12;

// Half-width of the reference status glyph ("M" -- the widest status
// letter). GitFileDelegate draws each status letter flush-right against the
// file row's content edge with Qt::AlignRight, which ignores the extra
// reserved space in its rect -- so the glyph's visual center sits at
// (rightEdge - glyphAdvance / 2), NOT (rightEdge - reservedRectWidth / 2).
// The group row's count badge must subtract this same bare advance (no
// extra padding) from that edge to land its center on that X-axis.
inline int statusGlyphHalfWidth(const QFontMetrics& statusMetrics)
{
    return statusMetrics.horizontalAdvance(QLatin1Char('M')) / 2;
}

} // namespace GitUiLayout

// Shared semantic colours for every Git file-status surface. Keep this in
// one place so Source Control and expanded Graph files never drift apart.
inline QColor gitFileStateColor(GitFileState state, bool dark)
{
    switch (state) {
    case GitFileState::Modified:   return dark ? QColor(0xe5, 0xb5, 0x67) : QColor(0xa8, 0x62, 0x00);
    case GitFileState::Added:      return dark ? QColor(0x89, 0xd1, 0x85) : QColor(0x16, 0x80, 0x3c);
    case GitFileState::Untracked:  return dark ? QColor(0x89, 0xd1, 0x85) : QColor(0x16, 0x80, 0x3c);
    case GitFileState::Deleted:    return dark ? QColor(0xf4, 0x87, 0x71) : QColor(0xc4, 0x2b, 0x1c);
    case GitFileState::Renamed:    return dark ? QColor(0x75, 0xbe, 0xff) : QColor(0x00, 0x78, 0xd4);
    case GitFileState::Conflicted: return dark ? QColor(0xf1, 0x4c, 0x4c) : QColor(0xd1, 0x34, 0x38);
    }
    return dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x44, 0x44, 0x44);
}

inline QFont gitFileStatusFont(const QFont& base)
{
    QFont font(base);
    font.setWeight(QFont::DemiBold);
    return font;
}
