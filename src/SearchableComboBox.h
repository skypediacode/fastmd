#pragma once
#include "CompactComboBox.h"

class QLineEdit;
class QListView;
class SearchableComboPopup;

// A CompactComboBox whose dropdown opens with a search field above the list,
// for lists too long to scroll through (the installed font families). The
// popup is built on the first open and lists the combo's own model through a
// filter proxy, so typing filters strings already in memory: nothing is
// re-read and the items are never rebuilt. Selecting a row sets the current
// index and emits activated() exactly as the stock popup does.
class SearchableComboBox : public CompactComboBox
{
public:
    explicit SearchableComboBox(QWidget* parent = nullptr, int rowHeight = 32);
    ~SearchableComboBox() override;

    void setSearchPlaceholderText(const QString& text);
    // Row 0 stays listed whatever the search text (a "Default" entry).
    void setFirstRowPinned(bool pinned);

    void showPopup() override;
    void hidePopup() override;

    // Null until the popup has been opened once.
    QWidget* searchPopup() const;
    QLineEdit* searchField() const;
    QListView* searchList() const;

private:
    friend class SearchableComboPopup;
    void commitRow(int sourceRow);

    SearchableComboPopup* m_popup = nullptr;
    QString m_placeholder;
    bool m_firstRowPinned = false;
};
