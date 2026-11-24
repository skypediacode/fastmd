// Guards the two shortcut contracts the application menu depends on now that
// it lives in a custom title bar rather than in QMainWindow's own menu bar row.
//
// 1. A QAction in one of those menus must still fire its shortcut. Qt resolves
//    a menu action's shortcut context by walking from the QMenu up to the
//    widget its menuAction() is attached to, so reparenting the QMenuBar is
//    exactly the kind of change that could silently kill every menu shortcut
//    in the window.
// 2. QAction::setVisible(false) is NOT a way to keep a command out of a menu
//    while keeping its shortcut: Qt disables a hidden action and drops it from
//    the shortcut map, and setEnabled(true) afterwards is a no-op while the
//    action is hidden. That is what silently killed View > Focus File Search
//    (Shift+F1); MainWindow now owns that action directly instead.

#include "Stylesheet.h"
#include "WindowTitleBar.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QTest>

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

void testHidingAnActionDisablesItIrreversibly()
{
    QAction action(QStringLiteral("Focus File Search"));
    action.setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F1));
    CHECK(action.isEnabled());

    action.setVisible(false);
    CHECK(!action.isEnabled());

    // The trap: re-enabling a hidden action does nothing, so "hide it but
    // leave the shortcut working" is not expressible this way at all.
    action.setEnabled(true);
    CHECK(!action.isEnabled());
}

void testTitleBarShortcutsFire()
{
    QMainWindow window;
    auto* titleBar = new WindowTitleBar(&window);
    window.setMenuWidget(titleBar);

    QMenu* view = titleBar->menuBar()->addMenu(QStringLiteral("&View"));

    bool menuFired = false;
    QAction* menuAction = view->addAction(QStringLiteral("Preview Panel"));
    menuAction->setShortcut(QKeySequence(Qt::Key_F9));
    QObject::connect(menuAction, &QAction::triggered, [&] { menuFired = true; });

    // How MainWindow keeps Shift+F1 working without showing the command in the
    // View menu: the action belongs to the window, not to any menu.
    bool windowFired = false;
    auto* windowAction = new QAction(QStringLiteral("Focus File Search"), &window);
    windowAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F1));
    QObject::connect(windowAction, &QAction::triggered, [&] { windowFired = true; });
    window.addAction(windowAction);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QTest::qWait(50);

    if (QApplication::activeWindow() != &window) {
        // The shortcut map keys off the active window, so without activation
        // the dispatch half simply cannot be asserted here.
        std::printf("MenuShortcutContextTest: window never activated, "
                    "shortcut dispatch not asserted\n");
        return;
    }

    QTest::keyClick(&window, Qt::Key_F9);
    CHECK(menuFired);

    QTest::keyClick(&window, Qt::Key_F1, Qt::ShiftModifier);
    CHECK(windowFired);

    // It must stay out of the menu while doing so.
    CHECK(!view->actions().contains(windowAction));
}

