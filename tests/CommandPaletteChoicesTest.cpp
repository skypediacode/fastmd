// Regression coverage for CommandPalettePopup::showChoices(): the palette's own
// search row and list present a transient set of choices (the Terminal's shell
// profiles), a picked choice runs after the popup hides, and the next ordinary
// open shows the commands again.

#include "CommandPalettePopup.h"
#include "CommandRegistry.h"

#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QTest>

namespace {

Command makeCommand(const QString& id, const QString& title, std::function<void()> run,
                    const QStringList& keywords = {})
{
    Command command;
    command.id = id;
    command.title = title;
    command.keywords = keywords;
    command.run = std::move(run);
    return command;
}

QStringList rowTitles(QListWidget* list)
{
    QStringList titles;
    for (int i = 0; i < list->count(); ++i)
        titles.append(list->item(i)->text());
    return titles;
}

} // namespace

class CommandPaletteChoicesTest : public QObject {
    Q_OBJECT

private slots:
    void choicesReplaceCommandsUntilHidden();
    void emptyChoicesShowEmptyText();
};

void CommandPaletteChoicesTest::choicesReplaceCommandsUntilHidden()
{
    CommandRegistry commands;
    int commandRuns = 0;
    commands.registerCommand(makeCommand(QStringLiteral("terminal.new-terminal"),
                                         QStringLiteral("New Terminal"),
                                         [&commandRuns] { ++commandRuns; }));

    CommandPalettePopup popup(&commands);
    auto* search = popup.findChild<QLineEdit*>(QStringLiteral("commandPaletteSearchEdit"));
    auto* list = popup.findChild<QListWidget*>(QStringLiteral("commandPaletteList"));
    QVERIFY(search && list);

    QString picked;
    auto* choices = new CommandRegistry;
    choices->registerCommand(makeCommand(QStringLiteral("pwsh"), QStringLiteral("PowerShell (Default)"),
                                         [&picked] { picked = QStringLiteral("pwsh"); },
                                         { QStringLiteral("pwsh") }));
    choices->registerCommand(makeCommand(QStringLiteral("cmd"), QStringLiteral("Command Prompt"),
                                         [&picked] { picked = QStringLiteral("cmd"); },
                                         { QStringLiteral("cmd") }));
    QPointer<CommandRegistry> choicesGuard(choices);

    popup.showChoices(nullptr, choices, QStringLiteral("Select a profile"),
                      QStringLiteral("No shells"));
    QVERIFY(popup.isVisible());
    QCOMPARE(choices->parent(), &popup);
    QCOMPARE(rowTitles(list), QStringList({ QStringLiteral("PowerShell (Default)"),
                                            QStringLiteral("Command Prompt") }));

    search->setText(QStringLiteral("command"));
    QCOMPARE(rowTitles(list), QStringList({ QStringLiteral("Command Prompt") }));

    QTest::keyClick(search, Qt::Key_Return);
    QVERIFY(!popup.isVisible());
    QTRY_COMPARE(picked, QStringLiteral("cmd"));
    QCOMPARE(commandRuns, 0);
    // Retained after hiding, so a pick can still run from it.
    QVERIFY(choicesGuard);

    popup.showForAnchor(nullptr);
    QCOMPARE(rowTitles(list), QStringList({ QStringLiteral("New Terminal") }));
    QTest::keyClick(search, Qt::Key_Return);
    QTRY_COMPARE(commandRuns, 1);

    // A later choice list retires the previous one.
    popup.showChoices(nullptr, new CommandRegistry, QStringLiteral("Select a profile"),
                      QStringLiteral("No shells"));
    QTRY_VERIFY(!choicesGuard);
    popup.hide();
}

void CommandPaletteChoicesTest::emptyChoicesShowEmptyText()
{
    CommandRegistry commands;
    CommandPalettePopup popup(&commands);
    auto* search = popup.findChild<QLineEdit*>(QStringLiteral("commandPaletteSearchEdit"));
    auto* list = popup.findChild<QListWidget*>(QStringLiteral("commandPaletteList"));
    QVERIFY(search && list);

    popup.showChoices(nullptr, new CommandRegistry, QStringLiteral("Select a profile"),
                      QStringLiteral("No supported shell was found on this system."));
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->text(),
             QStringLiteral("No supported shell was found on this system."));
    QVERIFY(!(list->item(0)->flags() & Qt::ItemIsSelectable));
    // Nothing to run: Return leaves the popup open rather than acting.
    QTest::keyClick(search, Qt::Key_Return);
    QVERIFY(popup.isVisible());
    popup.hide();

    popup.showForAnchor(nullptr);
    QCOMPARE(list->item(0)->text(), QStringLiteral("No matching commands"));
    popup.hide();
}

QTEST_MAIN(CommandPaletteChoicesTest)
#include "CommandPaletteChoicesTest.moc"
