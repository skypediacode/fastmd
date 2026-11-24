#pragma once

#include <QMenu>
#include <QPainter>
#include <QAction>
#include <QActionEvent>
#include <QPaintEvent>
#include <QShowEvent>
#include <QCursor>
#include <QKeySequence>
#include <QStyleOptionMenuItem>
#include <QList>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVariantAnimation>

#include <algorithm>

class AlignedMenu : public QMenu
{
    Q_OBJECT
public:
    // How the menu animates itself into view.
    //   DropDown    — grows downward out of the menu bar (File/Edit/View/Help).
    //   ContextMenu — fades in while sliding a few pixels toward its resting
    //                 place, away from the cursor. Size never changes, so no
    //                 frame of the animation costs a relayout.
    enum ShowAnimationStyle { DropDown, ContextMenu };

    explicit AlignedMenu(QWidget* parent = nullptr) : QMenu(parent)
    {
        init();
    }

    explicit AlignedMenu(const QString& title, QWidget* parent = nullptr) : QMenu(title, parent)
    {
        init();
    }

    void setCompactShortcutLayout(bool compact)
    {
        if (m_compactShortcutLayout == compact)
            return;
        m_compactShortcutLayout = compact;
        m_shortcutLayoutDirty = true;
    }

    // QMenu treats a tab in action text as its own native shortcut column.
    // Dynamic menus that prepare their layout before QMenu measures it need
    // ordinary spacing instead, otherwise that column is counted twice.
    void setShortcutTabPaddingEnabled(bool enabled)
    {
        if (m_shortcutTabPaddingEnabled == enabled)
            return;
        m_shortcutTabPaddingEnabled = enabled;
        m_shortcutLayoutDirty = true;
    }

    // Toolbar dropdowns use the same shortcut-column presentation as the
    // application menus, but normally omit action icons.  Keeping this on the
    // menu rather than stripping icons from QAction lets a caller opt in for a
    // particular dropdown without affecting the action anywhere else.
    void setActionIconsVisible(bool visible)
    {
        if (m_actionIconsVisible == visible)
            return;
        m_actionIconsVisible = visible;
        updateGeometry();
        update();
    }

    void setShowAnimationEnabled(bool enabled)
    {
        m_showAnimationEnabled = enabled;
    }

    void setShowAnimationDuration(int durationMs)
    {
        m_showAnimationDurationMs = qMax(0, durationMs);
    }

    void setShowAnimationStyle(ShowAnimationStyle style)
    {
        m_showAnimationStyle = style;
    }

    // Call after repopulating a dynamic menu during aboutToShow. QMenu emits
    // that signal before it calculates its final visible item geometry, but an
    // earlier slot may already have prepared our shortcut column.
    void refreshShortcutLayout()
    {
        m_shortcutLayoutDirty = true;
        rebuildShortcutLayout();
        applyShortcutLayout();
    }

    // Removed custom sizeHint to let Qt natively calculate it based on the tab-padded text

protected:
    bool event(QEvent* e) override
    {
        if (e->type() == QEvent::Show) {
            if (m_showAnimationEnabled) {
                m_opacityAnim->stop();
                m_geomAnim->stop();
                setWindowOpacity(0.0);
                m_animationPending = true;
                QTimer::singleShot(0, this, &AlignedMenu::startShowAnimation);
            }
        } else if (e->type() == QEvent::Hide) {
            m_animationPending = false;
            m_opacityAnim->stop();
            m_geomAnim->stop();
            setWindowOpacity(1.0);
            resetItemHover();
        }
        return QMenu::event(e);
    }

    void actionEvent(QActionEvent* event) override
    {
        QMenu::actionEvent(event);
        // The action set (and therefore possibly the shortcut column) changed --
        // invalidate the cached layout so it is recomputed lazily on the next
        // aboutToShow, instead of unconditionally on every single open.
        m_shortcutLayoutDirty = true;
    }

