#pragma once

#include <QAbstractItemView>

// QAbstractScrollArea sends pointer events to its viewport, not to the view
// widget. Read-only navigation views must therefore make the viewport the
// explicit owner of the ordinary arrow cursor. Leaving it inherited lets an
// I-beam inherited during native hit-test transitions remain in effect.
inline void setReadOnlyItemViewCursor(QAbstractItemView* view)
{
    Q_ASSERT(view);
    view->viewport()->setCursor(Qt::ArrowCursor);
}
