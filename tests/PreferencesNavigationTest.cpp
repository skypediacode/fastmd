// Guards the Command Palette's "Settings: ..." commands: every target resolves
// to a control on the tab it belongs to, a command lands focus on that control
// (or on the setting that controls it while it is disabled), opening one never
// changes a value, and none of it builds Preferences before a command runs.

#include "CommandRegistry.h"
#include "MainWindow.h"
#include "PreferencesDialog.h"
#include "PreferencesTargets.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <functional>

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL line %d: %s\n", line, expression);
    }
}
#define CHECK(x) check((x), #x, __LINE__)

using PreferencesTargets::Id;

void settle()
{
    for (int i = 0; i < 8; ++i)
        QApplication::processEvents(QEventLoop::AllEvents);
}

QString idName(Id id)
{
    return QString::number(static_cast<int>(id));
}

// Where each setting lives. Moving one to another tab should be a deliberate,
// visible change here, not a silent one.
QString expectedTab(Id id)
{
    switch (id) {
    case Id::RestoreSession:
    case Id::CloseAppOnLastTab:
    case Id::ConfirmCloseAllTerminals:
    case Id::HomeFolder:
    case Id::DefaultSaveFolder:
    case Id::OpenPdfExternally:
    case Id::OpenHtmlExternally:
    case Id::OpenImagesExternally:
    case Id::ShowExportSuccessDialog:
        return QStringLiteral("General");
    case Id::HighlightCurrentLine:
    case Id::RenderSingleLineBreaks:
    case Id::DefaultEditorMode:
    case Id::AutoSave:
    case Id::AutoSaveDelay:
    case Id::CrashRecovery:
    case Id::SpellCheck:
        return QStringLiteral("Editor");
    case Id::EditorFont:
    case Id::PreviewFont:
    case Id::PrintFont:
    case Id::LineSpacing:
    case Id::ParagraphSpacing:
    case Id::ListSpacing:
        return QStringLiteral("Typography");
    case Id::FileAssociations:
        return QStringLiteral("File Associations");
    case Id::Count:
        break;
    }
    return {};
}

QTabWidget* tabsOf(PreferencesDialog& dialog)
{
    return dialog.findChild<QTabWidget*>(QStringLiteral("preferencesTabs"));
}

QString currentTab(PreferencesDialog& dialog)
{
    QTabWidget* tabs = tabsOf(dialog);
    return tabs ? tabs->tabText(tabs->currentIndex()) : QString();
}

QWidget* firstAvailable(PreferencesDialog& dialog, Id id)
{
    for (QWidget* widget : dialog.targetWidgets(id)) {
        if (widget->isVisible() && widget->isEnabled())
            return widget;
    }
    return nullptr;
}

// Every value the dialog can report, so a reveal that touched one shows up.
QString values(const PreferencesDialog& d)
{
    return QStringList{
        QString::number(d.restoreSessionOnStartup()), QString::number(d.closeAppOnLastTabClose()),
        d.defaultSaveFolder(), d.homeFolder(),
        QString::number(d.openPdfExternally()), QString::number(d.openHtmlExternally()),
        QString::number(d.openImagesExternally()), QString::number(d.showExportSuccessDialog()),
        QString::number(static_cast<int>(d.defaultNewDocMode())),
        QString::number(d.highlightCurrentLineEnabled()),
        QString::number(d.renderSingleLineBreaksEnabled()),
        QString::number(d.autoSaveEnabled()), QString::number(d.autoSaveDelayMs()),
        QString::number(d.crashRecoveryEnabled()),
        QString::number(d.spellCheckEnabled()), d.spellCheckLanguage(),
        d.editorFontFamily(), d.previewFontFamily(), d.printFontFamily(),
        QString::number(d.documentSpacing().line), QString::number(d.documentSpacing().paragraph),
        QString::number(d.documentSpacing().list),
    }.join(u'|');
}

