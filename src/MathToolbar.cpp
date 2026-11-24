#include "MathToolbar.h"
#include "MarkdownEditor.h"
#include "MathSymbols.h"
#include "MathSymbolPopup.h"
#include "IconHelper.h"
#include "ToolbarPopupCoordinator.h"
#include "ToolbarHoverAnimation.h"
#include "FlowLayout.h"

#include <QWidget>
#include <QToolButton>
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QStyle>
#include <QTimer>

namespace {
constexpr int kMathToolbarIconSize = 40;
}

MathToolbar::MathToolbar(QWidget* parent, bool dark)
{
    m_dark = dark;
    m_container = new QWidget(parent);
    m_container->setObjectName(QStringLiteral("mathToolbar"));
    m_container->setAttribute(Qt::WA_StyledBackground, true);
    m_flowLayout = new FlowLayout(m_container, 0, 0, 0);
    m_flowLayout->setContentsMargins(6, 0, 8, 0);

    m_popupDismissTimer = new QTimer(m_container);
    m_popupDismissTimer->setSingleShot(true);
    m_popupDismissTimer->setInterval(200);
    QObject::connect(m_popupDismissTimer, &QTimer::timeout, m_container, [this]() {
        if (m_openPopup && !pointerOverActivePopup(QCursor::pos()))
            m_openPopup->close();
    });
    const QColor iconColor = m_dark ? QColor(0xdd, 0xdd, 0xdd) : QColor(0x44, 0x44, 0x44);

    auto addQuickAction = [this, iconColor](const QString& id) {
        const MathSymbol* symbol = getMathSymbol(id);
        if (!symbol)
            return;

        const MathSymbol& sym = *symbol;
        auto* action = new QAction(sym.tooltip, m_container);
        action->setToolTip(sym.tooltip);
        action->setIcon(IconHelper::generatedMathSvgIcon(sym.id, iconColor));
        const QString latex = sym.insertLatex;
        QObject::connect(action, &QAction::triggered, m_container, [this, latex]() {
            insertSnippet(latex);
        });
        auto* button = new QToolButton(m_container);
        button->setDefaultAction(action);
        button->setIconSize(QSize(kMathToolbarIconSize, kMathToolbarIconSize));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_flowLayout->addWidget(button);
        m_quickActions.append({ action, sym.id });
        m_toolbarButtons.append({ button, ButtonInteraction::ImmediateCommand });
    };

    const QVector<MathGroup>& groups = mathPopupGroups();
    auto addGroupButton = [this, &groups, iconColor](const QString& id) {
        int groupIndex = -1;
        for (int i = 0; i < groups.size(); ++i) {
            if (groups.at(i).id == id) {
                groupIndex = i;
                break;
            }
        }
        if (groupIndex < 0)
            return;

        const MathGroup& group = groups.at(groupIndex);
        auto* action = new QAction(group.title, m_container);
        action->setToolTip(group.title);
        const QString iconId = group.id == QStringLiteral("matrices")
            ? QStringLiteral("matrices")
            : group.id;
        const QIcon groupIcon = IconHelper::generatedMathSvgIcon(iconId, iconColor);
        action->setIcon(groupIcon);
        auto* button = new QToolButton(m_container);
        button->setDefaultAction(action);
        button->setIconSize(QSize(kMathToolbarIconSize, kMathToolbarIconSize));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_flowLayout->addWidget(button);
        // The group button's own icon stays square like every other main-toolbar
        // button; `aspectRatio` in MathGroupConfig only governs the popup's cell
        // layout (where long expressions like gcd(a,b) need the extra width).
        m_groupButtons.append({ action, button, groupIndex });
        const ButtonInteraction interaction = group.interaction == MathGroupInteraction::PopupMenu
            ? ButtonInteraction::PopupMenu : ButtonInteraction::PickerDialog;
        m_toolbarButtons.append({ button, interaction, groupIndex });
        QObject::connect(action, &QAction::triggered, m_container, [this, groupIndex, button]() {
            showGroupPopup(groupIndex, button);
        });
    };

    auto addSeparator = [this]() {
        auto* sep = new QWidget(m_container);
        sep->setObjectName(QStringLiteral("toolbarFlowSeparator"));
        sep->setAttribute(Qt::WA_StyledBackground, true);
        sep->setFixedSize(11, kMathToolbarIconSize + 8);
        m_flowLayout->addWidget(sep);
    };

    // Keep the existing actions and popups intact, but interleave them by the
    // way people build expressions instead of presenting all shortcuts first.
    addQuickAction(QStringLiteral("pow"));
    addQuickAction(QStringLiteral("sub"));
    addQuickAction(QStringLiteral("subsup"));

    addQuickAction(QStringLiteral("sqrt"));
    addQuickAction(QStringLiteral("nthroot"));

    addQuickAction(QStringLiteral("abs"));
    addQuickAction(QStringLiteral("norm"));

    addQuickAction(QStringLiteral("hat"));
    addQuickAction(QStringLiteral("bar"));
    addQuickAction(QStringLiteral("vec"));
    addQuickAction(QStringLiteral("frac"));
    addQuickAction(QStringLiteral("limit-template"));
    addQuickAction(QStringLiteral("sum"));
    addQuickAction(QStringLiteral("prod"));
    addGroupButton(QStringLiteral("matrices"));

    addSeparator();

    addGroupButton(QStringLiteral("greek"));
    addGroupButton(QStringLiteral("brackets"));
    addGroupButton(QStringLiteral("accents"));
    addGroupButton(QStringLiteral("fractions-scripts"));
    addGroupButton(QStringLiteral("operators"));
    addGroupButton(QStringLiteral("relations"));
    addGroupButton(QStringLiteral("sets-logic"));
    addGroupButton(QStringLiteral("arrows"));

    addGroupButton(QStringLiteral("functions"));
    addGroupButton(QStringLiteral("derivative"));
    addGroupButton(QStringLiteral("integration"));
    addGroupButton(QStringLiteral("miscellaneous"));

    m_container->setVisible(false);

    ToolbarPopupCoordinator::instance().registerToolbar(
        m_container,
        [this](const QPoint& pos) { return buttonAt(pos, nullptr, nullptr); },
        [this](QToolButton* button) {
            for (const GroupButton& groupButton : m_groupButtons)
                if (groupButton.button == button)
                    return mathPopupGroups().at(groupButton.groupIndex).interaction
                        == MathGroupInteraction::PopupMenu;
            return false;
        },
        [this](QToolButton* button) {
            for (const GroupButton& groupButton : m_groupButtons)
                if (groupButton.button == button) {
                    openPopupForGroup(groupButton.groupIndex, button);
                    return;
                }
        });
    ToolbarHoverAnimation::install(m_container);
}

