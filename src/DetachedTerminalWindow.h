#pragma once

// The floating window a detached Terminal panel lives in while popped out of
// the main window, VS Code's "detach terminal" style.
//
// Owns no terminal state of its own -- it is a thin QWidget shell that a
// TerminalPanel is reparented into and back out of. Closing it (Alt+F4, the
// header's own Attach/Close actions, or the system menu's Close, since the
// native title bar is removed -- see nativeEvent()) must never destroy the
// live terminal sessions: it emits reattachRequested() first so MainWindow can
// reparent the panel back into the docked splitter before this window's
// WA_DeleteOnClose destroys the (by then empty) shell.
//
// No native title bar: the window keeps its normal WS_CAPTION/WS_THICKFRAME
// styling (so DWM still gives it a drop shadow, rounded Win11 corners, Aero
// Snap, and taskbar/Alt+Tab behavior) but nativeEvent() answers WM_NCCALCSIZE
// so no space is reserved for a caption, and WM_NCHITTEST so the panel's own
// header strip acts as the caption (drag to move, double-click to
// maximize/restore, right-click for the system menu) while its buttons and
// the rest of the panel keep receiving ordinary clicks. This is the standard
// "borderless native window" technique (kept alongside the OS frame rather
// than replacing it, unlike Qt::FramelessWindowHint) so resizing, snapping,
// and taskbar behavior all stay native without reimplementing them.
//
// Do not drop WS_CAPTION to try to shrink DWM's shadow: it breaks the
// WM_NCCALCSIZE/WM_NCHITTEST suppression above (Windows falls back to
// painting its own default, undarkened title bar and border) for no reliable
// shadow win in return -- tried and reverted, see git history.

#include <QPointer>
#include <QWidget>

class QCloseEvent;
class QShowEvent;
class TerminalPanel;

class DetachedTerminalWindow : public QWidget {
    Q_OBJECT

public:
    // Takes ownership of `content` for as long as this window is alive; the
    // caller is expected to reparent it out again on reattachRequested().
    // This must be a peer top-level window, rather than a Qt-owned window of
    // MainWindow. Windows keeps an owned top-level above its owner, which
    // would make it impossible to activate the main window above a detached
    // terminal (or bring the terminal back above it). Its close/reattach
    // signal, not QObject parent ownership, governs its lifetime.
    explicit DetachedTerminalWindow(TerminalPanel* content);

    // Re-applies the hidden native caption's DWM mode for the app theme.
    // DWM uses it for the frame/shadow even though the visible terminal can
    // have an independent Light/Dark theme. Called at detach, after ShowWindow
    // (pre-show DWM attributes may be dropped), and on app theme changes.
    void setWindowChromeDarkMode(bool dark);

    // Minimize / maximize / restore, driven by the panel header's own window
    // controls. The header is this window's only caption once the native title
    // bar is suppressed, so it has to carry those buttons itself.
    void minimizeWindow();
    void toggleMaximized();

signals:
    // Emitted from closeEvent(), before the event is accepted, so a connected
    // slot can move `content` back to the docked panel while it is still safe
    // to reparent (this window is about to be deleted via WA_DeleteOnClose).
    void reattachRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    // Pushes the real window state back onto the header's maximize/restore
    // glyph, which must also track changes this window never initiated (Aero
    // Snap, Win+Up, the system menu, a header double-click handled natively).
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
#ifdef Q_OS_WIN
    // Intercepts WM_NCCALCSIZE (suppress the reserved caption area) and
    // WM_NCHITTEST (make the panel's header behave like the caption, and the
    // window edges resizable) so the native title bar disappears while the
    // window keeps native resize/move/snap/taskbar behavior. See the class
    // comment above.
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    // Restored in the constructor (before the caller's first show()) rather
    // than on showEvent(), so a spurious re-show (minimize/restore) never
    // fights a resize the user made while the window was open.
    void restoreSavedGeometry();
#ifdef Q_OS_WIN
    // Classifies a WM_NCHITTEST screen point as a resize border, the header
    // (caption), or ordinary client content. Physical-pixel screen
    // coordinates in, an HTxxx result out.
    qintptr hitTestNative(int x, int y);
    // Starts a Qt system move from the client-side terminal header if a window
    // manager does not honor the corresponding WM_NCHITTEST result.
    // This is a fallback only; the native path above remains responsible for
    // the cursor and for every other edge/corner.
    bool startHeaderSystemGesture(QWidget* watched, QEvent* event);

    bool m_manualTopResize = false;
    QPoint m_topResizePress;
    QRect m_topResizeGeometry;
#endif

    // Installs this window's event filter on `widget` and every descendant it
    // already has. Called again from QEvent::ChildAdded, so header controls
    // built after this window was created are covered too.
    void watchWidgetTree(QWidget* widget);

    // Not owned, and it outlives this window by design (closeEvent() reparents
    // it back into the docked splitter). QPointer per Rule 12: the panel is a
    // cross-reference held by a sibling, not a child this window controls.
    QPointer<TerminalPanel> m_content;
};
