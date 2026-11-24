#include "TabWidget.h"
#include "DocumentModel.h"
#include "MarkdownEditor.h"
#include "PreviewWidget.h"
#include "ImageViewer.h"
#include "GitDiffView.h"
#include "ThemedSplitter.h"
#include "ThemePalette.h"
#include "SyntaxDefinitionResolver.h"
#include "StartupProfiler.h"

#include <QSplitter>
#include <QSplitterHandle>
#include <QToolButton>
#include <QTimer>
#include <QHBoxLayout>
#include <QStyle>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QFileInfo>
#include <QEvent>
#include <QContextMenuEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QEnterEvent>
#include <QPaintEvent>
#include <QApplication>
#include <QCursor>
#include <QPointer>

int TabWidget::s_untitled = 0;

namespace {
// Our own tab-bar children carry an object name so the trailing-space
// calculation can tell them apart from QTabBar's own scroll buttons, which
// are the only other QToolButtons parented to the bar.
const char kCloseButtonName[] = "tabCloseButton";
const char kNewTabButtonName[] = "tabNewButton";

// The button is as tall as a tab so it reads as part of the strip rather than
// as a control floating in the vacant area; its hover pill is inset from that
// and rounded like a toolbar button, so it still looks lighter than a tab.
constexpr int kNewTabButtonWidth = 36;
constexpr int kNewTabButtonFallbackHeight = 32;
constexpr int kNewTabButtonGap = 2;
constexpr int kNewTabHoverInsetX = 3;
constexpr int kNewTabHoverInsetY = 3;
// The close button is exactly its hover square, so its bounds are the hover
// bounds; TabBar::updateCloseButtonGeometry() places it.
constexpr int kCloseTabButtonSize = 20;
// QTabBar#documentTabBar::tab's margin-right in AppStyle: the space between
// one tab's border and the next tab, which is not part of the visible tab.
constexpr int kTabSpacing = 2;
// Its margin-top / margin-bottom: the tabs start flush with the top of the
// Workspace column, and the gap between the tabs and the Editor/Preview cards
// below is kTabBottomMargin.
constexpr int kTabTopMargin = ThemeCard::TabTopMargin;
constexpr int kTabBottomMargin = ThemeCard::TabGap;
static_assert(ThemeCard::TabTopMargin % 2 == 0 && ThemeCard::TabGap % 2 == 0,
              "tab edges must land on whole device pixels at 150% and 200%");

// The current tab's shadow is kShadowLayers rounded rects, each growing by a
// step of kShadowSpread / kShadowLayers on every side with no offset. Overlapping
// layers build the alpha toward the tab's edge and fade it out over
// kShadowSpread, so it reads as a soft border rather than a drop shadow. It costs
// a few fills and needs no blur effect or offscreen pass. Spread is capped by
// TabGap: the shadow must end before the Editor/Preview cards begin.
constexpr qreal kShadowSpread = 1.0;
constexpr int kShadowLayers = 6;
// Per-layer alpha. Six layers at 2/255 peak near 5% black at the tab's rim in
// Light; Dark's canvas is near black, so the same falloff needs about 9/255.
constexpr int kShadowLayerAlphaLight = 2;
constexpr int kShadowLayerAlphaDark = 9;

// The tab strip is transparent over the window canvas; painting the canvas
// keeps the button opaque over a tab that scrolled underneath it when the bar
// overflows.
QColor tabBarBackground(bool dark)
{
    return ThemePalette::color(ThemePalette::Canvas, dark);
}

// One translucent fill for the "+" and the close glyphs, so it reads the same
// over the canvas, an inactive tab's hover fill and the current tab's sheet.
QColor tabButtonFill(bool dark, bool pressed)
{
    return pressed ? (dark ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 40))
                   : (dark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 22));
}

// The dragged tab's gray: 70/255 black over its fill, applied to the opaque fill
// of the tab's own rounded box rather than as a square overlay on top of it.
QColor draggedTabFill(const QColor& base)
{
    constexpr int kKeep = 255 - 70;
    return QColor(base.red() * kKeep / 255, base.green() * kKeep / 255, base.blue() * kKeep / 255);
}

// QTabBar creates its overflow buttons itself, so they are styled through
// TabBar::eventFilter() rather than subclassed.
const char kScrollLeftButtonName[] = "ScrollLeftButton";
const char kScrollRightButtonName[] = "ScrollRightButton";
constexpr int kScrollHoverInsetX = 2;
constexpr int kScrollHoverInsetY = 3;
constexpr qreal kScrollChevronHalfWidth = 2.5;
constexpr qreal kScrollChevronHalfHeight = 4.0;

QList<QToolButton*> scrollButtons(const QObject* bar)
{
    QList<QToolButton*> buttons;
    for (QObject* child : bar->children()) {
        auto* button = qobject_cast<QToolButton*>(child);
        if (!button)
            continue;
        const QString name = button->objectName();
        if (name == QLatin1String(kScrollLeftButtonName) || name == QLatin1String(kScrollRightButtonName))
            buttons.append(button);
    }
    return buttons;
}
}

