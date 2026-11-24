#include "MathSymbolPopup.h"
#include "IconHelper.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QHideEvent>
#include <QToolTip>
#include <QApplication>
#include <QWheelEvent>
#include <QToolButton>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QShowEvent>
#include <QTimer>

namespace {
// Matches QMenu styling from Stylesheet.h
const QColor kLightBg(0xff, 0xff, 0xff);
const QColor kLightBorder(0xd4, 0xd4, 0xd4);
const QColor kLightText(0x22, 0x22, 0x22);
const QColor kLightSeparator(0xe8, 0xe8, 0xe8);

const QColor kDarkBg(0x20, 0x21, 0x22);
const QColor kDarkBorder(0x2a, 0x2b, 0x2c);
const QColor kDarkText(0xbf, 0xbf, 0xbf);
const QColor kDarkSeparator(0x2a, 0x2b, 0x2c);

// Windows accent selection background (QMenu::item:selected)
const QColor kSelectedBg(0x00, 0x78, 0xd4);
const QColor kSelectedText(0xff, 0xff, 0xff);
} // namespace

#include <QScreen>

MathSymbolPopup::MathSymbolPopup(const MathGroup& group, bool dark,
                                  std::function<void(const QString&)> callback,
                                  QWidget* parent,
                                  std::function<bool(const QPoint&)> onOutsideClick,
                                  std::function<void(const QPoint&)> onToolbarHover)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    , m_group(group)
    , m_dark(dark)
    , m_listLayout(group.listLayout)
    , m_matrixLayout(group.id == QStringLiteral("matrices"))
    , m_callback(std::move(callback))
    , m_onOutsideClick(std::move(onOutsideClick))
    , m_onToolbarHover(std::move(onToolbarHover))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_opacityAnim = new QPropertyAnimation(this, "windowOpacity", this);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_geomAnim = new QPropertyAnimation(this, "geometry", this);
    m_geomAnim->setEasingCurve(QEasingCurve::OutCubic);

    const int count = m_group.symbols.size();
    m_columns = m_group.config.columns > 0 ? m_group.config.columns : (m_listLayout ? 1 : qMin(8, qMax(1, count)));
    
    m_contentHeight = contentHeight();
    m_viewHeight = m_contentHeight;
    
    resize(popupWidth(), m_viewHeight);

    // Qt::Popup already grabs mouse/keyboard, but a click landing on a native
    // child window (e.g. the WebView2 preview) can bypass that grab. Watching
    // every application mouse press guarantees the popup still closes.
    qApp->installEventFilter(this);
    m_filterInstalled = true;
}

int MathSymbolPopup::popupWidth() const
{
    if (m_listLayout && m_group.config.popupWidth <= 0) {
        const int iconHeight = qMax(1, m_group.config.cellHeight - 2 * m_group.config.iconInset);
        int widestIcon = 0;
        for (const MathSymbol& symbol : m_group.symbols) {
            if (!isSeparator(symbol)) {
                const double aspectRatio = IconHelper::pngIntrinsicAspectRatio(
                    IconHelper::mathPngResource(symbol.id));
                widestIcon = qMax(widestIcon, qRound(iconHeight * aspectRatio));
            }
        }
        const int iconLeft = 4 + m_group.config.iconInset + 7;
        return qMax(120, iconLeft + widestIcon + m_group.config.iconInset + 8);
    }

    if (hasVariableWidthSecondaryRow()) {
        int widestRow = 0;
        int rowWidth = 0;
        int column = 0;
        bool afterFirstSeparator = false;

        for (const MathSymbol& symbol : m_group.symbols) {
            if (isSeparator(symbol)) {
                widestRow = qMax(widestRow, rowWidth);
                rowWidth = 0;
                column = 0;
                afterFirstSeparator = true;
                continue;
            }

            if (column > 0)
                rowWidth += m_gap;
            rowWidth += cellWidth(symbol, afterFirstSeparator);
            const int columnLimit = afterFirstSeparator && m_group.config.secondaryColumns > 0
                ? m_group.config.secondaryColumns
                : m_columns;
            if (++column == columnLimit) {
                widestRow = qMax(widestRow, rowWidth);
                rowWidth = 0;
                column = 0;
            }
        }
        widestRow = qMax(widestRow, rowWidth);
        return m_pad * 2 + widestRow;
    }

    const int cellWidth = m_group.config.secondaryCellWidth > 0
        ? qMax(m_group.config.cellWidth, m_group.config.secondaryCellWidth)
        : m_group.config.cellWidth;
    const int calcWidth = m_pad * 2 + m_columns * (cellWidth + m_gap) - m_gap;
    return m_group.config.popupWidth > 0 ? m_group.config.popupWidth : calcWidth;
}

