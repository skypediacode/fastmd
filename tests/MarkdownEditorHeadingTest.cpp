#include "CommandRegistry.h"
#include "CommandShortcutRegistry.h"
#include "MainWindow.h"
#include "MarkdownEditor.h"

#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolBar>
#include <cstdio>
#include <utility>

namespace {
int checks = 0;
int failures = 0;
void check(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL line %d: %s\n", line, expression);
    }
}
#define CHECK(x) check((x), #x, __LINE__)

using HeadingSlot = void (MarkdownEditor::*)();
const HeadingSlot headings[] = {
    &MarkdownEditor::fmtH1, &MarkdownEditor::fmtH2, &MarkdownEditor::fmtH3,
    &MarkdownEditor::fmtH4, &MarkdownEditor::fmtH5, &MarkdownEditor::fmtH6,
};

QString heading(int level)
{
    return QString(level, QLatin1Char('#')) + QStringLiteral(" Title");
}

void editorBehavior()
{
    MarkdownEditor editor(nullptr, 14);
    for (int level = 1; level <= 6; ++level) {
        const auto apply = headings[level - 1];
        for (const QString& source : {QString(), QStringLiteral("Title")}) {
            editor.setPlainText(source);
            editor.document()->clearUndoRedoStacks();
            (editor.*apply)();
            const QString expected = QString(level, QLatin1Char('#')) + QLatin1Char(' ') + source;
            CHECK(editor.toPlainText() == expected);
            editor.undo();
            CHECK(editor.toPlainText() == source);
            CHECK(!editor.document()->isUndoAvailable());
            editor.redo();
            CHECK(editor.toPlainText() == expected);
            (editor.*apply)();
            CHECK(editor.toPlainText() == source);
            editor.undo();
            CHECK(editor.toPlainText() == expected);
            editor.redo();
            CHECK(editor.toPlainText() == source);
        }
        for (int previous = 1; previous <= 6; ++previous) {
            editor.setPlainText(heading(previous));
            editor.document()->clearUndoRedoStacks();
            (editor.*apply)();
            const QString expected = previous == level ? QStringLiteral("Title") : heading(level);
            CHECK(editor.toPlainText() == expected);
            editor.undo();
            CHECK(editor.toPlainText() == heading(previous));
            CHECK(!editor.document()->isUndoAvailable());
            editor.redo();
            CHECK(editor.toPlainText() == expected);
        }
        // Existing H1-H3 semantics: only the active cursor's line is formatted,
        // even when the selection spans several lines or ends at column zero.
        for (const auto& range : {std::pair{2, 4}, std::pair{2, 10},
                                  std::pair{10, 2}, std::pair{2, 6}}) {
            const QString source = QStringLiteral("first\nTitle\nlast");
            editor.setPlainText(source);
            editor.document()->clearUndoRedoStacks();
            QTextCursor cursor = editor.textCursor();
            cursor.setPosition(range.first);
            cursor.setPosition(range.second, QTextCursor::KeepAnchor);
            const int block = cursor.blockNumber();
            editor.setTextCursor(cursor);
            (editor.*apply)();
            QStringList lines = source.split(QLatin1Char('\n'));
            lines[block].prepend(QString(level, QLatin1Char('#')) + QLatin1Char(' '));
            CHECK(editor.toPlainText() == lines.join(QLatin1Char('\n')));
            editor.undo();
            CHECK(editor.toPlainText() == source);
            CHECK(!editor.document()->isUndoAvailable());
            editor.redo();
            CHECK(editor.toPlainText() == lines.join(QLatin1Char('\n')));
        }
    }
}

void actionsAndPalette()
{
    // Never show MainWindow: no first paint, startup tasks, or WebView2 host.
    MainWindow window(nullptr, MainWindow::StartupMode::TransferTarget);
    CHECK(QMetaObject::invokeMethod(&window, "newFile", Qt::DirectConnection));
    auto* editor = window.findChild<MarkdownEditor*>();
    CHECK(editor);
    if (!editor)
        return;
    QAction* paletteAction = nullptr;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->shortcut() == QKeySequence(Qt::Key_F1))
            paletteAction = action;
    }
    CHECK(paletteAction);
    if (!paletteAction)
        return;
    paletteAction->trigger();
    auto* registry = window.findChild<CommandRegistry*>();
    CHECK(registry);
    if (!registry)
        return;
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("mainToolbar"));
    CHECK(toolbar);
    for (int level = 1; level <= 6; ++level) {
        const QString title = QStringLiteral("Heading %1").arg(level);
        const QKeySequence chord(Qt::CTRL | (Qt::Key_1 + level - 1));
        QAction* action = nullptr;
        int owners = 0;
        for (auto* candidate : window.findChildren<QAction*>()) {
            if (candidate->shortcuts().contains(chord)) {
                action = candidate;
                ++owners;
            }
        }
        CHECK(owners == 1);
        if (!action)
            continue;
        CHECK(action->text() == title);
        CHECK(action->shortcutContext() == Qt::WindowShortcut);
        CHECK(action->isEnabled());
        CHECK(toolbar && toolbar->actions().contains(action) == (level <= 3));
        CHECK(level <= 3 || window.actions().contains(action));
        editor->setPlainText(QStringLiteral("Title"));
        action->trigger();
        CHECK(editor->toPlainText() == heading(level));
        editor->undo();
        CHECK(editor->toPlainText() == QStringLiteral("Title"));
        int commands = 0;
        for (const auto& command : registry->commands()) {
            if (command.title != title)
                continue;
            ++commands;
            CHECK(command.id == QStringLiteral("insert.heading-%1").arg(level));
            CHECK(command.shortcut == chord);
            CHECK(command.enabled && command.enabled());
            editor->setPlainText(QStringLiteral("Title"));
            command.run();
            CHECK(editor->toPlainText() == heading(level));
            command.run();
            CHECK(editor->toPlainText() == QStringLiteral("Title"));
            CHECK(QMetaObject::invokeMethod(&window, "setEditorMode", Qt::DirectConnection, Q_ARG(bool, false)));
            CHECK(!action->isEnabled());
            CHECK(!command.enabled());
            CHECK(QMetaObject::invokeMethod(&window, "setEditorMode", Qt::DirectConnection, Q_ARG(bool, true)));
            CHECK(action->isEnabled());
            CHECK(command.enabled());
            for (int alias = 1; alias <= 6; ++alias)
                CHECK(command.keywords.contains(QStringLiteral("h%1").arg(alias)));
        }
        CHECK(commands == 1);
    }
    editor->document()->setModified(false);
}
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FastMDTests"));
    QCoreApplication::setApplicationName(QStringLiteral("HeadingTest"));
    QTemporaryDir settings;
    CHECK(settings.isValid());
    if (!settings.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    editorBehavior();
    actionsAndPalette();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
