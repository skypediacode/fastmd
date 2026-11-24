#pragma once

// The "borderless native window" primitives shared by every FastMD top-level
// that suppresses the OS caption and paints its own instead: MainWindow's
// custom title bar and DetachedTerminalWindow's terminal header.
//
// The technique is deliberately *not* Qt::FramelessWindowHint. WS_CAPTION and
// WS_THICKFRAME stay set -- so DWM keeps the drop shadow, the Win11 rounded
// corners, Aero Snap, Win+Arrow, the system menu, and taskbar/Alt+Tab
// behavior -- and only the reserved non-client *rectangle* is shrunk via
// WM_NCCALCSIZE so no space is left for a caption. Everything the caption used
// to provide (drag, double-click, resize cursors) comes back through
// WM_NCHITTEST, which is what the pure helpers below classify.
//
// The classification is pure and lives here rather than in either window so a
// single authority can be regression-tested without a GUI (see
// tests/NativeWindowFrameTest.cpp).

#include <QPoint>
#include <QRect>
#include <QtGlobal>

namespace NativeWindowFrame {

enum class ResizeEdge {
    None,
    Left, Right, Top, Bottom,
    TopLeft, TopRight, BottomLeft, BottomRight
};

// `windowRect` is the window rect in physical screen pixels, with width/height
// exclusive of the far edge exactly as GetWindowRect reports it (x = left,
// x + width = right). `frameX`/`frameY` are the resize-border thicknesses.
// Returns None for any point that is not inside a border band.
ResizeEdge hitTestResizeBorder(const QRect& windowRect, int frameX, int frameY,
                               const QPoint& screenPos);

// Drops the vertical component of a top edge, leaving the horizontal one
// intact. Used where the top resize band overlaps an interactive title-bar
// control: Windows itself gives the caption buttons priority over HTTOP, so a
// custom caption has to do the same or its buttons lose their top rows.
ResizeEdge withoutTopEdge(ResizeEdge edge);

#ifdef Q_OS_WIN
// HWND/NCCALCSIZE_PARAMS are passed as void* so <windows.h> stays out of this
// header (it is included by widgets that must not pull it in transitively).

// SM_CXFRAME/SM_CYFRAME + SM_CXPADDEDBORDER at the window's current DPI. These
// are the *invisible* resize borders Windows 10/11 place outside the visible
// window edge, which is why shrinking the non-client rect by exactly this much
// lands the client area on the visible edges.
int frameThicknessX(void* hwnd);
int frameThicknessY(void* hwnd);

// Shrinks an in-flight WM_NCCALCSIZE (wParam == TRUE) rect down to the resize
// margins, leaving no room for a caption. The top edge is left alone so the
// custom title bar can paint all the way up; a maximized window gets its top
// margin added back, because Windows places a maximized window exactly
// `frameY` beyond the work area on every side and the removed caption is no
// longer there to absorb it.
void applyBorderlessNcCalcSize(void* hwnd, void* ncCalcSizeParams);

// HTLEFT/HTTOPRIGHT/... for an edge, or HTNOWHERE for None.
int hitTestCodeForEdge(ResizeEdge edge);

// IDC_SIZENS/IDC_SIZENWSE/... for an edge, or nullptr for None.
const wchar_t* cursorForEdge(ResizeEdge edge);
#endif

} // namespace NativeWindowFrame
