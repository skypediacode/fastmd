#pragma once

#include <QPointer>
#include <QWidget>

class CommandRegistry;
class CommandPaletteSearchRow;
class CommandItemDelegate;
class QLineEdit;
class QListWidget;

// VS Code-style floating command palette.
//
// The popup owns no commands: it renders whatever CommandRegistry holds, so
// registering a new command is enough to make it appear here. Its search row
// is drawn to sit exactly over CommandPaletteLauncher's box in the toolbar,
// with the result list dropping below.
//
// Like OutlinePopup this is permanently a Qt::Tool window (Rule 11) -- the
// click-outside dismissal a Qt::Popup would give for free is reimplemented on
// QEvent::WindowDeactivate so the native window is never recreated.
class CommandPalettePopup : public QWidget {
    Q_OBJECT
public:
    CommandPalettePopup(CommandRegistry* registry, QWidget* parent = nullptr);

    void setDarkMode(bool dark);
    // Positions the search row over `anchor` (the toolbar launcher), resets
    // the query and shows the full command list.
    void showForAnchor(QWidget* anchor);
    // Lists `choices` in place of the commands -- same search row, ranking and
    // keys -- until the popup next hides; a picked choice runs like a command.
    // Takes ownership of `choices`. `emptyText` is shown when nothing matches,
    // including when `choices` holds no commands at all.
    void showChoices(QWidget* anchor, CommandRegistry* choices,
                     const QString& placeholder, const QString& emptyText);

signals:
    void visibilityChanged(bool visible);

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void refilter();
    void moveSelection(int delta);
    void activateCurrent();
    void updateGeometryForAnchor();
    qreal anchorSubPixelOffset(const QPoint& topLeftInAnchorWindow) const;
    int  rowHeight() const;
    void showList(QWidget* anchor);
    CommandRegistry* currentRegistry() const;

    CommandRegistry*         m_registry = nullptr;
    // Owned; kept past hide() so a picked choice can still run, and replaced
    // by the next showChoices().
    QPointer<CommandRegistry> m_choices;
    bool                     m_showingChoices = false;
    QString                  m_emptyText;
    CommandPaletteSearchRow* m_searchRow = nullptr;
    QLineEdit*               m_search = nullptr;
    QListWidget*             m_list = nullptr;
    CommandItemDelegate*     m_delegate = nullptr;
    QWidget*                 m_anchor = nullptr;
    bool                     m_dark = false;
};
