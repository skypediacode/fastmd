#include "ToolbarOverflow.h"
#include "AlignedMenu.h"

#include "IconHelper.h"

#include <QAction>
#include <QColor>
#include <QMenu>
#include <QSize>
#include <QToolBar>
#include <QToolButton>
#include <QVariant>

namespace {

QAction* overflowAction(QToolBar* toolbar, QAction* action, QMenu* menu)
{
    QWidget* widget = toolbar->widgetForAction(action);
    if (!widget)
        return action;

    auto* toolButton = qobject_cast<QToolButton*>(widget);
    if (!toolButton)
        return action;

    if (QAction* defaultAction = toolButton->defaultAction())
        return defaultAction;

    auto* proxy = new QAction(toolButton->toolTip(), menu);
    proxy->setToolTip(toolButton->toolTip());
    proxy->setIcon(IconHelper::trimTransparentMargins(toolButton->icon()));
    // Toolbar mirrors can keep a display-only shortcut here while their
    // canonical menu action owns the actual application shortcut. Preserve it
    // when Qt needs a proxy action for the toolbar overflow menu.
    const QVariant displayedShortcut = action->property("alignedMenuShortcut");
    if (displayedShortcut.isValid())
        proxy->setProperty("alignedMenuShortcut", displayedShortcut);
    QObject::connect(proxy, &QAction::triggered, toolButton, &QToolButton::click);
    return proxy;
}

}

QToolButton* ToolbarOverflow::extensionButton(QToolBar* toolbar)
{
    if (!toolbar)
        return nullptr;

    for (QObject* child : toolbar->children()) {
        if (qstrcmp(child->metaObject()->className(), "QToolBarExtension") != 0)
            continue;
        if (auto* button = qobject_cast<QToolButton*>(child)) {
            button->setObjectName(QStringLiteral("toolbarOverflowButton"));
            button->setProperty("toolbarHoverBorderless", true);
            return button;
        }
    }
    return nullptr;
}

void ToolbarOverflow::installMenu(QToolBar* toolbar, QToolButton* button)
{
    if (!toolbar || !button)
        return;

    auto* menu = new AlignedMenu(button);
    menu->setObjectName(QStringLiteral("toolbarOverflowMenu"));
    menu->setShortcutTabPaddingEnabled(false);
    QObject::connect(menu, &QMenu::aboutToShow, menu, [toolbar, menu]() {
        menu->clear();
        bool addedAction = false;
        bool pendingSeparator = false;
        for (QAction* action : toolbar->actions()) {
            if (action->isSeparator()) {
                if (addedAction)
                    pendingSeparator = true;
                continue;
            }

            QWidget* widget = toolbar->widgetForAction(action);
            if (!widget || widget->isVisible())
                continue;

            if (pendingSeparator && addedAction)
                menu->addSeparator();
            pendingSeparator = false;
            menu->addAction(overflowAction(toolbar, action, menu));
            addedAction = true;
        }
        // AlignedMenu's own aboutToShow handler ran before this dynamic menu
        // population slot. Refresh now so the first opening has a shortcut
        // column too; previously it only appeared on the second opening.
        menu->refreshShortcutLayout();
    });
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
}

void ToolbarOverflow::updateExtensionButton(QToolButton* button, bool dark, int iconSize)
{
    if (!button)
        return;

    const QColor iconColor = dark ? QColor(0xcc, 0xcc, 0xcc) : QColor(0x44, 0x44, 0x44);
    button->setIcon(IconHelper::materialIcon(QChar(0xE5D3), iconColor, iconSize));
    button->setToolTip(QObject::tr("More"));
}