void MathToolbar::insertSnippet(const QString& latex)
{
    if (m_activeEditor)
        m_activeEditor->insertMathSnippet(latex);
}

QToolButton* MathToolbar::buttonAt(const QPoint& globalPos, ButtonInteraction* outInteraction,
                                   int* outGroupIndex) const
{
    for (const ToolbarButton& tb : m_toolbarButtons) {
        if (!tb.button || !tb.button->isEnabled())
            continue;
        const QRect buttonRect(tb.button->mapToGlobal(QPoint(0, 0)), tb.button->size());
        if (buttonRect.contains(globalPos)) {
            if (outInteraction)
                *outInteraction = tb.interaction;
            if (outGroupIndex)
                *outGroupIndex = tb.groupIndex;
            return tb.button;
        }
    }
    return nullptr;
}

void MathToolbar::setVisualProperty(QToolButton* button, const char* name, bool enabled)
{
    if (!button || button->property(name).toBool() == enabled)
        return;
    button->setProperty(name, enabled);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}

void MathToolbar::updateToolbarHover(const QPoint& globalPos)
{
    QToolButton* hitButton = buttonAt(globalPos, nullptr, nullptr);
    if (m_hoveredButton == hitButton)
        return;
    QToolButton* previousButton = m_hoveredButton;
    m_hoveredButton = hitButton;
    setVisualProperty(m_hoveredButton, "mathToolbarHover", true);
    ToolbarHoverAnimation::setHovered(m_hoveredButton, true);
    setVisualProperty(previousButton, "mathToolbarHover", false);
    ToolbarHoverAnimation::setHovered(previousButton, false);
}

void MathToolbar::setPopupOpenButton(QToolButton* button)
{
    if (m_popupButton == button)
        return;
    setVisualProperty(m_popupButton, "mathToolbarPopupOpen", false);
    m_popupButton = button;
    setVisualProperty(m_popupButton, "mathToolbarPopupOpen", true);
}

void MathToolbar::showGroupPopup(int groupIndex, QToolButton* button)
{
    const QVector<MathGroup>& groups = mathPopupGroups();
    if (groupIndex >= 0 && groupIndex < groups.size()
        && groups.at(groupIndex).interaction == MathGroupInteraction::PickerDialog) {
        // The Matrices button opens a dedicated insertion dialog instead of
        // the usual symbol-grid popup -- close whatever popup is currently
        // open (this may be reached mid-switch from another group's popup
        // via the onOutsideClick callback below) and hand off.
        if (m_openPopup)
            m_openPopup->close();
        if (m_matrixButtonHandler)
            m_matrixButtonHandler();
        return;
    }

    // Reached only when no popup is currently open (an open popup's own
    // outside-click handling intercepts and consumes clicks on these buttons
    // directly -- see the onOutsideClick callback in openPopupForGroup).
    // Kept as a defensive fallback so a stray toggle-close still works even
    // if that interception is ever bypassed.
    if (m_openPopup && m_openGroupIndex == groupIndex) {
        m_openPopup->close();
        return;
    }
    openPopupForGroup(groupIndex, button);
}

