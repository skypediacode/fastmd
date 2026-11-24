#include "GitItemRoles.h"
#include "SourceControlPanel.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMouseEvent>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <cstdio>
#include <functional>

// Store crash dumps of 3.6.4 and 3.6.7 showed the panel's model-reset handler
// running while the panel was being destroyed: QWidget::~QWidget deletes the
// file list's viewport (and the action strips reparented into it) before the
// list's QTreeModel, whose final reset still reached the panel. This drives the
// real panel through that destruction order.

static bool g_ok = true;

static void expect(bool value, const char* what)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        g_ok = false;
    }
}

static bool waitFor(const std::function<bool()>& condition, int timeoutMs = 20000)
{
    for (int waited = 0; waited < timeoutMs; waited += 20) {
        if (condition())
            return true;
        QTest::qWait(20);
    }
    return condition();
}

static bool runGit(const QString& dir, const QStringList& args)
{
    QProcess git;
    git.setWorkingDirectory(dir);
    git.start(QStringLiteral("git"), args);
    return git.waitForFinished(30000) && git.exitStatus() == QProcess::NormalExit
        && git.exitCode() == 0;
}

static void writeFile(const QString& path)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write("changed\n");
}

static QTreeWidgetItem* fileRow(QTreeWidget* list, const QString& path)
{
    for (QTreeWidgetItemIterator it(list); *it; ++it) {
        if ((*it)->data(0, GitItemRoles::Path).toString() == path)
            return *it;
    }
    return nullptr;
}

static QList<GitRowActions*> viewportStrips(QTreeWidget* list)
{
    return list->viewport()->findChildren<GitRowActions*>(Qt::FindDirectChildrenOnly);
}

static int visibleStrips(QTreeWidget* list)
{
    int visible = 0;
    for (GitRowActions* strip : viewportStrips(list))
        visible += strip->isVisible() ? 1 : 0;
    return visible;
}

static void hover(QTreeWidget* list, QTreeWidgetItem* item)
{
    if (QTreeWidgetItem* parent = item->parent())
        parent->setExpanded(true);
    const QPoint pos = list->visualItemRect(item).center();
    QMouseEvent move(QEvent::MouseMove, pos, list->viewport()->mapToGlobal(pos),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &move);
}

// Selects a.md and hovers b.md, which reparents both strips into the list's
// viewport -- the state every crashing session was in.
static bool showBothStrips(QTreeWidget* list)
{
    QTreeWidgetItem* a = fileRow(list, QStringLiteral("a.md"));
    QTreeWidgetItem* b = fileRow(list, QStringLiteral("b.md"));
    if (!a || !b)
        return false;
    list->setCurrentItem(a);
    hover(list, b);
    return visibleStrips(list) == 2;
}

static SourceControlPanel* openPanel(const QString& repo, QTreeWidget** list)
{
    auto* panel = new SourceControlPanel;
    panel->resize(360, 640);
    panel->show();
    panel->setRepositoryFromWorkspace(repo);
    *list = panel->findChild<QTreeWidget*>(QStringLiteral("gitFileList"));
    expect(*list != nullptr, "the panel has its Changes list");
    if (*list) {
        QTreeWidget* l = *list;
        expect(waitFor([l] {
                   return fileRow(l, QStringLiteral("a.md")) && fileRow(l, QStringLiteral("b.md"));
               }),
               "a refresh lists the repository's changes");
    }
    return panel;
}

static void destroyPanel(SourceControlPanel* panel, QTreeWidget* list)
{
    QList<QPointer<GitRowActions>> strips;
    for (GitRowActions* strip : viewportStrips(list))
        strips << strip;
    expect(strips.size() == 2, "both strips live in the viewport at teardown");
    QAbstractItemModel* model = list->model();
    QObject* panelObject = panel;
    auto allGone = [&strips] {
        for (const QPointer<GitRowActions>& strip : strips) {
            if (strip)
                return false;
        }
        return true;
    };

    // The viewport (which emits destroyed() before deleting its children, the
    // strips) goes while the model still has its final reset ahead. The panel
    // must already be off the model by then. Disconnecting here also keeps a
    // regression a test failure rather than a crash.
    bool viewportDestroyed = false;
    bool panelStillConnected = false;
    QObject::connect(list->viewport(), &QObject::destroyed, [&] {
        viewportDestroyed = true;
        panelStillConnected = QObject::disconnect(model, nullptr, panelObject, nullptr);
    });
    bool resetAfterStripsGone = false;
    QObject::connect(model, &QAbstractItemModel::modelAboutToBeReset, [&] {
        resetAfterStripsGone = viewportDestroyed && allGone();
    });

    delete panel;

    expect(viewportDestroyed, "the list's viewport is destroyed with the panel");
    expect(resetAfterStripsGone,
           "the model's final reset comes after the viewport and strips are gone");
    expect(!panelStillConnected,
           "the panel is no longer connected to the model when its children die");
}

static void modelResetStillHidesStrips(const QString& repo)
{
    QTreeWidget* list = nullptr;
    SourceControlPanel* panel = openPanel(repo, &list);
    if (!list) {
        delete panel;
        return;
    }
    expect(showBothStrips(list), "selection and hover show their action strips");
    // QTreeWidget::clear() resets the model; the strips must not outlive the
    // rows they point at.
    list->clear();
    expect(visibleStrips(list) == 0, "a model reset hides both strips");

    panel->refresh();
    expect(waitFor([list] { return fileRow(list, QStringLiteral("a.md")) != nullptr; }),
           "a refresh after the reset repopulates the list");
    expect(showBothStrips(list), "the strips work again after a reset");
    destroyPanel(panel, list);
}

static void teardownLeavesDeadStripsAlone(const QString& repo)
{
    QTreeWidget* list = nullptr;
    SourceControlPanel* panel = openPanel(repo, &list);
    if (!list) {
        delete panel;
        return;
    }
    expect(showBothStrips(list), "both strips are in the viewport before teardown");
    destroyPanel(panel, list);
}

int main(int argc, char* argv[])
{
    if (QDir(QStringLiteral(FASTMD_QT_PLATFORMS_DIR)).exists(QStringLiteral("qoffscreen.dll")))
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QStringLiteral(FASTMD_QT_PLATFORMS_DIR).toLocal8Bit());
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FastMDTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SourceControlPanelTeardownTest"));
    QTemporaryDir settings;
    QTemporaryDir repo;
    if (!settings.isValid() || !repo.isValid()) {
        std::fprintf(stderr, "FAIL: temporary directories\n");
        return 1;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());

    if (!runGit(repo.path(), { QStringLiteral("init"), QStringLiteral("-q") })) {
        std::puts("SourceControlPanelTeardownTest: SKIP (git is not available)");
        return 0;
    }
    writeFile(repo.path() + QStringLiteral("/a.md"));
    writeFile(repo.path() + QStringLiteral("/b.md"));

    modelResetStillHidesStrips(repo.path());
    // Closing and reopening Source Control builds a fresh panel each time.
    teardownLeavesDeadStripsAlone(repo.path());
    teardownLeavesDeadStripsAlone(repo.path());

    if (g_ok)
        std::puts("SourceControlPanelTeardownTest: all checks passed");
    return g_ok ? 0 : 1;
}
