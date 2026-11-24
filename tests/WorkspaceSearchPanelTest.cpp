#include "ApplicationTheme.h"
#include "WorkspaceSearchPanel.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>

#include <cstdio>
#include <functional>

static bool g_ok = true;

static void expect(bool value, const char* what)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        g_ok = false;
    }
}

static bool waitFor(const std::function<bool()>& condition, int timeoutMs = 8000)
{
    for (int waited = 0; waited < timeoutMs; waited += 20) {
        if (condition())
            return true;
        QTest::qWait(20);
    }
    return condition();
}

static void writeBytes(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
}

static QByteArray readBytes(const QString& path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

static int matchCount(QStandardItemModel* model, const QString& fileName)
{
    for (int i = 0; i < model->rowCount(); ++i) {
        if (model->item(i)->text() == fileName)
            return model->item(i)->rowCount();
    }
    return -1;
}

int main(int argc, char** argv)
{
    // The offscreen platform is not deployed beside the test executables, so it
    // is loaded from the Qt kit itself; without it the test would need a desktop.
    if (QDir(QStringLiteral(FASTMD_QT_PLATFORMS_DIR)).exists(QStringLiteral("qoffscreen.dll"))) {
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QStringLiteral(FASTMD_QT_PLATFORMS_DIR).toLocal8Bit());
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    // The offscreen platform ships without a font database, which renders every
    // glyph as a box; snapshots are only useful with real text.
    if (!qEnvironmentVariableIsEmpty("FASTMD_PANEL_SNAPSHOT_DIR")
        && QDir(QStringLiteral("C:/Windows/Fonts")).exists())
        qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
    QApplication app(argc, argv);

    QTemporaryDir tmp;
    const QString root = QDir::cleanPath(tmp.path());
    const QByteArray bom("\xEF\xBB\xBF", 3);
    writeBytes(root + "/a.md", "needle one\nsecond needle\n");
    writeBytes(root + "/b.txt", "nothing here\n");
    writeBytes(root + "/sub/c.md", "Needle in a subfolder\n");
    writeBytes(root + "/d.txt", bom + "needle\r\nneedle x\r\n");
    writeBytes(root + "/e.md", "stale disk copy without a hit\n");
    writeBytes(root + "/.git/config", "needle in git\n");

    QPlainTextEdit editor;
    editor.setPlainText(QStringLiteral("open needle unsaved"));
    editor.document()->clearUndoRedoStacks();

    WorkspaceSearchPanel panel;
    panel.resize(360, 600);
    int editedHookCalls = 0;
    panel.setOpenEditorsProvider([&]() {
        return QList<WorkspaceSearchPanel::OpenEditor>{{root + "/e.md", &editor}};
    });
    panel.setEditedHook([&](QPlainTextEdit*) { ++editedHookCalls; });
    panel.setRootPath(root);

    const auto edits = panel.findChildren<QLineEdit*>(QStringLiteral("findOverlayEdit"));
    const auto options = panel.findChildren<QToolButton*>(QStringLiteral("findOverlayOption"));
    const auto actions = panel.findChildren<QToolButton*>(QStringLiteral("findOverlayAction"));
    auto* results = panel.findChild<QTreeView*>(QStringLiteral("workspaceSearchResults"));
    auto* status = panel.findChild<QLabel*>(QStringLiteral("workspaceSearchStatus"));
    expect(edits.size() == 2 && options.size() == 4 && actions.size() == 1 && results && status,
           "panel builds its controls");
    if (!g_ok)
        return 1;
    QLineEdit* queryEdit = edits[0];
    QLineEdit* replaceEdit = edits[1];
    QToolButton* matchCase = options[0];
    QToolButton* regexButton = options[2];
    auto* model = qobject_cast<QStandardItemModel*>(results->model());

    // --- plain search across files, .git excluded, open buffer wins ----------
    queryEdit->setText(QStringLiteral("needle"));
    expect(waitFor([&] { return model->rowCount() == 4 && status->text().contains("results"); }),
           "search finishes with four files");
    expect(matchCount(model, "a.md") == 2, "a.md has two matches");
    expect(matchCount(model, "c.md") == 1, "case-insensitive match in a subfolder");
    expect(matchCount(model, "d.txt") == 2, "BOM/CRLF file is searched");
    expect(matchCount(model, "e.md") == 1, "open buffer is searched, not the stale disk copy");
    expect(matchCount(model, "config") == -1, ".git is excluded");
    expect(status->text().startsWith("6 results in 4 files"), "summary counts");

    // Optional visual check: FASTMD_PANEL_SNAPSHOT_DIR=<dir> renders the panel in
    // both themes and at a wide and a narrow sidebar width, with the real
    // application stylesheet, so alignment and colors can be inspected without
    // launching the app.
    if (const QString dir = qEnvironmentVariable("FASTMD_PANEL_SNAPSHOT_DIR"); !dir.isEmpty()) {
        QDir().mkpath(dir);
        panel.setReplaceExpanded(true);
        replaceEdit->setText(QStringLiteral("replacement"));
        panel.show();
        for (const bool dark : {false, true}) {
            app.setPalette(ApplicationTheme::palette(dark));
            app.setStyleSheet(ApplicationTheme::styleSheet(dark));
            panel.setDarkMode(dark);
            for (const int width : {360, 230}) {
                panel.resize(width, 420);
                QTest::qWait(150);
                results->setCurrentIndex(model->item(0)->child(1)->index());
                panel.grab().save(QStringLiteral("%1/panel-%2-%3.png")
                                      .arg(dir, dark ? "dark" : "light").arg(width));
            }
        }
        app.setStyleSheet(QString());
        app.setPalette(QPalette());
        panel.setDarkMode(false);
        panel.setReplaceExpanded(false);
        replaceEdit->clear();
        results->clearSelection();
        panel.hide();
    }

    // --- Match Case narrows the results ----------------------------------------
    matchCase->setChecked(true);
    expect(waitFor([&] { return model->rowCount() == 3 && status->text().contains("5 results"); }),
           "match case drops the capitalised hit");
    matchCase->setChecked(false);
    expect(waitFor([&] { return model->rowCount() == 4; }), "match case off restores it");

    // --- invalid regex keeps the previous results --------------------------------
    regexButton->setChecked(true);
    queryEdit->setText(QStringLiteral("(["));
    expect(waitFor([&] { return queryEdit->parentWidget()->property("invalid").toBool(); }),
           "invalid regex marks the field");
    expect(model->rowCount() == 4, "invalid regex does not wipe results");
    expect(status->text().contains("Invalid regular expression"), "invalid regex is explained");
    regexButton->setChecked(false);
    queryEdit->setText(QStringLiteral("needle"));
    expect(waitFor([&] { return model->rowCount() == 4 && !queryEdit->parentWidget()->property("invalid").toBool(); }),
           "a valid query recovers");

    // --- replace across files, with confirmation, into open + closed files ---------
    panel.setReplaceExpanded(true);
    replaceEdit->setText(QStringLiteral("PIN"));
    QTimer confirm;
    confirm.setInterval(50);
    QObject::connect(&confirm, &QTimer::timeout, [&]() {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* box = qobject_cast<QMessageBox*>(w); box && box->isVisible()) {
                box->button(QMessageBox::Yes)->click();
                confirm.stop();
                return;
            }
        }
    });
    confirm.start();
    actions[0]->click();
    expect(waitFor([&] { return model->rowCount() == 0 && status->text().contains("Replaced"); }),
           "replace all completes and the rerun finds nothing left");
    expect(!confirm.isActive(), "multi-file Replace All asked for confirmation");
    expect(status->text().contains("6 occurrences") || status->text().contains("Replaced 6"),
           "replace all reports six occurrences");

    expect(editor.toPlainText() == QStringLiteral("open PIN unsaved"), "open buffer edited live");
    expect(editedHookCalls == 1, "owner was told about the edited document");
    expect(readBytes(root + "/e.md") == "stale disk copy without a hit\n",
           "open document was not written to disk");
    editor.undo();
    expect(editor.toPlainText() == QStringLiteral("open needle unsaved"),
           "one Ctrl+Z reverts the whole file");

    expect(readBytes(root + "/a.md") == "PIN one\nsecond PIN\n", "closed file replaced");
    expect(readBytes(root + "/sub/c.md") == "PIN in a subfolder\n", "case-insensitive replacement");
    expect(readBytes(root + "/d.txt") == bom + "PIN\r\nPIN x\r\n", "BOM and CRLF preserved");
    expect(readBytes(root + "/.git/config") == "needle in git\n", ".git untouched");

    // --- stale results are revalidated, never trusted ---------------------------------
    queryEdit->setText(QStringLiteral("PIN"));
    expect(waitFor([&] { return model->rowCount() == 3 && status->text().contains("5 results"); }),
           "second search finds the replacements");
    {
        int opened = 0;
        QObject::connect(&panel, &WorkspaceSearchPanel::openMatchRequested,
                         [&](const QString&, int, int, int) { ++opened; });
        QStandardItem* fileItem = nullptr;
        for (int i = 0; i < model->rowCount(); ++i) {
            if (model->item(i)->text() == "a.md")
                fileItem = model->item(i);
        }
        if (!fileItem || fileItem->rowCount() != 2) {
            for (int i = 0; i < model->rowCount(); ++i)
                std::fprintf(stderr, "  row %d: %s (%d) | status: %s\n", i,
                             qPrintable(model->item(i)->text()), int(model->item(i)->rowCount()),
                             qPrintable(status->text()));
        }
        expect(fileItem && fileItem->rowCount() == 2, "a.md lists both replacements");
        if (fileItem && fileItem->rowCount() == 2) {
            // The first hit disappears on disk after the search ran.
            writeBytes(root + "/a.md", "xx one\nsecond PIN\n");
            results->setCurrentIndex(fileItem->child(0)->index());
            results->setFocus();
            QTest::keyClick(results, Qt::Key_Return);
            expect(opened == 0, "a stale result is not opened");
            expect(waitFor([&] { return matchCount(model, "a.md") == 1; }),
                   "the stale file is refreshed instead");
            expect(status->text().contains("out of date"), "the refresh is explained");

            // The surviving hit is still valid and is opened.
            QStandardItem* refreshed = nullptr;
            for (int i = 0; i < model->rowCount(); ++i) {
                if (model->item(i)->text() == "a.md")
                    refreshed = model->item(i);
            }
            if (refreshed && refreshed->rowCount() == 1) {
                results->setCurrentIndex(refreshed->child(0)->index());
                QTest::keyClick(results, Qt::Key_Return);
                expect(opened == 1, "a valid result is opened");

                // Replace one, from the keyboard, edits only that occurrence.
                replaceEdit->setText(QStringLiteral("ZED"));
                results->setCurrentIndex(refreshed->child(0)->index());
                QTest::keyClick(results, Qt::Key_1, Qt::ControlModifier | Qt::ShiftModifier);
                expect(readBytes(root + "/a.md") == "xx one\nsecond ZED\n",
                       "replace one rewrites just that match on disk");
                expect(waitFor([&] { return matchCount(model, "a.md") == -1; }),
                       "the replaced result leaves the list");
            }
        }
    }

    // --- a changed root clears the results but keeps the query ---------------------
    queryEdit->setText(QStringLiteral("ZED"));
    expect(waitFor([&] { return model->rowCount() == 1; }), "third search finds the last edit");
    QTemporaryDir other;
    panel.setRootPath(QDir::cleanPath(other.path()));
    expect(model->rowCount() == 0, "results are cleared when the workspace changes");
    expect(queryEdit->text() == QStringLiteral("ZED"), "query survives a workspace change");

    if (g_ok)
        std::puts("WorkspaceSearchPanelTest: all checks passed");
    return g_ok ? 0 : 1;
}
