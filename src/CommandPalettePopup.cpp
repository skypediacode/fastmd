#include "CommandPalettePopup.h"
#include "DarkTheme.h"
#include "CommandPaletteLauncher.h"
#include "CommandRegistry.h"
#include "IconHelper.h"
#include "ThemePalette.h"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRegion>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QFontInfo>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

enum CommandRole {
    IdRole = Qt::UserRole + 1,
    CategoryRole,
    ShortcutRole,
    HitsRole,
};

constexpr int RowHeight   = 28;
constexpr int MaxListRows = 12;
constexpr int ClearSize   = CommandPalette::IconSize;
constexpr int ClearButtonSize = 18;
constexpr int LeftInset   = 10;
constexpr int RightInset  = 10;
constexpr int IconGap     = 5;

// QAbstractScrollArea gives its scroll bar a rectangular child widget.  A
// style-sheet radius only rounds that widget's paint, not its visible bounds,
// so it can still cover the palette's rounded lower-right corner.  Clip only
// that corner of the scrollbar itself; the scrollbar remains visible and
// scrolls exactly as normal.
class CommandPaletteScrollBar final : public QScrollBar {
public:
    explicit CommandPaletteScrollBar(QWidget* parent)
        : QScrollBar(Qt::Vertical, parent)
    {
    }

    void refreshCornerMask()
    {
        updateCornerMask();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QScrollBar::resizeEvent(event);
        updateCornerMask();
    }

    bool event(QEvent* event) override
    {
        const bool refreshAfterPolish = event->type() == QEvent::Polish
            || event->type() == QEvent::StyleChange
            || event->type() == QEvent::Show;
        const bool handled = QScrollBar::event(event);

        // On startup Qt can apply the application stylesheet after the scroll
        // area has laid out its child scrollbar.  Refresh on the next event
        // turn, after that polish is complete; changing themes already takes
        // this path, which is why it previously appeared to repair itself.
        if (refreshAfterPolish)
            QTimer::singleShot(0, this, [this] { refreshCornerMask(); });
        return handled;
    }

private:
    void updateCornerMask()
    {
        constexpr int radius = 8;
        const int cornerRadius = qMin(radius, qMin(width(), height()));
        if (cornerRadius <= 0) {
            clearMask();
            return;
        }

        QPainterPath shape;
        shape.moveTo(0, 0);
        shape.lineTo(width(), 0);
        shape.lineTo(width(), height() - cornerRadius);
        shape.quadTo(width(), height(), width() - cornerRadius, height());
        shape.lineTo(0, height());
        shape.closeSubpath();
        setMask(QRegion(shape.toFillPolygon().toPolygon()));
    }
};

} // namespace

// ---------------------------------------------------------------------------
// The palette's search row. It paints the very same box CommandPaletteLauncher
// does (shared CommandPalette::paintSearchBox) and hosts a chrome-free
// QLineEdit on top, so opening the palette looks like the launcher gaining a
// caret rather than a different widget appearing over it.
//
// The clear button is a real tool button so it can share the Workspace Search
// Files visual treatment. Its geometry remains independently centered while
// the search glyph and editable content receive their optical correction.
class CommandPaletteSearchRow : public QWidget {
public:
    explicit CommandPaletteSearchRow(QWidget* parent) : QWidget(parent)
    {
        setObjectName(QStringLiteral("commandPaletteSearchRow"));
        setFixedHeight(CommandPalette::BoxHeight);
        setMouseTracking(true);
    }

    void setDarkMode(bool dark)
    {
        m_dark = dark;
        m_background = dark ? DarkTheme::color(DarkTheme::Elevated) : QColor(0xff, 0xff, 0xff);
        // Keep the magnifier visually continuous with the launcher beneath
        // the popup. Opening the palette moves focus to the line edit, but
        // that state must not recolor the icon.
        const QColor iconColor = dark ? QColor(0x99, 0x99, 0x99) : QColor(0x70, 0x70, 0x70);
        const QColor clearColor = dark ? QColor(0x99, 0x99, 0x99) : QColor(0x70, 0x70, 0x70);
        m_placeholderColor = dark ? QColor(0x8c, 0x8c, 0x8c) : QColor(0x77, 0x77, 0x77);
        m_icon = CommandPalette::searchIcon(iconColor, CommandPalette::IconSize);
        // Match the Workspace Search Files close glyph and its theme color.
        if (m_clearButton)
            m_clearButton->setIcon(IconHelper::materialIcon(QChar(0xE5CD), clearColor, 12));
        update();
    }