// The "+" is painted rather than styled for the same reason CloseTabButton is:
// it lives inside a QTabBar whose QToolButton rules are owned by the scroll
// buttons, and its hover/pressed treatment has to match the close glyph's.
class TabBar::NewTabButton : public QToolButton {
public:
    explicit NewTabButton(QWidget* parent = nullptr) : QToolButton(parent) {
        setObjectName(QLatin1String(kNewTabButtonName));
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::NoFocus);
        setAutoRaise(true);
        setFixedSize(kNewTabButtonWidth, kNewTabButtonFallbackHeight);
        setAttribute(Qt::WA_Hover);
    }

    void setDarkMode(bool dark) {
        if (m_dark == dark)
            return;
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), tabBarBackground(m_dark));
        p.setRenderHint(QPainter::Antialiasing);

        if (isDown() || underMouse()) {
            p.setPen(Qt::NoPen);
            p.setBrush(tabButtonFill(m_dark, isDown()));
            p.drawRoundedRect(QRectF(rect()).adjusted(kNewTabHoverInsetX, kNewTabHoverInsetY,
                                                      -kNewTabHoverInsetX, -kNewTabHoverInsetY),
                              ThemeCard::ButtonRadius, ThemeCard::ButtonRadius);
        }

        // Snapped to a half-pixel grid so both strokes land on whole device
        // pixels at 100% scaling instead of straddling two rows.
        const QPointF c(qRound(width() / 2.0) - 0.5, qRound(height() / 2.0) - 0.5);
        const QColor glyph = m_dark ? QColor(0xd7, 0xd7, 0xd7) : QColor(0x44, 0x44, 0x44);
        p.setPen(QPen(glyph, 1.4, Qt::SolidLine, Qt::RoundCap));
        const qreal d = 5.5;
        p.drawLine(QPointF(c.x() - d, c.y()), QPointF(c.x() + d, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - d), QPointF(c.x(), c.y() + d));
    }

    // Without this a right-press is ignored by QAbstractButton and propagates
    // to the tab bar, which would open the empty-area context menu on top of
    // the button.
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            event->accept();
            return;
        }
        QToolButton::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) {
            event->accept();
            return;
        }
        QToolButton::mouseReleaseEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent* event) override { event->accept(); }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*)      override { update(); }

private:
    bool m_dark = false;
};

TabBar::TabBar(QWidget* parent)
    : QTabBar(parent)
{
    setObjectName(QStringLiteral("documentTabBar"));
    setAcceptDrops(true);
    m_newTabButton = new NewTabButton(this);
    m_newTabButton->setToolTip(tr("New Tab (Ctrl+T)"));
    m_newTabButton->setAccessibleName(tr("New Tab"));
    connect(m_newTabButton, &QToolButton::clicked, this, &TabBar::newTabRequested);
    m_newTabButton->show();
    for (QToolButton* button : scrollButtons(this))
        button->installEventFilter(this);
}

void TabBar::setDarkMode(bool dark)
{
    if (m_newTabButton)
        m_newTabButton->setDarkMode(dark);
    if (m_dark != dark) {
        m_dark = dark;
        update();
        for (QToolButton* button : scrollButtons(this))
            button->update();
    }
}

bool TabBar::eventFilter(QObject* watched, QEvent* event)
{
    auto* button = qobject_cast<QToolButton*>(watched);
    if (!button)
        return QTabBar::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::Paint:
        paintScrollButton(button);
        return true;
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
        button->update();
        break;
    default:
        break;
    }
    return QTabBar::eventFilter(watched, event);
}

