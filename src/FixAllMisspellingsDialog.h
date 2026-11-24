#pragma once
#include <QDialog>
#include <QString>
#include <QStringList>
#include <QTextCursor>
#include <QVector>

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// One distinct misspelled word plus every place it occurs. Occurrences are
// QTextCursors rather than integer offsets so QTextDocument re-maps them
// through any edit made while the dialog is open; the apply step still
// re-verifies each one against `word` before touching the document.
struct FixAllEntry {
    QString word;
    QStringList suggestions;
    int chosen = 0;
    bool include = true;
    QVector<QTextCursor> occurrences;
};

// Review step for Fix All Misspellings. Shows one row per distinct misspelled
// word with its occurrence count and a chooser of the spell checker's
// suggestions. Unchecking a row excludes it from this operation only - it
// never adds the word to the permanent dictionary.
class FixAllMisspellingsDialog : public QDialog {
    Q_OBJECT
public:
    FixAllMisspellingsDialog(const QVector<FixAllEntry>& entries, QWidget* parent = nullptr);

    // The entries with the user's include/suggestion choices applied.
    QVector<FixAllEntry> entries() const;

protected:
    void showEvent(QShowEvent* event) override;

private:
    void applyTitleBarTheme();
    void updateApplyButton();

    QVector<FixAllEntry> m_entries;
    QTreeWidget* m_tree = nullptr;
    QPushButton* m_applyButton = nullptr;
};
