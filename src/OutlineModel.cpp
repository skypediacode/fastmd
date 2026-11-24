#include "OutlineModel.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <functional>

namespace {

bool matchesFilter(QTreeWidgetItem* item, const QString& text, QTreeWidgetItem*& firstMatch)
{
    const bool self = item->text(0).contains(text, Qt::CaseInsensitive);
    if (self && !firstMatch)
        firstMatch = item;

    bool childVisible = false;
    for (int i = 0; i < item->childCount(); ++i) {
        if (matchesFilter(item->child(i), text, firstMatch))
            childVisible = true;
    }
    const bool visible = self || childVisible;
    item->setHidden(!visible);
    if (visible)
        item->setExpanded(true);
    return visible;
}

}

QList<OutlineHeading> OutlineModel::parse(const QString& text)
{
    QList<OutlineHeading> headings;
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        if (line.isEmpty() || line.at(0) != QLatin1Char('#'))
            continue;

        int level = 0;
        while (level < line.size() && line.at(level) == QLatin1Char('#'))
            ++level;
        if (level < 1 || level > 6 || level >= line.size())
            continue;
        if (!line.at(level).isSpace())
            continue;

        const QString title = line.mid(level).trimmed();
        if (title.isEmpty())
            continue;

        headings.append({ level, title, i });
    }
    return headings;
}

void OutlineModel::populate(QTreeWidget* tree, const QString& text, const QString& emptyText)
{
    tree->clear();

    QTreeWidgetItem* lastItems[7] = { nullptr };
    const QList<OutlineHeading> headings = parse(text);

    for (const OutlineHeading& heading : headings) {
        auto* item = new QTreeWidgetItem();
        item->setText(0, heading.title);
        item->setToolTip(0, heading.title);
        item->setData(0, Qt::UserRole, heading.line);

        QTreeWidgetItem* parent = nullptr;
        for (int l = heading.level - 1; l >= 1; --l) {
            if (lastItems[l]) {
                parent = lastItems[l];
                break;
            }
        }

        if (parent)
            parent->addChild(item);
        else
            tree->addTopLevelItem(item);

        lastItems[heading.level] = item;
        for (int l = heading.level + 1; l <= 6; ++l)
            lastItems[l] = nullptr;
    }

    if (tree->topLevelItemCount() == 0) {
        auto* emptyItem = new QTreeWidgetItem();
        emptyItem->setText(0, emptyText);
        emptyItem->setFlags(Qt::NoItemFlags);
        tree->addTopLevelItem(emptyItem);
    } else {
        tree->expandAll();
    }
}

QTreeWidgetItem* OutlineModel::filter(QTreeWidget* tree, const QString& text)
{
    QTreeWidgetItem* firstMatch = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto* item = tree->topLevelItem(i);
        if (item->flags() == Qt::NoItemFlags)
            continue;
        if (text.isEmpty()) {
            std::function<void(QTreeWidgetItem*)> showAll = [&](QTreeWidgetItem* p) {
                p->setHidden(false);
                p->setExpanded(true);
                for (int c = 0; c < p->childCount(); ++c)
                    showAll(p->child(c));
            };
            showAll(item);
            continue;
        }
        matchesFilter(item, text, firstMatch);
    }
    return firstMatch;
}
