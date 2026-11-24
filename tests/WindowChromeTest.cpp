// Regression guard for the two pure halves of FastMD's custom window chrome:
// the resize-border classification that replaces the suppressed OS caption
// (NativeWindowFrame) and the title-bar zone classification that decides which
// parts of that caption drag the window and which stay clickable
// (TitleBarLayout). Both are answers to WM_NCHITTEST, where a mistake shows up
// as "the menu drags the window" or "the top row of the buttons is dead" --
// neither of which a non-interactive test could otherwise catch.

#include "NativeWindowFrame.h"
#include "TitleBarLayout.h"

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

using Edge = NativeWindowFrame::ResizeEdge;
using Zone = TitleBarLayout::Zone;

// A 1200x750 window at (100, 50) with Windows 10/11's usual 8px invisible
// resize borders.
constexpr int kLeft = 100;
constexpr int kTop = 50;
constexpr int kWidth = 1200;
constexpr int kHeight = 750;
constexpr int kFrame = 8;

Edge edgeAt(int x, int y)
{
    return NativeWindowFrame::hitTestResizeBorder(
        QRect(kLeft, kTop, kWidth, kHeight), kFrame, kFrame, QPoint(x, y));
}

void testResizeBordersCoverEveryEdgeAndCorner()
{
    const int right = kLeft + kWidth - 1;
    const int bottom = kTop + kHeight - 1;

    CHECK(edgeAt(kLeft, kTop) == Edge::TopLeft);
    CHECK(edgeAt(right, kTop) == Edge::TopRight);
    CHECK(edgeAt(kLeft, bottom) == Edge::BottomLeft);
    CHECK(edgeAt(right, bottom) == Edge::BottomRight);

    CHECK(edgeAt(kLeft, kTop + 300) == Edge::Left);
    CHECK(edgeAt(right, kTop + 300) == Edge::Right);
    CHECK(edgeAt(kLeft + 600, kTop) == Edge::Top);
    CHECK(edgeAt(kLeft + 600, bottom) == Edge::Bottom);

    // The last pixel of each band still resizes; the first pixel past it does
    // not. An off-by-one here is a border that cannot be grabbed at all.
    CHECK(edgeAt(kLeft + kFrame - 1, kTop + 300) == Edge::Left);
    CHECK(edgeAt(kLeft + kFrame, kTop + 300) == Edge::None);
    CHECK(edgeAt(kLeft + 600, kTop + kFrame - 1) == Edge::Top);
    CHECK(edgeAt(kLeft + 600, kTop + kFrame) == Edge::None);
    CHECK(edgeAt(right - kFrame + 1, kTop + 300) == Edge::Right);
    CHECK(edgeAt(right - kFrame, kTop + 300) == Edge::None);
}

void testPointsOutsideTheWindowAreNotBorders()
{
    CHECK(edgeAt(kLeft - 1, kTop + 300) == Edge::None);
    CHECK(edgeAt(kLeft + kWidth, kTop + 300) == Edge::None);
    CHECK(edgeAt(kLeft + 600, kTop - 1) == Edge::None);
    CHECK(edgeAt(kLeft + 600, kTop + kHeight) == Edge::None);
    CHECK(edgeAt(kLeft + 600, kTop + 300) == Edge::None);
}

void testDroppingTheTopEdgeKeepsTheHorizontalOne()
{
    // Windows gives a caption's own controls priority over the top resize
    // band. Dropping Top must leave a corner still resizable sideways rather
    // than disabling it outright.
    CHECK(NativeWindowFrame::withoutTopEdge(Edge::Top) == Edge::None);
    CHECK(NativeWindowFrame::withoutTopEdge(Edge::TopLeft) == Edge::Left);
    CHECK(NativeWindowFrame::withoutTopEdge(Edge::TopRight) == Edge::Right);
    CHECK(NativeWindowFrame::withoutTopEdge(Edge::Bottom) == Edge::Bottom);
    CHECK(NativeWindowFrame::withoutTopEdge(Edge::BottomLeft) == Edge::BottomLeft);
    CHECK(NativeWindowFrame::withoutTopEdge(Edge::Left) == Edge::Left);
    CHECK(NativeWindowFrame::withoutTopEdge(Edge::None) == Edge::None);
}

// The layout of a 1000px-wide title bar: a 30px icon, a menu bar whose items
// occupy 300px, then empty caption space, then three 46px buttons.
constexpr int kBarWidth = 1000;
constexpr int kBarHeight = 32;
const QRect kIcon(0, 0, 30, kBarHeight);
const QRect kMenu(30, 0, 300, kBarHeight);
const QRect kMinimize(kBarWidth - 138, 0, 46, kBarHeight);
const QRect kMaximize(kBarWidth - 92, 0, 46, kBarHeight);
const QRect kClose(kBarWidth - 46, 0, 46, kBarHeight);

Zone zoneAt(int x, int y)
{
    return TitleBarLayout::zoneAt(QPoint(x, y), kIcon, kMenu, kMinimize, kMaximize, kClose);
}

