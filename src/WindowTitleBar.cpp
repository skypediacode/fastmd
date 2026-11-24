#include "WindowTitleBar.h"

#include "ThemePalette.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QMenuBar>
#include <QPointer>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOption>
#include <QToolButton>

#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace TitleBarInternal {

namespace {
// Segoe Fluent Icons is Windows 11's caption glyph font; Segoe MDL2 Assets is
// its Windows 10 predecessor and carries the same code points. Painting the
// glyphs (rather than drawing lines by hand) is what keeps them identical to
// every other window's at every scale factor.
constexpr char16_t kGlyphMinimize = 0xE921;
constexpr char16_t kGlyphMaximize = 0xE922;
constexpr char16_t kGlyphRestore  = 0xE923;
constexpr char16_t kGlyphClose    = 0xE8BB;

// Resolved to exactly ONE family, and cached.
//
// Naming both families on a single QFont is what a QFont::setFamilies() call
// means to Qt: the extra names are prepended to that font's fallback list, and
// building the list makes Qt open and populate every installed family. That is
// ~160 ms on the GUI thread, paid inside the first caption button's drawText()
// -- the same defect, and the same cost, that ef0fd02 removed from the editor's
// font (see STARTUP.md). Picking the one family that is actually installed
// keeps the glyphs identical and builds no fallback list at all.
const QFont& glyphFont()
{
    static const QFont font = []() {
        QFont f;
        // Segoe Fluent Icons is Windows 11's caption glyph font; Segoe MDL2
        // Assets is its Windows 10 predecessor and carries the same code
        // points. hasFamily() reads the family list the startup warm-up worker
        // has already enumerated; it does not realize a font engine.
        const QString fluent = QStringLiteral("Segoe Fluent Icons");
        f.setFamily(QFontDatabase::hasFamily(fluent)
                        ? fluent
                        : QStringLiteral("Segoe MDL2 Assets"));
        f.setPixelSize(10);
        return f;
    }();
    return font;
}

QColor captionGlyphColor(bool dark, bool windowActive)
{
    if (!windowActive)
        return dark ? QColor(0x7A, 0x7A, 0x7A) : QColor(0x9A, 0x9A, 0x9A);
    return dark ? QColor(0xE6, 0xE6, 0xE6) : QColor(0x1A, 0x1A, 0x1A);
}

QColor captionHoverFill(bool dark, bool pressed)
{
    if (dark)
        return pressed ? QColor(255, 255, 255, 26) : QColor(255, 255, 255, 18);
    return pressed ? QColor(0, 0, 0, 30) : QColor(0, 0, 0, 20);
}

// Rasterized from the bundled SVG at the exact device size and then tinted, so
// its 1px strokes land on whole device pixels at every scale factor. Cached:
// the buttons repaint on every hover change. QImage rather than QPixmap so the
// static cache may safely outlive QApplication.
QImage codiconImage(const QString& name, const QColor& color, qreal dpr)
{
    static QHash<QString, QImage> cache;
    const int px = qRound(WindowTitleBar::kLayoutIconSize * dpr);
    const QString key = name + QLatin1Char(':') + QString::number(px) + QLatin1Char(':')
                      + QString::number(color.rgba(), 16);
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;

    QImageReader reader(QStringLiteral(":/icons/codicons/%1.svg").arg(name));
    reader.setScaledSize(QSize(px, px));
    QImage image = reader.read();
    if (!image.isNull()) {
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), color);
        image.setDevicePixelRatio(dpr);
    }
    cache.insert(key, image);
    return image;
}
}

// A flat, hand-painted caption button. QSS is not used here: the close
// button's hover fill is a fixed accent that must not follow the app's button
// palette, and the maximize button additionally paints hover/pressed states
// that never arrive as Qt events (see WindowTitleBar::setMaximizeButtonHovered).
class CaptionButton final : public QAbstractButton {
public:
    CaptionButton(char16_t glyph, bool isClose, QWidget* parent)
        : QAbstractButton(parent), m_glyph(QChar(glyph)), m_isClose(isClose)
    {
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(WindowTitleBar::kButtonWidth, WindowTitleBar::kHeight);
        setAttribute(Qt::WA_Hover, true);
    }

