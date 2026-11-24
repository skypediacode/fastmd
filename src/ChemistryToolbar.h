#pragma once
#include <QPointer>
#include <QVector>
#include <QString>

class QWidget;
class QToolButton;
class QTimer;
class FlowLayout;
class MarkdownEditor;
class ChemistryPopup;
class ToolbarSvgButton;
struct ChemistrySection;

class ChemistryToolbar {
public:
    explicit ChemistryToolbar(QWidget* parent, bool dark);
    QWidget* containerWidget() const { return m_container; }
    void setActiveEditor(MarkdownEditor* editor) { m_editor = editor; }
    void updateIcons(bool dark);
private:
    void insert(const QString& latex);
    void openSectionPopup(QToolButton* button, const QString& sectionTitle);
    bool pointerOverActivePopup(const QPoint& globalPos) const;
    void updateActivePopupHover(const QPoint& globalPos);
    QToolButton* buttonAt(const QPoint& globalPos) const;
    bool isPopupButton(QToolButton* button) const;
    QVector<ChemistrySection> filteredSections(const QString& sectionTitle) const;

    QWidget* m_container = nullptr;
    FlowLayout* m_flowLayout = nullptr;
    MarkdownEditor* m_editor = nullptr;
    QPointer<ChemistryPopup> m_popup;
    QPointer<QWidget> m_focusBeforePopup;
    QPointer<ToolbarSvgButton> m_reactsButton;
    QPointer<ToolbarSvgButton> m_statesButton;
    QPointer<QToolButton> m_activePopupButton;
    QVector<QToolButton*> m_toolbarButtons;
    QTimer* m_popupDismissTimer = nullptr;
    bool m_dark = false;

    // Every main-toolbar button that shows a generated SVG, so updateIcons()
    // can recolor them without needing to know which is which.
    struct IconButton { ToolbarSvgButton* button; QString id; };
    QVector<IconButton> m_iconButtons;
};
