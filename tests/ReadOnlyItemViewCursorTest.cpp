#include "ReadOnlyItemViewCursor.h"

#include <QApplication>
#include <QListView>
#include <QTreeView>
#include <QTreeWidget>
#include <QWidget>

#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

void testViewportOwnsReadOnlyCursor()
{
    QWidget textCursorParent;
    textCursorParent.setCursor(Qt::IBeamCursor);
    QTreeView tree(&textCursorParent);
    QTreeWidget treeWidget(&textCursorParent);
    QListView list(&textCursorParent);

    for (QAbstractItemView* view : { static_cast<QAbstractItemView*>(&tree),
                                     static_cast<QAbstractItemView*>(&treeWidget),
                                     static_cast<QAbstractItemView*>(&list) }) {
        CHECK(!view->viewport()->testAttribute(Qt::WA_SetCursor));
        CHECK(view->viewport()->cursor().shape() == Qt::IBeamCursor);
        setReadOnlyItemViewCursor(view);
        CHECK(view->viewport()->testAttribute(Qt::WA_SetCursor));
        CHECK(view->viewport()->cursor().shape() == Qt::ArrowCursor);
    }
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    testViewportOwnsReadOnlyCursor();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
