#include "ThemedSplitter.h"
#include "ThemePalette.h"

#include <QChildEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSplitterHandle>

namespace {

class ThemedSplitterHandle : public QSplitterHandle {
public:
    ThemedSplitterHandle(Qt::Orientation orientation, QSplitter* parent,
                         ThemedSplitter::HandleStyle style)
        : QSplitterHandle(orientation, parent)
        , m_style(style)
    {
        setAttribute(Qt::WA_Hover, m_style == ThemedSplitter::HandleStyle::Gap);
    }

    void setDarkMode(bool dark)
    {
        m_dark = dark;
        update();
    }

    // QSplitter lays a handle out around its contents rect, so margins widen
    // the drag target without widening the gap (Qt's own "tiny mode" for
    // handles under 5px works the same way).
    void setHitSlop(int slop)
    {
        m_slop = slop;
        if (orientation() == Qt::Horizontal)
            setContentsMargins(slop, 0, slop, 0);
        else
            setContentsMargins(0, slop, 0, slop);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        if (m_slop <= 0) {
            QSplitterHandle::resizeEvent(event);
            return;
        }
        // Skip the base class, whose tiny mode would reset the margins; keep
        // the widened handle above the neighbouring panes it overlaps.
        QWidget::resizeEvent(event);
        raise();
    }

    void moveEvent(QMoveEvent* event) override
    {
        QSplitterHandle::moveEvent(event);
        if (m_slop > 0)
            raise();
    }

    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave)
            update();
        return QSplitterHandle::event(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressed = true;
            update();
        }
        QSplitterHandle::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressed = false;
            update();
        }
        QSplitterHandle::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const bool vertical = orientation() == Qt::Horizontal;
        if (m_style == ThemedSplitter::HandleStyle::Divider) {
            const QColor line = ThemePalette::color(ThemePalette::Border, m_dark);
            if (vertical)
                painter.fillRect(QRect(width() / 2, 0, 1, height()), line);
            else
                painter.fillRect(QRect(0, height() / 2, width(), 1), line);
            return;
        }

        // The grip only hints that the gap can be dragged; it stays well below
        // the contrast of the card borders until the pointer is over it.
        QColor dot = ThemePalette::color(ThemePalette::TextSecondary, m_dark);
        dot.setAlphaF(m_pressed ? 0.95f : underMouse() ? 0.75f : 0.35f);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(dot);
        constexpr qreal diameter = 2.0;
        constexpr qreal pitch = 5.0;
        const QPointF center = QRectF(rect()).center();
        for (int i = -1; i <= 1; ++i) {
            const QPointF c = vertical ? QPointF(center.x(), center.y() + i * pitch)
                                       : QPointF(center.x() + i * pitch, center.y());
            painter.drawEllipse(c, diameter / 2, diameter / 2);
        }
    }

private:
    ThemedSplitter::HandleStyle m_style;
    int m_slop = 0;
    bool m_dark = false;
    bool m_pressed = false;
};

}

ThemedSplitter::ThemedSplitter(Qt::Orientation orientation, HandleStyle style,
                               int handleWidthPx, QWidget* parent)
    : QSplitter(orientation, parent)
    , m_style(style)
    , m_handleWidthPx(handleWidthPx)
{
    setHandleWidth(m_handleWidthPx);
}

void ThemedSplitter::setDarkMode(bool dark)
{
    m_dark = dark;
    for (int i = 1; i < count(); ++i) {
        if (auto* h = dynamic_cast<ThemedSplitterHandle*>(handle(i)))
            h->setDarkMode(dark);
    }
    update();
}

void ThemedSplitter::setPaintsCards(bool paints)
{
    m_paintsCards = paints;
    const int inset = paints ? ThemeCard::Inset : 0;
    setContentsMargins(inset, inset, inset, inset);
    update();
}

void ThemedSplitter::setHandleHitSlop(int px)
{
    m_hitSlop = px;
    for (int i = 1; i < count(); ++i) {
        if (auto* h = dynamic_cast<ThemedSplitterHandle*>(handle(i)))
            h->setHitSlop(px);
    }
    refresh();
}

void ThemedSplitter::reassertHandleWidth()
{
    setHandleWidth(m_handleWidthPx);
}

QSplitterHandle* ThemedSplitter::createHandle()
{
    auto* h = new ThemedSplitterHandle(orientation(), this, m_style);
    h->setDarkMode(m_dark);
    if (m_hitSlop > 0)
        h->setHitSlop(m_hitSlop);
    return h;
}

void ThemedSplitter::childEvent(QChildEvent* event)
{
    QSplitter::childEvent(event);
    if (!m_paintsCards || !event->child()->isWidgetType()
        || qobject_cast<QSplitterHandle*>(event->child()))
        return;
    if (event->added())
        event->child()->installEventFilter(this);
    else if (event->removed())
        event->child()->removeEventFilter(this);
}

bool ThemedSplitter::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::DynamicPropertyChange:
        update();
        break;
    default:
        break;
    }
    return QSplitter::eventFilter(watched, event);
}

void ThemedSplitter::paintEvent(QPaintEvent* event)
{
    QSplitter::paintEvent(event);
    if (!m_paintsCards)
        return;
    QPainter painter(this);
    for (int i = 0; i < count(); ++i) {
        QWidget* pane = widget(i);
        if (!pane || pane->isHidden())
            continue;
        const QVariant role = pane->property(ThemeCard::RoleProperty);
        if (!role.isValid())
            continue;
        const QRect card = pane->geometry().adjusted(-ThemeCard::Inset, -ThemeCard::Inset,
                                                     ThemeCard::Inset, ThemeCard::Inset);
        if (card.intersects(event->rect()))
            ThemeCard::paint(painter, card, ThemePalette::Role(role.toInt()), m_dark);
    }
}
