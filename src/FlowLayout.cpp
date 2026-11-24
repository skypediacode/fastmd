#include "FlowLayout.h"

#include <QWidget>

FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::FlowLayout(int margin, int hSpacing, int vSpacing)
    : m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    QLayoutItem* item;
    while ((item = takeAt(0)))
        delete item;
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_itemList.append(item);
}

int FlowLayout::horizontalSpacing() const
{
    if (m_hSpace >= 0)
        return m_hSpace;
    return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
    if (m_vSpace >= 0)
        return m_vSpace;
    return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

int FlowLayout::count() const
{
    return m_itemList.size();
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return m_itemList.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    if (index < 0 || index >= m_itemList.size())
        return nullptr;
    return m_itemList.takeAt(index);
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_itemList)
        size = size.expandedTo(item->minimumSize());

    const QMargins margins = contentsMargins();
    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    const QRect effectiveRect = rect.adjusted(left, top, -right, -bottom);
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;

    for (QLayoutItem* item : m_itemList) {
        const QWidget* widget = item->widget();
        int spaceX = horizontalSpacing();
        if (spaceX == -1)
            spaceX = widget ? widget->style()->layoutSpacing(
                QSizePolicy::ToolButton, QSizePolicy::ToolButton, Qt::Horizontal) : 0;
        int spaceY = verticalSpacing();
        if (spaceY == -1)
            spaceY = widget ? widget->style()->layoutSpacing(
                QSizePolicy::ToolButton, QSizePolicy::ToolButton, Qt::Vertical) : 0;

        int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {
            x = effectiveRect.x();
            y = y + lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;
            lineHeight = 0;
        }

        if (!testOnly)
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));

        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }
    return y + lineHeight - rect.y() + bottom;
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    QObject* parentObj = parent();
    if (!parentObj)
        return -1;
    if (parentObj->isWidgetType()) {
        auto* widget = static_cast<QWidget*>(parentObj);
        return widget->style()->pixelMetric(pm, nullptr, widget);
    }
    return static_cast<QLayout*>(parentObj)->spacing();
}