    void setLineEdit(QLineEdit* edit)
    {
        m_edit = edit;
        m_edit->setParent(this);
        QObject::connect(m_edit, &QLineEdit::textChanged, this, [this](const QString& text) {
            setClearVisible(!text.isEmpty());
            update();
        });
        m_clearButton = new QToolButton(this);
        m_clearButton->setObjectName(QStringLiteral("workspaceSearchClearButton"));
        m_clearButton->setFixedSize(ClearButtonSize, ClearButtonSize);
        m_clearButton->setIconSize(QSize(12, 12));
        m_clearButton->setAutoRaise(true);
        m_clearButton->setFocusPolicy(Qt::NoFocus);
        m_clearButton->setCursor(Qt::PointingHandCursor);
        m_clearButton->setToolTip(tr("Clear search"));
        m_clearButton->hide();
        QObject::connect(m_clearButton, &QToolButton::clicked, m_edit, &QLineEdit::clear);
        layoutLineEdit();
    }

    // Painted here instead of via QLineEdit::setPlaceholderText(), whose
    // internal draw path sits a device pixel off from where real typed text
    // lands (see the vertical-centering comment in paintEvent() below).
    void setPlaceholder(const QString& text, const QFont& font)
    {
        m_placeholderText = text;
        m_placeholderFont = font;
        update();
    }

    // Sub-pixel correction applied to the row's pixmaps so they rasterize onto
    // the same device row the launcher's do -- see anchorSubPixelOffset().
    void setIconOffset(qreal offset)
    {
        if (qFuzzyCompare(m_iconOffset, offset))
            return;
        m_iconOffset = offset;
        update();
    }

    void setClearVisible(bool visible)
    {
        if (m_clearVisible == visible)
            return;
        m_clearVisible = visible;
        if (m_clearButton)
            m_clearButton->setVisible(visible);
        update();
    }

    static int textLeft() { return LeftInset + CommandPalette::IconSize + IconGap; }
    static int textRight() { return RightInset + ClearSize + IconGap; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        CommandPalette::paintSearchBox(painter, rect(), m_background, m_dark, false);
        painter.drawPixmap(QPointF(LeftInset, CommandPalette::iconTop(height())
                                             + m_iconOffset), m_icon);
        if (m_edit && m_edit->text().isEmpty() && !m_placeholderText.isEmpty()) {
            painter.setFont(m_placeholderFont);
            painter.setPen(m_placeholderColor);
            const QRect editRect = m_edit->geometry();
            const QFontMetrics fm(m_placeholderFont);
            const int baseline =
                CommandPalette::textBaseline(editRect.y(), editRect.height(), fm);
            painter.drawText(editRect.x(), baseline, m_placeholderText);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        QWidget::leaveEvent(event);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        layoutLineEdit();
    }

private:
    // Positions the line edit directly rather than through a QLayout: some
    // native styles report a different sizeHint() for a QLineEdit once it has
    // focus (extra internal frame/caret metrics baked into the style even
    // with setFrame(false)), and a QVBoxLayout with Qt::AlignVCenter re-centers
    // on that sizeHint on every relayout, so the text visibly dropped a couple
    // of pixels the moment the field gained focus. A fixed geometry computed
    // from this row's own height sidesteps the native style entirely.
    void layoutLineEdit()
    {
        if (!m_edit)
            return;
        const int left = textLeft();
        const int right = width() - textRight();
        // The edit keeps the row's full logical height so Qt's text and caret
        // share the same 2dp optical correction as the painted search glyph.
        m_edit->setGeometry(left, -CommandPalette::SearchContentYOffset,
                            qMax(0, right - left), height());
        if (m_clearButton)
            m_clearButton->setGeometry(width() - RightInset - ClearButtonSize,
                                        (height() - ClearButtonSize) / 2,
                                        ClearButtonSize, ClearButtonSize);
    }

    QPointer<QLineEdit> m_edit;
    QToolButton* m_clearButton = nullptr;
    QPixmap m_icon;
    QColor  m_background;
    QString m_placeholderText;
    QFont   m_placeholderFont;
    QColor  m_placeholderColor;
    qreal   m_iconOffset = 0.0;
    bool    m_dark = false;
    bool    m_clearVisible = false;
};

// Draws one result row: dimmed "Category:" prefix, the command title with its
// matched characters emboldened, and the shortcut right-aligned.
class CommandItemDelegate : public QStyledItemDelegate {
public:
    explicit CommandItemDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

