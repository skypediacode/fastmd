#pragma once

// FastMD's compact custom title bar: the app icon, the existing application
// menu bar, draggable caption space, VS Code-style layout controls, and the
// three window buttons.
//
// It owns no commands. The QMenuBar it exposes is the one MainWindow::
// createMenus() populates, so File/Edit/Format/Insert/View/Help and every
// QAction in them are unchanged -- this widget only relocates them.
//
// It is installed with QMainWindow::setMenuWidget(), which puts it exactly
// where the menu bar row used to be, so the toolbar below it does not move.
// The window keeps its native frame; MainWindow::nativeEvent() suppresses the
// OS caption (WM_NCCALCSIZE) and answers WM_NCHITTEST from zoneAt() below. No
// state here is static: each window owns its own title bar.

#include "TitleBarLayout.h"

#include <QColor>
#include <QFont>
#include <QKeySequence>
#include <QPixmap>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QResizeEvent;
class QMenuBar;

namespace TitleBarInternal { class CaptionButton; class LayoutToggleButton; class WindowIcon; }

class WindowTitleBar : public QWidget {
    Q_OBJECT

public:
    // Logical (device-independent) pixels -- Qt scales this for us, so there is
    // no DPI arithmetic anywhere in this widget. Even on purpose: this is the
    // vertical offset of everything below it, and an odd logical height puts
    // the whole window on a half device pixel at fractional scaling (see the
    // command palette's alignment notes in AGENTS.md). The menu bar row it
    // replaced was 32; the extra 4 gives the caption a little more room
    // without changing the size of anything inside it.
    static constexpr int kHeight = 36;
    // The app icon and the box it is centered in. Both are logical pixels and
    // are independent of kHeight, which is what keeps the icon the same size
    // when the bar grows.
    static constexpr int kIconSize = 16;
    static constexpr int kIconBox = 30;
    // Windows' own caption button metric; keeping it makes the Snap Layout
    // flyout line up with the maximize button it is anchored to.
    static constexpr int kButtonWidth = 46;
    // VS Code's title-bar layout controls: a 16px Codicon with 3px of padding,
    // 4px apart and 4px clear of the window buttons.
    static constexpr int kLayoutIconSize = 16;
    static constexpr int kLayoutButtonSize = 22;
    static constexpr int kLayoutButtonGap = 4;

    explicit WindowTitleBar(QWidget* parent = nullptr);

    QMenuBar* menuBar() const { return m_menuBar; }

    // The document context the native caption used to show, minus the
    // "-- FastMD" suffix: `Untitled 1 - fastmd-demo`. Purely decorative -- it
    // is painted rather than being a child widget, so it cannot take a click
    // and the whole strip stays draggable caption. Composed by
    // MainWindow::updateWindowTitle(), which is also what sets the real window
    // title; there is no second copy of the document or workspace state.
    void setTitleText(const QString& text);

    // Where that title is painted, or a null rect when there is no room for
    // it. Public so a test can assert it stays centred on the window and clear
    // of both neighbours without driving the GUI.
    QRect titleRect() const;

    // The font and colour the title is drawn with: the application menu's own,
    // read back from the QMenuBar this bar hosts. Public so a test can assert
    // the two really do match rather than merely look similar.
    QFont titleFont() const;
    QColor titleColor() const;

    void setDarkMode(bool dark);
    // Swaps the middle button's glyph between Maximize and Restore. Driven by
    // MainWindow::changeEvent() so states this widget never initiated (Aero
    // Snap, Win+Up, the system menu, a caption double-click handled natively)
    // are reflected too.
    void setMaximized(bool maximized);
    // Windows dims an inactive window's caption; so does this one.
    void setWindowActive(bool active);

    // Shifts the whole content row right so the first *visible* column of the
    // app icon lands on `inkLeftInBar`. Ink, not the icon's box: a Material
    // Icons glyph carries side bearing inside its 24px box while the app icon
    // fills its own, so aligning the boxes still leaves the two pictures
    // visibly out of line. MainWindow measures the target from the real first
    // toolbar button rather than passing a hand-picked offset.
    void setAppIconInkLeft(int inkLeftInBar);

    // First column of `pixmap` carrying any ink, in logical pixels (the
    // pixmap's own device pixel ratio is divided out). 0 for a null or fully
    // transparent pixmap. Shared with MainWindow so both ends of the alignment
    // are measured the same way.
    static int opaqueLeftInset(const QPixmap& pixmap);

