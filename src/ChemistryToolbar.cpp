#include "ChemistryToolbar.h"
#include "ChemistrySymbols.h"
#include "ChemistryPopup.h"
#include "ToolbarSvgButton.h"
#include "IconHelper.h"
#include "MarkdownEditor.h"
#include "ToolbarPopupCoordinator.h"
#include "ToolbarHoverAnimation.h"
#include "FlowLayout.h"
#include <QAction>
#include <QToolButton>
#include <QApplication>
#include <QCursor>
#include <QSet>
#include <QTimer>
#include <QWidget>

namespace {
constexpr int kChemistryButtonWidth = 96;
constexpr int kChemistryButtonHeight = 48;

const ChemistryItem* itemForId(const QVector<ChemistrySection>& sections, const QString& id)
{
    for (const ChemistrySection& section : sections)
        for (const ChemistryItem& item : section.items)
            if (item.id == id)
                return &item;
    return nullptr;
}
}

ChemistryToolbar::ChemistryToolbar(QWidget* parent, bool dark)
{
    m_dark = dark;
    m_container = new QWidget(parent);
    m_container->setObjectName(QStringLiteral("chemistryToolbar"));
    m_container->setAttribute(Qt::WA_StyledBackground, true);
    m_flowLayout = new FlowLayout(m_container, 0, 0, 0);
    m_flowLayout->setContentsMargins(6, 0, 8, 0);

    m_popupDismissTimer = new QTimer(m_container);
    m_popupDismissTimer->setSingleShot(true);
    m_popupDismissTimer->setInterval(200);
    QObject::connect(m_popupDismissTimer, &QTimer::timeout, m_container, [this]() {
        if (m_popup && !pointerOverActivePopup(QCursor::pos()))
            m_popup->close();
    });
    const auto& sections = chemistrySections();
    const QColor iconColor = m_dark ? QColor(0xdd, 0xdd, 0xdd) : QColor(0x44, 0x44, 0x44);

    const QStringList compactIds{
        QStringLiteral("common-h2o"),
        QStringLiteral("formula-co2"),
        QStringLiteral("formula-naoh"),
        QStringLiteral("formula-h2so4"),
        QStringLiteral("common-hplus"),
        QStringLiteral("ion-ohminus"),
        QStringLiteral("ion-ca2plus"),
        QStringLiteral("ion-so4"),
        QStringLiteral("ion-agcl2"),
        QStringLiteral("isotope-c")
    };
    for (const QString& id : compactIds) {
        const ChemistryItem* found = itemForId(sections, id);
        if (!found)
            continue;

        const double ar = IconHelper::mathSymbolAspectRatio(id);
        auto* btn = new ToolbarSvgButton(IconHelper::generatedChemistrySvgIcon(id, iconColor), ar, false,
                                         m_container, true);
        btn->setFixedSize(kChemistryButtonWidth, kChemistryButtonHeight);
        btn->setToolTip(found->tooltip);
        m_flowLayout->addWidget(btn);
        m_toolbarButtons.append(btn);
        const QString latex = found->insertLatex;
        QObject::connect(btn, &QToolButton::clicked, m_container, [this, latex] { insert(latex); });
        m_iconButtons.append({ btn, id });
    }

    auto* sep = new QWidget(m_container);
    sep->setObjectName(QStringLiteral("toolbarFlowSeparator"));
    sep->setAttribute(Qt::WA_StyledBackground, true);
    sep->setFixedSize(11, kChemistryButtonHeight);
    m_flowLayout->addWidget(sep);

    const double reactsAspectRatio = IconHelper::mathSymbolAspectRatio(QStringLiteral("chemistry-reaction-launcher"));
    auto* reacts = new ToolbarSvgButton(
        IconHelper::generatedChemistrySvgIcon(QStringLiteral("chemistry-reaction-launcher"), iconColor),
        reactsAspectRatio, false, m_container);
    reacts->setFixedSize(kChemistryButtonWidth, kChemistryButtonHeight);
    reacts->setToolTip(QObject::tr("Arrows"));
    m_flowLayout->addWidget(reacts);
    m_reactsButton = reacts;
    m_toolbarButtons.append(reacts);
    QObject::connect(reacts, &QToolButton::clicked, m_container, [this, reacts] {
        openSectionPopup(reacts, QStringLiteral("Arrows"));
    });

    const double statesAspectRatio = IconHelper::mathSymbolAspectRatio(QStringLiteral("chemistry-state-launcher"));
    auto* states = new ToolbarSvgButton(
        IconHelper::generatedChemistrySvgIcon(QStringLiteral("chemistry-state-launcher"), iconColor),
        statesAspectRatio, false, m_container);
    states->setFixedSize(kChemistryButtonWidth, kChemistryButtonHeight);
    states->setToolTip(QObject::tr("States"));
    m_flowLayout->addWidget(states);
    m_statesButton = states;
    m_toolbarButtons.append(states);
    QObject::connect(states, &QToolButton::clicked, m_container, [this, states] {
        openSectionPopup(states, QStringLiteral("States"));
    });
    ToolbarPopupCoordinator::instance().registerToolbar(
        m_container,
        [this](const QPoint& pos) { return buttonAt(pos); },
        [this](QToolButton* button) { return isPopupButton(button); },
        [this](QToolButton* button) {
            if (button == m_statesButton)
                openSectionPopup(button, QStringLiteral("States"));
            else
                openSectionPopup(button, QStringLiteral("Arrows"));
        });

    updateIcons(false);
    ToolbarHoverAnimation::install(m_container);
    m_container->hide();
}

