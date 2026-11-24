#include "MenuCommandBar.h"
#include "IconHelper.h"

#include <QAction>
#include <QBoxLayout>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <QWidgetAction>

namespace {

constexpr ushort kIconCut       = 0xE14E; // content_cut
constexpr ushort kIconCopy      = 0xE14D; // content_copy
constexpr ushort kIconPaste     = 0xE14F; // content_paste
constexpr ushort kIconSelectAll = 0xE162; // select_all
constexpr ushort kIconDelete    = 0xE872; // delete

constexpr int kButtonMinWidth = 64;
constexpr int kIconPx         = 20;
constexpr int kDividerInset   = 8;

ushort glyphFor(int command)
{
    switch (command) {
    case MenuCommandBar::Cut:       return kIconCut;
    case MenuCommandBar::Copy:      return kIconCopy;
    case MenuCommandBar::Paste:     return kIconPaste;
    case MenuCommandBar::SelectAll: return kIconSelectAll;
    default:                        return kIconDelete;
    }
}

QString labelFor(int command)
{
    switch (command) {
    case MenuCommandBar::Cut:       return MenuCommandBar::tr("Cut");
    case MenuCommandBar::Copy:      return MenuCommandBar::tr("Copy");
    case MenuCommandBar::Paste:     return MenuCommandBar::tr("Paste");
    case MenuCommandBar::SelectAll: return MenuCommandBar::tr("Select All");
    default:                        return MenuCommandBar::tr("Delete");
    }
}

bool isSelectable(const QAction* action)
{
    return action && action->isVisible() && action->isEnabled() && !action->isSeparator();
}

} // namespace

MenuCommandBar::MenuCommandBar() : QWidget(nullptr)
{
    setObjectName(QStringLiteral("menuCommandBar"));

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(2, 4, 2, 4);
    row->setSpacing(1);

    for (int i = 0; i < CommandCount; ++i) {
        auto* button = new QToolButton(this);
        button->setObjectName(QStringLiteral("menuCommandButton"));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(kIconPx, kIconPx));
        button->setText(labelFor(i));
        button->setAccessibleName(labelFor(i));
        button->setFocusPolicy(Qt::NoFocus);
        button->setMinimumWidth(kButtonMinWidth);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->installEventFilter(this);
        connect(button, &QToolButton::clicked, this, [this, i]() { triggerCommand(i); });
        row->addWidget(button, 1);
        m_buttons[i] = button;
    }
}

MenuCommandBar* MenuCommandBar::create(QObject* owner)
{
    auto* bar = new MenuCommandBar();
    bar->m_widgetAction = new QWidgetAction(owner);
    // Must stay enabled: QMenu mirrors a widget action's enabled state onto its
    // default widget, so disabling the carrier greys out the whole strip. The
    // bar keeps QMenu's navigation off the row by filtering the menu's keys
    // instead.
    bar->m_widgetAction->setDefaultWidget(bar);
    return bar;
}

void MenuCommandBar::setIconColor(const QColor& color)
{
    if (m_iconColor == color && m_dividerColor.isValid())
        return;
    m_iconColor = color;
    // A light glyph color means a dark menu; derive the decoration colors from
    // that rather than threading the theme flag through every call site.
    const bool dark = color.lightness() > 128;
    m_dividerColor = dark ? QColor(255, 255, 255, 36) : QColor(0, 0, 0, 30);
    m_selectionFill = dark ? QColor(57, 148, 188, 120) : QColor(0, 120, 212, 42);
    m_selectionBorder = dark ? QColor(57, 148, 188, 220) : QColor(0, 120, 212, 200);
    rebuildIcons();
    update();
}

void MenuCommandBar::rebuildIcons()
{
    for (int i = 0; i < CommandCount; ++i)
        m_buttons[i]->setIcon(IconHelper::materialIconTight(QChar(glyphFor(i)), m_iconColor, kIconPx));
}

void MenuCommandBar::setCommands(QAction* cut, QAction* copy, QAction* paste,
                                 QAction* selectAll, QAction* remove)
{
    QAction* const incoming[CommandCount] = { cut, copy, paste, selectAll, remove };
    for (int i = 0; i < CommandCount; ++i) {
        m_commands[i] = incoming[i];
        QAction* action = incoming[i];
        m_buttons[i]->setEnabled(action && action->isEnabled());

        // No tooltip: the button already carries its label, so one would only
        // repeat it over the menu. The shortcut still reaches assistive tech
        // through the accessible description.
        QString description = labelFor(i);
        if (action && !action->shortcut().isEmpty())
            description += QStringLiteral(" (%1)")
                               .arg(action->shortcut().toString(QKeySequence::NativeText));
        m_buttons[i]->setAccessibleDescription(description);
    }
    m_current = -1;
}

void MenuCommandBar::attachTo(QMenu* menu)
{
    if (!menu || !m_widgetAction)
        return;
    if (m_menu && m_menu != menu)
        detach();
    m_menu = menu;
    m_current = -1;
    menu->addAction(m_widgetAction);
    menu->addSeparator();
    menu->installEventFilter(this);
}

void MenuCommandBar::detach()
{
    if (m_menu)
        m_menu->removeEventFilter(this);
    m_menu = nullptr;
    m_current = -1;
    for (auto& command : m_commands)
        command = nullptr;
}

