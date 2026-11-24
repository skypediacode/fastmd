#include "DetachedTerminalWindow.h"
#include "ApplicationTheme.h"
#include "NativeWindowFrame.h"
#include "TerminalPanel.h"

#include <QAbstractButton>
#include <QChildEvent>
#include <QCloseEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QShowEvent>
#include <QtMath>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
constexpr auto kGeometryKey = "terminal/detachedGeometry";
// The docked panel's own default height is a third of the editor area, which
// reads as cramped once it is the whole window -- a detached terminal wants
// room to actually work in, VS Code's popped-out terminal included.
constexpr int kDefaultWidth = 1200;
constexpr int kDefaultHeight = 800;
}

#ifdef Q_OS_WIN
// Defined in MainWindow.cpp; shared instead of duplicated (see its use from
// MarkdownEditor.cpp and MainWindowExport.cpp for the same pattern).
bool setWindowsTitleBarDarkMode(QWidget* window, bool dark, bool includeCaptionColor);
#endif

DetachedTerminalWindow::DetachedTerminalWindow(TerminalPanel* content)
    : QWidget(nullptr, Qt::Window)
    , m_content(content)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Terminal — FastMD"));
    if (content && content->window())
        setWindowIcon(content->window()->windowIcon());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    if (content) {
        layout->addWidget(content);
        connect(content, &TerminalPanel::minimizeRequested, this,
                &DetachedTerminalWindow::minimizeWindow);
        connect(content, &TerminalPanel::maximizeRestoreRequested, this,
                &DetachedTerminalWindow::toggleMaximized);
        content->setMaximized(isMaximized());

        // The terminal header consists of ordinary child widgets, so mouse
        // presses can be delivered straight to its labels rather than this
        // shell. Watch them all to retain a reliable client-side fallback for
        // move/top-resize when Windows declines the custom hit-test result.
        watchWidgetTree(content);
    }
    installEventFilter(this);

    restoreSavedGeometry();

#ifdef Q_OS_WIN
    setWindowChromeDarkMode(ApplicationTheme::isDark());
#endif
}

void DetachedTerminalWindow::setWindowChromeDarkMode(bool dark)
{
#ifdef Q_OS_WIN
    // The caption is suppressed, so its DWM mode controls the native frame
    // and shadow, not the terminal's own separately themed content. Follow
    // the app/desktop chrome instead of darkening the shadow for a pinned
    // Dark terminal in a Light app.
    setWindowsTitleBarDarkMode(this, dark, false);
#else
    Q_UNUSED(dark);
#endif
}

void DetachedTerminalWindow::minimizeWindow()
{
    showMinimized();
}

void DetachedTerminalWindow::toggleMaximized()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

void DetachedTerminalWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    // Minimizing also clears Qt's maximized bit, so only a restored/maximized
    // window may rewrite the glyph -- otherwise minimizing a maximized window
    // would leave it showing "Maximize" when it comes back maximized.
    if (event->type() == QEvent::WindowStateChange && m_content && !isMinimized())
        m_content->setMaximized(isMaximized());
}

void DetachedTerminalWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_content)
        m_content->setMaximized(isMaximized());
#ifdef Q_OS_WIN
    // A DWM attribute set before the window's first native ShowWindow can be
    // silently dropped (the same reason GoToLineDialog re-applies its title
    // bar theme from showEvent rather than trusting its constructor-time
    // call alone). Re-assert it here, once the window is actually being
    // shown, so the native frame reliably matches the application theme.
    setWindowChromeDarkMode(ApplicationTheme::isDark());
#endif
}

void DetachedTerminalWindow::restoreSavedGeometry()
{
    const QByteArray geometry = QSettings().value(QString::fromLatin1(kGeometryKey)).toByteArray();
    if (!geometry.isEmpty() && restoreGeometry(geometry))
        return;
    resize(kDefaultWidth, kDefaultHeight);
}

void DetachedTerminalWindow::closeEvent(QCloseEvent* event)
{
    QSettings().setValue(QString::fromLatin1(kGeometryKey), saveGeometry());

    // Hide the native window *before* the panel (a WebView2-hosted child
    // window) gets reparented out from under it. MainWindow's slot below
    // reparents it synchronously, so without this the still-visible detached
    // window briefly shows its own now-empty layout -- and reparenting a
    // WebView2 child HWND across top-levels forces a native repaint -- which
    // is what read as a flicker right before the window closed.
    hide();

    // MainWindow's slot reparents the panel out of this window's layout
    // synchronously, so by the time QWidget::closeEvent() runs (and
    // WA_DeleteOnClose schedules deletion) this window is already empty.
    emit reattachRequested();
    QWidget::closeEvent(event);
}

