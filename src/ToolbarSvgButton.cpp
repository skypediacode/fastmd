#include "ToolbarSvgButton.h"

#include <QPainter>
#include <QFontMetrics>
#include <QStyle>
#include <QStyleOptionToolButton>

ToolbarSvgButton::ToolbarSvgButton(const QIcon& icon, double aspectRatio,
                                   bool hasDropdown, QWidget* parent,
                                   bool squareButton, bool allowNaturalWidth)
    : QToolButton(parent)
    , m_icon(icon)
    , m_aspectRatio(aspectRatio > 0.0 ? aspectRatio : 1.0)
    , m_hasDropdown(hasDropdown)
    , m_squareButton(squareButton)
    , m_allowNaturalWidth(allowNaturalWidth)
{
    setAutoRaise(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setProperty("svgToolbarButton", true);
}

ToolbarSvgButton::ToolbarSvgButton(const QString& text, bool hasDropdown, QWidget* parent)
    : QToolButton(parent)
    , m_text(text)
    , m_hasDropdown(hasDropdown)
{
    setAutoRaise(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setProperty("svgToolbarButton", true);
}

void ToolbarSvgButton::refreshIcon(const QIcon& icon)
{
    m_icon = icon;
    update();
}

void ToolbarSvgButton::setPopupOpen(bool open)
{
    if (property("popupOpen").toBool() == open)
        return;
    setProperty("popupOpen", open);
    setDown(open);
    update();
}

void ToolbarSvgButton::setIconAlignment(Qt::Alignment alignment)
{
    m_iconAlignment = alignment;
    update();
}

void ToolbarSvgButton::setIconHorizontalPadding(int padding)
{
    m_iconHorizontalPadding = qMax(0, padding);
    updateGeometry();
    update();
}

QSize ToolbarSvgButton::sizeHint() const
{
    if (m_squareButton)
        return QSize(kButtonHeight, kButtonHeight);
    if (!m_text.isEmpty()) {
        const int dropdownWidth = m_hasDropdown ? kDropdownWidth : 0;
        const int width = qBound(kMinimumWidth,
            QFontMetrics(font()).horizontalAdvance(m_text) + 2 * kTextHorizontalPadding + dropdownWidth,
            kMaximumWidth);
        return QSize(width, kButtonHeight);
    }
    const int iconWidth = qRound(kIconHeight * m_aspectRatio);
    const int dropdownWidth = m_hasDropdown && !m_squareButton ? kDropdownWidth : 0;
    const int naturalWidth = iconWidth + 2 * m_iconHorizontalPadding + dropdownWidth;
    const int width = m_allowNaturalWidth
        ? qMax(kMinimumWidth, naturalWidth)
        : qBound(kMinimumWidth, naturalWidth, kMaximumWidth);
    return QSize(width, kButtonHeight);
}

QSize ToolbarSvgButton::minimumSizeHint() const
{
    return sizeHint();
}

void ToolbarSvgButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.icon = QIcon();
    option.text.clear();
    style()->drawPrimitive(QStyle::PE_PanelButtonTool, &option, &painter, this);

    const int dropdownWidth = m_hasDropdown && !m_squareButton ? kDropdownWidth : 0;
    const QRect notationRect(0, 0, width() - dropdownWidth, height());
    if (!m_text.isEmpty()) {
        const QRect textRect(notationRect.adjusted(kTextHorizontalPadding, 0,
                                                   -kTextHorizontalPadding, 0));
        painter.setPen(palette().color(QPalette::ButtonText));
        painter.setFont(font());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, m_text);
    } else {
        const int iconWidth = qRound(kIconHeight * m_aspectRatio);
        const int x = m_iconAlignment.testFlag(Qt::AlignLeft)
            ? notationRect.left() + m_iconHorizontalPadding
            : notationRect.x() + (m_squareButton
                ? (notationRect.width() - iconWidth) / 2
                : qMax(m_iconHorizontalPadding,
                       (notationRect.width() - iconWidth) / 2));
        const QRect iconRect(x, (height() - kIconHeight) / 2, iconWidth, kIconHeight);
        m_icon.paint(&painter, iconRect, Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }

    if (!m_hasDropdown)
        return;

    painter.setPen(Qt::NoPen);
    painter.setBrush(palette().color(QPalette::ButtonText));
    const int xArrow = width() - (m_squareButton ? 7 : kDropdownWidth - 5);
    const int yArrow = height() / 2 - 2;
    painter.drawPolygon(QPolygon{ QPoint(xArrow, yArrow), QPoint(xArrow + 5, yArrow),
                                  QPoint(xArrow + 2, yArrow + 3) });
}
