// CompactComboBox popup geometry and SearchableComboBox's search dropdown:
// filtering, keyboard, Escape, the pinned first row, placement and sizing.

#include "ApplicationTheme.h"
#include "SearchableComboBox.h"

#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QListView>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const QString& what)
{
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", qPrintable(what));
    }
}

QStringList fontNames()
{
    QStringList names = { QStringLiteral("Arial"), QStringLiteral("Cambria"), QStringLiteral("Consolas"),
                          QStringLiteral("Georgia"), QStringLiteral("Segoe UI"), QStringLiteral("Tahoma"),
                          QStringLiteral("Times New Roman"), QStringLiteral("Verdana") };
    for (int i = 0; i < 60; ++i)
        names.append(QStringLiteral("Zz Filler %1").arg(i, 2, 10, QLatin1Char('0')));
    return names;
}

SearchableComboBox* newFontCombo(QWidget* parent)
{
    auto* combo = new SearchableComboBox(parent);
    combo->setFixedSize(280, 32);
    combo->setMaxVisibleItems(12);
    combo->setFirstRowPinned(true);
    combo->setSearchPlaceholderText(QStringLiteral("Search fonts"));
    combo->addItem(QStringLiteral("Default (Segoe UI)"), QString());
    for (const QString& name : fontNames())
        combo->addItem(name, name);
    return combo;
}

QStringList listed(QListView* list)
{
    QStringList names;
    for (int row = 0; row < list->model()->rowCount(); ++row)
        names.append(list->model()->index(row, 0).data().toString());
    return names;
}

QString currentText(QListView* list)
{
    return list->currentIndex().data().toString();
}

QRect globalRect(QWidget* widget)
{
    return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
}

// The stock popup's list must fill its container. It used to be capped at
// eight rows while QComboBox sized the container for maxVisibleItems() rows,
// which left the difference as blank bands above and below the list.
void testCompactPopupFillsContainer()
{
    for (int visible : { 8, 16 }) {
        QWidget window;
        auto* layout = new QVBoxLayout(&window);
        auto* combo = new CompactComboBox(&window);
        combo->setMaxVisibleItems(visible);
        for (int i = 0; i < 60; ++i)
            combo->addItem(QStringLiteral("Item %1").arg(i));
        layout->addWidget(combo);
        layout->addStretch();
        window.move(20, 20);
        window.resize(320, 80);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "window exposed");

        // showPopup() is protected on CompactComboBox, public on QComboBox.
        static_cast<QComboBox*>(combo)->showPopup();
        QTest::qWait(30);
        QWidget* view = combo->view();
        QWidget* container = view->parentWidget();
        const int slack = container->height() - view->height();
        // Windows (the app's platform) drops the list below the combo in a
        // container QComboBox sizes for maxVisibleItems() rows. The offscreen
        // platform's style uses the "popup" mode instead (list over the combo,
        // scroll arrows, a screen-high container), where only the cap applies;
        // FASTMD_TEST_NATIVE=1 runs this test on the Windows platform.
        QStyleOptionComboBox option;
        option.initFrom(combo);
        const bool popupMode = combo->style()->styleHint(QStyle::SH_ComboBox_Popup, &option, combo);
        check(view->isVisible(), QStringLiteral("stock popup opens (%1 rows)").arg(visible));
        check(popupMode || (slack >= 0 && slack <= 4),
              QStringLiteral("stock popup list fills its container with %1 visible rows: container %2px, list %3px")
                  .arg(visible).arg(container->height()).arg(view->height()));
        check(view->height() >= visible * combo->rowHeight(),
              QStringLiteral("stock popup shows %1 rows (list %2px)").arg(visible).arg(view->height()));
        combo->hidePopup();
    }
}