// Painted like the "+" button: the canvas fill is opaque so a tab scrolled
// beneath the button does not show through it, and the hover/pressed pill is
// rounded inside it. A disabled button (the bar is already at that end) takes
// no hover and draws its chevron in the disabled tone.
void TabBar::paintScrollButton(QToolButton* button) const
{
    QPainter p(button);
    const QRect box = button->rect();
    p.fillRect(box, tabBarBackground(m_dark));
    p.setRenderHint(QPainter::Antialiasing);

    const bool enabled = button->isEnabled();
    if (enabled && (button->isDown() || button->underMouse())) {
        p.setPen(Qt::NoPen);
        p.setBrush(ThemePalette::color(button->isDown() ? ThemePalette::Pressed
                                                        : ThemePalette::RowHover, m_dark));
        p.drawRoundedRect(QRectF(box).adjusted(kScrollHoverInsetX, kScrollHoverInsetY,
                                               -kScrollHoverInsetX, -kScrollHoverInsetY),
                          ThemeCard::ButtonRadius, ThemeCard::ButtonRadius);
    }

    // Same half-pixel snapping as the "+" so the strokes stay crisp at 100%.
    const QPointF c(qRound(box.width() / 2.0) - 0.5, qRound(box.height() / 2.0) - 0.5);
    const bool left = button->objectName() == QLatin1String(kScrollLeftButtonName);
    const qreal tip = left ? -kScrollChevronHalfWidth : kScrollChevronHalfWidth;
    const qreal back = -tip;
    QPainterPath chevron;
    chevron.moveTo(c.x() + back, c.y() - kScrollChevronHalfHeight);
    chevron.lineTo(c.x() + tip, c.y());
    chevron.lineTo(c.x() + back, c.y() + kScrollChevronHalfHeight);
    const ThemePalette::Role glyphRole = enabled ? ThemePalette::Text : ThemePalette::Disabled;
    p.setPen(QPen(ThemePalette::color(glyphRole, m_dark), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(chevron);
}

QRect TabBar::tabArea() const
{
    // An overflowing bar shows one scroll button at each edge; QTabBar clips
    // its tabs to the space between them, and so must anything painted for a
    // tab. Only the left one sits at the bar's origin (see trailingLimit()).
    int left = 0;
    for (QObject* child : children()) {
        auto* button = qobject_cast<QToolButton*>(child);
        if (!button || !button->isVisible())
            continue;
        const QString name = button->objectName();
        if (name == QLatin1String(kNewTabButtonName) || name == QLatin1String(kCloseButtonName))
            continue;
        if (button->geometry().left() <= kNewTabButtonGap)
            left = qMax(left, button->geometry().right() + 1);
    }
    return QRect(left, 0, qMax(0, trailingLimit() - left), height());
}

QRect TabBar::tabBox(int index) const
{
    return tabRect(index).adjusted(0, kTabTopMargin, -kTabSpacing, -kTabBottomMargin);
}

// The right edge the "+" must stay clear of: QTabBar's scroll buttons when the
// tabs overflow, otherwise the bar's own edge. The scroll buttons are the only
// QToolButton children the bar creates itself, so everything FastMD parents to
// the bar is excluded by object name.
int TabBar::trailingLimit() const
{
    int limit = width();
    for (QObject* child : children()) {
        auto* button = qobject_cast<QToolButton*>(child);
        if (!button || !button->isVisible())
            continue;
        const QString name = button->objectName();
        if (name == QLatin1String(kNewTabButtonName) || name == QLatin1String(kCloseButtonName))
            continue;
        // Qt's own scroll buttons only ever sit in the trailing cluster at the
        // bar's right edge; a left() at/near the origin means this button's
        // geometry has not been laid out for the current tab set yet (e.g. the
        // very frame overflow first appears). Trusting a stale (0, 0) geometry
        // here used to collapse the limit to ~0 and yank the "+" button to the
        // far left instead of leaving it parked after the last visible tab.
        const int left = button->geometry().left();
        if (left <= kNewTabButtonGap)
            continue;
        limit = qMin(limit, left);
    }
    return limit;
}

void TabBar::updateNewTabButtonGeometry()
{
    // QTabBar lays its scroll buttons out over the full bar height; they only
    // need to cover the tabs, so their opaque canvas fill stops at the gap.
    for (QObject* child : children()) {
        auto* button = qobject_cast<QToolButton*>(child);
        if (!button || (button->objectName() != QLatin1String("ScrollLeftButton")
                        && button->objectName() != QLatin1String("ScrollRightButton")))
            continue;
        QRect geometry = button->geometry();
        if (geometry.height() != height() - kTabBottomMargin) {
            geometry.setHeight(height() - kTabBottomMargin);
            button->setGeometry(geometry);
        }
    }

    if (!m_newTabButton)
        return;

    // Matching the tab's own box is what keeps the "+" aligned with the strip:
    // the row can be taller than a tab, so centring in the bar (or using a
    // fixed square) leaves the button visibly small and low.
    int boxHeight = qMin(height() - kTabTopMargin - kTabBottomMargin, kNewTabButtonFallbackHeight);
    int x = kNewTabButtonGap;
    int y = kTabTopMargin;
    if (count() > 0) {
        const QRect last = tabBox(count() - 1);
        x = last.right() + 1 + kTabSpacing;
        y = last.top();
        boxHeight = last.height();
    }
    // Overflowing (or scrolled) tabs push the last tab past the visible area;
    // park the button just inside whatever room is left instead.
    x = qBound(0, x, qMax(0, trailingLimit() - kNewTabButtonWidth - kNewTabButtonGap));

    const QRect target(x, y, kNewTabButtonWidth, qMax(1, boxHeight));
    if (m_newTabButton->geometry() == target)
        return;
    m_newTabButton->setFixedSize(target.size());
    m_newTabButton->setGeometry(target);
    m_newTabButton->raise();
}

// QTabBar places its right-side close buttons inside the tab's own rect, which
// leaves them hugging the right edge. The inset is the same on the right as on
// the top and bottom, so the hover square sits centred in the tab's box and its
// corner lands on the centre of the tab's rounded corner rather than inside the arc.
void TabBar::updateCloseButtonGeometry()
{
    for (int i = 0; i < count(); ++i) {
        QWidget* button = tabButton(i, QTabBar::RightSide);
        if (!button || button->objectName() != QLatin1String(kCloseButtonName))
            continue;
        const QRect box = tabBox(i);
        const int inset = (box.height() - kCloseTabButtonSize) / 2;
        const QRect target(box.right() + 1 - inset - kCloseTabButtonSize, box.top() + inset,
                           kCloseTabButtonSize, kCloseTabButtonSize);
        if (button->geometry() != target)
            button->setGeometry(target);
    }
}

void TabBar::tabLayoutChange()
{
    QTabBar::tabLayoutChange();
    updateCloseButtonGeometry();
}

void TabBar::resizeEvent(QResizeEvent* event)
{
    QTabBar::resizeEvent(event);
    updateNewTabButtonGeometry();
}

void TabBar::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragStarted && m_dragPageId != 0 && (event->buttons() & Qt::LeftButton)
        && (event->globalPosition().toPoint() - m_dragStart).manhattanLength()
               >= QApplication::startDragDistance()) {
        m_dragStarted = true;
        emit tabDragStarted(m_dragPageId, m_dragStart, event->globalPosition().toPoint());
        event->accept();
        return;
    }
    if (!m_dragStarted)
        event->accept();
}

void TabBar::setDragVisual(qulonglong pageId, int insertIndex, bool destination)
{
    m_visualPageId = pageId;
    m_visualInsertIndex = insertIndex;
    m_visualDestination = destination;
    update();
}

void TabBar::clearDragVisual()
{
    m_visualPageId = 0;
    m_visualInsertIndex = -1;
    update();
}

// The current tab is a self-contained, slightly raised pill: the tab row spans
// both the Editor and the Preview card, so it must not merge into either. Fill,
// outline and a soft shadow into the gap mark it -- deliberately no accent
// stroke. QSS cannot paint the shadow outside the tab's box, so the pill is
// painted here, under QTabBar's label.
// The dragged tab is painted through the same rounded box as the current tab, with
// only its fill tinted, so its shape, border and shadow stay the normal tab's.
void TabBar::paintCurrentTab(QPainter& painter, int dragged) const
{
    const int index = currentIndex();
    if (index < 0)
        return;
    const QRect box = tabBox(index);
    if (box.isEmpty())
        return;

    painter.save();
    painter.setClipRect(tabArea());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    const QRectF shadowBase(box);
    painter.setBrush(QColor(0, 0, 0, m_dark ? kShadowLayerAlphaDark : kShadowLayerAlphaLight));
    for (int layer = 1; layer <= kShadowLayers; ++layer) {
        const qreal grow = kShadowSpread * layer / kShadowLayers;
        painter.drawRoundedRect(shadowBase.adjusted(-grow, -grow, grow, grow),
                                ThemeCard::TabRadius + grow, ThemeCard::TabRadius + grow);
    }
    // Elevated is white in Light; in Dark it sits below the row hover, so the
    // lighter Hover step reads as raised there.
    QColor fill = ThemePalette::color(m_dark ? ThemePalette::Hover : ThemePalette::Elevated, m_dark);
    if (index == dragged)
        fill = draggedTabFill(fill);
    ThemeCard::paintRounded(painter, box, fill,
                            ThemePalette::color(m_dark ? ThemePalette::Active : ThemePalette::Border, m_dark),
                            ThemeCard::TabRadius);
    painter.restore();
}