    void setDarkMode(bool dark) { m_dark = dark; }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        return QSize(0, RowHeight);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::TextAntialiasing, true);

        const bool selected = option.state & QStyle::State_Selected;
        const bool usable   = index.flags() & Qt::ItemIsEnabled;

        ThemeCard::paintRow(*painter, option.rect, m_dark, selected,
                            option.state & QStyle::State_MouseOver);

        QColor titleColor = m_dark ? QColor(0xdd, 0xdd, 0xdd) : QColor(0x1f, 0x1f, 0x1f);
        QColor dimColor   = m_dark ? QColor(0x8c, 0x8c, 0x8c) : QColor(0x77, 0x77, 0x77);
        if (!usable) {
            titleColor = dimColor;
            dimColor   = m_dark ? QColor(0x60, 0x60, 0x60) : QColor(0xa6, 0xa6, 0xa6);
        }

        QFont baseFont = option.font;
        QFont boldFont = baseFont;
        boldFont.setBold(true);

        const QRect content = option.rect.adjusted(LeftInset, 0, -LeftInset, 0);
        int x = content.left();

        // Shortcut first: it reserves the right edge, everything else elides
        // into whatever is left.
        const QString shortcut = index.data(ShortcutRole).toString();
        int rightBudget = 0;
        if (!shortcut.isEmpty()) {
            const QFontMetrics fm(baseFont);
            rightBudget = fm.horizontalAdvance(shortcut) + 12;
            painter->setFont(baseFont);
            painter->setPen(dimColor);
            painter->drawText(QRect(content.right() - rightBudget + 12, content.top(),
                                    rightBudget - 12, content.height()),
                              Qt::AlignRight | Qt::AlignVCenter, shortcut);
        }

        const int available = content.width() - rightBudget;
        const QString category = index.data(CategoryRole).toString();
        if (!category.isEmpty()) {
            const QFontMetrics fm(baseFont);
            const QString prefix = category + QStringLiteral(": ");
            const int natural = fm.horizontalAdvance(prefix);
            const int width = qMin(natural, available / 2);
            painter->setFont(baseFont);
            painter->setPen(dimColor);
            // Elide only when the cap actually bit: passing the exact natural
            // advance to elidedText() still trims the last glyph.
            painter->drawText(QRect(x, content.top(), width, content.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              width < natural ? fm.elidedText(prefix, Qt::ElideRight, width)
                                              : prefix);
            x += width;
        }

        const QString title = index.data(Qt::DisplayRole).toString();
        QSet<int> hits;
        const QVariantList hitList = index.data(HitsRole).toList();
        for (const QVariant& hit : hitList)
            hits.insert(hit.toInt());

        const int titleBudget = content.left() + available - x;
        painter->setPen(titleColor);
        drawHighlighted(painter, QRect(x, content.top(), titleBudget, content.height()),
                        title, hits, baseFont, boldFont);

        painter->restore();
    }

private:
    // Draws `text` left-aligned in `rect`, emboldening the characters whose
    // index is in `hits`, and appending an ellipsis if it does not fit.
    static void drawHighlighted(QPainter* painter, const QRect& rect, const QString& text,
                                const QSet<int>& hits, const QFont& base, const QFont& bold)
    {
        const QFontMetrics fmBase(base);
        const QFontMetrics fmBold(bold);
        const int ellipsisWidth = fmBase.horizontalAdvance(QStringLiteral("…"));

        int x = rect.left();
        for (int i = 0; i < text.size(); ++i) {
            const bool hit = hits.contains(i);
            const QFont& font = hit ? bold : base;
            const QFontMetrics& fm = hit ? fmBold : fmBase;
            const int advance = fm.horizontalAdvance(text.at(i));
            const bool last = (i == text.size() - 1);
            if (x + advance > rect.right() && !last) {
                painter->setFont(base);
                painter->drawText(QRect(x, rect.top(), ellipsisWidth, rect.height()),
                                  Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("…"));
                return;
            }
            painter->setFont(font);
            painter->drawText(QRect(x, rect.top(), advance + 1, rect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, text.mid(i, 1));
            x += advance;
        }
    }

    bool m_dark = false;
};

// ---------------------------------------------------------------------------
CommandPalettePopup::CommandPalettePopup(CommandRegistry* registry, QWidget* parent)
    : QWidget(parent), m_registry(registry)
{
    setObjectName(QStringLiteral("commandPalettePopup"));
    // Permanently Qt::Tool: see the class comment and Rule 11.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_TranslucentBackground);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_searchRow = new CommandPaletteSearchRow(this);
    m_search = new QLineEdit(m_searchRow);
    m_search->setObjectName(QStringLiteral("commandPaletteSearchEdit"));
    m_search->setFrame(false);
    m_search->setTextMargins(0, 0, 0, 0);
    // Match the row's fixed height exactly: QLineEdit's own sizeHint (frame
    // metrics baked into the native style even with setFrame(false)) can run
    // taller than the row, which throws off both its own vertical centering
    // and where it centers the trailing clear action.
    m_search->setFixedHeight(CommandPalette::BoxHeight);
    QFont searchFont = m_search->font();
    searchFont.setPixelSize(QFontInfo(searchFont).pixelSize() + 2);
    m_search->setFont(searchFont);
    m_search->installEventFilter(this);
    m_searchRow->setLineEdit(m_search);
    m_emptyText = tr("No matching commands");
    m_searchRow->setPlaceholder(tr("Type a command name…"), searchFont);
    connect(m_search, &QLineEdit::textChanged, this, &CommandPalettePopup::refilter);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("commandPaletteList"));
    m_list->setAutoFillBackground(false);
    m_list->viewport()->setAutoFillBackground(false);
    m_list->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBar(new CommandPaletteScrollBar(m_list));
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setMouseTracking(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_delegate = new CommandItemDelegate(this);
    m_list->setItemDelegate(m_delegate);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        activateCurrent();
    });

    layout->addWidget(m_searchRow, 0);
    layout->addWidget(m_list, 1);

    setDarkMode(false);
}