bool MathSymbolPopup::hasVariableWidthSecondaryRow() const
{
    return m_group.id == QStringLiteral("accents") || m_group.id == QStringLiteral("integration");
}

int MathSymbolPopup::cellWidth(const MathSymbol& symbol, bool afterFirstSeparator) const
{
    if (hasVariableWidthSecondaryRow() && (afterFirstSeparator || m_group.id == QStringLiteral("integration"))) {
        const int iconHeight = qMax(1, m_group.config.cellHeight - 2 * m_group.config.iconInset);
        const double aspectRatio = IconHelper::pngIntrinsicAspectRatio(
            IconHelper::mathPngResource(symbol.id));
        return qRound(iconHeight * aspectRatio) + 2 * m_group.config.iconInset;
    }

    return afterFirstSeparator && m_group.config.secondaryCellWidth > 0
        ? m_group.config.secondaryCellWidth
        : m_group.config.cellWidth;
}

void MathSymbolPopup::adjustGeometryToScreen(QToolButton* button)
{
    if (!button)
        return;

    QScreen* screen = button->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    const QRect screenRect = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    const QPoint btnTopLeft = button->mapToGlobal(QPoint(0, 0));
    const QPoint btnBottomLeft = button->mapToGlobal(QPoint(0, button->height()));

    const int width = popupWidth();
    const int contentH = m_contentHeight;

    const int availBelow = screenRect.bottom() - btnBottomLeft.y() - 8;
    const int availAbove = btnTopLeft.y() - screenRect.top() - 8;

    int targetY = btnBottomLeft.y();
    int targetHeight = contentH;

    if (contentH <= availBelow) {
        targetY = btnBottomLeft.y();
        targetHeight = contentH;
    } else if (contentH <= availAbove) {
        targetY = btnTopLeft.y() - contentH;
        targetHeight = contentH;
    } else {
        if (availBelow >= availAbove) {
            targetY = btnBottomLeft.y();
            targetHeight = qMax(120, availBelow);
        } else {
            targetY = screenRect.top() + 8;
            targetHeight = qMax(120, availAbove);
        }
    }

    int targetX = btnTopLeft.x();
    if (targetX + width > screenRect.right() - 8) {
        targetX = screenRect.right() - 8 - width;
    }
    if (targetX < screenRect.left() + 8) {
        targetX = screenRect.left() + 8;
    }

    m_viewHeight = targetHeight;
    resize(width, m_viewHeight);
    move(targetX, targetY);
}

QRect MathSymbolPopup::cellRect(int index) const
{
    QRect rect = contentRect(index);
    rect.translate(0, -m_scrollY);
    return rect;
}

