#include "NativeWindowFrame.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace NativeWindowFrame {

ResizeEdge hitTestResizeBorder(const QRect& windowRect, int frameX, int frameY,
                               const QPoint& screenPos)
{
    const int left = windowRect.x();
    const int top = windowRect.y();
    const int right = windowRect.x() + windowRect.width();
    const int bottom = windowRect.y() + windowRect.height();

    const bool onLeft = screenPos.x() >= left && screenPos.x() < left + frameX;
    const bool onRight = screenPos.x() < right && screenPos.x() >= right - frameX;
    const bool onTop = screenPos.y() >= top && screenPos.y() < top + frameY;
    const bool onBottom = screenPos.y() < bottom && screenPos.y() >= bottom - frameY;

    if (onTop && onLeft)     return ResizeEdge::TopLeft;
    if (onTop && onRight)    return ResizeEdge::TopRight;
    if (onBottom && onLeft)  return ResizeEdge::BottomLeft;
    if (onBottom && onRight) return ResizeEdge::BottomRight;
    if (onLeft)              return ResizeEdge::Left;
    if (onRight)             return ResizeEdge::Right;
    if (onTop)               return ResizeEdge::Top;
    if (onBottom)            return ResizeEdge::Bottom;
    return ResizeEdge::None;
}

ResizeEdge withoutTopEdge(ResizeEdge edge)
{
    switch (edge) {
    case ResizeEdge::Top:      return ResizeEdge::None;
    case ResizeEdge::TopLeft:  return ResizeEdge::Left;
    case ResizeEdge::TopRight: return ResizeEdge::Right;
    default:                   return edge;
    }
}

#ifdef Q_OS_WIN
int frameThicknessX(void* hwnd)
{
    const UINT dpi = GetDpiForWindow(static_cast<HWND>(hwnd));
    return GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

int frameThicknessY(void* hwnd)
{
    const UINT dpi = GetDpiForWindow(static_cast<HWND>(hwnd));
    return GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

void applyBorderlessNcCalcSize(void* hwnd, void* ncCalcSizeParams)
{
    auto* params = static_cast<NCCALCSIZE_PARAMS*>(ncCalcSizeParams);
    const auto window = static_cast<HWND>(hwnd);
    const int frameX = frameThicknessX(hwnd);
    const int frameY = frameThicknessY(hwnd);

    params->rgrc[0].left   += frameX;
    params->rgrc[0].right  -= frameX;
    params->rgrc[0].bottom -= frameY;

    WINDOWPLACEMENT placement{};
    placement.length = sizeof(WINDOWPLACEMENT);
    if (GetWindowPlacement(window, &placement) && placement.showCmd == SW_SHOWMAXIMIZED)
        params->rgrc[0].top += frameY;
}

int hitTestCodeForEdge(ResizeEdge edge)
{
    switch (edge) {
    case ResizeEdge::Left:        return HTLEFT;
    case ResizeEdge::Right:       return HTRIGHT;
    case ResizeEdge::Top:         return HTTOP;
    case ResizeEdge::Bottom:      return HTBOTTOM;
    case ResizeEdge::TopLeft:     return HTTOPLEFT;
    case ResizeEdge::TopRight:    return HTTOPRIGHT;
    case ResizeEdge::BottomLeft:  return HTBOTTOMLEFT;
    case ResizeEdge::BottomRight: return HTBOTTOMRIGHT;
    case ResizeEdge::None:        break;
    }
    return HTNOWHERE;
}

const wchar_t* cursorForEdge(ResizeEdge edge)
{
    switch (edge) {
    case ResizeEdge::Left:
    case ResizeEdge::Right:       return IDC_SIZEWE;
    case ResizeEdge::Top:
    case ResizeEdge::Bottom:      return IDC_SIZENS;
    case ResizeEdge::TopLeft:
    case ResizeEdge::BottomRight: return IDC_SIZENWSE;
    case ResizeEdge::TopRight:
    case ResizeEdge::BottomLeft:  return IDC_SIZENESW;
    case ResizeEdge::None:        break;
    }
    return nullptr;
}
#endif

} // namespace NativeWindowFrame