    void paintEvent(QPaintEvent* event) override
    {
        QMenu::paintEvent(event);

        // Cache QFontMetrics across paints (each animation frame repaints this
        // menu) instead of reconstructing it every call; only rebuilt when the
        // font actually changes.
        if (m_metricsFont != font()) {
            m_metrics = QFontMetrics(font());
            m_metricsFont = font();
        }

        QPainter painter(this);
        constexpr int rightPadding = 24;
        const QPoint cursorPos = mapFromGlobal(QCursor::pos());

        for (const auto& item : m_shortcutItems) {
            if (item.action.isNull() || !item.action->isVisible())
                continue;
            const QRect itemRect = actionGeometry(item.action.data());
            if (!itemRect.isValid())
                continue;

            const bool selected = itemRect.contains(cursorPos) && item.action->isEnabled();
            const QPalette::ColorGroup group = item.action->isEnabled()
                ? QPalette::Active : QPalette::Disabled;
            painter.setPen(palette().color(
                selected ? QPalette::Active : group,
                selected ? QPalette::HighlightedText : QPalette::Text));

            const int shortcutWidth = m_metrics.horizontalAdvance(item.text);
            const QRect shortcutRect(
                width() - rightPadding - shortcutWidth,
                itemRect.top(), shortcutWidth, itemRect.height());
            painter.drawText(shortcutRect, Qt::AlignRight | Qt::AlignVCenter, item.text);
        }
    }

private slots:
    void startShowAnimation()
    {
        if (!m_animationPending)
            return;
        m_animationPending = false;

        if (!isVisible()) {
            setWindowOpacity(1.0);
            return;
        }

        const QRect targetRect = geometry();
        if (targetRect.height() <= 10) {
            setWindowOpacity(1.0);
            return;
        }

        QRect startRect = targetRect;
        if (m_showAnimationStyle == ContextMenu) {
            // Slide in from whichever side the menu is going to open toward, so
            // the motion reads as the menu unfolding away from the pointer.
            // Qt flips a context menu above the cursor near the screen bottom.
            constexpr int kSlide = 8;
            const int cursorY = QCursor::pos().y();
            startRect.translate(0, targetRect.bottom() <= cursorY ? kSlide : -kSlide);
        } else {
            startRect.setTop(targetRect.top() - 12);
            startRect.setBottom(targetRect.top() + static_cast<int>(targetRect.height() * 0.85));
        }

        setGeometry(startRect);
        setWindowOpacity(0.0);

        m_opacityAnim->setDuration(m_showAnimationDurationMs);
        m_opacityAnim->setStartValue(0.0);
        m_opacityAnim->setEndValue(1.0);

        m_geomAnim->setDuration(m_showAnimationDurationMs);
        m_geomAnim->setStartValue(startRect);
        m_geomAnim->setEndValue(targetRect);

        m_opacityAnim->start();
        m_geomAnim->start();
    }

    void onAboutToShow()
    {
        // The shortcut-column width computation walks every action and measures
        // text with QFontMetrics -- previously this ran on every single open. It
        // now only recomputes when the action set has actually changed, which
        // removes a source of inconsistent frame timing on repeat opens.
        if (m_shortcutLayoutDirty)
            rebuildShortcutLayout();
        applyShortcutLayout();
    }

    void initStyleOption(QStyleOptionMenuItem* option, const QAction* action) const override
    {
        QMenu::initStyleOption(option, action);
        if (!m_actionIconsVisible) {
            option->icon = QIcon();
            option->maxIconWidth = 0;
        }
    }

    void applyShortcutLayout()
    {
        for (const auto& item : m_shortcutItems) {
            if (item.action.isNull())
                continue;
            if (!item.action->property("alignedMenuBaseText").isValid())
                item.action->setProperty("alignedMenuBaseText", item.originalText);
            item.action->setShortcutVisibleInContextMenu(false);
            item.action->setText(item.paddedText);
        }
    }