QRect MathSymbolPopup::contentRect(int index) const
{
    int y = m_pad;
    int col = 0;
    int rowWidth = 0;
    bool rowHasItems = false;
    bool completedNormalRow = false;
    bool afterFirstSeparator = false;

    for (int i = 0; i <= index; ++i) {
        const bool separator = isSeparator(m_group.symbols.at(i));
        if (separator) {
            if (rowHasItems) {
                y += m_group.config.cellHeight;
                rowHasItems = false;
                col = 0;
                rowWidth = 0;
            }
            completedNormalRow = false;
            afterFirstSeparator = true;
            const QRect rect(0, y, popupWidth(), m_separatorHeight);
            if (i == index)
                return rect;
            y += m_separatorHeight;
            continue;
        }

        if (!rowHasItems && completedNormalRow) {
            if (!m_listLayout)
                y += m_gap;
            completedNormalRow = false;
        }

        const bool fullRowLayout = m_listLayout || m_group.id == QStringLiteral("integration");
        const int widthForSymbol = cellWidth(m_group.symbols.at(i), afterFirstSeparator);
        const int x = fullRowLayout ? m_pad : m_pad + rowWidth + (col > 0 ? m_gap : 0);
        const int width = fullRowLayout
            ? qMax(1, popupWidth() - x - m_pad)
            : widthForSymbol;
        const QRect rect(x, y, width, m_group.config.cellHeight);
        if (i == index)
            return rect;

        rowHasItems = true;
        const int columnLimit = afterFirstSeparator && m_group.config.secondaryColumns > 0
            ? m_group.config.secondaryColumns
            : m_columns;
        if (++col == columnLimit) {
            y += m_group.config.cellHeight;
            rowHasItems = false;
            col = 0;
            rowWidth = 0;
            completedNormalRow = true;
        } else {
            rowWidth += (col > 1 ? m_gap : 0) + widthForSymbol;
        }
    }

    return {};
}

int MathSymbolPopup::contentHeight() const
{
    if (m_group.symbols.isEmpty())
        return m_pad * 2;
    return contentRect(m_group.symbols.size() - 1).bottom() + 1 + m_pad;
}

bool MathSymbolPopup::isSeparator(const MathSymbol& symbol) const
{
    return symbol.id.startsWith(QStringLiteral("separator"));
}

int MathSymbolPopup::indexAt(const QPoint& pos) const
{
    for (int i = 0; i < m_group.symbols.size(); ++i) {
        if (cellRect(i).contains(pos))
            return i;
    }
    return -1;
}

void MathSymbolPopup::paintEvent(QPaintEvent*)
{
    const QColor bg = m_dark ? kDarkBg : kLightBg;
    const QColor border = m_dark ? kDarkBorder : kLightBorder;
    const QColor text = m_dark ? kDarkText : kLightText;
    const QColor separatorColor = m_dark ? kDarkSeparator : kLightSeparator;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath outline;
    outline.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), m_radius, m_radius);
    p.fillPath(outline, bg);
    p.setPen(border);
    p.drawPath(outline);

    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(m_listLayout ? 13 : 16);
    p.setFont(f);

    for (int i = 0; i < m_group.symbols.size(); ++i) {
        const QRect r = cellRect(i);
        const MathSymbol& symbol = m_group.symbols.at(i);

        if (isSeparator(symbol)) {
            p.setPen(separatorColor);
            const int margin = m_group.config.separatorMargin;
            p.drawLine(margin, r.center().y(), width() - margin - 1, r.center().y());
            continue;
        }

        const bool isHovered = (i == m_hoverIndex);
        if (isHovered) {
            QPainterPath cellPath;
            cellPath.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            p.fillPath(cellPath, kSelectedBg);
        }

        const QColor glyphColor = isHovered ? kSelectedText : text;
        const QIcon generatedIcon = IconHelper::generatedMathSvgIcon(symbol.id, glyphColor);
        if (!generatedIcon.isNull()) {
            const int inset = m_group.config.iconInset;
            const double AR = IconHelper::pngIntrinsicAspectRatio(IconHelper::mathPngResource(symbol.id));
            const int availH = r.height() - inset * 2;
            const int iconH = qMax(1, availH);
            const int iconW = qMin(r.width() - inset * 2, qRound(iconH * AR));
            
            const int iconX = m_listLayout || m_group.id == QStringLiteral("integration")
                ? r.left() + inset + 7
                : r.center().x() - iconW / 2;
            const QRect iconRect(iconX, r.center().y() - iconH / 2, iconW, iconH);
            generatedIcon.paint(&p, iconRect, Qt::AlignCenter);
        } else {
            p.setPen(glyphColor);
            p.setFont(f);
            p.drawText(r, Qt::AlignCenter, symbol.insertLatex);
        }
    }

    if (m_contentHeight > m_viewHeight) {
        const int scrollbarHeight = qMax(20, m_viewHeight * m_viewHeight / m_contentHeight);
        const int scrollbarY = m_scrollY * (m_viewHeight - scrollbarHeight) / (m_contentHeight - m_viewHeight);
        p.fillRect(width() - 8, scrollbarY + 2, 6, scrollbarHeight - 4, m_dark ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 40));
    }
}