void CommandPalettePopup::setDarkMode(bool dark)
{
    m_dark = dark;
    m_searchRow->setDarkMode(dark);
    m_delegate->setDarkMode(dark);
    update();
}

int CommandPalettePopup::rowHeight() const
{
    return RowHeight;
}

void CommandPalettePopup::showForAnchor(QWidget* anchor)
{
    m_showingChoices = false;
    m_emptyText = tr("No matching commands");
    m_searchRow->setPlaceholder(tr("Type a command name…"), m_search->font());
    showList(anchor);
}

void CommandPalettePopup::showChoices(QWidget* anchor, CommandRegistry* choices,
                                      const QString& placeholder, const QString& emptyText)
{
    // The previous list may still be running the choice that opened this one.
    if (m_choices && m_choices != choices)
        m_choices->deleteLater();
    m_choices = choices;
    if (choices)
        choices->setParent(this);
    m_showingChoices = true;
    m_emptyText = emptyText;
    m_searchRow->setPlaceholder(placeholder, m_search->font());
    showList(anchor);
}

CommandRegistry* CommandPalettePopup::currentRegistry() const
{
    return m_showingChoices ? m_choices.data() : m_registry;
}

void CommandPalettePopup::showList(QWidget* anchor)
{
    m_anchor = anchor;
    m_search->blockSignals(true);
    m_search->clear();
    m_search->blockSignals(false);
    m_searchRow->setClearVisible(false);
    refilter();

    show();
    // The native tool window and its children receive their final geometry as
    // it is shown. Reapply the same anchor formula on the next event turn so
    // the focused row (including its painted magnifier) cannot drift from the
    // unfocused launcher at startup, then refresh the scrollbar corner mask.
    QTimer::singleShot(0, this, [this] {
        updateGeometryForAnchor();
        static_cast<CommandPaletteScrollBar*>(m_list->verticalScrollBar())->refreshCornerMask();
    });
    raise();
    // A Qt::Tool window must actually be active for the WindowDeactivate-based
    // dismissal in event() to fire and for typing to reach the search field.
    activateWindow();
    m_search->setFocus(Qt::PopupFocusReason);
    emit visibilityChanged(true);
}

