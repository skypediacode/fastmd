#pragma once

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QPointer>
#include <QWidget>
#include <QWindow>

// Qt keeps one cursor per native window, and a native child whose own cursor
// is unset shows its parent window's. winId() on an alien child promotes the
// surrounding hierarchy to native windows, but the window that held the
// hovered alien widget keeps the cursor that widget gave it: the widget now
// lives in a new child window, so its Leave only resets the child. Every
// unset child -- menu bar, toolbar, tab bar, status bar -- then shows the
// stale cursor for good.
//
// Call right after a winId() that may have promoted the hierarchy. Each native
// window's cursor is re-derived the way Qt's enter/leave dispatch
// (qt_qpa_set_cursor) would have set it, on the first mouse move afterwards:
// until then the platform's window-under-mouse still names the pre-promotion
// window, and correcting it earlier would change the visible cursor under a
// pointer that has not moved.
class NativeWindowCursorSync final : public QObject {
public:
    static void schedule(QWidget* widget)
    {
        QWidget* top = widget ? widget->window() : nullptr;
        if (!top || top->findChild<QObject*>(objectNameValue(), Qt::FindDirectChildrenOnly))
            return;
        new NativeWindowCursorSync(top);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        // Widget-level moves arrive after QWidgetWindow has dispatched the
        // Enter/Leave for this position, so Qt's own state is current.
        if (event->type() == QEvent::MouseMove && watched->isWidgetType()) {
            qApp->removeEventFilter(this);
            setObjectName(QString());
            if (m_top)
                sync(m_top);
            deleteLater();
        }
        return false;
    }

private:
    explicit NativeWindowCursorSync(QWidget* top) : QObject(top), m_top(top)
    {
        setObjectName(objectNameValue());
        qApp->installEventFilter(this);
    }

    static QString objectNameValue() { return QStringLiteral("fastmdNativeWindowCursorSync"); }

    static bool ownsCursor(const QWidget* w)
    {
        return w->isWindow() || w->testAttribute(Qt::WA_SetCursor);
    }

    static void sync(QWidget* top)
    {
        // The native window under the pointer keeps the cursor its hovered
        // widget resolves to; every other native window gets Qt's default.
        QWidget* owner = QApplication::widgetAt(QCursor::pos());
        if (owner && owner->window() != top)
            owner = nullptr;
        while (owner && !owner->internalWinId() && !ownsCursor(owner) && owner->parentWidget())
            owner = owner->parentWidget();
        QWidget* nativeOwner = nullptr;
        if (owner && ownsCursor(owner))
            nativeOwner = owner->internalWinId() ? owner : owner->nativeParentWidget();

        const auto apply = [owner, nativeOwner](QWidget* w) {
            QWindow* handle = w->windowHandle();
            if (!handle)
                return;
            if (w == nativeOwner)
                handle->setCursor(owner->cursor());
            else if (ownsCursor(w))
                handle->setCursor(w->cursor());
            else
                handle->unsetCursor();
        };
        for (QWidget* child : top->findChildren<QWidget*>()) {
            if (child->internalWinId() && !child->isWindow() && child->window() == top)
                apply(child);
        }
        apply(top);
    }

    QPointer<QWidget> m_top;
};
