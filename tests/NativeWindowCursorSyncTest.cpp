#include "NativeWindowCursorSync.h"

#include <QApplication>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

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

void sendMove(QWidget* receiver)
{
    QMouseEvent move(QEvent::MouseMove, QPointF(1, 1), receiver->mapToGlobal(QPointF(1, 1)),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(receiver, &move);
}

// The top-level carries the cursor its hovered alien button gave it when a
// sibling's winId() promotes the hierarchy; afterwards only the button's new
// native parent ever hears about that button again.
void testPromotionReleasesStaleTopLevelCursor()
{
    QWidget top;
    top.setGeometry(-4000, -4000, 200, 100);
    auto* panel = new QWidget(&top);
    panel->setGeometry(0, 0, 100, 100);
    auto* button = new QWidget(panel);
    button->setGeometry(10, 10, 30, 30);
    button->setCursor(Qt::PointingHandCursor);
    auto* host = new QWidget(&top);
    host->setGeometry(100, 0, 100, 100);
    top.show();

    QWindow* topHandle = top.windowHandle();
    CHECK(topHandle != nullptr);
    if (!topHandle)
        return;
    topHandle->setCursor(button->cursor());

    host->winId();
    CHECK(panel->internalWinId() != 0);

    NativeWindowCursorSync::schedule(host);
    NativeWindowCursorSync::schedule(host);
    CHECK(top.findChildren<QObject*>(QStringLiteral("fastmdNativeWindowCursorSync"),
                                     Qt::FindDirectChildrenOnly).size() == 1);
    CHECK(topHandle->cursor().shape() == Qt::PointingHandCursor);

    sendMove(panel);
    CHECK(topHandle->cursor().shape() == Qt::ArrowCursor);
    CHECK(button->cursor().shape() == Qt::PointingHandCursor);

    // Consumed by that move: a later promotion can schedule again.
    CHECK(top.findChild<QObject*>(QStringLiteral("fastmdNativeWindowCursorSync"),
                                  Qt::FindDirectChildrenOnly) == nullptr);
    NativeWindowCursorSync::schedule(host);
    CHECK(top.findChild<QObject*>(QStringLiteral("fastmdNativeWindowCursorSync"),
                                  Qt::FindDirectChildrenOnly) != nullptr);
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    testPromotionReleasesStaleTopLevelCursor();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
