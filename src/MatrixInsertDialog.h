#pragma once
#include <QDialog>
#include <QVector>

class QSpinBox;
class QToolButton;
class QHBoxLayout;
class QGridLayout;
class MarkdownEditor;
class MatrixGridSelector;
class RoundedScrollArea;
class PreviewWidget;

// Dialog opened by the Math Toolbar's Matrices button: lets the user pick
// matrix dimensions (numeric spin boxes or a LyX-style press-and-drag grid),
// a delimiter type via single-select buttons, and shows a live schematic
// preview before inserting the LaTeX matrix at the editor's cursor.
class MatrixInsertDialog : public QDialog {
    Q_OBJECT
public:
    explicit MatrixInsertDialog(QWidget* parent = nullptr);

    void setEditor(MarkdownEditor* editor);
    void setDarkMode(bool dark);
    void openDialog();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void applyTitleBarTheme();
    void buildTypeButtons(QGridLayout* grid);
    void refreshTypeIcons();
    void refreshPanelTheme();
    void selectType(int index);
    void updatePreview();
    void insertAndClose();

    QSpinBox* m_rowsSpin = nullptr;
    QSpinBox* m_colsSpin = nullptr;
    RoundedScrollArea* m_gridScrollArea = nullptr;
    MatrixGridSelector*  m_gridSelector = nullptr;
    QWidget* m_previewFrame = nullptr;
    PreviewWidget* m_preview = nullptr;
    QVector<QToolButton*> m_typeButtons;
    int m_typeIndex = 0;
    bool m_darkMode = false;

    MarkdownEditor* m_editor = nullptr;
    QWidget* m_focusBeforeOpen = nullptr;
};