void testInteractiveRegionsAreNeverCaption()
{
    CHECK(zoneAt(15, 16) == Zone::Client);   // app icon
    CHECK(zoneAt(40, 16) == Zone::Client);   // File
    CHECK(zoneAt(329, 16) == Zone::Client);  // last menu item's last pixel
    CHECK(zoneAt(kBarWidth - 120, 16) == Zone::Minimize);
    CHECK(zoneAt(kBarWidth - 70, 16) == Zone::Maximize);
    CHECK(zoneAt(kBarWidth - 20, 16) == Zone::Close);
    // Every one of them at y = 0, where the top resize band would otherwise
    // swallow the first rows.
    CHECK(zoneAt(15, 0) == Zone::Client);
    CHECK(zoneAt(kBarWidth - 20, 0) == Zone::Close);
}

void testEmptySpaceDragsTheWindow()
{
    CHECK(zoneAt(330, 16) == Zone::Caption);  // just past the last menu item
    CHECK(zoneAt(600, 16) == Zone::Caption);
    CHECK(zoneAt(kBarWidth - 139, 16) == Zone::Caption);  // just left of Minimize
    CHECK(zoneAt(600, 0) == Zone::Caption);
    CHECK(zoneAt(600, kBarHeight - 1) == Zone::Caption);
}

void testMenuBarTrailingSpaceStaysDraggable()
{
    // The occupied menu rect is what matters, not the QMenuBar's widget width:
    // a menu bar laid out beside a stretch still reports trailing empty space,
    // and treating that as client would leave nothing to drag the window by.
    const QRect occupied(30, 0, 120, kBarHeight);
    CHECK(TitleBarLayout::zoneAt(QPoint(200, 16), kIcon, occupied,
                                 kMinimize, kMaximize, kClose) == Zone::Caption);
    CHECK(TitleBarLayout::zoneAt(QPoint(100, 16), kIcon, occupied,
                                 kMinimize, kMaximize, kClose) == Zone::Client);
}

void testAbsentControlsNeverMatch()
{
    // An empty menu bar (no actions) reports a null rect; it must not swallow
    // the origin, which QRect() would if it were treated as containing (0, 0).
    CHECK(TitleBarLayout::zoneAt(QPoint(0, 0), QRect(), QRect(),
                                 QRect(), QRect(), QRect()) == Zone::Caption);
}

// The centred document title: its width is limited by whichever side of the
// window's centre line runs out of room first, which is what lets it stay
// centred on the window while never reaching the menus or the caption buttons.
void testCenteredTitleWidthIsLimitedByTheTighterSide()
{
    // Symmetric free band: the whole band is usable.
    CHECK(TitleBarLayout::centeredTitleMaxWidth(1000, 300, 700) == 400);

    // Menus grow on the left; the left side is now the binding constraint, and
    // the allowance shrinks on BOTH sides so the text stays centred.
    CHECK(TitleBarLayout::centeredTitleMaxWidth(1000, 400, 700) == 200);

    // Buttons bite in from the right instead.
    CHECK(TitleBarLayout::centeredTitleMaxWidth(1000, 300, 560) == 120);

    // A bound that has crossed the centre line leaves nothing at all, which is
    // the signal to drop the title rather than overlap a neighbour.
    CHECK(TitleBarLayout::centeredTitleMaxWidth(1000, 520, 900) <= 0);
    CHECK(TitleBarLayout::centeredTitleMaxWidth(1000, 100, 480) <= 0);

    // A window too narrow for anything between the two.
    CHECK(TitleBarLayout::centeredTitleMaxWidth(420, 300, 340) <= 0);
}

void testCenteredTitleNeverReachesItsNeighbours()
{
    // Whatever the bounds, half the granted width must fit on each side of the
    // centre line -- the property the paint code relies on to place the text
    // without a second clamp.
    for (int barWidth : { 420, 700, 1000, 1600 }) {
        for (int leftBound = 0; leftBound < barWidth; leftBound += 37) {
            for (int rightBound = leftBound; rightBound <= barWidth; rightBound += 53) {
                const int maxWidth = TitleBarLayout::centeredTitleMaxWidth(
                    barWidth, leftBound, rightBound);
                if (maxWidth <= 0)
                    continue;
                const int centre = barWidth / 2;
                CHECK(centre - maxWidth / 2 >= leftBound);
                CHECK(centre + maxWidth / 2 <= rightBound);
            }
        }
    }
}

} // namespace

int main()
{
    testResizeBordersCoverEveryEdgeAndCorner();
    testPointsOutsideTheWindowAreNotBorders();
    testDroppingTheTopEdgeKeepsTheHorizontalOne();
    testInteractiveRegionsAreNeverCaption();
    testEmptySpaceDragsTheWindow();
    testMenuBarTrailingSpaceStaysDraggable();
    testAbsentControlsNeverMatch();
    testCenteredTitleWidthIsLimitedByTheTighterSide();
    testCenteredTitleNeverReachesItsNeighbours();

    if (g_failures == 0)
        std::printf("WindowChromeTest: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