void everyTargetResolves()
{
    PreferencesDialog dialog;
    for (int i = 0; i < static_cast<int>(Id::Count); ++i) {
        const Id id = static_cast<Id>(i);
        const QList<QWidget*> widgets = dialog.targetWidgets(id);
        CHECK(!widgets.isEmpty());
        if (widgets.isEmpty()) {
            std::printf("  target %s has no control\n", qPrintable(idName(id)));
            continue;
        }
        QTabWidget* tabs = tabsOf(dialog);
        CHECK(tabs);
        if (!tabs)
            return;
        QWidget* page = nullptr;
        for (int t = 0; t < tabs->count(); ++t) {
            for (QWidget* widget : widgets) {
                if (tabs->widget(t)->isAncestorOf(widget))
                    page = tabs->widget(t);
            }
        }
        CHECK(page);
        if (!page)
            continue;
        CHECK(tabs->tabText(tabs->indexOf(page)) == expectedTab(id));
        for (QWidget* widget : widgets) {
            CHECK(widget);
            // The tab header is the one control allowed outside the page.
            CHECK(widget && (page->isAncestorOf(widget) || widget == tabs->tabBar()));
            CHECK(widget && widget->focusPolicy() != Qt::NoFocus);
        }
    }
}

void registeredCommandsAreDistinct()
{
    CommandRegistry registry;
    QList<Id> opened;
    PreferencesTargets::registerAll(&registry, [&opened](Id id) { opened.append(id); });
    CHECK(registry.commands().size() == PreferencesTargets::all().size());

    QSet<QString> ids;
    QSet<QString> titles;
    QSet<int> targets;
    for (const PreferencesTargets::Spec& spec : PreferencesTargets::all()) {
        CHECK(spec.id != Id::Count);
        CHECK(spec.id != Id::SpellCheck);
        targets.insert(static_cast<int>(spec.id));
        titles.insert(spec.title.toLower());
    }
    for (const Command& command : registry.commands()) {
        CHECK(command.category == QStringLiteral("Settings"));
        CHECK(command.id.startsWith(QStringLiteral("settings.")));
        CHECK(command.shortcut.isEmpty());
        CHECK(!command.enabled);
        ids.insert(command.id);
    }
    CHECK(ids.size() == registry.commands().size());
    CHECK(titles.size() == registry.commands().size());
    CHECK(targets.size() == registry.commands().size());
    CHECK(!ids.contains(QStringLiteral("settings.print-/-pdf-font")));
    CHECK(ids.contains(QStringLiteral("settings.print-pdf-font")));

    for (int i = 0; i < registry.commands().size(); ++i)
        registry.commands().at(i).run();
    CHECK(opened.size() == PreferencesTargets::all().size());
    for (int i = 0; i < opened.size(); ++i)
        CHECK(opened.at(i) == PreferencesTargets::all().at(i).id);
}

void revealFocusesTheControlWithoutChangingIt()
{
    for (const PreferencesTargets::Spec& spec : PreferencesTargets::all()) {
        PreferencesDialog dialog;
        dialog.setAutoSaveEnabled(true);
        const QString before = values(dialog);
        dialog.revealTarget(spec.id);
        CHECK(currentTab(dialog) == expectedTab(spec.id));
        dialog.show();
        settle();
        QWidget* expected = dialog.targetWidgets(spec.id).first();
        CHECK(dialog.focusWidget() == expected);
        if (dialog.focusWidget() != expected)
            std::printf("  %s did not focus its control\n", qPrintable(spec.title));
        // The focus frame belongs to keyboard navigation, not to a command.
        CHECK(!dialog.testAttribute(Qt::WA_KeyboardFocusChange));
        CHECK(values(dialog) == before);
        CHECK(!QApplication::activePopupWidget());
        // Enter must still mean OK wherever focus landed.
        auto* button = qobject_cast<QPushButton*>(dialog.focusWidget());
        CHECK(!button || button->text() != QStringLiteral("Associate Markdown and Text Files with FastMD"));
        if (auto* combo = qobject_cast<QComboBox*>(expected))
            CHECK(!combo->view()->isVisible());
        dialog.reject();
    }
}

