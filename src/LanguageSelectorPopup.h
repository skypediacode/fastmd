#pragma once
#include <QWidget>
#include <QString>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

// Lazily instantiated, searchable status-bar language picker. Populated and
// cached on first show() from the shared, process-wide KSyntaxHighlighting
// repository -- never enumerate definitions on the startup path. Emits only
// semantic requests; it never touches the editor, model, toolbar, or
// preview directly (MainWindow's centralized language operation owns that).
class LanguageSelectorPopup : public QWidget {
    Q_OBJECT
public:
    explicit LanguageSelectorPopup(QWidget* parent = nullptr);

    // Shows (building the row cache on first call) positioned above `anchor`,
    // clamped to the anchor's screen geometry.
    void popup(QWidget* anchor, const QString& currentDefinitionName, bool isAutomatic);

    // Re-marks the checked row while the popup stays open -- used when the
    // active document's language changes underneath it.
    void setCurrentLanguage(const QString& currentDefinitionName, bool isAutomatic);

signals:
    void autoDetectRequested();
    // Empty name means Plain Text; "FastMD Markdown" means Markdown.
    void definitionRequested(const QString& stableName);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ensurePopulated();
    void applyFilter(const QString& text);
    void refreshCheckedState();
    void updateAccessibleName();
    void activateItem(QTreeWidgetItem* item);

    QLineEdit*   m_search = nullptr;
    QTreeWidget* m_tree = nullptr;
    bool         m_populated = false;

    QString m_currentDefinitionName;
    bool    m_isAutomatic = true;
};
