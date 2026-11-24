// Guards runtime theme switches against stale palettes. A widget the
// application stylesheet styles (QTabWidget#documentTabs) is repolished by
// qApp->setStyleSheet(); if that runs while the previous theme's application
// palette is still current, the repolish fixes the old palette onto the widget
// and its unstyled children (the custom-painted Workspace toggle corner), and
// the following QApplication::setPalette() never reaches them. Startup was
// unaffected because those widgets are first polished after both calls.
//
// Also guards the Editor font preference: the QSS rule it appends must reach
// MarkdownEditor (outranking the QPlainTextEdit rule) and nothing else, survive
// a theme switch, and leave the default stylesheet untouched.

#include "ApplicationTheme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QToolButton>
#include <QWidget>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

bool isDarkColor(const QColor& color) { return color.lightness() < 128; }

// Same class name as the real editor, which is all a QSS type selector sees.
class MarkdownEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    using QPlainTextEdit::QPlainTextEdit;
};

// What a toggle built under the current theme gets, i.e. the startup path.
QColor freshToggleText()
{
    QTabWidget host;
    host.setObjectName(QStringLiteral("documentTabs"));
    auto* corner = new QWidget(&host);
    auto* toggle = new QToolButton(corner);
    toggle->setObjectName(QStringLiteral("workspaceToggleButton"));
    host.setCornerWidget(corner, Qt::TopLeftCorner);
    host.show();
    QApplication::processEvents();
    return toggle->palette().color(QPalette::ButtonText);
}

void checkTheme(const QWidget& corner, const QToolButton& toggle, bool dark, int line)
{
    check(isDarkColor(corner.palette().color(QPalette::Window)) == dark,
          "corner Window role follows the application theme", line);
    check(isDarkColor(toggle.palette().color(QPalette::ButtonText)) == !dark,
          "toggle ButtonText role follows the application theme", line);
    check(toggle.palette().color(QPalette::ButtonText) == freshToggleText(),
          "toggle ButtonText matches a toggle built under this theme", line);
}

void testRuntimeSwitch(const QString& startTheme)
{
    ApplicationTheme::apply(startTheme);

    QTabWidget tabs;
    tabs.setObjectName(QStringLiteral("documentTabs"));
    tabs.addTab(new QWidget, QStringLiteral("doc"));
    auto* corner = new QWidget(&tabs);
    auto* layout = new QHBoxLayout(corner);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* toggle = new QToolButton(corner);
    toggle->setObjectName(QStringLiteral("workspaceToggleButton"));
    toggle->setCheckable(true);
    toggle->setChecked(true);
    layout->addWidget(toggle);
    tabs.setCornerWidget(corner, Qt::TopLeftCorner);
    tabs.show();
    QApplication::processEvents();

    bool dark = startTheme == QStringLiteral("dark");
    checkTheme(*corner, *toggle, dark, __LINE__);
    for (int i = 0; i < 4; ++i) {
        dark = !dark;
        ApplicationTheme::apply(dark ? QStringLiteral("dark") : QStringLiteral("light"));
        QApplication::processEvents();
        checkTheme(*corner, *toggle, dark, __LINE__);
        toggle->setChecked(!toggle->isChecked());
        checkTheme(*corner, *toggle, dark, __LINE__);
    }
}

void testEditorFontFamily()
{
    ApplicationTheme::apply(QStringLiteral("light"));
    CHECK(ApplicationTheme::editorFontFamily().isEmpty());
    CHECK(!ApplicationTheme::styleSheet(false).contains(QStringLiteral("MarkdownEditor {")));
    CHECK(!ApplicationTheme::styleSheet(true).contains(QStringLiteral("MarkdownEditor {")));
    const QString lightDefault = qApp->styleSheet();

    MarkdownEditor editor;
    QPlainTextEdit diffPane;
    editor.show();
    diffPane.show();
    QApplication::processEvents();
    CHECK(editor.font().family() == QStringLiteral("Consolas"));
    CHECK(diffPane.font().family() == QStringLiteral("Consolas"));

    CHECK(ApplicationTheme::applyEditorFontFamily(QStringLiteral("Courier New")));
    QApplication::processEvents();
    CHECK(editor.font().family() == QStringLiteral("Courier New"));
    CHECK(diffPane.font().family() == QStringLiteral("Consolas"));
    const QString custom = qApp->styleSheet();
    CHECK(custom.startsWith(lightDefault));
    CHECK(!ApplicationTheme::applyEditorFontFamily(QStringLiteral(" Courier New ")));
    CHECK(qApp->styleSheet() == custom);

    ApplicationTheme::apply(QStringLiteral("dark"));
    QApplication::processEvents();
    CHECK(editor.font().family() == QStringLiteral("Courier New"));
    CHECK(diffPane.font().family() == QStringLiteral("Consolas"));

    CHECK(ApplicationTheme::applyEditorFontFamily(QString()));
    QApplication::processEvents();
    CHECK(editor.font().family() == QStringLiteral("Consolas"));
    CHECK(!qApp->styleSheet().contains(QStringLiteral("MarkdownEditor {")));
    ApplicationTheme::apply(QStringLiteral("light"));
    CHECK(qApp->styleSheet() == lightDefault);
}

}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    testRuntimeSwitch(QStringLiteral("light"));
    testRuntimeSwitch(QStringLiteral("dark"));
    testEditorFontFamily();
    if (g_failures == 0)
        std::printf("ApplicationThemeSwitchTest: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

#include "ApplicationThemeSwitchTest.moc"