// A dragged tab that is not the current one has no pill of its own, so it gets
// the same tinted rounded box without the current tab's border or shadow.
void TabBar::paintDraggedTab(QPainter& painter, int dragged) const
{
    painter.save();
    painter.setClipRect(tabArea());
    ThemeCard::paintRounded(painter, tabBox(dragged), draggedTabFill(tabBarBackground(m_dark)),
                            Qt::transparent, ThemeCard::TabRadius);
    painter.restore();
}

int TabBar::draggedIndex() const
{
    if (m_visualPageId == 0)
        return -1;
    for (int i = 0; i < count(); ++i) {
        if (tabData(i).toULongLong() == m_visualPageId)
            return i;
    }
    return -1;
}

void TabBar::paintEvent(QPaintEvent* event)
{
    updateNewTabButtonGeometry();
    updateCloseButtonGeometry();
    const int dragged = draggedIndex();
    {
        QPainter painter(this);
        paintCurrentTab(painter, dragged);
        if (dragged >= 0 && dragged != currentIndex())
            paintDraggedTab(painter, dragged);
    }
    QTabBar::paintEvent(event);
    if (m_visualPageId == 0)
        return;
    QPainter painter(this);
    // An in-bar reorder already moves the dragged tab live, so a marker on its
    // own edges (before it, or after it where dropping changes nothing) would
    // only paint a blue line across the dragged tab.
    const bool onDraggedTab = !m_visualDestination && dragged >= 0
        && (m_visualInsertIndex == dragged || m_visualInsertIndex == dragged + 1);
    if (m_visualInsertIndex >= 0 && !onDraggedTab) {
        int x = width() - 2;
        if (m_visualInsertIndex < count())
            x = tabRect(m_visualInsertIndex).left();
        else if (count() > 0)
            x = tabRect(count() - 1).right() + 1;
        painter.setPen(QPen(palette().color(QPalette::Highlight), 3));
        painter.drawLine(x, 4, x, height() - 4);
    }
}


class TabWidget::CloseTabButton : public QToolButton {
public:
    explicit CloseTabButton(QWidget* parent = nullptr) : QToolButton(parent) {
        setObjectName(QLatin1String(kCloseButtonName));
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::NoFocus);
        setAutoRaise(true);
        setFixedSize(kCloseTabButtonSize, kCloseTabButtonSize);
        setAttribute(Qt::WA_Hover);
    }

    void setDarkMode(bool dark) {
        if (m_dark == dark)
            return;
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QPointF c = QRectF(rect()).center();
        const qreal d = 4.0; // half-arm length — always 8px total, regardless of widget size

        if (isDown() || underMouse()) {
            p.setPen(Qt::NoPen);
            p.setBrush(tabButtonFill(m_dark, isDown()));
            p.drawRoundedRect(QRectF(rect()), ThemeCard::ControlRadius, ThemeCard::ControlRadius);
        }

        p.setPen(QPen(glyphColor(), 1.3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c + QPointF(-d, -d), c + QPointF(d,  d));
        p.drawLine(c + QPointF( d, -d), c + QPointF(-d, d));
    }

    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*)      override { update(); }

private:
    // The glyph follows its tab's label: full strength on the current or
    // hovered tab, secondary on the others. QTabBar repaints a tab's rect on
    // hover and current changes, and this button lies inside it.
    QColor glyphColor() const {
        bool emphasized = underMouse();
        if (auto* bar = qobject_cast<const QTabBar*>(parentWidget()); bar && !emphasized) {
            const int index = bar->tabAt(geometry().center());
            emphasized = index >= 0
                && (index == bar->currentIndex()
                    || bar->tabRect(index).contains(bar->mapFromGlobal(QCursor::pos())));
        }
        return ThemePalette::color(emphasized ? ThemePalette::Text : ThemePalette::TextSecondary,
                                   m_dark);
    }

    bool m_dark = false;
};

// ---------------------------------------------------------------------------
TabWidget::TabWidget(QWidget* parent)
    : QTabWidget(parent)
{
    setObjectName(QStringLiteral("documentTabs"));
    auto* bar = new TabBar(this);
    setTabBar(bar);

    setTabsClosable(false);
    setMovable(true);
    setDocumentMode(true);
    // After setDocumentMode(), which turns it back on: the tabs sit on the
    // canvas above the cards, whose own top edges are the only rule there.
    bar->setDrawBase(false);

    // Both the "+" button and a double-click on the vacant area go out to
    // MainWindow's New Tab workflow rather than calling addNewTab() directly,
    // so a new tab always gets the same UI/workspace sync as File > New Tab.
    connect(bar,  &TabBar::newTabRequested,       this, &TabWidget::newTabRequested);
    connect(bar,  &TabBar::middleClickCloseRequested, this, &TabWidget::onCloseRequested);
    connect(bar,  &TabBar::tabRenameRequested,    this, &TabWidget::tabRenameRequested);
    connect(bar,  &TabBar::tabContextMenuRequested, this, &TabWidget::tabContextMenuRequested);
    connect(bar,  &TabBar::emptyAreaContextMenuRequested, this, &TabWidget::tabBarContextMenuRequested);
    connect(bar,  &TabBar::tabDragStarted, this, [this](qulonglong pageId, QPoint press, QPoint pos) {
        TabPage* page = reinterpret_cast<TabPage*>(pageId);
        if (page && indexOf(page) >= 0)
            emit tabDragStarted(page, press, pos);
    });
    connect(bar,  &QTabBar::tabMoved,             this, [this](int from, int to) {
        if (from >= 0 && from < m_pages.size() && to >= 0 && to < m_pages.size()) {
            m_pages.move(from, to);
        }
    });
    connect(this, &QTabWidget::currentChanged,    this, &TabWidget::onCurrentChanged);
    connect(this, &QTabWidget::tabCloseRequested, this, &TabWidget::onCloseRequested);

    tabBar()->installEventFilter(this);
}