    void setGlyph(char16_t glyph)
    {
        const QChar next(glyph);
        if (m_glyph == next)
            return;
        m_glyph = next;
        update();
    }

    void setDarkMode(bool dark)
    {
        if (m_dark == dark)
            return;
        m_dark = dark;
        update();
    }

    void setWindowActive(bool active)
    {
        if (m_windowActive == active)
            return;
        m_windowActive = active;
        update();
    }

    void setExternalHovered(bool hovered)
    {
        if (m_externalHovered == hovered)
            return;
        m_externalHovered = hovered;
        update();
    }

    void setExternalPressed(bool pressed)
    {
        if (m_externalPressed == pressed)
            return;
        m_externalPressed = pressed;
        update();
    }

protected:
    void enterEvent(QEnterEvent* event) override { QAbstractButton::enterEvent(event); update(); }
    void leaveEvent(QEvent* event) override { QAbstractButton::leaveEvent(event); update(); }

    void paintEvent(QPaintEvent*) override
    {
        const bool pressed = m_externalPressed || isDown();
        const bool hovered = m_externalHovered || underMouse() || pressed;

        QPainter painter(this);
        if (hovered) {
            const QColor fill = m_isClose
                ? (pressed ? QColor(0xC5, 0x42, 0x36) : QColor(0xC4, 0x2B, 0x1C))
                : captionHoverFill(m_dark, pressed);
            painter.fillRect(rect(), fill);
        }

        QColor glyphColor = captionGlyphColor(m_dark, m_windowActive);
        if (m_isClose && hovered)
            glyphColor = QColor(0xFF, 0xFF, 0xFF);

        painter.setPen(glyphColor);
        painter.setFont(glyphFont());
        painter.drawText(rect(), Qt::AlignCenter, QString(m_glyph));
    }

private:
    QChar m_glyph;
    bool m_isClose = false;
    bool m_dark = false;
    bool m_windowActive = true;
    bool m_externalHovered = false;
    bool m_externalPressed = false;
};

// A VS Code-style layout control. It owns no state: checked and enabled are
// read from the action, so it cannot drift from the menus and shortcuts that
// drive the same action.
class LayoutToggleButton final : public QAbstractButton {
public:
    LayoutToggleButton(QAction* action, const QString& onIcon, const QString& offIcon,
                       const QString& toolTip, const QKeySequence& shortcutHint, QWidget* parent)
        : QAbstractButton(parent), m_action(action), m_onIcon(onIcon), m_offIcon(offIcon),
          m_toolTip(toolTip), m_shortcutHint(shortcutHint)
    {
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(WindowTitleBar::kLayoutButtonSize, WindowTitleBar::kLayoutButtonSize);
        setAttribute(Qt::WA_Hover, true);
        setAccessibleName(toolTip);
        connect(this, &QAbstractButton::clicked, action, &QAction::trigger);
        connect(action, &QAction::changed, this, &LayoutToggleButton::sync);
        sync();
    }

    void sync()
    {
        if (!m_action)
            return;
        setEnabled(m_action->isEnabled());
        const QKeySequence shortcut = m_action->shortcut().isEmpty()
            ? m_shortcutHint
            : m_action->shortcut();
        setToolTip(shortcut.isEmpty()
            ? m_toolTip
            : QStringLiteral("%1 (%2)").arg(m_toolTip, shortcut.toString(QKeySequence::NativeText)));
        update();
    }

    void setDarkMode(bool dark)
    {
        if (m_dark == dark)
            return;
        m_dark = dark;
        update();
    }

