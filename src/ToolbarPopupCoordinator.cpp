#include "ToolbarPopupCoordinator.h"

#include <QApplication>
#include <QCursor>
#include <QToolButton>
#include <QWidget>

ToolbarPopupCoordinator& ToolbarPopupCoordinator::instance()
{
    static QPointer<ToolbarPopupCoordinator> coordinator;
    if (!coordinator)
        coordinator = new ToolbarPopupCoordinator(qApp);
    return *coordinator;
}

ToolbarPopupCoordinator::ToolbarPopupCoordinator(QObject* parent)
    : QObject(parent)
{
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    m_hoverTimer->setInterval(75);
    QObject::connect(m_hoverTimer, &QTimer::timeout, this, [this]() {
        QObject* owner = m_pendingOwner;
        QToolButton* button = m_pendingButton;
        m_pendingOwner = nullptr;
        m_pendingButton = nullptr;
        if (!owner || !button)
            return;
        // A delayed switch must not use the button that was under the cursor
        // when the timer started: the active popup may now cover it.
        if (activePopupContains(QCursor::pos()))
            return;
        if (!QRect(button->mapToGlobal(QPoint(0, 0)), button->size()).contains(QCursor::pos()))
            return;
        if (m_closePopup)
            m_closePopup();
        for (const Registration& registration : m_registrations) {
            if (registration.owner == owner) {
                registration.openPopup(button);
                return;
            }
        }
    });
}

bool ToolbarPopupCoordinator::activePopupContains(const QPoint& globalPos) const
{
    if (!m_activePopup || !m_activePopup->isVisible())
        return false;

    // A popup is a top-level Qt::Popup window, so its geometry is global.
    // Mapping its full widget rect explicitly keeps this true if that ever
    // changes, and includes its painted border, margins, and child rows.
    const QRect popupRect(m_activePopup->mapToGlobal(QPoint(0, 0)),
                          m_activePopup->size());
    return popupRect.contains(globalPos);
}

void ToolbarPopupCoordinator::cancelPendingHover()
{
    m_hoverTimer->stop();
    m_pendingOwner = nullptr;
    m_pendingButton = nullptr;
}

ToolbarPopupCoordinator::~ToolbarPopupCoordinator() = default;

void ToolbarPopupCoordinator::registerToolbar(QObject* owner, ButtonAt buttonAt,
                                               IsPopupButton isPopupButton, OpenPopup openPopup)
{
    m_registrations.append({ owner, std::move(buttonAt), std::move(isPopupButton),
                             std::move(openPopup) });
    // Toolbars (and their buttons) are destroyed with their window, but this
    // coordinator is a single process-wide singleton -- without this, a
    // closed window's registration (and the captured, now-dangling widget
    // pointers inside it) would stay live for the rest of the process and
    // any other window's popup interaction could call into freed memory.
    QObject::connect(owner, &QObject::destroyed, this, [this, owner]() {
        unregisterToolbar(owner);
    });
}

void ToolbarPopupCoordinator::unregisterToolbar(QObject* owner)
{
    for (int i = m_registrations.size() - 1; i >= 0; --i) {
        if (m_registrations.at(i).owner == owner)
            m_registrations.removeAt(i);
    }
    if (m_pendingOwner == owner) {
        cancelPendingHover();
    }
    if (m_activeOwner == owner) {
        m_activeOwner = nullptr;
        m_activeButton = nullptr;
        m_activePopup = nullptr;
        m_closePopup = {};
    }
}

void ToolbarPopupCoordinator::notifyHover(const QPoint& globalPos)
{
    if (!m_activeOwner)
        return;
    if (activePopupContains(globalPos)) {
        cancelPendingHover();
        return;
    }
    for (const Registration& registration : m_registrations) {
        QToolButton* button = registration.buttonAt(globalPos);
        if (!button)
            continue;
        if (!registration.isPopupButton(button)) {
            cancelPendingHover();
            return;
        }
        if (registration.owner == m_activeOwner && button == m_activeButton)
            return;
        if (registration.owner == m_pendingOwner && button == m_pendingButton)
            return;
        m_pendingOwner = registration.owner;
        m_pendingButton = button;
        m_hoverTimer->start();
        return;
    }
}

bool ToolbarPopupCoordinator::handleOutsideClick(const QPoint& globalPos)
{
    for (const Registration& registration : m_registrations) {
        QToolButton* button = registration.buttonAt(globalPos);
        if (!button)
            continue;
        if (!m_activeOwner)
            return false;
        cancelPendingHover();
        const bool popupButton = registration.isPopupButton(button);
        if (popupButton && registration.owner == m_activeOwner && button == m_activeButton) {
            m_closePopup();
        } else if (popupButton) {
            m_closePopup();
            registration.openPopup(button);
        } else {
            m_closePopup();
            button->click();
        }
        return true;
    }
    return false;
}

void ToolbarPopupCoordinator::popupOpened(QObject* owner, QToolButton* button, QWidget* popup,
                                          ClosePopup closePopup)
{
    cancelPendingHover();
    m_activeOwner = owner;
    m_activeButton = button;
    m_activePopup = popup;
    m_closePopup = std::move(closePopup);
}

void ToolbarPopupCoordinator::popupClosed(QObject* owner)
{
    if (m_activeOwner != owner)
        return;
    cancelPendingHover();
    m_activeOwner = nullptr;
    m_activeButton = nullptr;
    m_activePopup = nullptr;
    m_closePopup = {};
}