TabWidget::~TabWidget()
{
    // A final splitter move may be less than one debounce interval old when the
    // window closes. Flush it before the child timer is destroyed so the next
    // launch does not restore the previous position.
    if (m_splitterPersistTimer && m_splitterPersistTimer->isActive())
        persistCachedSplitterState();
}

void TabWidget::persistCachedSplitterState()
{
    if (m_splitterPersistTimer)
        m_splitterPersistTimer->stop();
    QSettings s;
    s.setValue(QStringLiteral("splitterState"), m_cachedSplitterState);
}

// ---------------------------------------------------------------------------
QTimer* TabWidget::ensureSplitterPersistTimer()
{
    if (!m_splitterPersistTimer) {
        m_splitterPersistTimer = new QTimer(this);
        m_splitterPersistTimer->setSingleShot(true);
        m_splitterPersistTimer->setInterval(300);
        connect(m_splitterPersistTimer, &QTimer::timeout,
                this, &TabWidget::persistCachedSplitterState);
    }
    return m_splitterPersistTimer;
}

// ---------------------------------------------------------------------------
void TabWidget::setDefaultTabSettings(const RestoredTabSettings& settings, EditorMode mode)
{
    m_cachedSplitterState = settings.splitterState;
    m_cachedDefaultMode = static_cast<int>(mode);
    m_cachedPreviewVisible = settings.markdownPreviewVisible;
    m_cachedWordWrapEnabled = settings.wordWrapEnabled;
    m_cachedFontSize = settings.editorFontSize;
}

// ---------------------------------------------------------------------------
namespace {
// Spell checking is for prose: Markdown and Plain Text documents only. Code
// languages, every viewer mode, and the read-only bundled User Guide are never
// checked.
bool spellCheckEligible(const DocumentModel& model)
{
    if (model.isViewer() || model.isBundled())
        return false;
    const EditorMode mode = model.editorMode();
    if (mode != EditorMode::Markdown && mode != EditorMode::PlainText)
        return false;
    return mode == EditorMode::Markdown
        || model.selectedDefinitionName().isEmpty();
}
} // namespace