bool MathToolbar::pointerOverActivePopup(const QPoint& globalPos) const
{
    if (!m_openPopup)
        return false;
    if (m_openPopup->geometry().contains(globalPos))
        return true;
    if (!m_popupButton)
        return false;
    return QRect(m_popupButton->mapToGlobal(QPoint(0, 0)), m_popupButton->size()).contains(globalPos);
}

void MathToolbar::handlePopupPointerMove(const QPoint& globalPos)
{
    ToolbarPopupCoordinator::instance().notifyHover(globalPos);
    if (!m_openPopup)
        return;

    if (pointerOverActivePopup(globalPos)) {
        m_popupDismissTimer->stop();
    } else if (!m_popupDismissTimer->isActive()) {
        m_popupDismissTimer->start();
    }
}

void MathToolbar::openPopupForGroup(int groupIndex, QToolButton* button)
{
    const QVector<MathGroup>& groups = mathPopupGroups();
    if (!button || groupIndex < 0 || groupIndex >= groups.size())
        return;

    m_popupDismissTimer->stop();
    if (m_openPopup) {
        m_switchingPopups = true;
        m_openPopup->close();
        m_switchingPopups = false;
    }

    m_focusBeforePopup = QApplication::focusWidget();

    auto* popup = new MathSymbolPopup(groups.at(groupIndex), m_dark, [this](const QString& latex) {
        insertSnippet(latex);
    }, m_container, [this](const QPoint& globalPos) {
        if (ToolbarPopupCoordinator::instance().handleOutsideClick(globalPos))
            return true;
        // A click outside the popup: if it landed on a different group's
        // button, switch to that popup immediately from this same click
        // instead of just closing and waiting for a second click.
        ButtonInteraction interaction = ButtonInteraction::ImmediateCommand;
        int hitGroupIndex = -1;
        QToolButton* hitButton = buttonAt(globalPos, &interaction, &hitGroupIndex);
        if (!hitButton)
            return false;
        if (interaction == ButtonInteraction::PopupMenu) {
            if (hitGroupIndex == m_openGroupIndex)
                return true;
        } else if (interaction == ButtonInteraction::PickerDialog) {
            showGroupPopup(hitGroupIndex, hitButton);
        } else {
            if (m_openPopup)
                m_openPopup->close();
            hitButton->click();
        }
        return true;
    }, [this](const QPoint& globalPos) {
        handlePopupPointerMove(globalPos);
    });
    m_openPopup = popup;
    m_openGroupIndex = groupIndex;
    setPopupOpenButton(button);
    ToolbarPopupCoordinator::instance().popupOpened(m_container, button, popup, [popup]() {
        if (popup)
            popup->close();
    });

    QObject::connect(popup, &MathSymbolPopup::closed, m_container, [this, popup]() {
        if (m_openPopup != popup)
            return;
        m_popupDismissTimer->stop();
        m_openPopup = nullptr;
        m_openGroupIndex = -1;
        ToolbarPopupCoordinator::instance().popupClosed(m_container);
        setPopupOpenButton(nullptr);
        setVisualProperty(m_hoveredButton, "mathToolbarHover", false);
        m_hoveredButton = nullptr;
        if (m_switchingPopups)
            return;
        if (m_focusBeforePopup)
            m_focusBeforePopup->setFocus(Qt::PopupFocusReason);
        else if (m_activeEditor)
            m_activeEditor->setFocus(Qt::PopupFocusReason);
    });

    popup->adjustGeometryToScreen(button);
    popup->show();
    popup->raise();
}

void MathToolbar::updateIcons(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    const QColor iconColor = dark ? QColor(0xdd, 0xdd, 0xdd) : QColor(0x44, 0x44, 0x44);
    for (const QuickAction& qa : m_quickActions)
        qa.action->setIcon(IconHelper::generatedMathSvgIcon(qa.id, iconColor));

    const QVector<MathGroup>& groups = mathPopupGroups();
    for (const GroupButton& gb : m_groupButtons) {
        if (gb.groupIndex >= 0 && gb.groupIndex < groups.size()) {
            const MathGroup& group = groups.at(gb.groupIndex);
            const QString iconId = group.id == QStringLiteral("matrices")
                ? QStringLiteral("matrices")
                : group.id;
            const QIcon groupIcon = IconHelper::generatedMathSvgIcon(iconId, iconColor);
            gb.action->setIcon(groupIcon);
        }
    }
}