void MathSymbolPopup::mouseMoveEvent(QMouseEvent* e)
{
    const int idx = indexAt(e->pos());
    if (idx != m_hoverIndex && (idx < 0 || !isSeparator(m_group.symbols.at(idx)))) {
        m_hoverIndex = idx;
        update();
    } else if (idx >= 0 && isSeparator(m_group.symbols.at(idx)) && m_hoverIndex != -1) {
        m_hoverIndex = -1;
        update();
    }
    if (idx >= 0)
        QToolTip::showText(e->globalPosition().toPoint(), m_group.symbols.at(idx).tooltip, this);
    else
        QToolTip::hideText();
}

void MathSymbolPopup::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int idx = indexAt(e->pos());
    if (idx < 0 || isSeparator(m_group.symbols.at(idx)))
        return;
    auto callback = m_callback;
    const QString latex = m_group.symbols.at(idx).insertLatex;
    callback(latex);
    QTimer::singleShot(0, this, &QWidget::close);
}

void MathSymbolPopup::leaveEvent(QEvent*)
{
    m_hoverIndex = -1;
    update();
}

void MathSymbolPopup::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

void MathSymbolPopup::wheelEvent(QWheelEvent* e)
{
    if (m_contentHeight <= m_viewHeight)
        return;
    m_scrollY -= e->angleDelta().y();
    m_scrollY = qBound(0, m_scrollY, m_contentHeight - m_viewHeight);
    update();
    
    // Update hover
    const int idx = indexAt(e->position().toPoint());
    if (idx != m_hoverIndex && (idx < 0 || !isSeparator(m_group.symbols.at(idx)))) {
        m_hoverIndex = idx;
    } else if (idx < 0 || isSeparator(m_group.symbols.at(idx))) {
        m_hoverIndex = -1;
    }
}

void MathSymbolPopup::hideEvent(QHideEvent* event)
{
    if (m_filterInstalled) {
        qApp->removeEventFilter(this);
        m_filterInstalled = false;
    }
    QToolTip::hideText();
    QWidget::hideEvent(event);
    emit closed();
}

void MathSymbolPopup::showEvent(QShowEvent* event)
{
    const QRect targetRect = geometry();
    if (targetRect.height() > 10) {
        QRect startRect = targetRect;
        startRect.setTop(targetRect.top() - 12);
        startRect.setBottom(targetRect.top() + static_cast<int>(targetRect.height() * 0.85));

        setGeometry(startRect);
        setWindowOpacity(0.0);

        m_opacityAnim->stop();
        m_opacityAnim->setDuration(150);
        m_opacityAnim->setStartValue(0.0);
        m_opacityAnim->setEndValue(1.0);
        m_opacityAnim->start();

        m_geomAnim->stop();
        m_geomAnim->setDuration(150);
        m_geomAnim->setStartValue(startRect);
        m_geomAnim->setEndValue(targetRect);
        m_geomAnim->start();
    }
    QWidget::showEvent(event);
}

bool MathSymbolPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseMove && isVisible()) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = me->globalPosition().toPoint();
        if (m_onToolbarHover)
            m_onToolbarHover(globalPos);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = me->globalPosition().toPoint();
        if (!geometry().contains(globalPos) && isVisible()) {
            // Let the owner decide first: if this click landed on a different
            // Math Toolbar group button, it directly opens that popup (closing
            // this one as part of the same call) and the click must not also
            // reach the button afterwards, or it would just toggle straight
            // back off. Only a plain "click elsewhere" falls through to a
            // simple close, leaving the event free to reach its real target.
            if (m_onOutsideClick && m_onOutsideClick(globalPos))
                return true;
            close();
        }
    }
    return QWidget::eventFilter(watched, event);
}