// ---------------------------------------------------------------------------
int TabWidget::addNewTab(const QString& filePath, bool activate,
                         const RestoredTabSettings* restoredSettings,
                         const GitDiffSpec* diffSpec)
{
    FASTMD_MARK("    tab: begin");
    int fontSize = FastMdDefaults::EditorFontSize;
    if (restoredSettings) {
        if (restoredSettings->editorFontSize > 0)
            fontSize = restoredSettings->editorFontSize;
    } else {
        fontSize = m_cachedFontSize;
    }

    auto* page    = new TabPage;
    page->model   = new DocumentModel(++s_untitled);
    FASTMD_MARK("    tab: model constructed");
    page->editor  = new MarkdownEditor(nullptr, fontSize);
    FASTMD_MARK("    tab: editor constructed");
    page->preview = new PreviewWidget;
    FASTMD_MARK("    tab: preview constructed");
    // The splitter paints the Editor and Preview cards itself and keeps each
    // pane inside its card edge, which also keeps a multi-DIP buffer beside
    // the native WebView2 child window that nothing can paint over.
    auto* pageSplitter = new ThemedSplitter(Qt::Horizontal, ThemedSplitter::HandleStyle::Gap,
                                            ThemeCard::CardSplitterHandle);
    pageSplitter->setPaintsCards(true);
    page->splitter = pageSplitter;
    page->splitter->setObjectName(QStringLiteral("editorPreviewSplitter"));
    ThemeCard::setRole(page->editor, ThemePalette::Editor);
    ThemeCard::setRole(page->preview, ThemePalette::Preview);

    if (diffSpec) {
        // Deliberately no setFilePath(): a diff is not the file, and claiming
        // its path key would displace the real editing tab in the process-wide
        // open-document registry.
        page->model->setDiffSpec(*diffSpec);
        page->model->setEditorMode(EditorMode::Diff);
        page->model->setDisplayNameOverride(diffSpec->tabTitle());
    } else if (!filePath.isEmpty()) {
        page->model->setFilePath(filePath);
        page->model->setEditorMode(DocumentModel::modeForPath(filePath));
        // Provisional guess so preview/highlighter visibility computed later
        // in this function is consistent; MainWindow::initializeDocumentLanguage()
        // refines this (real auto-detection, JSON/Markdown normalization) once
        // the tab is fully constructed.
        if (!isViewerMode(page->model->editorMode())) {
            const bool markdownGuess = page->model->editorMode() == EditorMode::Markdown;
            page->model->setSelectedDefinitionName(
                markdownGuess ? SyntaxDefinitionResolver::fastMdMarkdownDefinitionName() : QString());
            page->model->setAutomaticallyDetectedName(page->model->selectedDefinitionName());
            page->model->setLanguageSelectionSource(LanguageSelectionSource::Automatic);
        }
    }
    page->editor->setDocumentPath(filePath);

    page->editor->setDarkMode(m_darkMode);
    page->preview->setDarkMode(m_darkMode);

    if (auto* splitter = dynamic_cast<ThemedSplitter*>(page->splitter))
        splitter->setDarkMode(m_darkMode);

    page->splitter->addWidget(page->editor);
    page->splitter->addWidget(page->preview);
    page->splitter->setStretchFactor(0, 50);
    page->splitter->setStretchFactor(1, 50);
    page->splitter->setSizes({ 500, 500 });
    page->splitter->handle(1)->installEventFilter(this);
    page->editor->installEventFilter(this);
    page->preview->installEventFilter(this);

    QByteArray state;
    bool markdownPreviewVisible = true;
    bool wordWrapEnabled = true;

    if (restoredSettings) {
        state = restoredSettings->splitterState;
        markdownPreviewVisible = restoredSettings->markdownPreviewVisible;
        wordWrapEnabled = restoredSettings->wordWrapEnabled;
    } else {
        state = m_cachedSplitterState;
        if (filePath.isEmpty() && !diffSpec) {
            page->model->setEditorMode(static_cast<EditorMode>(m_cachedDefaultMode));
            // New-document initial intent (language-indicator-plan.md): starts
            // Automatic so the first Save As/rename can redetect from the
            // chosen file name instead of being pinned to this default guess.
            const bool markdownDefault = page->model->editorMode() == EditorMode::Markdown;
            page->model->setSelectedDefinitionName(
                markdownDefault ? SyntaxDefinitionResolver::fastMdMarkdownDefinitionName() : QString());
            page->model->setAutomaticallyDetectedName(page->model->selectedDefinitionName());
            page->model->setLanguageSelectionSource(LanguageSelectionSource::Automatic);
        }
        markdownPreviewVisible = m_cachedPreviewVisible;
        wordWrapEnabled = m_cachedWordWrapEnabled;
    }

    // A diff tab's splitter has a third child and two hidden ones; an editable
    // tab's saved two-pane state does not describe it.
    if (diffSpec)
        state.clear();

    if (!state.isEmpty()) {
        page->splitter->restoreState(state);
        // restoreState() resets the handle width to whatever an older saved
        // session encoded (or the native style's default, ~1px) — reassert it
        // so the handle stays visibly grabbable and themed.
        if (auto* splitter = dynamic_cast<ThemedSplitter*>(page->splitter))
            splitter->reassertHandleWidth();
    }
    page->model->setMarkdownPreviewVisible(markdownPreviewVisible);
    const bool isImage = page->model->editorMode() == EditorMode::Image;
    const bool isDiff = page->model->editorMode() == EditorMode::Diff;
    const bool readOnly = isViewerMode(page->model->editorMode());
    const bool markdownMode = page->model->editorMode() == EditorMode::Markdown;
    // Reading Mode is a pure view override: it decides this tab's *initial*
    // layout without touching markdownPreviewVisible(), which stays the user's
    // real Editor/Preview preference for when Reading Mode is turned off.
    const bool readingMode = m_readingMode && markdownMode;
    // Notepad Mode is Reading Mode's mirror: it suppresses the Preview instead
    // of the Editor, for the same "don't flash the wrong layout" reason.
    const bool notepadMode = m_notepadMode && markdownMode;
    page->preview->setVisible(!isImage && !isDiff
        && (readOnly || (markdownMode && (markdownPreviewVisible || readingMode) && !notepadMode)));
    page->editor->setVisible(!readOnly && !readingMode);
    page->editor->setMarkdownMode(page->model->editorMode() == EditorMode::Markdown);
    page->editor->setWordWrapEnabled(wordWrapEnabled);
    page->editor->setCurrentLineHighlightEnabled(m_currentLineHighlightEnabled);
    page->editor->setSpellCheckActive(m_spellCheckActive && spellCheckEligible(*page->model));
    FASTMD_MARK("    tab: panes configured");

    page->preview->setBaseFontSize(fontSize);
    page->preview->setBodyFontFamily(m_previewFontFamily);
    page->preview->setDocumentSpacing(m_documentSpacing);
    if (isImage) {
        page->imageViewer = new ImageViewer;
        ThemeCard::setRole(page->imageViewer, ThemePalette::Editor);
        page->imageViewer->setDarkMode(m_darkMode);
        page->splitter->addWidget(page->imageViewer);
        page->imageViewer->load(filePath);
    } else if (isDiff) {
        // GitDiffView is itself a splitter of two panes: old on the left,
        // new on the right, with its own persisted split ratio.
        page->diffView = new GitDiffView;
        ThemeCard::setRole(page->diffView, ThemePalette::Editor);
        page->diffView->setDarkMode(m_darkMode);
        page->diffView->setWordWrapEnabled(wordWrapEnabled);
        page->diffView->setZoomDelta(m_cachedDiffZoomDelta);
        page->diffView->restoreSplitterState(
            QSettings().value(QStringLiteral("git/diffSplitter")).toByteArray());
        page->splitter->addWidget(page->diffView);
        page->diffView->showPlaceholder(tr("Loading diff…"));
        connect(page->diffView, &GitDiffView::zoomDeltaChanged,
                this, &TabWidget::diffZoomChanged);
    } else if (readOnly) {
        page->preview->showReadOnlyFile(filePath);
    }

    // splitterMoved fires per mouse-move; on Windows QSettings is the registry,
    // so only the in-memory cache is updated eagerly and the write is coalesced.
    // A diff tab is excluded: its splitter holds two hidden panes plus the diff
    // view, and saving that state would corrupt every editable tab's layout.
    if (!isDiff) {
        connect(page->splitter, &QSplitter::splitterMoved, this, [this, page](int, int) {
            m_cachedSplitterState = page->splitter->saveState();
            ensureSplitterPersistTimer()->start();
        });
    }

    // Editor → preview scroll sync is owned by MainWindow::connectEditor() for
    // the active tab (the only tab whose preview is visible); a second
    // connection here posted a duplicate cross-process WebView2 message per
    // scroll step and bypassed MainWindow's m_scrollSyncing re-entrancy guard.

    connect(page->preview, &PreviewWidget::outlineRequested, this, &TabWidget::outlineRequested);

    // Preview-to-Editor source highlighting always targets this page's own editor --
    // the signal is scoped to its PreviewWidget instance, so no MainWindow/active-tab
    // routing is needed (unlike Outline, whose popup is a MainWindow-owned widget).
    connect(page->preview, &PreviewWidget::previewFlashRequested, page->editor,
            &MarkdownEditor::flashLineIfVisible);

    m_pages.append(page);
    int idx = addTab(page->splitter, page->model->displayName());
    FASTMD_MARK("    tab: inserted into tab widget");
    tabBar()->setTabData(idx, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(page)));
    tabBar()->setTabButton(idx, QTabBar::RightSide, makeCloseButton(page));
    if (activate) {
        setCurrentIndex(idx);
        // A hidden QPlainTextEdit cannot hold native focus, so handing it over
        // would leave the window with nothing focused; in Reading Mode the
        // Preview is focused by MainWindow once the tab is on screen.
        if (page->editor->isVisibleTo(page->splitter))
            page->editor->restoreKeyboardFocus();
    }
    FASTMD_MARK("    tab: complete");
    return idx;
}

