#pragma once
#include <QWidget>
#include <QPointer>

class QLineEdit;
class QToolButton;
class QLabel;
class QGridLayout;
class QTimer;
class QPropertyAnimation;
class QParallelAnimationGroup;
class QHideEvent;
class FindController;

// VS Code-style Find/Replace view. Frameless Qt::Tool top-level so it paints
// above PreviewWidget's native WebView2 child (see AGENTS.md / find-replace-plan.md).
// Purely a view over FindController -- all search/replace state and editor
// coupling live there.
class FindOverlay : public QWidget {
    Q_OBJECT
public:
    explicit FindOverlay(FindController* controller, QWidget* parent);

    void openFind();
    void openReplace();
    void closeOverlay();

    // Top-right of this widget is where the overlay parks itself; MainWindow
    // points it at the active editor so the overlay never covers the toolbar
    // and stays over the text being searched.
    void setAnchorWidget(QWidget* widget);

    // A read-only PDF: only the controls the PDF engine honours, parked below
    // the viewer's own toolbar.
    void setPdfMode(bool pdfMode);

    void setDarkMode(bool dark);
    void anchorToParent();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void buildUi();
    QToolButton* makeButton(QWidget* parent, const QString& objectName, bool checkable);
    static QWidget* makeSeparator(QWidget* parent);
    void openInternal(bool expandReplace);
    void focusFindField();
    void seedFromSelection();
    void setReplaceExpanded(bool expanded);
    void flushPendingTerm();
    void updateCountLabel();
    void updateIcons();
    void updateInvalidState();
    void updateFocusState();
    void updateEnabledStates();
    void applyFixedMetrics();
    void updateFixedHeight();
    void doFindNext();
    void doFindPrevious();
    void doReplaceOne();
    void doReplaceAll();
    void pushOptions();
    void animateShow();
    void animateHide();

    FindController* m_controller = nullptr;
    QPointer<QWidget> m_anchorWidget;

    QWidget*     m_findRow = nullptr;
    QWidget*     m_replaceRow = nullptr;
    QWidget*     m_navSeparator = nullptr;
    QWidget*     m_optionsSeparator = nullptr;
    QLineEdit*   m_findEdit = nullptr;
    QLineEdit*   m_replaceEdit = nullptr;
    QToolButton* m_chevronButton = nullptr;
    QToolButton* m_matchCaseButton = nullptr;
    QToolButton* m_wholeWordButton = nullptr;
    QToolButton* m_regexButton = nullptr;
    QToolButton* m_preserveCaseButton = nullptr;
    QToolButton* m_inSelectionButton = nullptr;
    QToolButton* m_prevButton = nullptr;
    QToolButton* m_nextButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    QToolButton* m_replaceOneButton = nullptr;
    QToolButton* m_replaceAllButton = nullptr;
    QLabel*      m_countLabel = nullptr;

    QGridLayout* m_grid = nullptr;
    bool m_replaceExpanded = false;
    bool m_pdfMode = false;
    bool m_dark = false;
    bool m_closing = false;

    QTimer* m_refreshDebounce = nullptr;
    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_geometryAnim = nullptr;
    QParallelAnimationGroup* m_showAnimGroup = nullptr;
};
