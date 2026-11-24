#include "NativeTreeContainer.h"
#include <QVBoxLayout>
#include <QTreeView>
#include <QHeaderView>

NativeTreeContainer::NativeTreeContainer(QTreeView* treeView, QWidget* parent)
    : QFrame(parent)
    , m_treeView(treeView)
{
    setObjectName(QStringLiteral("NativeTreeContainer"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_treeView->setParent(this);
    m_treeView->setHeaderHidden(true); // Hidden in the tree, we use a standalone header

    m_standaloneHeader = new QHeaderView(Qt::Horizontal, this);
    // Placeholder until WorkspacePanelController::setHeaderHeight() syncs this
    // to the tab bar's actual rendered height once the app stylesheet is applied.
    m_standaloneHeader->setFixedHeight(28);
    m_standaloneHeader->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // All sizing is driven by the tree header; no independent stretching here.
    m_standaloneHeader->setStretchLastSection(false);
    m_standaloneHeader->setFocusPolicy(Qt::NoFocus);
    m_standaloneHeader->setSectionsMovable(false);

    // Sync column widths from the hidden tree header UP to the standalone header.
    connect(m_treeView->header(), &QHeaderView::sectionResized, m_standaloneHeader, [this](int logicalIndex, int /*oldSize*/, int newSize) {
        if (m_standaloneHeader->sectionSize(logicalIndex) != newSize)
            m_standaloneHeader->resizeSection(logicalIndex, newSize);
    });

    // Sync column widths from the standalone header DOWN to the hidden tree header
    // (necessary for Interactive columns the user resizes via the standalone header).
    connect(m_standaloneHeader, &QHeaderView::sectionResized, m_treeView->header(), [this](int logicalIndex, int /*oldSize*/, int newSize) {
        if (m_treeView->header()->sectionSize(logicalIndex) != newSize)
            m_treeView->header()->resizeSection(logicalIndex, newSize);
    });

    // Sync sort indicator in both directions.
    connect(m_standaloneHeader, &QHeaderView::sortIndicatorChanged, m_treeView->header(), [this](int logicalIndex, Qt::SortOrder order) {
        m_treeView->header()->setSortIndicator(logicalIndex, order);
    });
    connect(m_treeView->header(), &QHeaderView::sortIndicatorChanged, m_standaloneHeader, [this](int logicalIndex, Qt::SortOrder order) {
        m_standaloneHeader->setSortIndicator(logicalIndex, order);
    });

    layout->addWidget(m_standaloneHeader);
    layout->addWidget(m_treeView, 1);
}

void NativeTreeContainer::syncHeaderState()
{
    QHeaderView* treeHeader = m_treeView->header();

    m_standaloneHeader->setModel(m_treeView->model());
    m_standaloneHeader->setSectionsClickable(treeHeader->sectionsClickable());
    m_standaloneHeader->setHighlightSections(treeHeader->highlightSections());
    m_standaloneHeader->setSortIndicatorShown(treeHeader->isSortIndicatorShown());

    int count = treeHeader->count();
    for (int i = 0; i < count; ++i) {
        // Sync resize modes: Interactive sections stay Interactive so the user
        // can drag them on the standalone header; all others are Fixed so they
        // simply follow whatever the tree header reports.
        QHeaderView::ResizeMode mode = treeHeader->sectionResizeMode(i);
        m_standaloneHeader->setSectionResizeMode(
            i, mode == QHeaderView::Interactive ? QHeaderView::Interactive : QHeaderView::Fixed);

        // Sync visibility and sizes.
        m_standaloneHeader->setSectionHidden(i, treeHeader->isSectionHidden(i));
        m_standaloneHeader->resizeSection(i, treeHeader->sectionSize(i));
    }

    // Sync visual indices (section order).
    for (int visualIdx = 0; visualIdx < count; ++visualIdx) {
        int logicalIdx = treeHeader->logicalIndex(visualIdx);
        int standaloneVisualIdx = m_standaloneHeader->visualIndex(logicalIdx);
        if (standaloneVisualIdx != visualIdx)
            m_standaloneHeader->moveSection(standaloneVisualIdx, visualIdx);
    }
}