QWidget* TabWidget::makeCloseButton(TabPage* page)
{
    auto* button = new CloseTabButton(tabBar());
    button->setDarkMode(m_darkMode);

    connect(button, &QToolButton::clicked, this, [this, page]() {
        int idx = m_pages.indexOf(page);
        if (idx >= 0)
            onCloseRequested(idx);
    });

    return button;
}

// ---------------------------------------------------------------------------
void TabWidget::closeTabAt(int index)
{
    if (index < 0 || index >= m_pages.size()) return;

    TabPage* page = m_pages.takeAt(index);
    removeTab(index);

    delete page->model;
    // splitter owns editor and preview as children
    delete page->splitter;
    delete page;
}

TabPage* TabWidget::takeTabPage(int index)
{
    if (index < 0 || index >= m_pages.size())
        return nullptr;
    if (QWidget* button = tabBar()->tabButton(index, QTabBar::RightSide)) {
        tabBar()->setTabButton(index, QTabBar::RightSide, nullptr);
        button->deleteLater();
    }
    TabPage* page = m_pages.takeAt(index);
    page->preview->prepareForReparent();
    removeTab(index);
    // Keep the splitter attached to Qt's internal stack until insertTabPage()
    // reparents it directly to the destination. Making a native WebView2 host
    // temporarily parentless destroys its host HWND on some Windows builds.
    return page;
}

int TabWidget::insertTabPage(TabPage* page, int index, bool activate)
{
    if (!page)
        return -1;
    index = qBound(0, index, m_pages.size());
    page->splitter->setParent(this);
    m_pages.insert(index, page);
    const int inserted = insertTab(index, page->splitter, page->model->displayName());
    // Only now is the splitter in its final parent chain, so this is the first
    // point where the preview's native host window is the one WebView2 should
    // attach to. Re-creating it any earlier would hand the async controller
    // creation a window that insertTab() then replaces.
    // A diff tab's Preview is permanently hidden, so there is nothing for
    // WebView2 to attach to and no reason to pay for a browser process.
    if (!page->diffView)
        page->preview->ensureInitialized();
    tabBar()->setTabData(inserted, QVariant::fromValue<qulonglong>(reinterpret_cast<qulonglong>(page)));
    tabBar()->setTabButton(inserted, QTabBar::RightSide, makeCloseButton(page));
    if (activate) {
        setCurrentIndex(inserted);
        // A hidden QPlainTextEdit cannot hold native focus; handing it over
        // would leave the window with nothing focused.
        if (page->editor->isVisibleTo(page->splitter))
            page->editor->restoreKeyboardFocus();
    }
    return inserted;
}

int TabWidget::indexOf(TabPage* page) const
{
    return m_pages.indexOf(page);
}

void TabWidget::moveTabPage(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to)
        return;
    tabBar()->moveTab(from, to);
}

void TabWidget::setDragVisual(TabPage* page, int insertIndex, bool destination)
{
    if (auto* bar = qobject_cast<TabBar*>(tabBar()))
        bar->setDragVisual(reinterpret_cast<qulonglong>(page), insertIndex, destination);
}

void TabWidget::clearDragVisual()
{
    if (auto* bar = qobject_cast<TabBar*>(tabBar()))
        bar->clearDragVisual();
}

// ---------------------------------------------------------------------------
TabPage* TabWidget::currentPage() const
{
    int i = currentIndex();
    return (i >= 0 && i < m_pages.size()) ? m_pages.at(i) : nullptr;
}

TabPage* TabWidget::pageAt(int i) const
{
    return (i >= 0 && i < m_pages.size()) ? m_pages.at(i) : nullptr;
}

MarkdownEditor* TabWidget::currentEditor()  const { auto* p = currentPage(); return p ? p->editor  : nullptr; }
PreviewWidget*  TabWidget::currentPreview() const { auto* p = currentPage(); return p ? p->preview : nullptr; }
DocumentModel*  TabWidget::currentModel()   const { auto* p = currentPage(); return p ? p->model   : nullptr; }

// ---------------------------------------------------------------------------
void TabWidget::markDirty(int index, bool dirty)
{
    if (TabPage* p = pageAt(index))
        p->model->setDirty(dirty);
    refreshTitle(index);
}

void TabWidget::refreshTitle(int index)
{
    if (TabPage* p = pageAt(index)) {
        QString name = p->model->displayName();
        if (p->model->hasExternalConflict())
            name.prepend(QStringLiteral("⚠ "));
        else if (p->model->isDirty())
            name.prepend(QStringLiteral("* "));
        setTabText(index, name);
        setTabToolTip(index, p->model->hasExternalConflict()
            ? tr("This file changed on disk. Auto Save is paused until you Reload or Keep My Version.")
            : QString());
    }
}

