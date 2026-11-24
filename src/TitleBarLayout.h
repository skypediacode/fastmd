#pragma once

// Pure classification of a point inside the custom title bar, kept separate
// from WindowTitleBar so MainWindow's WM_NCHITTEST answer can be regression
// tested without a GUI (tests/NativeWindowFrameTest.cpp).
//
// The distinction this encodes is the whole reason the title bar does not
// simply report itself as caption: the app icon, the menu bar's *occupied*
// width, the layout controls, and the three window buttons must keep
// receiving ordinary mouse input, while everything left over behaves exactly like a native caption
// (drag to move, double-click to maximize, right-click for the system menu).

#include <QList>
#include <QPoint>
#include <QRect>
#include <QtGlobal>

namespace TitleBarLayout {

enum class Zone {
    // Ordinary client content -- the app icon, the menu bar's items and the
    // layout controls.
    Client,
    // Draggable caption space.
    Caption,
    Minimize,
    // Reported to Windows as HTMAXBUTTON so Win11 offers its Snap Layout
    // flyout on hover; see MainWindow::nativeEvent().
    Maximize,
    Close
};

// `menuRect` is the menu bar's *occupied* rectangle, not its widget geometry:
// a QMenuBar laid out beside a stretch still reports trailing empty space that
// must stay draggable. `controlRects` are the layout controls' own boxes, so
// the gaps between and around them stay caption. Every rect is in
// title-bar-local logical coordinates; a null/empty rect simply never matches.
inline Zone zoneAt(const QPoint& local,
                   const QRect& iconRect,
                   const QRect& menuRect,
                   const QRect& minimizeRect,
                   const QRect& maximizeRect,
                   const QRect& closeRect,
                   const QList<QRect>& controlRects = {})
{
    if (closeRect.contains(local))
        return Zone::Close;
    if (maximizeRect.contains(local))
        return Zone::Maximize;
    if (minimizeRect.contains(local))
        return Zone::Minimize;
    if (iconRect.contains(local) || menuRect.contains(local))
        return Zone::Client;
    for (const QRect& control : controlRects) {
        if (control.contains(local))
            return Zone::Client;
    }
    return Zone::Caption;
}

// Width the centered document title may occupy, given the free band between
// the menu bar and the window buttons.
//
// The title is centered on the *window*, not on that band, so its width is
// limited by whichever side runs out of room first: half of it has to fit on
// each side of the window's centre line. That is what keeps it from drifting
// left as the menus grow, while still guaranteeing it can never reach either
// neighbour. A result of zero or less means there is no room at all.
inline int centeredTitleMaxWidth(int barWidth, int leftBound, int rightBound)
{
    const int centre = barWidth / 2;
    return 2 * qMin(centre - leftBound, rightBound - centre);
}

} // namespace TitleBarLayout