void disabledTargetsFallBackToTheirParentSetting()
{
    {
        PreferencesDialog dialog;
        dialog.setAutoSaveEnabled(false);
        dialog.revealTarget(Id::AutoSaveDelay);
        dialog.show();
        settle();
        const QList<QWidget*> widgets = dialog.targetWidgets(Id::AutoSaveDelay);
        CHECK(widgets.size() == 2);
        CHECK(!widgets.first()->isEnabled());
        CHECK(dialog.focusWidget() == widgets.last());
        CHECK(qobject_cast<QCheckBox*>(widgets.last()));
        CHECK(!dialog.autoSaveEnabled());
    }
    {
        // Spell check: with no languages the checkbox is disabled and the
        // Windows language settings button is where the user can act.
        PreferencesDialog dialog;
        dialog.setAvailableSpellLanguages({});
        dialog.revealTarget(Id::SpellCheck);
        dialog.show();
        settle();
        const QList<QWidget*> widgets = dialog.targetWidgets(Id::SpellCheck);
        CHECK(!widgets.first()->isEnabled());
        CHECK(dialog.focusWidget() == widgets.last());
        CHECK(qobject_cast<QPushButton*>(widgets.last()));
    }
    {
        PreferencesDialog dialog;
        dialog.setAvailableSpellLanguages({ { QStringLiteral("en-US"), QStringLiteral("English") } });
        dialog.setSpellCheckEnabled(false);
        dialog.revealTarget(Id::SpellCheck);
        dialog.show();
        settle();
        CHECK(dialog.focusWidget() == dialog.targetWidgets(Id::SpellCheck).first());
        CHECK(!dialog.spellCheckEnabled());
    }
    {
        PreferencesDialog dialog;
        dialog.setDefaultNewDocMode(EditorMode::PlainText);
        dialog.revealTarget(Id::DefaultEditorMode);
        dialog.show();
        settle();
        auto* radio = qobject_cast<QRadioButton*>(dialog.focusWidget());
        CHECK(radio && radio->isChecked());
        CHECK(dialog.defaultNewDocMode() == EditorMode::PlainText);
    }
}