void DetachedTerminalWindow::watchWidgetTree(QWidget* widget)
{
    if (!widget)
        return;
    // installEventFilter() de-duplicates, so re-walking a subtree is harmless.
    widget->installEventFilter(this);
    for (QWidget* child : widget->findChildren<QWidget*>())
        child->installEventFilter(this);
}

bool DetachedTerminalWindow::eventFilter(QObject* watched, QEvent* event)
{
    // A header control built after this window was created -- a menu, a button
    // added by a later state change -- must be watched too, or the move and
    // top-resize fallbacks would simply not fire over it.
    if (event->type() == QEvent::ChildAdded) {
        if (auto* child = qobject_cast<QWidget*>(static_cast<QChildEvent*>(event)->child()))
            watchWidgetTree(child);
    }
#ifdef Q_OS_WIN
    if (auto* widget = qobject_cast<QWidget*>(watched);
        widget && event->type() == QEvent::MouseMove && m_content) {
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        const int frameY = NativeWindowFrame::frameThicknessY(hwnd);
        const int topResizeGrip = qCeil(frameY / devicePixelRatioF()) + 2;
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const int headerYPos = mouseEvent->globalPosition().toPoint().y()
                             - m_content->mapToGlobal(QPoint()).y();
        if (!IsZoomed(hwnd) && headerYPos >= 0 && headerYPos <= topResizeGrip)
            setCursor(Qt::SizeVerCursor);
        else
            unsetCursor();
    }
    if (auto* widget = qobject_cast<QWidget*>(watched);
        widget && event->type() == QEvent::Leave) {
        unsetCursor();
    }
    if (m_manualTopResize) {
        if (event->type() == QEvent::MouseMove) {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const int bottom = m_topResizeGeometry.bottom() + 1;
            const int requestedTop = m_topResizeGeometry.top()
                                   + mouseEvent->globalPosition().toPoint().y()
                                   - m_topResizePress.y();
            const int top = qMin(requestedTop, bottom - minimumHeight());
            setGeometry(m_topResizeGeometry.left(), top, m_topResizeGeometry.width(), bottom - top);
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_manualTopResize = false;
                releaseMouse();
                unsetCursor();
                return true;
            }
        }
    }
    if (auto* widget = qobject_cast<QWidget*>(watched);
        widget && event->type() == QEvent::MouseButtonDblClick && m_content) {
        // Only reached when the press was delivered as an ordinary client
        // event (the startHeaderSystemGesture() fallback path). When Windows
        // honors our HTCAPTION instead, it sends WM_NCLBUTTONDBLCLK and
        // DefWindowProc already does this -- the two paths are mutually
        // exclusive, so this cannot double-toggle.
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint panelPos = widget->mapTo(m_content, mouseEvent->position().toPoint());
        if (mouseEvent->button() == Qt::LeftButton
            && !qobject_cast<QAbstractButton*>(widget)
            && panelPos.y() >= 0 && panelPos.y() < m_content->headerHeight()) {
            toggleMaximized();
            return true;
        }
    }
    if (auto* widget = qobject_cast<QWidget*>(watched);
        widget && startHeaderSystemGesture(widget, event)) {
        return true;
    }
#else
    Q_UNUSED(watched);
    Q_UNUSED(event);
#endif
    return QWidget::eventFilter(watched, event);
}