void testSearchAndKeyboard()
{
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    SearchableComboBox* combo = newFontCombo(&window);
    layout->addWidget(combo);
    layout->addStretch();
    window.move(20, 20);
    window.resize(320, 60);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "window exposed");

    check(combo->searchPopup() == nullptr, "the search popup is not built before the first open");
    combo->setCurrentIndex(combo->findText(QStringLiteral("Segoe UI")));
    QSignalSpy activated(combo, &QComboBox::activated);

    combo->showPopup();
    QTest::qWait(30);
    QWidget* popup = combo->searchPopup();
    QLineEdit* search = combo->searchField();
    QListView* list = combo->searchList();
    check(popup && popup->isVisible(), "the popup opens");
    if (!popup || !search || !list)
        return;
    check(QApplication::focusWidget() == search, "opening the popup focuses the search field");
    check(search->placeholderText() == QStringLiteral("Search fonts"), "placeholder text");
    check(list->model()->rowCount() == combo->count(), "an empty search lists every item");
    check(currentText(list) == QStringLiteral("Segoe UI"), "the selected font is current on open");
    check(list->viewport()->rect().contains(list->visualRect(list->currentIndex())),
          "the selected font is scrolled into view on open");

    // Layout: the list starts right under the search field and the popup ends
    // right under the list; no blank bands.
    const QRect searchRect = search->geometry();
    const QRect listRect = list->geometry();
    check(listRect.top() - searchRect.bottom() - 1 <= 4,
          QStringLiteral("the list starts under the search field (gap %1px)")
              .arg(listRect.top() - searchRect.bottom() - 1));
    check(popup->height() - listRect.bottom() - 1 <= 5,
          QStringLiteral("no blank band under the list (%1px)").arg(popup->height() - listRect.bottom() - 1));
    check(list->height() == 12 * combo->rowHeight(),
          QStringLiteral("the list shows maxVisibleItems rows (%1px)").arg(list->height()));
    check(popup->width() == combo->width(), "the popup is as wide as the combo");
    const QRect comboGlobal = globalRect(combo);
    check(popup->geometry().top() >= comboGlobal.bottom(), "the popup opens below the combo");

    QTest::keyClicks(search, QStringLiteral("geo"));
    check(listed(list) == QStringList({ QStringLiteral("Default (Segoe UI)"), QStringLiteral("Georgia") }),
          QStringLiteral("'geo' finds Georgia, Default stays listed: %1").arg(listed(list).join(u'|')));
    check(currentText(list) == QStringLiteral("Georgia"), "the best match is current, not the pinned Default");
    check(list->height() == 2 * combo->rowHeight(), "the popup shrinks to the matches");
    check(popup->height() - list->geometry().bottom() - 1 <= 5, "no blank band after filtering");

    search->clear();
    QTest::keyClicks(search, QStringLiteral("GEO"));
    check(listed(list).contains(QStringLiteral("Georgia")), "search is case-insensitive");
    search->clear();
    QTest::keyClicks(search, QStringLiteral("new rom"));
    check(currentText(list) == QStringLiteral("Times New Roman"), "partial match inside a name");

    search->clear();
    check(list->model()->rowCount() == combo->count(), "clearing the search restores the full list");
    check(currentText(list) == QStringLiteral("Segoe UI"), "clearing the search returns to the selected font");

    QTest::keyClicks(search, QStringLiteral("a"));
    const QString first = currentText(list);
    QTest::keyClick(search, Qt::Key_Down);
    const QString second = currentText(list);
    check(second != first && !second.isEmpty(), "Down moves to the next match");
    QTest::keyClick(search, Qt::Key_Up);
    check(currentText(list) == first, "Up moves back");
    check(QApplication::focusWidget() == search, "arrow keys leave focus in the search field");

    search->clear();
    QTest::keyClicks(search, QStringLiteral("zzq-no-such-font"));
    check(listed(list) == QStringList({ QStringLiteral("Default (Segoe UI)") }),
          "with no match only Default is listed");
    check(currentText(list) == QStringLiteral("Default (Segoe UI)"), "and it is current");

    search->clear();
    QTest::keyClicks(search, QStringLiteral("geo"));
    QTest::keyClick(search, Qt::Key_Return);
    check(!popup->isVisible(), "Enter closes the popup");
    check(combo->currentText() == QStringLiteral("Georgia"), "Enter selects the highlighted font");
    check(combo->currentData().toString() == QStringLiteral("Georgia"), "the selected font's data");
    check(activated.count() == 1 && activated.at(0).at(0).toInt() == combo->findText(QStringLiteral("Georgia")),
          "Enter emits activated() once with the source row");

    combo->showPopup();
    QTest::qWait(30);
    check(search->text().isEmpty(), "reopening starts with an empty search");
    check(list->model()->rowCount() == combo->count(), "reopening lists every item");
    check(currentText(list) == QStringLiteral("Georgia"), "reopening highlights the new selection");
    QTest::keyClicks(search, QStringLiteral("times"));
    QTest::keyClick(search, Qt::Key_Escape);
    check(!popup->isVisible(), "Escape closes the popup");
    check(combo->currentText() == QStringLiteral("Georgia"), "Escape leaves the selection unchanged");
    check(activated.count() == 1, "Escape emits nothing");

    combo->showPopup();
    QTest::qWait(30);
    QTest::keyClick(search, Qt::Key_Up);
    QTest::keyClick(search, Qt::Key_Up);
    QTest::keyClick(search, Qt::Key_Up);
    QTest::keyClick(search, Qt::Key_Up);
    check(currentText(list) == QStringLiteral("Default (Segoe UI)"), "Up reaches Default");
    QTest::keyClick(search, Qt::Key_Enter);
    check(combo->currentIndex() == 0 && combo->currentData().toString().isEmpty(),
          "choosing Default selects the empty (built-in) family");

    combo->showPopup();
    QTest::qWait(30);
    QTest::keyClicks(search, QStringLiteral("verd"));
    const QModelIndex verdana = list->currentIndex();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualRect(verdana).center());
    check(!popup->isVisible(), "a click closes the popup");
    check(combo->currentText() == QStringLiteral("Verdana"), "a click selects the font");
}

