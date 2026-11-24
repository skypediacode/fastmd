#include "CommandPaletteLauncher.h"
#include "DarkTheme.h"
#include "IconHelper.h"
#include "ThemePalette.h"

#include <QEnterEvent>
#include <QFontInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTransform>

namespace {

constexpr int LeftInset  = 10;
constexpr int IconGap    = 5;

// The search-field shape shared by the launcher and the popup's search row:
// the toolbar buttons' radius, since the box shares their height, with the
// bottom corners left square where the popup's list continues below (its own
// bottom corners use the same radius).
QPainterPath boxPath(const QRectF& f, bool roundBottom)
{
    constexpr qreal r = ThemeCard::ButtonRadius - 0.5;
    constexpr qreal d = 2 * r;
    QPainterPath path;
    path.moveTo(f.left(), f.bottom() - (roundBottom ? r : 0));
    path.lineTo(f.left(), f.top() + r);
    path.arcTo(QRectF(f.left(), f.top(), d, d), 180, -90);
    path.lineTo(f.right() - r, f.top());
    path.arcTo(QRectF(f.right() - d, f.top(), d, d), 90, -90);
    if (roundBottom) {
        path.lineTo(f.right(), f.bottom() - r);
        path.arcTo(QRectF(f.right() - d, f.bottom() - d, d, d), 0, -90);
        path.lineTo(f.left() + r, f.bottom());
        path.arcTo(QRectF(f.left(), f.bottom() - d, d, d), 270, -90);
    } else {
        path.lineTo(f.bottomRight());
        path.lineTo(f.bottomLeft());
    }
    path.closeSubpath();
    return path;
}

} // namespace

QPixmap CommandPalette::searchIcon(const QColor& color, int size)
{
    // search=0xE8B6. Mirrored horizontally: Windows' own search box uses a
    // magnifier with the handle at the bottom-left, opposite of the Material
    // glyph's default bottom-right handle.
    const QPixmap glyph = IconHelper::materialIcon(QChar(0xe8b6), color, size).pixmap(size, size);
    return glyph.transformed(QTransform().scale(-1, 1));
}

int CommandPalette::textBaseline(int boxTop, int boxHeight, const QFontMetrics& fm)
{
    return boxTop + (boxHeight - fm.height() + 1) / 2 + fm.ascent();
}

int CommandPalette::iconTop(int boxHeight)
{
    return (boxHeight - IconSize) / 2 + 1 - SearchContentYOffset;
}

void CommandPalette::paintSearchBox(QPainter& painter, const QRect& rect,
                                    const QColor& background, bool dark, bool roundBottom)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // The same thin border QListWidget#commandPaletteList continues below the
    // popup's search row.
    const QColor border = dark ? DarkTheme::color(DarkTheme::Border) : QColor(0xd4, 0xd4, 0xd4);
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(background);
    painter.drawPath(boxPath(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), roundBottom));
    painter.restore();
}

CommandPaletteLauncher::CommandPaletteLauncher(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("commandPaletteLauncher"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover, true);
    setToolTip(tr("Search commands  (F1)"));
    setDarkMode(false);
}

void CommandPaletteLauncher::setDarkMode(bool dark)
{
    m_dark = dark;
    m_background = dark ? DarkTheme::color(DarkTheme::Elevated) : QColor(0xff, 0xff, 0xff);
    m_textColor  = dark ? QColor(0x99, 0x99, 0x99) : QColor(0x70, 0x70, 0x70);
    refreshIcon();
    update();
}

void CommandPaletteLauncher::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    update();
}

void CommandPaletteLauncher::refreshIcon()
{
    m_searchIcon = CommandPalette::searchIcon(m_textColor, CommandPalette::IconSize);
}

QSize CommandPaletteLauncher::sizeHint() const
{
    return QSize(CommandPalette::PreferredWidth, CommandPalette::BoxHeight);
}

QSize CommandPaletteLauncher::minimumSizeHint() const
{
    return QSize(CommandPalette::MinimumWidth, CommandPalette::BoxHeight);
}

void CommandPaletteLauncher::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QRect box(0, (height() - CommandPalette::BoxHeight) / 2,
                    width(), CommandPalette::BoxHeight);

    QColor background = m_background;
    if (m_active || m_hovered)
        background = m_dark ? background.lighter(115) : background.darker(103);
    CommandPalette::paintSearchBox(painter, box, background, m_dark);

    const int iconY = box.top() + CommandPalette::iconTop(box.height());
    painter.drawPixmap(LeftInset, iconY, m_searchIcon);

    const int textLeft = LeftInset + CommandPalette::IconSize + IconGap;
    const int textWidth = box.width() - textLeft - 8;
    painter.setPen(m_textColor);
    QFont font = painter.font();
    font.setPixelSize(QFontInfo(font).pixelSize() + 2);
    painter.setFont(font);
    const QFontMetrics fm = painter.fontMetrics();
    const QString label = tr("Click to search commands (F1)");
    const int baseline = CommandPalette::textBaseline(box.top(), box.height(), fm)
                       - CommandPalette::SearchContentYOffset;
    painter.drawText(textLeft, baseline,
                     fm.elidedText(label, Qt::ElideRight, textWidth));
}

void CommandPaletteLauncher::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit activated();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CommandPaletteLauncher::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Space) {
        emit activated();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CommandPaletteLauncher::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void CommandPaletteLauncher::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}