// Runs `inspect` against the modal Preferences the command opened, then closes
// it with `button`. A watchdog closes it regardless so a failure cannot hang.
void runWithDialog(const std::function<void()>& run,
                   const std::function<void(PreferencesDialog&)>& inspect,
                   QDialogButtonBox::StandardButton button, bool* opened)
{
    *opened = false;
    QTimer::singleShot(0, [&]() {
        settle();
        auto* dialog = qobject_cast<PreferencesDialog*>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        *opened = true;
        inspect(*dialog);
        auto* box = dialog->findChild<QDialogButtonBox*>();
        if (box && box->button(button))
            box->button(button)->click();
        else
            dialog->reject();
    });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, []() {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    watchdog.start(5000);
    run();
}

const Command* findCommand(const CommandRegistry& registry, const QString& category,
                           const QString& title)
{
    for (const Command& command : registry.commands()) {
        if (command.category == category && command.title == title)
            return &command;
    }
    return nullptr;
}

QByteArray settingsSnapshot()
{
    QSettings().sync();
    QFile file(QSettings().fileName());
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void paletteOpensPreferencesOnTheControl()
{
    MainWindow window(nullptr, MainWindow::StartupMode::TransferTarget);
    CHECK(window.findChildren<PreferencesDialog*>().isEmpty());

    QAction* paletteAction = nullptr;
    for (auto* action : window.findChildren<QAction*>()) {
        if (action->shortcut() == QKeySequence(Qt::Key_F1))
            paletteAction = action;
    }
    CHECK(paletteAction);
    if (!paletteAction)
        return;
    paletteAction->trigger();
    settle();
    auto* registry = window.findChild<CommandRegistry*>();
    CHECK(registry);
    if (!registry)
        return;
    // Building the palette registers the commands but not the dialog.
    CHECK(window.findChildren<PreferencesDialog*>().isEmpty());

    QSet<QString> ids;
    for (const Command& command : registry->commands())
        ids.insert(command.id);
    CHECK(ids.size() == registry->commands().size());

    for (const PreferencesTargets::Spec& spec : PreferencesTargets::all()) {
        const Command* command = findCommand(*registry, QStringLiteral("Settings"), spec.title);
        CHECK(command);
        CHECK(command && !command->keywords.isEmpty());
    }

    const auto first = [registry](const QString& query) {
        const QVector<CommandMatch> matches = registry->search(query);
        return matches.isEmpty() ? QString()
            : matches.first().command->category + QStringLiteral(": ") + matches.first().command->title;
    };
    const auto within = [registry](const QString& query, const QString& title, int limit) {
        const QVector<CommandMatch> matches = registry->search(query);
        for (int i = 0; i < qMin(limit, int(matches.size())); ++i) {
            if (matches.at(i).command->category == QStringLiteral("Settings")
                && matches.at(i).command->title == title)
                return true;
        }
        return false;
    };
    CHECK(first(QStringLiteral("preview font")) == QStringLiteral("Settings: Preview Font"));
    CHECK(first(QStringLiteral("pdf font")) == QStringLiteral("Settings: Print / PDF Font"));
    CHECK(first(QStringLiteral("autosave")) == QStringLiteral("Settings: Auto Save"));
    CHECK(first(QStringLiteral("open with")) == QStringLiteral("Settings: File Associations"));
    CHECK(within(QStringLiteral("font"), QStringLiteral("Editor Font"), 5));
    CHECK(within(QStringLiteral("typeface"), QStringLiteral("Print / PDF Font"), 5));
    CHECK(first(QStringLiteral("line spacing")) == QStringLiteral("Settings: Line Spacing"));
    CHECK(first(QStringLiteral("paragraph spacing")) == QStringLiteral("Settings: Paragraph Spacing"));
    CHECK(first(QStringLiteral("list spacing")) == QStringLiteral("Settings: List Spacing"));
    CHECK(within(QStringLiteral("line height"), QStringLiteral("Line Spacing"), 3));
    CHECK(within(QStringLiteral("folder"), QStringLiteral("Home Folder"), 8));
    CHECK(within(QStringLiteral("association"), QStringLiteral("File Associations"), 1));
    // The settings must not push established commands down their own queries.
    CHECK(first(QStringLiteral("settings")) == QStringLiteral("File: Preferences"));
    for (const char* query : { "pdf", "quit", "exit", "wrap", "theme", "spell" })
        CHECK(!first(QString::fromLatin1(query)).startsWith(QStringLiteral("Settings: ")));
    CHECK(first(QStringLiteral("spell check settings")) == QStringLiteral("Spell Check: Open Settings"));

    const QByteArray before = settingsSnapshot();
    bool opened = false;

    const Command* previewFont = findCommand(*registry, QStringLiteral("Settings"),
                                             QStringLiteral("Preview Font"));
    CHECK(previewFont);
    if (previewFont) {
        runWithDialog(previewFont->run, [](PreferencesDialog& dialog) {
            CHECK(dialog.parentWidget() && dialog.parentWidget()->isWindow());
            CHECK(currentTab(dialog) == QStringLiteral("Typography"));
            QWidget* combo = dialog.targetWidgets(Id::PreviewFont).first();
            CHECK(dialog.focusWidget() == combo);
            CHECK(dialog.previewFontFamily().isEmpty());
            CHECK(!QApplication::activePopupWidget());
        }, QDialogButtonBox::Cancel, &opened);
        CHECK(opened);
        CHECK(settingsSnapshot() == before);
    }

    const Command* delay = findCommand(*registry, QStringLiteral("Settings"),
                                       QStringLiteral("Auto Save Delay"));
    CHECK(delay);
    if (delay) {
        // Auto Save is off by default, so the delay gives way to its checkbox.
        runWithDialog(delay->run, [](PreferencesDialog& dialog) {
            CHECK(currentTab(dialog) == QStringLiteral("Editor"));
            CHECK(dialog.focusWidget() == dialog.targetWidgets(Id::AutoSaveDelay).last());
            CHECK(!dialog.autoSaveEnabled());
        }, QDialogButtonBox::Ok, &opened);
        CHECK(opened);
        // OK with nothing changed applies what was already there.
        QSettings settings;
        CHECK(!settings.value(QStringLiteral("autoSave/enabled"), false).toBool());
        CHECK(settings.value(QStringLiteral("autoSave/delayMs"), 2000).toInt() == 2000);
        CHECK(!settings.contains(QStringLiteral("preview/fontFamily")));
    }

    // Document Spacing: Cancel changes nothing, OK persists exactly the chosen
    // presets (zero included), and choosing the defaults again removes the keys.
    const Command* lineSpacing = findCommand(*registry, QStringLiteral("Settings"),
                                             QStringLiteral("Line Spacing"));
    CHECK(lineSpacing);
    if (lineSpacing) {
        const auto keysPresent = []() {
            QSettings settings;
            settings.sync();
            return QStringList{ QString::number(settings.contains(QStringLiteral("document/lineSpacing"))),
                                QString::number(settings.contains(QStringLiteral("document/paragraphSpacing"))),
                                QString::number(settings.contains(QStringLiteral("document/listSpacing"))) }
                .join(u'|');
        };
        runWithDialog(lineSpacing->run, [](PreferencesDialog& dialog) {
            CHECK(currentTab(dialog) == QStringLiteral("Typography"));
            CHECK(dialog.focusWidget() == dialog.targetWidgets(Id::LineSpacing).first());
            CHECK(dialog.documentSpacing().isDefault());
            for (Id id : { Id::LineSpacing, Id::ParagraphSpacing, Id::ListSpacing }) {
                auto* combo = qobject_cast<QComboBox*>(dialog.targetWidgets(id).first());
                CHECK(combo && combo->currentText().endsWith(QStringLiteral("(Default)")));
            }
            dialog.setDocumentSpacing({ 125, 0, 10 });
            CHECK(dialog.documentSpacing() == DocumentSpacing::Settings({ 125, 0, 10 }));
        }, QDialogButtonBox::Cancel, &opened);
        CHECK(opened);
        CHECK(keysPresent() == QStringLiteral("0|0|0"));

        runWithDialog(lineSpacing->run, [](PreferencesDialog& dialog) {
            dialog.setDocumentSpacing({ 125, 0, 10 });
        }, QDialogButtonBox::Ok, &opened);
        CHECK(opened);
        {
            QSettings settings;
            settings.sync();
            CHECK(settings.value(QStringLiteral("document/lineSpacing")).toInt() == 125);
            CHECK(settings.contains(QStringLiteral("document/paragraphSpacing"))
                  && settings.value(QStringLiteral("document/paragraphSpacing")).toInt() == 0);
            CHECK(settings.value(QStringLiteral("document/listSpacing")).toInt() == 10);
        }

        runWithDialog(lineSpacing->run, [](PreferencesDialog& dialog) {
            CHECK(dialog.documentSpacing() == DocumentSpacing::Settings({ 125, 0, 10 }));
            // A value that is no longer a preset falls back to the default.
            dialog.setDocumentSpacing({ 155, 13, 5 });
            CHECK(dialog.documentSpacing().isDefault());
            dialog.setDocumentSpacing({ 200, 18, 60 });
            CHECK(dialog.documentSpacing() == DocumentSpacing::Settings({ 200, 18, 60 }));
            dialog.setDocumentSpacing({});
        }, QDialogButtonBox::Ok, &opened);
        CHECK(opened);
        CHECK(keysPresent() == QStringLiteral("0|0|0"));
    }

    const Command* spell = findCommand(*registry, QStringLiteral("Spell Check"),
                                       QStringLiteral("Open Settings"));
    CHECK(spell);
    if (spell) {
        runWithDialog(spell->run, [](PreferencesDialog& dialog) {
            CHECK(currentTab(dialog) == QStringLiteral("Editor"));
            QWidget* expected = firstAvailable(dialog, Id::SpellCheck);
            CHECK(expected && dialog.focusWidget() == expected);
        }, QDialogButtonBox::Cancel, &opened);
        CHECK(opened);
    }

    const Command* preferences = findCommand(*registry, QStringLiteral("File"),
                                             QStringLiteral("Preferences"));
    CHECK(preferences);
    if (preferences) {
        runWithDialog(preferences->run, [](PreferencesDialog& dialog) {
            CHECK(currentTab(dialog) == QStringLiteral("General"));
        }, QDialogButtonBox::Cancel, &opened);
        CHECK(opened);
    }

    CHECK(window.findChildren<PreferencesDialog*>().isEmpty());
}

} // namespace

int main(int argc, char* argv[])
{
    if (QDir(QStringLiteral(FASTMD_QT_PLATFORMS_DIR)).exists(QStringLiteral("qoffscreen.dll")))
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QStringLiteral(FASTMD_QT_PLATFORMS_DIR).toLocal8Bit());
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FastMDTests"));
    QCoreApplication::setApplicationName(QStringLiteral("PreferencesNavigationTest"));
    QTemporaryDir settings;
    CHECK(settings.isValid());
    if (!settings.isValid())
        return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());

    everyTargetResolves();
    registeredCommandsAreDistinct();
    revealFocusesTheControlWithoutChangingIt();
    disabledTargetsFallBackToTheirParentSetting();
    paletteOpensPreferencesOnTheControl();

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
