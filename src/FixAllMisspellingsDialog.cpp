#include "FixAllMisspellingsDialog.h"

#include "CompactComboBox.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {
// ~20% larger than the original 560x460, so long words and suggestion
// lists have room without changing any of the layout metrics below.
constexpr QSize kDialogSize(720, 540);
constexpr int kSuggestionComboHeight = 32;
constexpr int kEntryRole = Qt::UserRole + 1;
}

FixAllMisspellingsDialog::FixAllMisspellingsDialog(const QVector<FixAllEntry>& entries,
                                                   QWidget* parent)
    : QDialog(parent)
    , m_entries(entries)
{
    setWindowTitle(tr("Fix All Misspellings"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    applyTitleBarTheme();
    setFixedSize(kDialogSize);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto* intro = new QLabel(tr("Review the corrections to apply. Clearing a row leaves that "
                                 "word unchanged; it is not added to your dictionary."), this);
    intro->setObjectName(QStringLiteral("fixAllIntro"));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("fixAllTree"));
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({ tr("Word"), tr("Occurrences"), tr("Suggested replacement") });
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_tree->header()->resizeSection(2, 200);
    layout->addWidget(m_tree, 1);

    for (int i = 0; i < m_entries.size(); ++i) {
        const FixAllEntry& entry = m_entries.at(i);
        auto* item = new QTreeWidgetItem(m_tree);
        item->setData(0, kEntryRole, i);
        item->setText(0, entry.word);
        const int count = entry.occurrences.size();
        item->setText(1, count == 1 ? tr("1 occurrence") : tr("%1 occurrences").arg(count));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);

        if (entry.suggestions.isEmpty()) {
            // Nothing to propose: the row is visible for context but can never
            // be applied.
            item->setCheckState(0, Qt::Unchecked);
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            item->setDisabled(true);
            item->setText(2, tr("No suggestions"));
            m_entries[i].include = false;
            continue;
        }

        item->setCheckState(0, Qt::Checked);
        // FastMD's standard dropdown, the same one Preferences uses, rather
        // than an unstyled QComboBox picking up the native grey chrome.
        auto* combo = new CompactComboBox(m_tree, kSuggestionComboHeight);
        // The matching style rule's 8px bottom-only margin separates one row's
        // dropdown from the next. It is a margin rather than a shorter combo
        // (or a taller row) because the tree sizes the row from this widget's
        // minimum. Keeping the gap below the box preserves its top alignment
        // with the Word and Occurrences text.
        combo->setProperty("fixAllRow", true);
        combo->addItems(entry.suggestions);
        combo->setCurrentIndex(0);
        connect(combo, &QComboBox::currentIndexChanged, this, [this, i](int index) {
            m_entries[i].chosen = index;
        });
        m_tree->setItemWidget(item, 2, combo);
    }

    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (column != 0 || !item)
            return;
        const int index = item->data(0, kEntryRole).toInt();
        if (index >= 0 && index < m_entries.size())
            m_entries[index].include = (item->checkState(0) == Qt::Checked);
        updateApplyButton();
    });

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    m_applyButton = new QPushButton(tr("Apply All"), this);
    m_applyButton->setObjectName(QStringLiteral("fixAllApply"));
    m_applyButton->setDefault(true);
    auto* cancel = new QPushButton(tr("Cancel"), this);
    cancel->setObjectName(QStringLiteral("fixAllCancel"));
    buttons->addWidget(m_applyButton);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(m_applyButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    updateApplyButton();
}

QVector<FixAllEntry> FixAllMisspellingsDialog::entries() const
{
    return m_entries;
}

void FixAllMisspellingsDialog::updateApplyButton()
{
    if (!m_applyButton)
        return;
    bool any = false;
    for (const FixAllEntry& entry : m_entries) {
        if (entry.include && !entry.suggestions.isEmpty()) {
            any = true;
            break;
        }
    }
    m_applyButton->setEnabled(any);
}

void FixAllMisspellingsDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    applyTitleBarTheme();
}

void FixAllMisspellingsDialog::applyTitleBarTheme()
{
#ifdef Q_OS_WIN
    const bool isDarkTheme = palette().color(QPalette::Window).lightness() < 128;
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const BOOL enabled = isDarkTheme ? TRUE : FALSE;
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 20, &enabled, sizeof(enabled))) ||
        SUCCEEDED(DwmSetWindowAttribute(hwnd, 19, &enabled, sizeof(enabled)))) {
        const COLORREF captionColor = isDarkTheme ? RGB(0x20, 0x21, 0x22) : RGB(0xf3, 0xf3, 0xf3);
        const COLORREF textColor = isDarkTheme ? RGB(0xbf, 0xbf, 0xbf) : RGB(0x20, 0x20, 0x20);
        DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
        DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif
}