    void setWindowActive(bool active)
    {
        if (m_windowActive == active)
            return;
        m_windowActive = active;
        update();
    }

protected:
    void enterEvent(QEnterEvent* event) override { QAbstractButton::enterEvent(event); update(); }
    void leaveEvent(QEvent* event) override { QAbstractButton::leaveEvent(event); update(); }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (isEnabled() && (underMouse() || isDown())) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(captionHoverFill(m_dark, isDown()));
            painter.drawRoundedRect(QRectF(rect()), ThemeCard::ControlRadius, ThemeCard::ControlRadius);
        }

        QColor color = captionGlyphColor(m_dark, m_windowActive);
        if (!isEnabled())
            color.setAlphaF(0.4f);
        const bool checked = m_action && m_action->isChecked();
        const QImage icon = codiconImage(checked ? m_onIcon : m_offIcon, color,
                                         devicePixelRatioF());
        const qreal inset = (WindowTitleBar::kLayoutButtonSize - WindowTitleBar::kLayoutIconSize) / 2.0;
        painter.drawImage(QPointF(inset, inset), icon);
    }

private:
    QPointer<QAction> m_action;
    QString m_onIcon;
    QString m_offIcon;
    QString m_toolTip;
    QKeySequence m_shortcutHint;
    bool m_dark = false;
    bool m_windowActive = true;
};

// The app icon at the far left. A widget rather than a QLabel so a press can
// raise the system menu the way a native caption's icon does, and so the
// pixmap always follows the window's own icon.
class WindowIcon final : public QWidget {
public:
    explicit WindowIcon(QWidget* parent) : QWidget(parent)
    {
        // Full bar height, fixed box width: the pixmap stays kIconSize and is
        // centered, so growing the bar never scales the icon.
        setFixedWidth(WindowTitleBar::kIconBox);
        setCursor(Qt::ArrowCursor);
    }

    std::function<void(const QPoint&)> onPressed;

    // Where the icon's first visible column falls inside this widget: the box
    // inset plus whatever transparent margin the icon itself carries.
    int inkLeft() const
    {
        return (width() - WindowTitleBar::kIconSize) / 2
             + WindowTitleBar::opaqueLeftInset(currentIcon().pixmap(
                   QSize(WindowTitleBar::kIconSize, WindowTitleBar::kIconSize),
                   devicePixelRatioF()));
    }

    QIcon currentIcon() const
    {
        QWidget* top = window();
        const QIcon icon = top ? top->windowIcon() : QIcon();
        return icon.isNull() ? qApp->windowIcon() : icon;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const QIcon icon = currentIcon();
        if (icon.isNull())
            return;
        QPainter painter(this);
        const QRect target((width() - WindowTitleBar::kIconSize) / 2,
                           (height() - WindowTitleBar::kIconSize) / 2,
                           WindowTitleBar::kIconSize, WindowTitleBar::kIconSize);
        icon.paint(&painter, target, Qt::AlignCenter, QIcon::Normal, QIcon::Off);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && onPressed) {
            onPressed(mapTo(window(), QPoint(0, height())));
            return;
        }
        QWidget::mousePressEvent(event);
    }

};

} // namespace TitleBarInternal

using TitleBarInternal::CaptionButton;
using TitleBarInternal::LayoutToggleButton;
using TitleBarInternal::WindowIcon;

namespace {
// Clearance the centred title keeps from the menu bar on one side and the
// window buttons on the other, so it never reads as touching either.
constexpr int kTitleGap = 16;
}