void MenuCommandBar::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_current >= 0 && m_current < CommandCount && m_selectionFill.isValid()) {
        const QRectF cell = QRectF(m_buttons[m_current]->geometry()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(QPen(m_selectionBorder, 1));
        painter.setBrush(m_selectionFill);
        painter.drawRoundedRect(cell, 4, 4);
    }

    if (!m_dividerColor.isValid())
        return;
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(m_dividerColor, 1));
    const int top = kDividerInset;
    const int bottom = height() - kDividerInset;
    if (bottom <= top)
        return;
    for (int i = 1; i < CommandCount; ++i) {
        if (m_current == i || m_current == i - 1)
            continue;
        const qreal x = m_buttons[i]->geometry().left() - 0.5;
        painter.drawLine(QPointF(x, top), QPointF(x, bottom));
    }
}

int MenuCommandBar::indexOfButton(const QObject* object) const
{
    for (int i = 0; i < CommandCount; ++i) {
        if (m_buttons[i] == object)
            return i;
    }
    return -1;
}

int MenuCommandBar::firstEnabledButton(int step) const
{
    const int start = step > 0 ? 0 : CommandCount - 1;
    for (int i = 0; i < CommandCount; ++i) {
        const int index = start + step * i;
        if (index >= 0 && index < CommandCount && m_buttons[index]->isEnabled())
            return index;
    }
    return -1;
}

QAction* MenuCommandBar::firstSelectableMenuAction() const
{
    if (!m_menu)
        return nullptr;
    for (QAction* action : m_menu->actions()) {
        if (action != m_widgetAction && isSelectable(action))
            return action;
    }
    return nullptr;
}

QAction* MenuCommandBar::lastSelectableMenuAction() const
{
    if (!m_menu)
        return nullptr;
    const QList<QAction*> actions = m_menu->actions();
    for (auto it = actions.crbegin(); it != actions.crend(); ++it) {
        if (*it != m_widgetAction && isSelectable(*it))
            return *it;
    }
    return nullptr;
}

void MenuCommandBar::setCurrentIndex(int index)
{
    if (m_current == index)
        return;
    m_current = index;
    update();
}

void MenuCommandBar::moveCurrent(int step)
{
    for (int i = 1; i <= CommandCount; ++i) {
        const int next = ((m_current + step * i) % CommandCount + CommandCount) % CommandCount;
        if (m_buttons[next]->isEnabled()) {
            setCurrentIndex(next);
            return;
        }
    }
}

void MenuCommandBar::triggerCommand(int index)
{
    if (index < 0 || index >= CommandCount)
        return;
    QPointer<QAction> action = m_commands[index];
    if (!action || !action->isEnabled())
        return;
    // Hide first so the command runs against a menu-free UI (matching what a
    // normal menu item does); exec()'s event loop only unwinds once this
    // handler returns, so the action outlives the call.
    if (m_menu)
        m_menu->close();
    if (action)
        action->trigger();
}

bool MenuCommandBar::handleMenuKey(QKeyEvent* keyEvent)
{
    const int key = keyEvent->key();
    QAction* const current = m_menu->activeAction();

    if (m_current < 0) {
        // The carrier row can still become QMenu's current action by hovering
        // the strip; never let it be activated, it has no command of its own.
        if (current == m_widgetAction
            && (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space))
            return true;
        if (key != Qt::Key_Up && key != Qt::Key_Down)
            return false;
        const QAction* edge = key == Qt::Key_Up ? firstSelectableMenuAction()
                                                : lastSelectableMenuAction();
        if (current != m_widgetAction && (!current || current != edge))
            return false;
        const int entry = firstEnabledButton(1);
        if (entry < 0)
            return false;
        m_menu->setActiveAction(nullptr);
        setCurrentIndex(entry);
        return true;
    }

    switch (key) {
    case Qt::Key_Left:
        moveCurrent(-1);
        return true;
    case Qt::Key_Right:
        moveCurrent(1);
        return true;
    case Qt::Key_Home:
        setCurrentIndex(firstEnabledButton(1));
        return true;
    case Qt::Key_End:
        setCurrentIndex(firstEnabledButton(-1));
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        triggerCommand(m_current);
        return true;
    case Qt::Key_Up:
    case Qt::Key_Down:
        // Hand vertical navigation back explicitly rather than letting QMenu
        // resolve it: its own search would land on the carrier row.
        setCurrentIndex(-1);
        m_menu->setActiveAction(key == Qt::Key_Up ? lastSelectableMenuAction()
                                                  : firstSelectableMenuAction());
        return true;
    default:
        return false;
    }
}

bool MenuCommandBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_menu) {
        if (event->type() == QEvent::KeyPress
            && handleMenuKey(static_cast<QKeyEvent*>(event)))
            return true;
        return QWidget::eventFilter(watched, event);
    }

    const int index = indexOfButton(watched);
    if (index >= 0 && event->type() == QEvent::Enter) {
        // The menu never sees a mouse move over this row, so an item highlighted
        // a moment ago would otherwise stay lit next to the hovered button; the
        // keyboard selection likewise defers to the mouse.
        if (m_menu)
            m_menu->setActiveAction(nullptr);
        setCurrentIndex(-1);
    }
    return QWidget::eventFilter(watched, event);
}
