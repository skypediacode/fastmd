#pragma once
#include <QString>
#include <QVector>
#include <QPointer>
#include <functional>

class QWidget;
class QToolButton;
class QAction;
class QTimer;
class FlowLayout;
class MarkdownEditor;
class MathSymbolPopup;

// Builds and owns the optional Math Toolbar (View -> Math Toolbar):
// quick-access buttons for the most common LaTeX constructs plus one popup
// dropdown per symbol group (Greek, Relations, Arrows, ...). Buttons wrap
// onto additional rows via FlowLayout as the window narrows -- every button
// stays directly clickable, nothing is hidden behind a "More" menu.
class MathToolbar {
public:
    explicit MathToolbar(QWidget* parent, bool dark);

    QWidget* containerWidget() const { return m_container; }
    void setActiveEditor(MarkdownEditor* editor) { m_activeEditor = editor; }
    void updateIcons(bool dark);
    // Invoked instead of opening a symbol popup when the Matrices group
    // button is clicked -- MainWindow supplies this to open MatrixInsertDialog.
    void setMatrixButtonHandler(std::function<void()> handler) { m_matrixButtonHandler = std::move(handler); }

private:
    enum class ButtonInteraction { PopupMenu, PickerDialog, ImmediateCommand };

    void insertSnippet(const QString& latex);
    void showGroupPopup(int groupIndex, QToolButton* button);
    void openPopupForGroup(int groupIndex, QToolButton* button);
    void handlePopupPointerMove(const QPoint& globalPos);
    bool pointerOverActivePopup(const QPoint& globalPos) const;
    QToolButton* buttonAt(const QPoint& globalPos, ButtonInteraction* outInteraction,
                          int* outGroupIndex) const;
    void updateToolbarHover(const QPoint& globalPos);
    void setPopupOpenButton(QToolButton* button);
    static void setVisualProperty(QToolButton* button, const char* name, bool enabled);

    QWidget* m_container = nullptr;
    FlowLayout* m_flowLayout = nullptr;
    MarkdownEditor* m_activeEditor = nullptr;
    bool m_dark = false;
    QPointer<MathSymbolPopup> m_openPopup;
    int m_openGroupIndex = -1;
    bool m_switchingPopups = false;
    QPointer<QWidget> m_focusBeforePopup;
    QPointer<QToolButton> m_hoveredButton;
    QPointer<QToolButton> m_popupButton;
    QTimer* m_popupDismissTimer = nullptr;
    std::function<void()> m_matrixButtonHandler;

    struct QuickAction { QAction* action; QString id; };
    QVector<QuickAction> m_quickActions;

    struct GroupButton { QAction* action; QToolButton* button; int groupIndex; };
    QVector<GroupButton> m_groupButtons;
    struct ToolbarButton { QToolButton* button; ButtonInteraction interaction; int groupIndex = -1; };
    QVector<ToolbarButton> m_toolbarButtons;
};