WindowTitleBar::WindowTitleBar(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("windowTitleBar"));
    // A plain QWidget ignores a QSS background unless it is told to draw one.
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(kHeight);

    auto* layout = new QHBoxLayout(this);
    m_layout = layout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_icon = new WindowIcon(this);
    m_icon->onPressed = [this](const QPoint& windowPos) { emit systemMenuRequested(windowPos); };
    layout->addWidget(m_icon, 0);

    m_menuBar = new QMenuBar(this);
    m_menuBar->setObjectName(QStringLiteral("titleBarMenuBar"));
    // Without this the menu bar claims the whole stretch and nothing is left
    // to drag the window by.
    m_menuBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    m_menuBar->setNativeMenuBar(false);
    // Aligned rather than stretched: QMenuBar lays its items out from its own
    // top edge, so letting it fill a taller bar would leave the items sitting
    // high. Centering it on its own size hint keeps them in the middle at any
    // bar height, and the hint is font-derived, so it follows DPI on its own.
    layout->addWidget(m_menuBar, 0, Qt::AlignVCenter);

    layout->addStretch(1);

    m_minimize = new CaptionButton(TitleBarInternal::kGlyphMinimize, false, this);
    m_minimize->setAccessibleName(tr("Minimize"));
    connect(m_minimize, &QAbstractButton::clicked, this, &WindowTitleBar::minimizeRequested);
    layout->addWidget(m_minimize, 0);

    m_maximize = new CaptionButton(TitleBarInternal::kGlyphMaximize, false, this);
    m_maximize->setAccessibleName(tr("Maximize"));
    connect(m_maximize, &QAbstractButton::clicked, this, &WindowTitleBar::maximizeRestoreRequested);
    layout->addWidget(m_maximize, 0);

    m_close = new CaptionButton(TitleBarInternal::kGlyphClose, true, this);
    m_close->setAccessibleName(tr("Close"));
    connect(m_close, &QAbstractButton::clicked, this, &WindowTitleBar::closeRequested);
    layout->addWidget(m_close, 0);
}

int WindowTitleBar::opaqueLeftInset(const QPixmap& pixmap)
{
    if (pixmap.isNull())
        return 0;
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int x = 0; x < image.width(); ++x) {
        for (int y = 0; y < image.height(); ++y) {
            // Antialiasing leaves a faint fringe a pixel or two outside the
            // shape; only ink a viewer can actually see should count.
            if (qAlpha(image.pixel(x, y)) > 24)
                return int(x / pixmap.devicePixelRatioF());
        }
    }
    return 0;
}

void WindowTitleBar::setAppIconInkLeft(int inkLeftInBar)
{
    const int margin = qMax(0, inkLeftInBar - m_icon->inkLeft());
    if (m_layout->contentsMargins().left() == margin)
        return;
    m_layout->setContentsMargins(margin, 0, 0, 0);
    // The menu moves with the row, which moves the title's left bound.
    update();
}

void WindowTitleBar::setDarkMode(bool dark)
{
    // The title's own colour comes from the menu bar's palette, which the
    // application style sheet has already re-resolved by this point, so only
    // the hand-painted caption buttons need telling.
    m_dark = dark;
    update();
    m_minimize->setDarkMode(dark);
    m_maximize->setDarkMode(dark);
    m_close->setDarkMode(dark);
    for (LayoutToggleButton* toggle : std::as_const(m_layoutToggles))
        toggle->setDarkMode(dark);
}

void WindowTitleBar::setMaximized(bool maximized)
{
    m_maximize->setGlyph(maximized ? TitleBarInternal::kGlyphRestore
                                   : TitleBarInternal::kGlyphMaximize);
    m_maximize->setAccessibleName(maximized ? tr("Restore") : tr("Maximize"));
}

void WindowTitleBar::setWindowActive(bool active)
{
    m_windowActive = active;
    for (LayoutToggleButton* toggle : std::as_const(m_layoutToggles))
        toggle->setWindowActive(active);
    m_minimize->setWindowActive(active);
    m_maximize->setWindowActive(active);
    m_close->setWindowActive(active);
    // A window that just lost activation cannot be told to drop a hover
    // highlight later: HTMAXBUTTON hover messages only arrive while active.
    if (!active) {
        m_maximize->setExternalHovered(false);
        m_maximize->setExternalPressed(false);
        m_close->setExternalHovered(false);
        m_close->setExternalPressed(false);
    }
}

