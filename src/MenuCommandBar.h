#pragma once

#include <QWidget>
#include <QColor>
#include <QPointer>

class QAction;
class QKeyEvent;
class QWidgetAction;
class QToolButton;
class QMenu;

// Windows 11 Notepad-style horizontal command strip for the top of a context
// menu: five evenly spaced icon-over-label buttons separated by hairline
// dividers. The bar owns no commands of its own -- it drives whatever QActions
// the caller hands it, so shortcuts, enabled state and behavior stay wherever
// they already live.
//
// The carrier QWidgetAction is deliberately kept disabled so QMenu's own
// keyboard navigation and hover highlighting skip the row; the bar implements
// both itself (Left/Right between buttons, Up/Down back to the menu) by
// filtering the menu's key events. The buttons are real child widgets and
// remain interactive for the mouse regardless.
class MenuCommandBar : public QWidget
{
    Q_OBJECT
public:
    enum Command { Cut, Copy, Paste, SelectAll, Remove, CommandCount };

    // The returned bar is owned by its QWidgetAction, which is parented to
    // `owner`. Do not give the bar a QWidget parent: QWidgetAction deletes its
    // default widget, so a parent-child cycle would double-delete it.
    static MenuCommandBar* create(QObject* owner);

    void setIconColor(const QColor& color);
    void setCommands(QAction* cut, QAction* copy, QAction* paste,
                     QAction* selectAll, QAction* remove);

    // Inserts the bar plus a trailing separator into `menu`. Call before adding
    // any other action so the strip stays the first row.
    void attachTo(QMenu* menu);
    void detach();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit MenuCommandBar();

    void rebuildIcons();
    void setCurrentIndex(int index);
    void moveCurrent(int step);
    void triggerCommand(int index);
    int indexOfButton(const QObject* object) const;
    int firstEnabledButton(int step) const;
    QAction* firstSelectableMenuAction() const;
    QAction* lastSelectableMenuAction() const;
    bool handleMenuKey(QKeyEvent* keyEvent);

    QToolButton*      m_buttons[CommandCount] = {};
    QPointer<QAction> m_commands[CommandCount];
    QWidgetAction*    m_widgetAction = nullptr;
    QPointer<QMenu>   m_menu;
    QColor            m_iconColor;
    QColor            m_dividerColor;
    QColor            m_selectionFill;
    QColor            m_selectionBorder;
    // Keyboard selection is tracked here rather than through Qt focus: a widget
    // inside a QMenu popup never becomes the focus widget of an active window,
    // so QSS ':focus' and QStyle::State_HasFocus never engage.
    int               m_current = -1;
};