// Audit of the other ways an action in this window can end up holding a
// shortcut nothing will ever deliver. Each arrangement below is one FastMD
// actually uses.
void testOtherActionPlacementsStillDeliverShortcuts()
{
    QMainWindow window;
    auto* titleBar = new WindowTitleBar(&window);
    window.setMenuWidget(titleBar);
    titleBar->menuBar()->addMenu(QStringLiteral("&File"));

    auto* toolBar = new QToolBar(&window);
    window.addToolBar(toolBar);

    bool toolbarFired = false;
    auto* toolbarAction = new QAction(QStringLiteral("Bold"), &window);
    toolbarAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    QObject::connect(toolbarAction, &QAction::triggered, [&] { toolbarFired = true; });
    toolBar->addAction(toolbarAction);

    // The main toolbar's dropdown groups (Insert Math, Open, Image, Table):
    // the action lives only in a QMenu handed to a QToolButton via setMenu(),
    // which is NOT the same as the button owning the action.
    auto* hostAction = new QAction(QStringLiteral("Insert Math"), &window);
    toolBar->addAction(hostAction);
    auto* dropdown = new QMenu(&window);
    bool dropdownFired = false;
    auto* dropdownAction = new QAction(QStringLiteral("Inline math"), &window);
    dropdownAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    QObject::connect(dropdownAction, &QAction::triggered, [&] { dropdownFired = true; });
    dropdown->addAction(dropdownAction);
    if (auto* button = qobject_cast<QToolButton*>(toolBar->widgetForAction(hostAction)))
        button->setMenu(dropdown);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QTest::qWait(50);
    if (QApplication::activeWindow() != &window) {
        std::printf("MenuShortcutContextTest: window never activated, "
                    "placement audit not asserted\n");
        return;
    }

    QTest::keyClick(&window, Qt::Key_B, Qt::ControlModifier);
    CHECK(toolbarFired);

    QTest::keyClick(&window, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(dropdownFired);
}

// F5 exists twice in FastMD: a window-wide View > Refresh and the Source
// Control panel's own Qt::WidgetWithChildrenShortcut. Qt refuses to dispatch
// an ambiguous shortcut at all, so whether the window-wide one can be enabled
// depends entirely on how Qt resolves that overlap while the panel has focus.
void testWindowAndWidgetShortcutOverlap()
{
    QMainWindow window;
    auto* panel = new QPlainTextEdit(&window);
    window.setCentralWidget(panel);

    int windowFired = 0;
    auto* windowAction = new QAction(QStringLiteral("Refresh"), &window);
    windowAction->setShortcut(QKeySequence(Qt::Key_F5));
    windowAction->setShortcutContext(Qt::WindowShortcut);
    QObject::connect(windowAction, &QAction::triggered, [&] { ++windowFired; });
    window.addAction(windowAction);

    int panelFired = 0;
    auto* panelAction = new QAction(QStringLiteral("Refresh Panel"), panel);
    panelAction->setShortcut(QKeySequence(Qt::Key_F5));
    panelAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(panelAction, &QAction::triggered, [&] { ++panelFired; });
    panel->addAction(panelAction);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QTest::qWait(50);
    if (QApplication::activeWindow() != &window) {
        std::printf("MenuShortcutContextTest: window never activated, "
                    "F5 overlap not asserted\n");
        return;
    }

    // Both match while the panel has focus, and Qt answers an ambiguous
    // shortcut by dispatching it to NOBODY (it only warns). Enabling a
    // window-wide F5 alongside the Source Control panel's own would therefore
    // not merely add a binding -- it would silently kill the panel's Refresh.
    // Anyone reviving View > Refresh has to remove one of the two bindings,
    // not enable the second one.
    panel->setFocus();
    QTest::qWait(20);
    QTest::keyClick(&window, Qt::Key_F5);
    CHECK(windowFired == 0);
    CHECK(panelFired == 0);

    // With focus outside the panel there is only one candidate, and it fires.
    panel->clearFocus();
    window.setFocus();
    QTest::qWait(20);
    QTest::keyClick(&window, Qt::Key_F5);
    CHECK(windowFired == 1);
    CHECK(panelFired == 0);
}

void testTitleBarKeepsItsContentsSizeAsTheBarGrows()
{
    // The bar's height is a logical (device-independent) constant Qt scales on
    // its own; the icon it holds is a separate one, so a taller bar never
    // means a bigger icon.
    CHECK(WindowTitleBar::kHeight == 36);
    CHECK(WindowTitleBar::kIconSize == 16);
    CHECK(WindowTitleBar::kHeight % 2 == 0);

    QMainWindow window;
    auto* titleBar = new WindowTitleBar(&window);
    window.setMenuWidget(titleBar);
    titleBar->menuBar()->addMenu(QStringLiteral("&File"));
    window.resize(900, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    CHECK(titleBar->height() == WindowTitleBar::kHeight);

    // Shifting the row right must move the icon, not resize it, and the menu
    // has to move with it so the spacing between them is unchanged.
    const QRect menuBefore = titleBar->menuBar()->geometry();
    titleBar->setAppIconInkLeft(24);
    titleBar->layout()->activate();
    const QRect menuAfter = titleBar->menuBar()->geometry();
    // No window icon is set here, so the icon contributes no ink inset of its
    // own and only the box inset applies.
    const int inset = (WindowTitleBar::kIconBox - WindowTitleBar::kIconSize) / 2;
    CHECK(menuAfter.left() - menuBefore.left() == 24 - inset);
    CHECK(menuAfter.size() == menuBefore.size());

    // A request smaller than the icon's own inset cannot push the row off its
    // left edge.
    titleBar->setAppIconInkLeft(0);
    titleBar->layout()->activate();
    CHECK(titleBar->menuBar()->geometry().left() == menuBefore.left());
}

// The alignment reference: where a picture's ink actually starts, which is not
// where its box does.
void testOpaqueLeftInsetFindsTheFirstVisibleColumn()
{
    CHECK(WindowTitleBar::opaqueLeftInset(QPixmap()) == 0);

    QPixmap blank(10, 10);
    blank.fill(Qt::transparent);
    CHECK(WindowTitleBar::opaqueLeftInset(blank) == 0);

    QPixmap full(10, 10);
    full.fill(Qt::black);
    CHECK(WindowTitleBar::opaqueLeftInset(full) == 0);

    // Three transparent columns, then ink on one row only.
    QImage image(10, 10, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    image.setPixelColor(3, 7, QColor(0, 0, 0, 255));
    CHECK(WindowTitleBar::opaqueLeftInset(QPixmap::fromImage(image)) == 3);

    // A faint antialiasing fringe left of the shape must not count as ink.
    image.setPixelColor(1, 7, QColor(0, 0, 0, 10));
    CHECK(WindowTitleBar::opaqueLeftInset(QPixmap::fromImage(image)) == 3);

    // The result is logical pixels, so a 2x pixmap reports half its columns.
    QImage retina(20, 20, QImage::Format_ARGB32);
    retina.fill(Qt::transparent);
    retina.setPixelColor(6, 10, QColor(0, 0, 0, 255));
    QPixmap retinaPixmap = QPixmap::fromImage(retina);
    retinaPixmap.setDevicePixelRatio(2.0);
    CHECK(WindowTitleBar::opaqueLeftInset(retinaPixmap) == 3);
}

// The centred document title, against a real laid-out title bar.
void testCenteredTitleLayout()
{
    QMainWindow window;
    auto* titleBar = new WindowTitleBar(&window);
    window.setMenuWidget(titleBar);
    for (const char* name : { "&File", "&Edit", "F&ormat", "&Insert", "&View", "&Help" })
        titleBar->menuBar()->addMenu(QString::fromLatin1(name));

    window.resize(1200, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // No title set: nothing is painted and nothing is reserved.
    CHECK(titleBar->titleRect().isNull());

    titleBar->setTitleText(QStringLiteral("README.md — fastmd-src"));
    QRect rect = titleBar->titleRect();
    CHECK(!rect.isNull());

    // Centred on the whole window, not on the free band between the menus and
    // the buttons: the two margins either side of it match to within rounding.
    CHECK(qAbs((rect.left()) - (titleBar->width() - rect.right() - 1)) <= 1);

    // Clear of the menus on the left and the window buttons on the right. The
    // zone map is the authority for both, so ask it rather than re-deriving
    // the neighbours' geometry here.
    CHECK(titleBar->zoneAt(QPoint(rect.left() - 1, rect.center().y()))
          == TitleBarLayout::Zone::Caption);
    CHECK(titleBar->zoneAt(QPoint(rect.right() + 1, rect.center().y()))
          == TitleBarLayout::Zone::Caption);

    // It is painted, not a widget, so the whole strip it sits on still drags
    // the window.
    CHECK(titleBar->zoneAt(rect.center()) == TitleBarLayout::Zone::Caption);

    // A very long title is elided rather than allowed to grow into either
    // neighbour.
    titleBar->setTitleText(QString(400, QLatin1Char('W')));
    rect = titleBar->titleRect();
    CHECK(!rect.isNull());
    CHECK(titleBar->zoneAt(QPoint(rect.left() - 1, rect.center().y()))
          == TitleBarLayout::Zone::Caption);
    CHECK(titleBar->zoneAt(QPoint(rect.right() + 1, rect.center().y()))
          == TitleBarLayout::Zone::Caption);

    // Narrowing must drop the title entirely rather than overlap anything.
    titleBar->setTitleText(QStringLiteral("README.md — fastmd-src"));
    for (const int narrow : { 420, 320 }) {
        window.resize(narrow, 700);
        QTest::qWait(20);
        CHECK(titleBar->titleRect().isNull());
    }

    // And the title must not be what stops the window getting that narrow:
    // FastMD's minimum width is CommandPalette::PreferredWidth (320).
    CHECK(titleBar->minimumSizeHint().width() <= 320);

    // Widening brings it back.
    window.resize(1200, 700);
    QTest::qWait(20);
    CHECK(!titleBar->titleRect().isNull());
}

// The centred title is meant to read as primary UI text, identical to the
// menu items beside it. It gets there by reading the menu bar's own resolved
// font and foreground rather than repeating AppStyle's QMenuBar rule, so this
// asserts the mechanism actually resolves -- a silent fallback to the default
// palette would look plausible and be wrong.
void testTitleTypographyMatchesTheMenu()
{
    for (const bool dark : { false, true }) {
        qApp->setStyleSheet(dark ? AppStyle::dark() : AppStyle::light());

        QMainWindow window;
        auto* titleBar = new WindowTitleBar(&window);
        window.setMenuWidget(titleBar);
        titleBar->menuBar()->addMenu(QStringLiteral("&File"));
        titleBar->setTitleText(QStringLiteral("README.md — fastmd-src"));
        window.resize(1200, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QMenuBar* menuBar = titleBar->menuBar();
        CHECK(titleBar->titleFont().pixelSize() == menuBar->font().pixelSize());
        CHECK(titleBar->titleFont().family() == menuBar->font().family());
        // Regular weight, like the menu items.
        CHECK(titleBar->titleFont().weight() == QFont::Normal);
        CHECK(!titleBar->titleFont().bold());

        // The values AppStyle's QMenuBar rule declares. Spelled out rather
        // than read back from the same place the code reads them, so a
        // mechanism that quietly resolved to the default palette instead
        // would fail here.
        const QColor expected = dark ? QColor(0xbf, 0xbf, 0xbf) : QColor(0x33, 0x33, 0x33);
        CHECK(titleBar->titleColor() == expected);
        CHECK(titleBar->titleFont().pixelSize() == 13);

        // Never dimmed for an inactive window, unlike the caption buttons.
        const QColor active = titleBar->titleColor();
        titleBar->setWindowActive(false);
        CHECK(titleBar->titleColor() == active);
        titleBar->setWindowActive(true);
    }
    qApp->setStyleSheet(QString());
}

// The centred title is decoration: over it, the window must behave exactly as
// it does over the empty caption space beside it (drag, double-click to
// maximise, right-click for the system menu, the same cursor). All four follow
// from one thing -- the title's pixels resolving to Zone::Caption, which is
// what MainWindow's WM_NCHITTEST turns into HTCAPTION and hands to Windows.
//
// Today that holds because the title is painted in paintEvent() rather than
// being a child widget, so there is nothing to own a click. This asserts the
// requirement rather than that implementation: should the text ever become a
// real widget, it has to be mouse-transparent, or these checks fail.
void testTitleIsPurelyVisual()
{
    QMainWindow window;
    auto* titleBar = new WindowTitleBar(&window);
    window.setMenuWidget(titleBar);
    for (const char* name : { "&File", "&Edit", "F&ormat", "&Insert", "&View", "&Help" })
        titleBar->menuBar()->addMenu(QString::fromLatin1(name));
    titleBar->setTitleText(QStringLiteral("README.md — fastmd-src"));
    window.resize(1200, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QRect rect = titleBar->titleRect();
    CHECK(!rect.isNull());

    // No child widget may own the title's pixels -- and if one ever does, it
    // must be transparent to the mouse so the caption handling stays
    // authoritative.
    for (QWidget* child : titleBar->findChildren<QWidget*>()) {
        if (!child->isVisible() || !child->geometry().intersects(rect))
            continue;
        CHECK(child->testAttribute(Qt::WA_TransparentForMouseEvents));
    }

    // Sampled across the whole painted rect, including its top and bottom rows
    // where the resize band and the menu row would show up if either leaked in.
    for (int x = rect.left(); x <= rect.right(); x += 7) {
        for (const int y : { 0, rect.center().y(), titleBar->height() - 1 }) {
            CHECK(titleBar->zoneAt(QPoint(x, y)) == TitleBarLayout::Zone::Caption);
            CHECK(titleBar->childAt(QPoint(x, y)) == nullptr);
        }
    }

    // Identical to the empty caption space on either side of it: same zone, so
    // the same HTCAPTION, so the same drag / double-click / system-menu /
    // cursor behaviour, with nothing special-cased for the text.
    const int leftGap = rect.left() - 8;
    const int rightGap = rect.right() + 8;
    CHECK(titleBar->zoneAt(QPoint(leftGap, rect.center().y()))
          == titleBar->zoneAt(rect.center()));
    CHECK(titleBar->zoneAt(QPoint(rightGap, rect.center().y()))
          == titleBar->zoneAt(rect.center()));

    // The bar itself must not take focus or claim a cursor of its own, either.
    CHECK(titleBar->focusPolicy() == Qt::NoFocus);
    CHECK(!titleBar->testAttribute(Qt::WA_SetCursor));
}

// THE CAPTION LIFECYCLE REGRESSION.
//
// The title bar is normally alien (no HWND), so every pixel of it belongs to
// the top-level window and MainWindow's WM_NCHITTEST decides what it means.
// Qt promotes widgets to real native windows whenever something in the window
// needs one, and once this bar owns an HWND, Windows asks *it* about those
// pixels and stops asking the top-level -- which killed drag, double-click
// maximise and the right-click system menu all at once, with nothing happening
// instead. Observed in the field as "works at startup, dead after opening a
// document".
//
// The invariant: however the bar is realised, its caption zones must defer to
// the top-level. Natively that means answering HTTRANSPARENT, which is what
// makes Windows pass the hit test down to the parent in the same thread.
void testCaptionSurvivesNativeWindowPromotion()
{
    QMainWindow window;
    auto* titleBar = new WindowTitleBar(&window);
    window.setMenuWidget(titleBar);
    for (const char* name : { "&File", "&Edit", "F&ormat", "&Insert", "&View", "&Help" })
        titleBar->menuBar()->addMenu(QString::fromLatin1(name));
    titleBar->setTitleText(QStringLiteral("README.md — fastmd-src"));
    window.resize(1200, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QRect title = titleBar->titleRect();
    CHECK(!title.isNull());

    // Force the promotion the way anything in the real window can.
    titleBar->setAttribute(Qt::WA_NativeWindow, true);
    titleBar->winId();
    QTest::qWait(30);
    CHECK(titleBar->internalWinId() != 0);

    // The zone map itself must be untouched by becoming native.
    CHECK(titleBar->zoneAt(title.center()) == TitleBarLayout::Zone::Caption);
    CHECK(titleBar->zoneAt(QPoint(title.left() - 8, title.center().y()))
          == TitleBarLayout::Zone::Caption);

#ifdef Q_OS_WIN
    // And the native answer for those zones must hand the decision back to the
    // parent rather than claiming the pixels.
    // Asked of the real window procedure, so this exercises Qt's dispatch into
    // WindowTitleBar::nativeEvent() exactly as Windows does.
    const HWND barHwnd = reinterpret_cast<HWND>(titleBar->winId());
    const qreal dpr = titleBar->devicePixelRatioF();
    auto hitTest = [&](const QPoint& local) -> LRESULT {
        POINT screen{ int(local.x() * dpr), int(local.y() * dpr) };
        ClientToScreen(barHwnd, &screen);
        return SendMessageW(barHwnd, WM_NCHITTEST, 0, MAKELPARAM(screen.x, screen.y));
    };

    // Caption space, and the painted title itself, defer to the top-level.
    CHECK(hitTest(title.center()) == HTTRANSPARENT);
    CHECK(hitTest(QPoint(title.left() - 8, title.center().y())) == HTTRANSPARENT);
    CHECK(hitTest(QPoint(title.right() + 8, title.center().y())) == HTTRANSPARENT);

    // The menus and the app icon keep ordinary Qt input -- they must NOT be
    // made transparent, or the menu bar would stop responding.
    CHECK(hitTest(QPoint(6, titleBar->height() / 2)) != HTTRANSPARENT);
    CHECK(hitTest(QPoint(40, titleBar->height() / 2)) != HTTRANSPARENT);
#endif
}

} // namespace

// The VS Code-style layout controls left of the window buttons: each one's own
// box is client content (so it gets its clicks and tooltip), while the gaps
// around it stay caption, and it only ever mirrors and triggers its action.
void testLayoutTogglesAreClientAndMirrorTheirAction()
{
    QMainWindow window;
    auto* titleBar = new WindowTitleBar(&window);
    window.setMenuWidget(titleBar);
    for (const char* name : { "&File", "&Edit", "F&ormat", "&Insert", "&View", "&Help" })
        titleBar->menuBar()->addMenu(QString::fromLatin1(name));
    QAction workspace(QStringLiteral("Workspace"));
    QAction preview(QStringLiteral("Preview"));
    QAction notepad(QStringLiteral("Notepad"));
    for (QAction* action : { &workspace, &preview, &notepad })
        action->setCheckable(true);
    preview.setShortcut(QKeySequence(Qt::Key_F9));
    titleBar->addLayoutToggle(&workspace, QStringLiteral("layout-sidebar-left"),
                              QStringLiteral("layout-sidebar-left-off"), QStringLiteral("Toggle Workspace"),
                              QKeySequence(Qt::Key_F8));
    titleBar->addLayoutToggle(&preview, QStringLiteral("layout-sidebar-right"),
                              QStringLiteral("layout-sidebar-right-off"), QStringLiteral("Toggle Preview"));
    titleBar->addLayoutToggle(&notepad, QStringLiteral("layout-centered"),
                              QStringLiteral("layout-centered-off"), QStringLiteral("Toggle Notepad"));
    titleBar->setTitleText(QString(400, QLatin1Char('W')));
    window.resize(1200, 700);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QList<QRect> rects = titleBar->layoutToggleRects();
    CHECK(rects.size() == 3);
    if (rects.size() != 3)
        return;
    CHECK(workspace.shortcut().isEmpty());
    const QWidget* workspaceToggle = titleBar->childAt(rects.at(0).center());
    CHECK(workspaceToggle && workspaceToggle->toolTip() == QStringLiteral("Toggle Workspace (F8)"));
    const QWidget* previewToggle = titleBar->childAt(rects.at(1).center());
    CHECK(previewToggle && previewToggle->toolTip() == QStringLiteral("Toggle Preview (F9)"));
    const QWidget* notepadToggle = titleBar->childAt(rects.at(2).center());
    CHECK(notepadToggle && notepadToggle->toolTip() == QStringLiteral("Toggle Notepad"));
    const int size = WindowTitleBar::kLayoutButtonSize;
    const int gap = WindowTitleBar::kLayoutButtonGap;
    for (int i = 0; i < rects.size(); ++i) {
        const QRect& rect = rects.at(i);
        CHECK(rect.size() == QSize(size, size));
        CHECK(rect.top() == (WindowTitleBar::kHeight - size) / 2);
        CHECK(titleBar->zoneAt(rect.center()) == TitleBarLayout::Zone::Client);
        CHECK(titleBar->zoneAt(rect.topLeft()) == TitleBarLayout::Zone::Client);
        CHECK(titleBar->zoneAt(rect.bottomRight()) == TitleBarLayout::Zone::Client);
        // Above and below the box is caption: the bar still drags, and the
        // top resize band is not shadowed by a control there.
        CHECK(titleBar->zoneAt(QPoint(rect.center().x(), rect.top() - 1))
              == TitleBarLayout::Zone::Caption);
        CHECK(titleBar->zoneAt(QPoint(rect.center().x(), rect.bottom() + 1))
              == TitleBarLayout::Zone::Caption);
        if (i > 0) {
            CHECK(rect.left() == rects.at(i - 1).right() + 1 + gap);
            CHECK(titleBar->zoneAt(QPoint(rect.left() - 1, rect.center().y()))
                  == TitleBarLayout::Zone::Caption);
        }
    }
    // Immediately before the window buttons, a gap's width clear of them.
    const QRect last = rects.last();
    CHECK(titleBar->zoneAt(QPoint(last.right() + 1, last.center().y()))
          == TitleBarLayout::Zone::Caption);
    CHECK(titleBar->zoneAt(QPoint(last.right() + 1 + gap, last.center().y()))
          == TitleBarLayout::Zone::Minimize);

    // The elided title stops short of the controls, not of Minimize.
    const QRect title = titleBar->titleRect();
    CHECK(!title.isNull());
    CHECK(title.right() < rects.first().left());

    // Clicks trigger the action; the button holds no checked state of its own.
    auto* previewButton = qobject_cast<QAbstractButton*>(titleBar->childAt(rects.at(1).center()));
    CHECK(previewButton != nullptr);
    if (!previewButton)
        return;
    CHECK(previewButton->toolTip() == QStringLiteral("Toggle Preview (F9)"));
    CHECK(!previewButton->isCheckable());
    QTest::mouseClick(previewButton, Qt::LeftButton);
    CHECK(preview.isChecked());
    QTest::mouseClick(previewButton, Qt::LeftButton);
    CHECK(!preview.isChecked());

    // A disabled action disables its button, even when it was changed under a
    // QSignalBlocker (as MainWindow does) and only syncLayoutToggles() said so.
    {
        const QSignalBlocker blocker(&preview);
        preview.setEnabled(false);
    }
    titleBar->syncLayoutToggles();
    CHECK(!previewButton->isEnabled());
    QTest::mouseClick(previewButton, Qt::LeftButton);
    CHECK(!preview.isChecked());
    preview.setEnabled(true);
    CHECK(previewButton->isEnabled());
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    testHidingAnActionDisablesItIrreversibly();
    testTitleBarShortcutsFire();
    testOtherActionPlacementsStillDeliverShortcuts();
    testWindowAndWidgetShortcutOverlap();
    testTitleBarKeepsItsContentsSizeAsTheBarGrows();
    testOpaqueLeftInsetFindsTheFirstVisibleColumn();
    testCenteredTitleLayout();
    testTitleTypographyMatchesTheMenu();
    testTitleIsPurelyVisual();
    testCaptionSurvivesNativeWindowPromotion();
    testLayoutTogglesAreClientAndMirrorTheirAction();

    if (g_failures == 0)
        std::printf("MenuShortcutContextTest: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