void WindowTitleBar::addLayoutToggle(QAction* action, const QString& onIcon,
                                     const QString& offIcon, const QString& toolTip,
                                     const QKeySequence& shortcutHint)
{
    auto* toggle = new LayoutToggleButton(action, onIcon, offIcon, toolTip, shortcutHint, this);
    toggle->setDarkMode(m_dark);
    toggle->setWindowActive(m_windowActive);
    // Spacing items rather than a container widget: a container could be
    // promoted to a native window of its own, and its HWND would then claim the
    // gaps that must stay draggable caption.
    if (m_layoutToggles.isEmpty())
        m_layout->insertSpacing(m_layout->indexOf(m_minimize), kLayoutButtonGap);
    int index = m_layout->indexOf(m_minimize) - 1;  // the gap before Minimize
    if (!m_layoutToggles.isEmpty())
        m_layout->insertSpacing(index++, kLayoutButtonGap);
    m_layout->insertWidget(index, toggle, 0, Qt::AlignVCenter);
    m_layoutToggles.append(toggle);
    update();
}

void WindowTitleBar::syncLayoutToggles()
{
    for (LayoutToggleButton* toggle : std::as_const(m_layoutToggles))
        toggle->sync();
}

QList<QRect> WindowTitleBar::layoutToggleRects() const
{
    QList<QRect> rects;
    for (const LayoutToggleButton* toggle : m_layoutToggles) {
        if (toggle->isVisible())
            rects.append(toggle->geometry());
    }
    return rects;
}

void WindowTitleBar::setMaximizeButtonHovered(bool hovered)
{
    m_maximize->setExternalHovered(hovered);
}

void WindowTitleBar::setMaximizeButtonPressed(bool pressed)
{
    m_maximize->setExternalPressed(pressed);
}

void WindowTitleBar::setCloseButtonHovered(bool hovered)
{
    m_close->setExternalHovered(hovered);
}

void WindowTitleBar::setCloseButtonPressed(bool pressed)
{
    m_close->setExternalPressed(pressed);
}

#ifdef Q_OS_WIN
bool WindowTitleBar::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == QByteArrayLiteral("windows_generic_MSG")) {
        auto* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            POINT point{ GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam) };
            ScreenToClient(msg->hwnd, &point);
            const qreal dpr = devicePixelRatioF();
            const QPoint local(qFloor(point.x / dpr), qFloor(point.y / dpr));
            switch (zoneAt(local)) {
            case TitleBarLayout::Zone::Caption:
            case TitleBarLayout::Zone::Maximize:
            case TitleBarLayout::Zone::Close:
                // The top-level owns caption gestures and native button hit
                // tests, including the resize bands and Snap Layout hover.
                *result = HTTRANSPARENT;
                return true;
            default:
                // The app icon, the menus and Minimize are
                // ordinary widgets and keep ordinary Qt mouse input.
                break;
            }
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}
#endif

QRect WindowTitleBar::menuRect() const
{
    if (!m_menuBar)
        return {};
    int usedRight = 0;
    for (QAction* action : m_menuBar->actions()) {
        if (!action->isVisible())
            continue;
        const QRect geometry = m_menuBar->actionGeometry(action);
        if (geometry.isValid())
            usedRight = qMax(usedRight, geometry.right() + 1);
    }
    // When the window is too narrow for every menu, QMenuBar folds the
    // remainder behind its own extension button, which sits past the last
    // visible item and would otherwise land in draggable caption space.
    for (const QToolButton* extension : m_menuBar->findChildren<QToolButton*>()) {
        if (extension->isVisible())
            usedRight = qMax(usedRight, extension->geometry().right() + 1);
    }
    if (usedRight <= 0)
        return {};
    // Full bar height on purpose: with the menu bar centered vertically, the
    // few pixels above and below an item would otherwise be caption, so a
    // slightly high click would drag the window instead of opening the menu.
    return QRect(m_menuBar->x(), 0, qMin(usedRight, m_menuBar->width()), height());
}