// The sub-pixel gap between where the anchor paints its search box and where
// this popup paints its own, in logical units, for the search row to absorb.
//
// Moving the native window cannot close this gap: Qt re-derives a *logical*
// origin from whatever native position the window has and paints its children
// from that, so window content is quantized to whole logical pixels however the
// window itself is placed. (Measured: nudging the native window one pixel moved
// the icon two, because it also flipped Qt's rounded logical origin.) The
// anchor's box, however, is a child inside another window and can sit on a
// fractional device row -- at 150% the launcher's box top is device 780.5 while
// the popup's is 780. Half a pixel is invisible in the box fill and absorbed by
// glyph rasterization, but the magnifier is a pixmap with hard edges and snaps
// a whole device pixel the other way, which is exactly what was visible.
//
// So align the paint, not the window: QPainter under a fractional scale can
// address an individual device row via a fractional logical coordinate.
qreal CommandPalettePopup::anchorSubPixelOffset(const QPoint& topLeftInAnchorWindow) const
{
    QWidget* const anchorWindow = m_anchor ? m_anchor->window() : nullptr;
    if (!anchorWindow || !anchorWindow->windowHandle() || !windowHandle())
        return 0.0;

    const auto anchorHwnd = reinterpret_cast<HWND>(anchorWindow->winId());
    const auto popupHwnd = reinterpret_cast<HWND>(winId());
    if (!anchorHwnd || !popupHwnd)
        return 0.0;

    // Both positions are read as Win32 screen coordinates, which are always
    // physical. Do NOT derive the popup's device top as `logicalTop * dpr`:
    // Qt's global logical space gives every screen its own origin and scale, so
    // that identity only holds on the primary monitor -- on a secondary one it
    // produced an offset large enough to push the magnifier out of the row.
    POINT clientOrigin { 0, 0 };
    RECT popupRect {};
    if (!ClientToScreen(anchorHwnd, &clientOrigin) || !GetWindowRect(popupHwnd, &popupRect))
        return 0.0;

    const qreal dpr = anchorWindow->windowHandle()->devicePixelRatio();
    if (dpr <= 0.0)
        return 0.0;

    const qreal anchorBoxDeviceTop = clientOrigin.y + topLeftInAnchorWindow.y() * dpr;
    const qreal offset = (anchorBoxDeviceTop - popupRect.top) / dpr;
    // A genuine rounding residual is under one logical pixel by construction.
    // Anything larger means the two windows are not where this assumed, so
    // correct nothing rather than displacing the icon.
    return qAbs(offset) < 1.0 ? offset : 0.0;
}

void CommandPalettePopup::updateGeometryForAnchor()
{
    const int rows = qMax(1, m_list->count());
    const int listHeight = qMin(rows, MaxListRows) * rowHeight() + 2;
    int width = CommandPalette::PreferredWidth;
    QPoint topLeft;
    // Where this popup's top-left sits inside the anchor's own window, in
    // logical units -- the input alignNativelyToAnchor() needs below.
    QPoint topLeftInAnchorWindow;

    if (m_anchor) {
        const int boxTop = (m_anchor->height() - CommandPalette::BoxHeight) / 2;
        // Deliberately NOT m_anchor->mapToGlobal(). That converts the point to
        // native pixels and back, so under fractional display scaling it rounds
        // twice and can report the launcher a whole logical pixel below where it
        // actually paints; placing the popup there then rounds a third time. At
        // 125% that stacked up to the focused search row sitting two physical
        // pixels below the unfocused launcher -- visible as the text and
        // magnifier stepping down the moment the palette opened.
        //
        // Composing the anchor's offset *within* its own window with that
        // window's global client origin stays in one logical coordinate space
        // and rounds once. It is also safe across a mixed-DPI desktop, since
        // both terms come from the same window on the same screen.
        QWidget* const anchorWindow = m_anchor->window();
        const QPoint boxInWindow = m_anchor->mapTo(anchorWindow, QPoint(0, boxTop));
        const QPoint anchorBoxTopLeft = anchorWindow->geometry().topLeft() + boxInWindow;
        width = qMax(m_anchor->width(), CommandPalette::MinimumWidth);
        const int right = anchorBoxTopLeft.x() + m_anchor->width();
        topLeft = QPoint(right - width, anchorBoxTopLeft.y());
        topLeftInAnchorWindow = boxInWindow + QPoint(m_anchor->width() - width, 0);
    } else {
        topLeft = QPoint(100, 100);
    }

    QRect geometry(topLeft, QSize(width, CommandPalette::BoxHeight + listHeight));
    if (QScreen* screen = QGuiApplication::screenAt(topLeft + QPoint(width / 2, 0))) {
        const QRect available = screen->availableGeometry();
        if (geometry.bottom() > available.bottom())
            geometry.setHeight(qMax(CommandPalette::BoxHeight + rowHeight(),
                                    available.bottom() - geometry.top()));
        if (geometry.left() < available.left())
            geometry.moveLeft(available.left());
    }
    setGeometry(geometry);

    const bool anchored = m_anchor && geometry.topLeft() == topLeft;
    m_searchRow->setIconOffset(anchored ? anchorSubPixelOffset(topLeftInAnchorWindow) : 0.0);
}

