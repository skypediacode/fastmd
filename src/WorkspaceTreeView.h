#pragma once

#include <QTreeView>
#include <QColor>
#include <QStringList>
#include <functional>

class QStyle;
class QPainter;
class QRect;

// The Workspace panel's tree widget. Adds: horizontal-scroll-on-Shift+wheel
// (existing behavior), a contextMenuRequested() signal (so
// WorkspacePanelController can build/own the QMenu instead of this view
// knowing about file operations), acceptance of external file/folder drops
// from Windows Explorer (externalPathsDropped()), internal drag-to-move
// (internalDropRequested()), and a flat chevron
// expand/collapse indicator drawn in place of the platform style's default
// branch decoration, matching Explorer's look (folders only; no dotted
// tree lines).
class WorkspaceTreeView final : public QTreeView {
    Q_OBJECT
public:
    explicit WorkspaceTreeView(QWidget* parent = nullptr);

    // Color used to paint the chevron. Uses an explicit provider (rather
    // than QPalette) to stay consistent with the app's theme setting, which
    // drives Material-icon coloring everywhere else and isn't necessarily
    // mirrored into QPalette by the QSS theme.
    void setBranchIconColorProvider(std::function<QColor()> provider);
    void setBranchVisibilityProvider(std::function<bool(const QModelIndex&)> provider);
    void setDarkTheme(bool dark);

    // Foreground a selected row is actually painted with. Like the chevron
    // color above this is NOT read from QPalette: the QSS theme never mirrors
    // its colors into the palette (HighlightedText stays #ffffff in both the
    // Active and Inactive color groups), so the row text comes from the
    // "QTreeView::item:selected" / ":selected:!active" rules in Stylesheet.h
    // and these values must track those. Row action icons drawn on top of the
    // selection use this so they match the text beside them.
    QColor selectionForeground() const;

signals:
    void contextMenuRequested(const QPoint& globalPos, const QModelIndex& index);
    void externalPathsDropped(const QStringList& paths, const QModelIndex& targetIndex);
    void internalDropRequested(const QModelIndex& targetIndex);
    void copyRequested();
    void pasteRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
    void drawBranches(QPainter* painter, const QRect& rect,
                      const QModelIndex& index) const override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QStyle* m_branchStyle = nullptr;
    std::function<bool(const QModelIndex&)> m_branchVisibilityProvider;
    bool m_darkTheme = false;
    // Row under the cursor, resolved once per repaint (see paintEvent).
    QModelIndex m_hoveredRow;
};