void WindowTitleBar::setTitleText(const QString& text)
{
    if (m_title == text)
        return;
    m_title = text;
    update();
}

QFont WindowTitleBar::titleFont() const
{
    // The menu items' own font, not a second copy of its size and family: the
    // QMenuBar rule in AppStyle is the single place the UI font is declared,
    // and QStyleSheetStyle has already resolved it onto the widget by the time
    // anything is painted.
    return m_menuBar ? m_menuBar->font() : font();
}

QColor WindowTitleBar::titleColor() const
{
    // Likewise the menu text's colour, so the title reads as primary text in
    // both themes and follows any future change to that one rule. Never dimmed
    // for an inactive window -- unlike the caption buttons, this is content.
    if (m_menuBar)
        return m_menuBar->palette().color(m_menuBar->foregroundRole());
    return palette().color(QPalette::WindowText);
}

QString WindowTitleBar::elidedTitle(int maxWidth) const
{
    const QFontMetrics metrics(titleFont());
    // Below roughly an ellipsis plus a few characters the title says nothing
    // useful, so it is dropped rather than shown as a stub. Derived from the
    // font, so it follows the UI font and DPI instead of being a pixel
    // constant.
    const int minimumUseful = metrics.horizontalAdvance(QStringLiteral("…"))
                            + metrics.averageCharWidth() * 4;
    if (maxWidth < minimumUseful)
        return {};
    const QString elided = metrics.elidedText(m_title, Qt::ElideRight, maxWidth);
    if (elided.isEmpty() || elided == QStringLiteral("…"))
        return {};
    return elided;
}

QRect WindowTitleBar::titleLayout(QString* text) const
{
    if (text)
        text->clear();
    if (m_title.isEmpty() || width() <= 0)
        return {};

    const QRect menu = menuRect();
    const int menuRight = menu.isNull()
        ? (m_icon ? m_icon->geometry().right() + 1 : 0)
        : menu.right() + 1;
    int buttonsLeft = m_minimize ? m_minimize->x() : width();
    for (const LayoutToggleButton* toggle : m_layoutToggles) {
        if (toggle->isVisible())
            buttonsLeft = qMin(buttonsLeft, toggle->x());
    }
    const int maxWidth = TitleBarLayout::centeredTitleMaxWidth(
        width(), menuRight + kTitleGap, buttonsLeft - kTitleGap);
    if (maxWidth <= 0)
        return {};

    const QString elided = elidedTitle(maxWidth);
    if (elided.isEmpty())
        return {};

    // Measured once and handed back with the rect: re-eliding to the rect's
    // own width could shave a second character off through rounding.
    const int textWidth = QFontMetrics(titleFont()).horizontalAdvance(elided);
    if (text)
        *text = elided;
    return QRect((width() - textWidth) / 2, 0, textWidth, height());
}

QRect WindowTitleBar::titleRect() const
{
    return titleLayout(nullptr);
}

void WindowTitleBar::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    // The documented way for a QWidget subclass with its own paintEvent to
    // keep honouring the application style sheet's background and border.
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

    QString text;
    const QRect rect = titleLayout(&text);
    if (rect.isNull() || text.isEmpty())
        return;

    painter.setFont(titleFont());
    painter.setPen(titleColor());
    painter.drawText(rect, Qt::AlignCenter, text);
}

void WindowTitleBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}

TitleBarLayout::Zone WindowTitleBar::zoneAt(const QPoint& local) const
{
    return TitleBarLayout::zoneAt(local,
                                  m_icon ? m_icon->geometry() : QRect(),
                                  menuRect(),
                                  m_minimize ? m_minimize->geometry() : QRect(),
                                  m_maximize ? m_maximize->geometry() : QRect(),
                                  m_close ? m_close->geometry() : QRect(),
                                  layoutToggleRects());
}