void CommandPalettePopup::refilter()
{
    m_list->clear();
    CommandRegistry* const registry = currentRegistry();
    if (!registry && !m_showingChoices) {
        updateGeometryForAnchor();
        return;
    }

    const QVector<CommandMatch> matches = registry
        ? registry->search(m_search->text()) : QVector<CommandMatch>();
    for (const CommandMatch& match : matches) {
        const Command* command = match.command;
        auto* item = new QListWidgetItem(command->title, m_list);
        item->setData(IdRole, command->id);
        item->setData(CategoryRole, command->category);
        if (!command->shortcut.isEmpty())
            item->setData(ShortcutRole, command->shortcut.toString(QKeySequence::NativeText));
        QVariantList hits;
        hits.reserve(match.titleHits.size());
        for (int hit : match.titleHits)
            hits.append(hit);
        item->setData(HitsRole, hits);

        const bool usable = !command->enabled || command->enabled();
        item->setFlags(usable ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::NoItemFlags);
    }

    if (m_list->count() == 0) {
        auto* empty = new QListWidgetItem(m_emptyText, m_list);
        empty->setFlags(Qt::NoItemFlags);
    } else {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->flags() & Qt::ItemIsSelectable) {
                m_list->setCurrentRow(i);
                break;
            }
        }
    }
    updateGeometryForAnchor();
}

void CommandPalettePopup::moveSelection(int delta)
{
    const int count = m_list->count();
    if (count == 0)
        return;
    int row = m_list->currentRow();
    for (int step = 0; step < count; ++step) {
        row += delta;
        if (row < 0)
            row = count - 1;
        else if (row >= count)
            row = 0;
        if (m_list->item(row)->flags() & Qt::ItemIsSelectable) {
            m_list->setCurrentRow(row);
            m_list->scrollToItem(m_list->item(row));
            return;
        }
    }
}

void CommandPalettePopup::activateCurrent()
{
    QListWidgetItem* item = m_list->currentItem();
    QPointer<CommandRegistry> registry = currentRegistry();
    if (!item || !(item->flags() & Qt::ItemIsSelectable) || !registry)
        return;
    const QString id = item->data(IdRole).toString();

    hide();

    // Run after the popup is gone so any dialog the command opens is parented
    // to a live, active main window and inherits focus cleanly. The callback
    // is copied out first: a command may open a new choice list, which
    // retires the registry being searched here.
    QTimer::singleShot(0, this, [registry, id]() {
        if (!registry)
            return;
        for (const Command& command : registry->commands()) {
            if (command.id != id)
                continue;
            if (command.enabled && !command.enabled())
                return;
            if (const std::function<void()> run = command.run)
                run();
            return;
        }
    });
}

bool CommandPalettePopup::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Down:     moveSelection(1);  return true;
        case Qt::Key_Up:       moveSelection(-1); return true;
        case Qt::Key_PageDown: moveSelection(MaxListRows - 1);  return true;
        case Qt::Key_PageUp:   moveSelection(-(MaxListRows - 1)); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:    activateCurrent(); return true;
        case Qt::Key_Escape:   hide();            return true;
        default: break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CommandPalettePopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool CommandPalettePopup::event(QEvent* event)
{
    // Replaces the click-outside dismissal a Qt::Popup would provide.
    if (event->type() == QEvent::WindowDeactivate && isVisible())
        hide();
    return QWidget::event(event);
}

void CommandPalettePopup::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_showingChoices = false;
    emit visibilityChanged(false);
}
