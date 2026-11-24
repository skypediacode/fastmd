#pragma once
#include <QList>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;

struct OutlineHeading {
    int     level = 1;
    QString title;
    int     line = 0;   // 0-based source line
};

// Single source of truth for the document outline. Both the floating
// OutlinePopup and the Go To dialog's Outline tab render from here, so heading
// detection is never duplicated.
namespace OutlineModel {

QList<OutlineHeading> parse(const QString& text);

// Rebuilds the tree hierarchically; each item stores its 0-based source line in
// Qt::UserRole. When nothing is found, a single disabled placeholder is added.
void populate(QTreeWidget* tree, const QString& text, const QString& emptyText);

// Hides items that neither match nor contain a match; ancestors of a match stay
// visible and expanded. Returns the first item whose own text matched, so a
// caller can preselect it (null for an empty filter or no match).
QTreeWidgetItem* filter(QTreeWidget* tree, const QString& text);

}
