#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QVector>
#include <functional>

class QToolButton;
class QWidget;

class ToolbarPopupCoordinator final : public QObject {
public:
    using ButtonAt = std::function<QToolButton*(const QPoint&)>;
    using IsPopupButton = std::function<bool(QToolButton*)>;
    using OpenPopup = std::function<void(QToolButton*)>;
    using ClosePopup = std::function<void()>;

    static ToolbarPopupCoordinator& instance();
    void registerToolbar(QObject* owner, ButtonAt buttonAt, IsPopupButton isPopupButton,
                         OpenPopup openPopup);
    void notifyHover(const QPoint& globalPos);
    bool handleOutsideClick(const QPoint& globalPos);
    void popupOpened(QObject* owner, QToolButton* button, QWidget* popup, ClosePopup closePopup);
    void popupClosed(QObject* owner);
    void unregisterToolbar(QObject* owner);

private:
    explicit ToolbarPopupCoordinator(QObject* parent);
    ~ToolbarPopupCoordinator();
    ToolbarPopupCoordinator(const ToolbarPopupCoordinator&) = delete;
    ToolbarPopupCoordinator& operator=(const ToolbarPopupCoordinator&) = delete;

    struct Registration {
        QObject* owner = nullptr;
        ButtonAt buttonAt;
        IsPopupButton isPopupButton;
        OpenPopup openPopup;
    };

    QVector<Registration> m_registrations;
    QObject* m_activeOwner = nullptr;
    QToolButton* m_activeButton = nullptr;
    ClosePopup m_closePopup;
    QTimer* m_hoverTimer = nullptr;
    QObject* m_pendingOwner = nullptr;
    QToolButton* m_pendingButton = nullptr;
    QPointer<QWidget> m_activePopup;

    bool activePopupContains(const QPoint& globalPos) const;
    void cancelPendingHover();
};