#ifdef Q_OS_WIN
bool DetachedTerminalWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == "windows_generic_MSG") {
        auto* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCCALCSIZE) {
            if (msg->wParam) {
                // The DWM "borderless window" technique, shared with
                // MainWindow's custom title bar -- see NativeWindowFrame.h.
                NativeWindowFrame::applyBorderlessNcCalcSize(
                    msg->hwnd, reinterpret_cast<void*>(msg->lParam));
                *result = 0;
                return true;
            }
        } else if (msg->message == WM_NCHITTEST) {
            *result = hitTestNative(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
            return true;
        } else if (msg->message == WM_SETCURSOR) {
            // The uppermost part of our custom frame is non-client space, so
            // it does not produce Qt mouse-move events. Set its resize cursor
            // here rather than waiting until a drag has already begun.
            POINT cursor{};
            GetCursorPos(&cursor);
            const qintptr hit = hitTestNative(cursor.x, cursor.y);
            LPCWSTR cursorId = nullptr;
            switch (hit) {
            case HTTOP:
            case HTBOTTOM:      cursorId = IDC_SIZENS; break;
            case HTLEFT:
            case HTRIGHT:       cursorId = IDC_SIZEWE; break;
            case HTTOPLEFT:
            case HTBOTTOMRIGHT: cursorId = IDC_SIZENWSE; break;
            case HTTOPRIGHT:
            case HTBOTTOMLEFT:  cursorId = IDC_SIZENESW; break;
            default: break;
            }
            if (cursorId) {
                SetCursor(LoadCursor(nullptr, cursorId));
                *result = TRUE;
                return true;
            }

            // Header widgets do not enable mouse tracking, so a Qt
            // MouseMove-based cursor reset is not guaranteed when the pointer
            // leaves the resize grip. WM_SETCURSOR is delivered for that move
            // regardless; explicitly restore the ordinary header cursor.
            if (m_content) {
                POINT clientPt = cursor;
                ScreenToClient(msg->hwnd, &clientPt);
                const qreal dpr = devicePixelRatioF();
                const QPoint local(qRound(clientPt.x / dpr), qRound(clientPt.y / dpr));
                if (local.y() >= 0 && local.y() < m_content->headerHeight()) {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    *result = TRUE;
                    return true;
                }
            }
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}

qintptr DetachedTerminalWindow::hitTestNative(int x, int y)
{
    const HWND hwnd = reinterpret_cast<HWND>(winId());

    if (!IsZoomed(hwnd)) {
        RECT windowRect{};
        GetWindowRect(hwnd, &windowRect);
        const auto edge = NativeWindowFrame::hitTestResizeBorder(
            QRect(windowRect.left, windowRect.top,
                  windowRect.right - windowRect.left, windowRect.bottom - windowRect.top),
            NativeWindowFrame::frameThicknessX(hwnd),
            NativeWindowFrame::frameThicknessY(hwnd),
            QPoint(x, y));
        if (edge != NativeWindowFrame::ResizeEdge::None)
            return NativeWindowFrame::hitTestCodeForEdge(edge);
    }

    if (m_content) {
        POINT clientPt{ x, y };
        ScreenToClient(hwnd, &clientPt);
        const qreal dpr = devicePixelRatioF();
        const QPoint local(qRound(clientPt.x / dpr), qRound(clientPt.y / dpr));
        if (local.y() >= 0 && local.y() < m_content->headerHeight()) {
            // Buttons (New Terminal, shell dropdown, More, Attach, Close, ...)
            // keep their ordinary click behavior; the bare header strip --
            // its title/status labels and any empty space -- drags the
            // window, matching every native Windows title bar.
            QWidget* child = m_content->childAt(local);
            if (!child || !qobject_cast<QAbstractButton*>(child))
                return HTCAPTION;
        }
    }
    return HTCLIENT;
}

bool DetachedTerminalWindow::startHeaderSystemGesture(QWidget* watched, QEvent* event)
{
    if (!m_content || event->type() != QEvent::MouseButtonPress)
        return false;

    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    if (mouseEvent->button() != Qt::LeftButton)
        return false;

    const QPoint panelPos = watched->mapTo(m_content, mouseEvent->position().toPoint());
    if (panelPos.y() < 0 || panelPos.y() >= m_content->headerHeight())
        return false;

    QWindow* nativeWindow = windowHandle();
    if (!nativeWindow)
        return false;

    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const int frameY = NativeWindowFrame::frameThicknessY(hwnd);
    // Anchor this fallback to the visible terminal header, not
    // frameGeometry(): with our custom NCCALCSIZE, Qt's frame position can
    // include an otherwise invisible caption offset. Measuring from that
    // position made the resize affordance land well below the top edge.
    const int topResizeGrip = qCeil(frameY / devicePixelRatioF()) + 2;
    const int headerYPos = mouseEvent->globalPosition().toPoint().y()
                         - m_content->mapToGlobal(QPoint()).y();
    if (!IsZoomed(hwnd) && headerYPos >= 0 && headerYPos <= topResizeGrip) {
        m_manualTopResize = true;
        m_topResizePress = mouseEvent->globalPosition().toPoint();
        m_topResizeGeometry = geometry();
        setCursor(Qt::SizeVerCursor);
        grabMouse();
        return true;
    }

    // Buttons must still receive their regular clicks. Everything else in the
    // header (title, status, and empty space) is the draggable caption.
    if (qobject_cast<QAbstractButton*>(watched))
        return false;
    return nativeWindow->startSystemMove();
}
#endif