void testPlacement()
{
    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();

    // Near the bottom of the screen the popup opens above the combo.
    QWidget window(nullptr, Qt::FramelessWindowHint);
    auto* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    SearchableComboBox* combo = newFontCombo(&window);
    layout->addWidget(combo);
    window.resize(300, 32);
    window.move(avail.left() + 40, avail.bottom() - 60);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "window exposed");
    combo->showPopup();
    QTest::qWait(30);
    QWidget* popup = combo->searchPopup();
    const QRect comboGlobal = globalRect(combo);
    check(popup->geometry().bottom() < comboGlobal.top(), "near the screen bottom the popup opens above");
    check(avail.contains(popup->geometry()),
          QStringLiteral("the popup stays on screen (%1,%2 %3x%4)")
              .arg(popup->x()).arg(popup->y()).arg(popup->width()).arg(popup->height()));
    // Filtering keeps it attached to the combo's top edge.
    QTest::keyClicks(combo->searchField(), QStringLiteral("geo"));
    check(popup->geometry().bottom() == comboGlobal.top() - 3,
          QStringLiteral("a popup above stays anchored while it shrinks (bottom %1, combo top %2)")
              .arg(popup->geometry().bottom()).arg(comboGlobal.top()));
    QTest::keyClick(combo->searchField(), Qt::Key_Escape);

    // With room on neither side for every row, the list is cut to whole rows
    // that fit, and still scrolls.
    combo->setMaxVisibleItems(200);
    window.move(avail.left() + 40, avail.top() + avail.height() / 2);
    QTest::qWait(30);
    combo->showPopup();
    QTest::qWait(30);
    QListView* list = combo->searchList();
    check(avail.contains(popup->geometry()), "a tall list is clamped to the screen");
    check(list->height() % combo->rowHeight() == 0, "the clamped list shows whole rows");
    check(list->verticalScrollBar()->isVisible() && list->verticalScrollBar()->maximum() > 0,
          "the clamped list scrolls");
    QTest::keyClick(combo->searchField(), Qt::Key_Escape);
}

} // namespace

int main(int argc, char** argv)
{
    if (!qEnvironmentVariableIsSet("FASTMD_TEST_NATIVE")
        && QDir(QStringLiteral(FASTMD_QT_PLATFORMS_DIR)).exists(QStringLiteral("qoffscreen.dll"))) {
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QStringLiteral(FASTMD_QT_PLATFORMS_DIR).toLocal8Bit());
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    for (const char* theme : { "light", "dark" }) {
        ApplicationTheme::apply(QString::fromLatin1(theme));
        testCompactPopupFillsContainer();
        testSearchAndKeyboard();
        testPlacement();
    }
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("SearchableComboBoxTest: all checks passed\n");
    return 0;
}