// ---------------------------------------------------------------------------
void TabWidget::setDarkMode(bool dark)
{
    m_darkMode = dark;
    for (auto* p : m_pages) {
        p->editor->setDarkMode(dark);
        p->preview->setDarkMode(dark);
        if (p->imageViewer)
            p->imageViewer->setDarkMode(dark);
        if (p->diffView)
            p->diffView->setDarkMode(dark);
        if (auto* splitter = dynamic_cast<ThemedSplitter*>(p->splitter))
            splitter->setDarkMode(dark);
    }
    for (QObject* child : tabBar()->children()) {
        if (auto* button = dynamic_cast<CloseTabButton*>(child))
            button->setDarkMode(dark);
    }
    if (auto* bar = qobject_cast<TabBar*>(tabBar()))
        bar->setDarkMode(dark);
}

// ---------------------------------------------------------------------------
void TabWidget::setCurrentLineHighlightEnabled(bool enabled)
{
    m_currentLineHighlightEnabled = enabled;
    for (auto* p : m_pages)
        p->editor->setCurrentLineHighlightEnabled(enabled);
}

void TabWidget::setPreviewFontFamily(const QString& family)
{
    m_previewFontFamily = family;
    for (auto* p : m_pages)
        p->preview->setBodyFontFamily(family);
}

void TabWidget::setDocumentSpacing(const DocumentSpacing::Settings& spacing)
{
    m_documentSpacing = DocumentSpacing::normalized(spacing);
    for (auto* p : m_pages)
        p->preview->setDocumentSpacing(m_documentSpacing);
}

void TabWidget::setSpellCheckActive(bool active)
{
    m_spellCheckActive = active;
    for (auto* p : m_pages)
        p->editor->setSpellCheckActive(active && spellCheckEligible(*p->model));
}

// ---------------------------------------------------------------------------
void TabWidget::onCurrentChanged(int index)
{
    if (TabPage* p = pageAt(index))
        emit editorActivated(p->editor, p->preview);
    else
        emit editorActivated(nullptr, nullptr);
    QTimer::singleShot(0, this, &TabWidget::updateTabBarLayout);
}

void TabWidget::onCloseRequested(int index)
{
    emit tabClosed(index);
}


void TabWidget::updateTabBarLayout()
{
    auto* bar = tabBar();
    if (!bar)
        return;

    QWidget* corner = cornerWidget(Qt::TopLeftCorner);
    const int w = (corner && !corner->isHidden()) ? corner->sizeHint().width() : 0;
    const QRect cornerRect(0, bar->y(), w, bar->height());
    if (bar->x() == w && bar->width() == width() - w
        && (!corner || corner->isHidden() || corner->geometry() == cornerRect))
        return;

    m_layoutGuard = true;
    if (corner && !corner->isHidden())
        corner->setGeometry(cornerRect);
    bar->move(w, bar->y());
    bar->resize(width() - w, bar->height());
    m_layoutGuard = false;
}

bool TabWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        QSplitter* splitter = nullptr;
        if (auto* handle = qobject_cast<QSplitterHandle*>(obj)) {
            splitter = qobject_cast<QSplitter*>(handle->parentWidget());
        } else if (auto* widget = qobject_cast<QWidget*>(obj)) {
            splitter = qobject_cast<QSplitter*>(widget->parentWidget());
            if (splitter && splitter->objectName() == QStringLiteral("editorPreviewSplitter")) {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                const QPoint position = widget->mapTo(splitter, mouseEvent->position().toPoint());
                const int handleCenter = splitter->handle(1)->geometry().center().x();
                if (qAbs(position.x() - handleCenter) > 8)
                    splitter = nullptr;
            }
        }

        if (splitter && splitter->objectName() == QStringLiteral("editorPreviewSplitter")) {
            // The splitter outlives this tab widget when its tab is dragged to
            // another window, so the pending callback must not hold a raw this.
            QPointer<TabWidget> self(this);
            QTimer::singleShot(0, splitter, [self, splitter]() {
                const int available = splitter->width() - splitter->handleWidth();
                const int first = available / 2;
                splitter->setSizes({ first, available - first });
                const QByteArray state = splitter->saveState();
                if (self)
                    self->m_cachedSplitterState = state;
                QSettings s;
                s.setValue(QStringLiteral("splitterState"), state);
            });
            return true;
        }
    }

    const bool cornerWidgetEvent = (obj == cornerWidget(Qt::TopLeftCorner));

    if ((obj == tabBar() || cornerWidgetEvent) &&
        (event->type() == QEvent::Move ||
         event->type() == QEvent::Resize ||
         event->type() == QEvent::Show ||
         event->type() == QEvent::Hide ||
         event->type() == QEvent::StyleChange ||
         event->type() == QEvent::FontChange) &&
        !m_layoutGuard)
        updateTabBarLayout();
    return false;
}

bool TabWidget::event(QEvent* event)
{
    bool result = QTabWidget::event(event);
    if (event->type() == QEvent::LayoutRequest)
        updateTabBarLayout();
    else if (event->type() == QEvent::StyleChange)
        updateTabBarLayout();
    return result;
}

void TabWidget::resizeEvent(QResizeEvent* event)
{
    QTabWidget::resizeEvent(event);
    updateTabBarLayout();
}

void TabWidget::showEvent(QShowEvent* event)
{
    QTabWidget::showEvent(event);
    updateTabBarLayout();
}

void TabWidget::tabInserted(int index)
{
    QTabWidget::tabInserted(index);
    updateTabBarLayout();
}

void TabWidget::tabRemoved(int index)
{
    QTabWidget::tabRemoved(index);
    updateTabBarLayout();
}