    void onAboutToHide()
    {
        for (const auto& item : m_shortcutItems) {
            if (!item.action.isNull()) {
                item.action->setShortcutVisibleInContextMenu(true);
                item.action->setText(item.originalText);
            }
        }
    }

private:
    void init()
    {
        setObjectName(QStringLiteral("topMenuBarMenu"));
        setProperty("menuHoverAnimated", true);
        m_opacityAnim = new QPropertyAnimation(this, "windowOpacity", this);
        m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_geomAnim = new QPropertyAnimation(this, "geometry", this);
        m_geomAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_itemHoverAnim = new QVariantAnimation(this);
        m_itemHoverAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_itemHoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            applyItemHoverColor(value.value<QColor>());
        });
        connect(this, &QMenu::aboutToShow, this, &AlignedMenu::onAboutToShow);
        connect(this, &QMenu::aboutToHide, this, &AlignedMenu::onAboutToHide);
        connect(this, &QMenu::hovered, this, [this](QAction* action) {
            if (action && action->isEnabled() && !action->isSeparator()) {
                if (m_hoveredAction != action) {
                    m_hoveredAction = action;
                    applyItemHoverColor(QColor(0, 120, 212, 0));
                }
                animateItemHover(true);
            }
        });
    }

    void animateItemHover(bool hovered)
    {
        const QColor target = hovered ? QColor(QStringLiteral("#0078d4"))
                                      : QColor(0, 120, 212, 0);
        const QColor current = m_itemHoverColor.isValid()
            ? m_itemHoverColor : QColor(0, 120, 212, 0);
        if (current == target)
            return;
        m_itemHoverAnim->stop();
        m_itemHoverAnim->setDuration(hovered ? 120 : 170);
        m_itemHoverAnim->setStartValue(current);
        m_itemHoverAnim->setEndValue(target);
        m_itemHoverAnim->start();
    }

    void resetItemHover()
    {
        m_itemHoverAnim->stop();
        m_hoveredAction = nullptr;
        applyItemHoverColor(QColor(0, 120, 212, 0));
    }

    void applyItemHoverColor(const QColor& color)
    {
        m_itemHoverColor = color;
        QPalette menuPalette = palette();
        for (QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive,
                                             QPalette::Disabled })
            menuPalette.setColor(group, QPalette::Highlight, color);
        setPalette(menuPalette);
    }

    // Qt's QKeySequence::toString() always renders modifiers in its own fixed
    // order (Ctrl, Alt, Shift, Meta), regardless of the order the modifiers
    // were OR'd together in code. This app's convention puts Shift before Alt
    // (e.g. "Shift+Alt+R"), so reorder the rendered token list to match.
    static QString reorderModifiers(const QString& nativeText)
    {
        static const QStringList kOrder = { QStringLiteral("Ctrl"), QStringLiteral("Shift"),
                                             QStringLiteral("Alt"), QStringLiteral("Meta") };
        QStringList parts = nativeText.split(QLatin1Char('+'));
        if (parts.size() < 2)
            return nativeText;
        const QString keyToken = parts.takeLast();
        std::stable_sort(parts.begin(), parts.end(), [](const QString& a, const QString& b)
                          { return kOrder.indexOf(a) < kOrder.indexOf(b); });
        parts.append(keyToken);
        return parts.join(QLatin1Char('+'));
    }

    void rebuildShortcutLayout()
    {
        m_shortcutItems.clear();
        int maxShortcutWidth = 0;
        const QFontMetrics metrics(font());

        for (QAction* action : actions()) {
            // A toolbar mirror may deliberately leave its QAction shortcut
            // unbound: the canonical menu action owns the binding, while the
            // mirror supplies this display-only value for an overflow menu.
            const QString displayedShortcut = action
                ? action->property("alignedMenuShortcut").toString() : QString();
            const QString shortcutText = !displayedShortcut.isEmpty()
                ? displayedShortcut
                : (action ? action->shortcut().toString(QKeySequence::NativeText) : QString());
            if (!action || shortcutText.isEmpty()
                || (action->property("alignedMenuShowShortcut").isValid()
                    && !action->property("alignedMenuShowShortcut").toBool()))
                continue;
            ShortcutItem item;
            item.action = action;
            item.text = reorderModifiers(shortcutText);
            // Overflow proxy actions can be rebuilt before a prior hide has
            // restored their padded label. Retain the base label for the
            // proxy's lifetime so each layout pass is idempotent.
            const QVariant baseText = action->property("alignedMenuBaseText");
            item.originalText = baseText.isValid() ? baseText.toString() : action->text();
            m_shortcutItems.append(item);
            maxShortcutWidth = qMax(maxShortcutWidth, metrics.horizontalAdvance(item.text));
        }

        if (maxShortcutWidth > 0) {
            const int spaces = (maxShortcutWidth + 12)
                / qMax(1, metrics.horizontalAdvance(QLatin1Char(' ')));
            const QString padding = m_compactShortcutLayout
                ? (m_shortcutTabPaddingEnabled ? QStringLiteral("\t\t") : QString(2, QLatin1Char(' ')))
                : (m_shortcutTabPaddingEnabled ? QStringLiteral("\t") : QString())
                    + QString(spaces, QLatin1Char(' '));
            for (auto& item : m_shortcutItems)
                item.paddedText = item.originalText + padding;
        } else {
            for (auto& item : m_shortcutItems)
                item.paddedText = item.originalText;
        }

        m_shortcutLayoutDirty = false;
    }

    struct ShortcutItem {
        QPointer<QAction> action;
        QString text;
        QString originalText;
        QString paddedText;
    };
    QList<ShortcutItem> m_shortcutItems;
    bool m_shortcutLayoutDirty = true;
    bool m_compactShortcutLayout = false;
    bool m_shortcutTabPaddingEnabled = true;
    bool m_actionIconsVisible = true;
    bool m_showAnimationEnabled = true;
    bool m_animationPending = false;
    ShowAnimationStyle m_showAnimationStyle = DropDown;
    int m_showAnimationDurationMs = 150; // transition duration of File/Edit/View/Help main app menu in milliseconds (matches Material Design spec)
    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_geomAnim = nullptr;
    QVariantAnimation* m_itemHoverAnim = nullptr;
    QColor m_itemHoverColor{0, 120, 212, 0};
    QPointer<QAction> m_hoveredAction;
    QFont m_metricsFont;
    QFontMetrics m_metrics{QFont()};
};

// The common presentation for menus opened by a main-toolbar button.  It
// inherits AlignedMenu's right-aligned shortcut column and deliberately hides
// action icons by default; callers such as Alignment can opt in with
// setActionIconsVisible(true).
class ToolbarDropdownMenu : public AlignedMenu
{
public:
    explicit ToolbarDropdownMenu(QWidget* parent = nullptr) : AlignedMenu(parent)
    {
        setObjectName(QStringLiteral("toolbarDropdownMenu"));
        setActionIconsVisible(false);
    }
};