void ChemistryToolbar::insert(const QString& latex)
{
    if (m_editor)
        m_editor->insertChemistrySnippet(latex);
}

void ChemistryToolbar::updateIcons(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    const QColor c = dark ? QColor(0xdd, 0xdd, 0xdd) : QColor(0x44, 0x44, 0x44);
    for (const IconButton& ib : m_iconButtons) {
        if (ib.button)
            ib.button->refreshIcon(IconHelper::generatedChemistrySvgIcon(ib.id, c));
    }
    if (m_reactsButton) {
        m_reactsButton->refreshIcon(IconHelper::generatedChemistrySvgIcon(
            QStringLiteral("chemistry-reaction-launcher"), c));
    }
    if (m_statesButton) {
        m_statesButton->refreshIcon(IconHelper::generatedChemistrySvgIcon(
            QStringLiteral("chemistry-state-launcher"), c));
    }
}

bool ChemistryToolbar::pointerOverActivePopup(const QPoint& globalPos) const
{
    if (!m_popup)
        return false;
    if (m_popup->geometry().contains(globalPos))
        return true;
    return m_activePopupButton
        && QRect(m_activePopupButton->mapToGlobal(QPoint(0, 0)), m_activePopupButton->size()).contains(globalPos);
}

QToolButton* ChemistryToolbar::buttonAt(const QPoint& globalPos) const
{
    for (QToolButton* button : m_toolbarButtons) {
        if (!button || !button->isEnabled())
            continue;
        if (QRect(button->mapToGlobal(QPoint(0, 0)), button->size()).contains(globalPos))
            return button;
    }
    return nullptr;
}

bool ChemistryToolbar::isPopupButton(QToolButton* button) const
{
    return button && (button == m_reactsButton || button == m_statesButton);
}

void ChemistryToolbar::updateActivePopupHover(const QPoint& globalPos)
{
    if (!m_popup)
        return;
    if (pointerOverActivePopup(globalPos))
        m_popupDismissTimer->stop();
    else if (!m_popupDismissTimer->isActive())
        m_popupDismissTimer->start();
}

QVector<ChemistrySection> ChemistryToolbar::filteredSections(const QString& sectionTitle) const
{
    QSet<QString> mainLatex;
    for (const IconButton& mainButton : m_iconButtons) {
        if (const ChemistryItem* mainItem = itemForId(chemistrySections(), mainButton.id))
            mainLatex.insert(mainItem->insertLatex);
    }

    QVector<ChemistrySection> popupSections;
    QSet<QString> seenLatex;
    for (const ChemistrySection& section : chemistrySections()) {
        if (section.title != sectionTitle)
            continue;
        ChemistrySection filtered;
        filtered.title = section.title;
        for (const ChemistryItem& item : section.items) {
            if (mainLatex.contains(item.insertLatex) || seenLatex.contains(item.insertLatex))
                continue;
            filtered.items.append(item);
            seenLatex.insert(item.insertLatex);
        }
        if (!filtered.items.isEmpty())
            popupSections.append(filtered);
    }
    return popupSections;
}

void ChemistryToolbar::openSectionPopup(QToolButton* button, const QString& sectionTitle)
{
    if (m_popup) {
        m_popup->close();
        return;
    }
    m_focusBeforePopup = QApplication::focusWidget();
    m_activePopupButton = button;

    const QVector<ChemistrySection> popupSections = filteredSections(sectionTitle);

    m_popup = new ChemistryPopup(m_dark, [this](const QString& latex) { insert(latex); },
                                 m_container,
                                 [this](const QPoint& globalPos) {
                                     return ToolbarPopupCoordinator::instance()
                                         .handleOutsideClick(globalPos);
                                 },
                                 popupSections, true,
                                 [this](const QPoint& globalPos) {
                                     ToolbarPopupCoordinator::instance().notifyHover(globalPos);
                                     updateActivePopupHover(globalPos);
                                 });
    if (auto* svgButton = qobject_cast<ToolbarSvgButton*>(button))
        svgButton->setPopupOpen(true);
    ToolbarPopupCoordinator::instance().popupOpened(m_container, button, m_popup, [this]() {
        if (m_popup)
            m_popup->close();
    });
    QObject::connect(m_popup, &ChemistryPopup::closed, m_container, [this, button] {
        m_popupDismissTimer->stop();
        if (auto* svgButton = qobject_cast<ToolbarSvgButton*>(button))
            svgButton->setPopupOpen(false);
        m_popup = nullptr;
        m_activePopupButton = nullptr;
        ToolbarPopupCoordinator::instance().popupClosed(m_container);
        if (m_focusBeforePopup)
            m_focusBeforePopup->setFocus(Qt::PopupFocusReason);
        else if (m_editor)
            m_editor->setFocus();
    });
    m_popup->adjustGeometryToScreen(button);
    m_popup->show();
    m_popup->raise();
}