    // `local` is in this widget's logical coordinates.
    TitleBarLayout::Zone zoneAt(const QPoint& local) const;

    // Maximize uses HTMAXBUTTON for the Windows 11 Snap Layout flyout; Close
    // uses HTCLOSE to include the physical right edge. Their hover/pressed
    // states come from non-client mouse messages rather than Qt mouse events.
    void setMaximizeButtonHovered(bool hovered);
    void setMaximizeButtonPressed(bool pressed);
    void setCloseButtonHovered(bool hovered);
    void setCloseButtonPressed(bool pressed);

    // Adds a layout control left of the window buttons, after any added
    // before it. It only mirrors `action` -- checked picks `onIcon` over
    // `offIcon` (Codicon names under :/icons/codicons/), disabled dims it --
    // and a click triggers the action, which stays the single authority.
    // `shortcutHint` is shown in the tooltip only when the action has no shortcut
    // of its own; it never binds one.
    void addLayoutToggle(QAction* action, const QString& onIcon, const QString& offIcon,
                         const QString& toolTip, const QKeySequence& shortcutHint = {});
    // Re-reads every bound action. QAction::changed is not enough on its own:
    // MainWindow updates these actions under QSignalBlocker, which blocks it.
    void syncLayoutToggles();
    QList<QRect> layoutToggleRects() const;

signals:
    void minimizeRequested();
    void maximizeRestoreRequested();
    void closeRequested();
    // Left-clicking the app icon opens the window's system menu, the way a
    // native caption's icon does. The position is in the *window's* logical
    // coordinates, not global ones: the receiver converts it once against the
    // window's own native client origin, because QWidget::mapToGlobal() rounds
    // twice under fractional scaling and can attribute a point to the
    // neighbouring screen (see AGENTS.md).
    void systemMenuRequested(const QPoint& windowPos);

protected:
#ifdef Q_OS_WIN
    // Survival of the caption across native-window promotion.
    //
    // This widget is ordinarily "alien" (no HWND of its own), so every pixel of
    // it belongs to the top-level window and MainWindow's WM_NCHITTEST decides
    // what it means. But Qt promotes widgets to real native windows whenever
    // something in the window needs one -- QWidgetPrivate::createWinId() calls
    // create() on whole rows of siblings -- and the moment this bar owns an
    // HWND, Windows asks *it* about those pixels and stops asking the
    // top-level. Every caption gesture then dies at once: drag, double-click to
    // maximise, and the right-click system menu, with nothing happening
    // instead, because the bar has no mouse handlers of its own.
    //
    // The fix is not to fight the promotion (nothing here controls what
    // triggers it) but to stay transparent through it: the caption zones answer
    // HTTRANSPARENT, which makes Windows pass the hit test down to the parent in
    // the same thread -- so MainWindow::titleBarHitTest() remains the single
    // authority whether this widget is native or not. The interactive zones are
    // left alone and keep receiving ordinary Qt input.
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif
    void paintEvent(QPaintEvent* event) override;
    // The title is centred on the window, so every width change moves it and
    // the whole strip has to be repainted, not just the newly exposed part.
    void resizeEvent(QResizeEvent* event) override;

private:
    // The menu bar's *occupied* width, not its widget width: a QMenuBar laid
    // out next to a stretch still reports trailing space that must stay
    // draggable caption.
    QRect menuRect() const;
    // The title after elision, and the rect it occupies. Empty text means the
    // title does not fit and is dropped entirely.
    QString elidedTitle(int maxWidth) const;
    // The single layout computation: returns the title's rect and, through
    // `text`, the string that was measured for it. A null rect means the title
    // has nowhere to go and is not drawn at all.
    QRect titleLayout(QString* text) const;

    QString m_title;
    QHBoxLayout* m_layout = nullptr;
    QMenuBar* m_menuBar = nullptr;
    TitleBarInternal::WindowIcon* m_icon = nullptr;
    TitleBarInternal::CaptionButton* m_minimize = nullptr;
    TitleBarInternal::CaptionButton* m_maximize = nullptr;
    TitleBarInternal::CaptionButton* m_close = nullptr;
    QList<TitleBarInternal::LayoutToggleButton*> m_layoutToggles;
    bool m_dark = false;
    bool m_windowActive = true;
};
