// Real Qt layout/controller integration, with isolated INI preferences and no
// MainWindow, editor, terminal, or WebView2 startup. Controller recreation is
// the restart boundary; the exact state written by shutdown is read back.
#include "WorkspacePanelController.h"
#include "ShellWarmUp.h"
#include "ThemedSplitter.h"
#include "WorkspaceSearchPanel.h"

#include <QAbstractFileIconProvider>
#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QSettings>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTreeView>
#include <QTest>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <cstdio>
#include <memory>

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

void settle()
{
    // Include queued Show/Resize/layout callbacks and their follow-up layouts.
    for (int i = 0; i < 8; ++i)
        QApplication::processEvents(QEventLoop::AllEvents);
}

// QIcon::cacheKey() identifies one icon object, so two icons built separately
// for the same glyph never share it. Compare the pixels the button shows.
QImage paintedIcon(const QToolButton* button)
{
    return button->icon().pixmap(QSize(18, 18)).toImage();
}

struct Window {
    QWidget root;
    QSplitter* reference;
    WorkspacePanelController controller;

    explicit Window(const QByteArray& saved = {}, bool expanded = true)
        : reference(new QSplitter(Qt::Vertical, &root)),
          controller(&root, [] { return QString(); }, [] { return false; })
    {
        root.resize(900, 900);
        auto* layout = new QHBoxLayout(&root);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(controller.panelWidget());
        reference->addWidget(new QWidget);
        reference->addWidget(new QWidget);
        reference->setSizes({600, 300});
        layout->addWidget(reference);
        controller.panelWidget()->setFixedWidth(350);
        controller.setOutlineReferenceWidget(reference);
        controller.setOutlineSplitterState(saved);
        controller.setOutlineExpanded(expanded, false);
        controller.setVisibleDeferringBuild(false);
        root.show();
        settle();
    }

    ~Window() { root.hide(); settle(); }

    void workspace()
    {
        controller.setVisible(true, false);
        controller.setPanelMode(WorkspacePanelController::PanelMode::Workspace, false);
        settle();
    }
    void git()
    {
        controller.setVisible(true, false);
        controller.setPanelMode(WorkspacePanelController::PanelMode::SourceControl, false);
        settle();
    }
    ThemedSplitter* splitter()
    {
        return static_cast<ThemedSplitter*>(
            root.findChild<QSplitter*>(QStringLiteral("workspaceOutlineSplitter")));
    }
    double outlineRatio()
    {
        const auto sizes = splitter()->sizes();
        return double(sizes[1]) / (sizes[0] + sizes[1]);
    }
    void drag(double outline)
    {
        auto* split = splitter();
        const int available = split->height() - split->handleWidth();
        // Send a real handle drag; this must exercise splitterMoved capture.
        auto* handle = split->handle(1);
        const QPoint start(handle->width() / 2, handle->height() / 2);
        const int target = qRound(available * (1.0 - outline));
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(handle, start + QPoint(0, target - split->sizes()[0]));
        QTest::mouseRelease(handle, Qt::LeftButton);
        settle();
        CHECK(std::abs(outlineRatio() - outline) < 0.02);
    }
    QByteArray shutdown()
    {
        QSettings settings;
        const auto state = controller.outlineSplitterState();
        if (!state.isEmpty())
            settings.setValue(QStringLiteral("workspace/outlineSplitterState"), state);
        settings.sync();
        return state;
    }
};

QByteArray readState()
{
    return QSettings().value(QStringLiteral("workspace/outlineSplitterState")).toByteArray();
}

void sequence(const char* name, bool hide, bool gitFirst, double ratio)
{
    std::printf("%s, Outline %.0f%%\n", name, ratio * 100);
    QSettings().clear();
    QByteArray saved;
    {
        Window window;
        window.workspace();
        window.drag(ratio);
        saved = window.controller.outlineSplitterState();
        CHECK(!saved.isEmpty());
        if (hide) {
            window.controller.setVisible(false, false);
            settle();
            // Reproduce invalid hidden-page geometry before shutdown.
            window.splitter()->resize(0, 0);
            settle();
            CHECK(window.controller.outlineSplitterState() == saved);
        }
        CHECK(window.shutdown() == saved);
    }
    {
        Window window(readState());
        CHECK(window.controller.outlineSplitterState() == saved);
        if (gitFirst) {
            window.git();
            CHECK(!window.splitter()->isVisible());
            CHECK(window.controller.outlineSplitterState() == saved);
            window.root.resize(1000, 1050);
            settle();
            CHECK(window.controller.outlineSplitterState() == saved);
            window.root.resize(900, 900);
            settle();
        }
        window.workspace();
        CHECK(std::abs(window.outlineRatio() - ratio) < 0.02);
        // D: repeated shared-stack switches keep the established split.
        for (int i = 0; i < 5; ++i) {
            window.git();
            window.workspace();
            CHECK(std::abs(window.outlineRatio() - ratio) < 0.02);
        }
        const auto referenceRatio = [&window] {
            return double(window.splitter()->handle(1)->mapToGlobal(QPoint()).y()
                          - window.reference->mapToGlobal(QPoint()).y())
                / (window.reference->height() - window.reference->handleWidth());
        };
        const double beforeResize = referenceRatio();
        window.root.resize(900, 1100);
        settle();
        CHECK(std::abs(referenceRatio() - beforeResize) < 0.01);
        window.root.resize(900, 900);
        settle();
        CHECK(std::abs(window.outlineRatio() - ratio) < 0.02);
        // Collapsing Outline saves its expanded split, including at shutdown.
        saved = window.controller.outlineSplitterState();
        window.controller.setOutlineExpanded(false, false);
        settle();
        CHECK(window.shutdown() == saved);
        window.controller.setOutlineExpanded(true, false);
        settle();
        CHECK(std::abs(window.outlineRatio() - ratio) < 0.02);
    }
    {
        Window window(readState(), false);
        window.git();
        window.workspace();
        CHECK(window.controller.outlineSplitterState() == readState());
        window.controller.setOutlineExpanded(true, false);
        settle();
        CHECK(std::abs(window.outlineRatio() - ratio) < 0.02);
    }
    {
        // Persisted Source Control mode can also keep the lazy Workspace body
        // hidden on its first materialize(), without an explicit F7 action.
        QSettings().setValue(QStringLiteral("workspace/panelMode"), 1);
        Window window(readState());
        window.controller.setVisible(true, false);
        settle();
        CHECK(window.controller.panelMode() == WorkspacePanelController::PanelMode::SourceControl);
        CHECK(window.controller.outlineSplitterState() == readState());
        window.workspace();
        CHECK(std::abs(window.outlineRatio() - ratio) < 0.02);
    }
}

void defaults()
{
    std::printf("E: fresh/invalid saved state\n");
    QSettings().clear();
    QByteArray valid;
    {
        Window window;
        window.git();
        CHECK(window.controller.outlineSplitterState().isEmpty());
        window.workspace();
        CHECK(std::abs(window.outlineRatio() - 1.0 / 3.0) < 0.01);
        valid = window.shutdown();
    }
    QList<QByteArray> invalid = { QByteArray("garbage"), valid.left(12), valid.left(valid.size() - 1) };
    auto zero = valid;
    // Version-1 QSplitter state: marker, version, count, first size, second.
    for (int i = 16; i < 20; ++i)
        zero[i] = 0;
    invalid.append(zero);
    for (const auto& state : invalid) {
        Window window(state);
        CHECK(window.controller.outlineSplitterState().isEmpty());
        window.workspace();
        CHECK(std::abs(window.outlineRatio() - 1.0 / 3.0) < 0.01);
    }
}

void modeToggleBehavior()
{
    std::printf("F: Workspace mode toggle behavior\n");
    QSettings().clear();
    Window window;
    window.controller.setVisible(true, false);
    auto* searchButton = window.root.findChild<QToolButton*>(
        QStringLiteral("workspaceSearchModeButton"));
    auto* sourceControlButton = window.root.findChild<QToolButton*>(
        QStringLiteral("workspaceModeToggle"));
    CHECK(searchButton != nullptr);
    CHECK(sourceControlButton != nullptr);
    if (!searchButton || !sourceControlButton)
        return;

    const auto workspaceMode = WorkspacePanelController::PanelMode::Workspace;
    const auto searchMode = WorkspacePanelController::PanelMode::Search;
    const auto sourceControlMode = WorkspacePanelController::PanelMode::SourceControl;
    CHECK(window.controller.panelMode() == workspaceMode);
    CHECK(searchButton->toolTip() == QStringLiteral("Search in Files (Ctrl+Shift+F)"));
    QTest::mouseClick(searchButton, Qt::LeftButton);
    settle();
    CHECK(window.controller.panelMode() == searchMode);
    CHECK(searchButton->toolTip() == QStringLiteral("Workspace"));
    window.controller.setPanelMode(sourceControlMode, false);
    const QImage workspaceIcon = paintedIcon(sourceControlButton);
    CHECK(!workspaceIcon.isNull());
    window.controller.setPanelMode(searchMode, false);
    CHECK(paintedIcon(searchButton) == workspaceIcon);
    QTest::mouseClick(searchButton, Qt::LeftButton);
    settle();
    CHECK(window.controller.panelMode() == workspaceMode);
    CHECK(searchButton->toolTip() == QStringLiteral("Search in Files (Ctrl+Shift+F)"));

    // Direct mode changes keep each toggle pointed at its next action.
    window.controller.setPanelMode(searchMode, false);
    CHECK(searchButton->toolTip() == QStringLiteral("Workspace"));
    CHECK(paintedIcon(searchButton) == workspaceIcon);
    window.controller.setPanelMode(sourceControlMode, false);
    CHECK(searchButton->toolTip() == QStringLiteral("Search in Files (Ctrl+Shift+F)"));
    CHECK(sourceControlButton->toolTip() == QStringLiteral("Workspace"));
    window.controller.setPanelMode(workspaceMode, false);
    CHECK(sourceControlButton->toolTip() == QStringLiteral("Source Control"));
    CHECK(searchButton->toolTip() == QStringLiteral("Search in Files (Ctrl+Shift+F)"));
}

void searchSeeding()
{
    std::printf("G: Search in Files seeds its query from the editor selection\n");
    QSettings().clear();
    Window window;
    window.controller.setVisible(true, false);
    QString selection;
    window.controller.setSearchSeedProvider([&selection] { return selection; });

    // Opening from Workspace mode seeds the query and selects it for typing.
    selection = QStringLiteral("needle");
    window.controller.openSearch(false);
    settle();
    CHECK(window.controller.panelMode() == WorkspacePanelController::PanelMode::Search);
    auto* search = window.controller.searchPanel();
    CHECK(search != nullptr);
    if (!search)
        return;
    auto* queryEdit = search->findChild<QLineEdit*>(QStringLiteral("findOverlayEdit"));
    CHECK(queryEdit != nullptr);
    if (!queryEdit)
        return;
    CHECK(search->queryText() == QStringLiteral("needle"));
    CHECK(queryEdit->selectedText() == QStringLiteral("needle"));

    // Reopening while Search is already showing replaces the query again.
    selection = QStringLiteral("second");
    window.controller.openSearch(false);
    settle();
    CHECK(search->queryText() == QStringLiteral("second"));
    CHECK(queryEdit->selectedText() == QStringLiteral("second"));

    // No usable selection (none, or multi-line which seeds as empty) keeps the query.
    selection.clear();
    window.controller.openSearch(false);
    settle();
    CHECK(search->queryText() == QStringLiteral("second"));

    // Callers that must not overwrite the query opt out of seeding.
    selection = QStringLiteral("ignored");
    window.controller.openSearch(false, false);
    settle();
    CHECK(search->queryText() == QStringLiteral("second"));

    // The Search toggle in the header is an entry point too.
    window.controller.setPanelMode(WorkspacePanelController::PanelMode::Workspace, false);
    settle();
    auto* searchButton = window.root.findChild<QToolButton*>(
        QStringLiteral("workspaceSearchModeButton"));
    CHECK(searchButton != nullptr);
    if (!searchButton)
        return;
    selection = QStringLiteral("clicked");
    QTest::mouseClick(searchButton, Qt::LeftButton);
    settle();
    CHECK(window.controller.panelMode() == WorkspacePanelController::PanelMode::Search);
    CHECK(search->queryText() == QStringLiteral("clicked"));
    CHECK(queryEdit->selectedText() == QStringLiteral("clicked"));

    // Without a provider, opening keeps whatever query is present.
    window.controller.setSearchSeedProvider({});
    selection = QStringLiteral("unused");
    window.controller.openSearch(false);
    settle();
    CHECK(search->queryText() == QStringLiteral("clicked"));
}

struct StartupPanel {
    QWidget window;
    WorkspacePanelController controller;

    explicit StartupPanel(const QString& root, const QString& currentFile = {})
        : controller(&window, [currentFile] { return currentFile; }, [] { return false; })
    {
        auto* layout = new QHBoxLayout(&window);
        layout->addWidget(controller.panelWidget());
        window.resize(400, 600);
        controller.setVisibleDeferringBuild(true);
        controller.setRoot(root, false);
        controller.ensureUi();
        window.show();
    }

    QFileSystemModel* model() { return controller.findChild<QFileSystemModel*>(); }
    QModelIndex entry(const QString& name, int column = 0)
    {
        QTreeView* tree = controller.treeView();
        for (int row = 0; tree->model() && row < tree->model()->rowCount(tree->rootIndex()); ++row) {
            const QModelIndex index = tree->model()->index(row, 0, tree->rootIndex());
            if (index.data().toString() == name)
                return index.siblingAtColumn(column);
        }
        return {};
    }
};

// The startup build waits for ShellWarmUp without blocking, and everything that
// can overtake it (a root change, hiding, showing, closing) ends in the same tree.
void startupTreeBuild()
{
    std::printf("Workspace startup tree build\n");
    QSettings().clear();
    QTemporaryDir files;
    QTemporaryDir other;
    QFile markdown(files.filePath(QStringLiteral("first.md")));
    CHECK(markdown.open(QIODevice::WriteOnly));
    markdown.close();
    CHECK(QDir(files.path()).mkdir(QStringLiteral("folder")));
    QFile nested(files.filePath(QStringLiteral("folder/nested.md")));
    CHECK(nested.open(QIODevice::WriteOnly));
    nested.close();
    // Before first.md by name, after it by file type.
    QFile text(files.filePath(QStringLiteral("a.txt")));
    CHECK(text.open(QIODevice::WriteOnly));
    text.close();
    QTemporaryDir created;
    QFile open(created.filePath(QStringLiteral("open.md")));
    CHECK(open.open(QIODevice::WriteOnly));
    open.close();

    StartupPanel plain(files.path());
    StartupPanel moved(files.path());
    StartupPanel hidden(files.path());
    StartupPanel shown(files.path());
    StartupPanel early(created.path(), open.fileName());
    auto closed = std::make_unique<StartupPanel>(files.path());
    for (StartupPanel* panel : {&plain, &moved, &hidden, &shown, &early, closed.get()})
        panel->controller.materializeAfterFirstPaint();

    // A fast shell can be warm already, and then there is nothing to wait for.
    const bool waiting = !ShellWarmUp::finished();
    if (waiting) {
        CHECK(!plain.model() && !moved.model() && !hidden.model() && !shown.model());
        plain.controller.updateRoot();
        CHECK(!plain.model());
        shown.controller.setVisible(false, false);
        shown.controller.setVisible(true, false);
        CHECK(shown.model());

        // The tree is usable, if empty, while it has no model.
        QTreeView* empty = early.controller.treeView();
        empty->setFocus();
        for (Qt::Key key : {Qt::Key_Down, Qt::Key_End, Qt::Key_Return, Qt::Key_Space, Qt::Key_F2, Qt::Key_A})
            QTest::keyClick(empty, key);
        QTest::keyClick(empty, Qt::Key_A, Qt::ControlModifier);
        QTest::mouseClick(empty->viewport(), Qt::LeftButton, {}, QPoint(20, 20));
        QTest::mouseDClick(empty->viewport(), Qt::LeftButton, {}, QPoint(20, 20));
        empty->expandAll();
        empty->collapseAll();
        early.controller.setSort(0, Qt::DescendingOrder, false);
        early.controller.setOptionalColumnsVisible(true, true, true, false);
        early.controller.setHeaderHeight(31);
        CHECK(!early.model());
    } else {
        std::printf("shell already warm: deferral not exercised\n");
    }
    moved.controller.setRoot(other.path(), false);
    hidden.controller.setVisible(false, false);
    closed.reset();

    // A file created before the tree exists still opens its inline rename, and
    // the pending build must not run afterwards and take it away again.
    auto* newFile = early.window.findChild<QToolButton*>(QStringLiteral("workspaceSplitButton"));
    CHECK(newFile != nullptr);
    newFile->click();
    CHECK(QDir(created.path()).entryList(QDir::Files).size() == 2);
    const auto renameEditor = [&] {
        return early.controller.treeView()->findChild<QLineEdit*>(QStringLiteral("workspaceRenameEditor"));
    };
    CHECK(QTest::qWaitFor([&] { return renameEditor() != nullptr; }, 3000));
    const QPointer<QLineEdit> editor = renameEditor();
    if (waiting) {
        QTest::qWait(250); // past the longest wait for the shell
        CHECK(editor && editor == renameEditor());
        for (int column = 1; column <= 3; ++column)
            CHECK(!early.controller.treeView()->isColumnHidden(column));
    }

    CHECK(QTest::qWaitFor([&] { return plain.entry(QStringLiteral("first.md")).isValid(); }, 5000));
    CHECK(plain.entry(QStringLiteral("..")).isValid());
    CHECK(plain.entry(QStringLiteral("folder")).isValid());
    CHECK(QTest::qWaitFor([&] { return shown.entry(QStringLiteral("first.md")).isValid(); }, 5000));
    CHECK(QTest::qWaitFor([&] { return moved.model() != nullptr; }, 5000));
    CHECK(QFileInfo(moved.model()->rootPath()) == QFileInfo(other.path()));
    if (waiting)
        CHECK(!hidden.model());
    hidden.controller.setVisible(true, false);
    CHECK(hidden.model());
    CHECK(QTest::qWaitFor([&] { return hidden.entry(QStringLiteral("first.md")).isValid(); }, 5000));

    // With the shell warm the startup build is the ordinary one.
    CHECK(QTest::qWaitFor([] { return ShellWarmUp::finished(); }, 10000));
    StartupPanel later(files.path());
    later.controller.materializeAfterFirstPaint();
    CHECK(later.model());

    // The model only adopts a file's gathered information (icon, children) when it
    // differs from what it was created with, and the type string is what differs.
    // So the provider's type must reach the model, and be the same for every file.
    const QFileSystemModel* model = plain.model();
    const QAbstractFileIconProvider* icons = model->iconProvider();
    const QString type = icons->type(QFileInfo(markdown.fileName()));
    CHECK(!type.isEmpty());
    CHECK(icons->type(QFileInfo(files.path())) == type);
    CHECK(QTest::qWaitFor([&] { return model->type(model->index(markdown.fileName())) == type; }, 5000));
    CHECK(icons->options().testFlag(QAbstractFileIconProvider::DontUseCustomDirectoryIcons));
    // Expanding shows the children of a folder the model has not read before.
    plain.controller.treeView()->expand(plain.entry(QStringLiteral("folder")));
    const auto nestedRow = [&] {
        const QModelIndex folder = plain.entry(QStringLiteral("folder"));
        const QAbstractItemModel* rows = plain.controller.treeView()->model();
        return rows->index(0, 0, folder).data().toString() == QStringLiteral("nested.md");
    };
    CHECK(QTest::qWaitFor(nestedRow, 5000));
    CHECK(plain.entry(QStringLiteral("first.md"), 2).data().toString() == QStringLiteral("Markdown File"));
    CHECK(plain.entry(QStringLiteral("folder"), 2).data().toString() == QStringLiteral("Folder"));
    CHECK(plain.entry(QStringLiteral("a.txt"), 2).data().toString() == QStringLiteral("Text File"));
    // Sorting by type uses the column's own text, not the model's constant.
    const auto before = [&](const char* first, const char* second) {
        return plain.entry(QLatin1String(first)).row() < plain.entry(QLatin1String(second)).row();
    };
    plain.controller.setSort(0, Qt::AscendingOrder, false);
    CHECK(before("folder", "a.txt") && before("a.txt", "first.md"));
    plain.controller.setSort(2, Qt::AscendingOrder, false);
    CHECK(before("folder", "first.md") && before("first.md", "a.txt"));
    plain.controller.setSort(2, Qt::DescendingOrder, false);
    CHECK(before("a.txt", "first.md"));
    // Rows show the shell's icon for the file, not the model's generic one. Only
    // the Windows platform has shell icons (FASTMD_TEST_NATIVE=1).
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        const auto pixels = [](const QIcon& icon) { return icon.pixmap(16, 16).toImage(); };
        for (const char* name : {"folder", "first.md", "a.txt"}) {
            const QImage row = pixels(plain.entry(QLatin1String(name)).data(Qt::DecorationRole).value<QIcon>());
            CHECK(!row.isNull());
            CHECK(row == pixels(icons->icon(QFileInfo(files.filePath(QLatin1String(name))))));
        }
        CHECK(!pixels(plain.entry(QStringLiteral("..")).data(Qt::DecorationRole).value<QIcon>()).isNull());
    }
    settle();
}
}

// Keep the file-search cases in the real controller integration target. Friend
// access only supplies deterministic worker completions (including stale ones);
// all keyboard input goes through QApplication and the production event filter.
class WorkspaceFileSearchNavigationTest {
public:
    static void run()
    {
        std::printf("Workspace file-search keyboard navigation\n");
        QSettings().clear();
        QTemporaryDir files;
        QTemporaryDir otherRoot;
        CHECK(files.isValid() && otherRoot.isValid());
        const QString first = files.filePath(QStringLiteral("first.md"));
        const QString last = files.filePath(QStringLiteral("last.md"));
        for (const auto& path : {first, last}) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                CHECK(false);
                return;
            }
            CHECK(file.write("test\n") == 5);
        }
        const QString folder = files.filePath(QStringLiteral("folder"));
        CHECK(QDir().mkpath(folder));

        QWidget window;
        WorkspacePanelController controller(&window, [] { return QString(); }, [] { return false; });
        auto* layout = new QHBoxLayout(&window);
        layout->addWidget(controller.panelWidget());
        window.resize(400, 600);
        controller.setRoot(files.path(), false);
        controller.setVisible(true, false);
        window.show();
        window.activateWindow();
        settle();
        auto* input = controller.m_searchBox;
        auto* list = controller.m_searchListView;
        auto* model = controller.m_searchModel;
        auto* selection = list->selectionModel();
        QSignalSpy opened(&controller, &WorkspacePanelController::fileActivated);
        controller.focusSearch();
        settle();
        CHECK(input->hasFocus());

        const QList<WorkspacePanelController::SearchHit> hits = {
            {QStringLiteral("folder"), folder, true},
            {QStringLiteral("first.md"), first, false},
            {QStringLiteral("disabled.md"), first, false},
            {QStringLiteral("status"), {}, false},
            {QStringLiteral("last.md"), last, false}
        };
        const auto complete = [&] {
            controller.m_searchDebounce->stop();
            controller.applySearchResults(controller.m_searchGeneration, hits);
            model->item(2)->setEnabled(false);
        };
        input->setText(QStringLiteral("test"));
        complete();
        const auto noSelection = [&] {
            CHECK(!selection->hasSelection());
            CHECK(!selection->currentIndex().isValid());
        };
        const auto selected = [&](int row) {
            CHECK(selection->currentIndex().row() == row);
            CHECK(selection->selectedIndexes().size() == 1);
            CHECK(selection->isSelected(model->index(row, 0)));
            CHECK(input->hasFocus());
        };
        noSelection();
        QTest::keyClick(input, Qt::Key_Down);
        selected(1);
        QTest::keyClick(input, Qt::Key_Down);
        selected(4);
        QTest::keyClick(input, Qt::Key_Down);
        selected(4);
        QTest::keyClick(input, Qt::Key_Up);
        selected(1);
        QTest::keyClick(input, Qt::Key_Up);
        selected(1);
        CHECK(opened.isEmpty());
        // Auto-repeat moves one eligible row per key event.
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier, {}, true);
        QApplication::sendEvent(input, &repeat);
        selected(4);

        QTest::keyClick(input, Qt::Key_Escape);
        noSelection();
        CHECK(input->text() == QStringLiteral("test"));
        CHECK(input->hasFocus());
        QTest::keyClick(input, Qt::Key_Up);
        selected(4);
        QTest::keyClick(input, Qt::Key_Return);
        CHECK(opened.size() == 1 && opened.last().first().toString() == last);
        QTest::keyClick(input, Qt::Key_Enter, Qt::KeypadModifier);
        CHECK(opened.size() == 2 && opened.last().first().toString() == last);
        QTest::keyClick(input, Qt::Key_Escape);
        QTest::keyClick(input, Qt::Key_Return);
        CHECK(opened.size() == 3 && opened.last().first().toString() == first);
        noSelection();
        QTest::keyClick(input, Qt::Key_Escape);
        CHECK(input->text().isEmpty());
        noSelection();

        input->setText(QStringLiteral("test"));
        complete();
        QTest::keyClick(input, Qt::Key_Down);
        input->setCursorPosition(2);
        QTest::keyClick(input, Qt::Key_Home);
        CHECK(input->cursorPosition() == 0);
        QTest::keyClick(input, Qt::Key_End, Qt::ShiftModifier);
        CHECK(input->selectedText() == QStringLiteral("test"));
        QTest::keyClick(input, Qt::Key_Left);
        CHECK(!input->hasSelectedText());
        input->setCursorPosition(2);
        QTest::keyClick(input, Qt::Key_Left);
        CHECK(input->cursorPosition() == 1);
        QTest::keyClick(input, Qt::Key_Right);
        CHECK(input->cursorPosition() == 2);
        QTest::keyClick(input, Qt::Key_Up, Qt::ControlModifier);
        QTest::keyClick(input, Qt::Key_Down, Qt::ShiftModifier);
        QTest::keyClick(input, Qt::Key_Return, Qt::ControlModifier);
        selected(1);
        CHECK(opened.size() == 3);
        QTest::keyClick(input, Qt::Key_A, Qt::ControlModifier);
        CHECK(input->selectedText() == QStringLiteral("test"));
        QTest::keyClick(input, Qt::Key_C, Qt::ControlModifier);
        CHECK(!input->text().isEmpty());

        // Composition gets first use of arrows, Enter, and Escape.
        input->setCursorPosition(input->text().size());
        QInputMethodEvent preedit(QStringLiteral("compose"), {});
        QApplication::sendEvent(input, &preedit);
        CHECK(controller.m_searchInputComposing);
        // Native IMEs consume their keys before the edit processes them. Check
        // filter passthrough directly; sending synthetic keys to QLineEdit
        // instead would exercise its normal editing, without a native IME.
        for (const auto type : {QEvent::ShortcutOverride, QEvent::KeyPress}) {
            for (const auto key : {Qt::Key_Down, Qt::Key_Return, Qt::Key_Escape}) {
                QKeyEvent event(type, key, Qt::NoModifier);
                CHECK(!controller.eventFilter(input, &event));
            }
        }
        CHECK(opened.size() == 3);
        CHECK(input->text() == QStringLiteral("test"));
        selected(1);
        QInputMethodEvent endComposition;
        QApplication::sendEvent(input, &endComposition);

        // A completion from the old query must be rejected immediately,
        // before the next query's debounce timer has had a chance to fire.
        const quint64 oldGeneration = controller.m_searchGeneration;
        input->setText(QStringLiteral("new query"));
        noSelection();
        CHECK(controller.m_searchDebounce->isActive());
        controller.applySearchResults(oldGeneration, hits);
        CHECK(!controller.m_searchResultsCurrent);
        QTest::keyClick(input, Qt::Key_Down);
        QTest::keyClick(input, Qt::Key_Return);
        noSelection();
        CHECK(opened.size() == 3);
        list->activated(model->index(1, 0));
        CHECK(opened.size() == 3);
        complete();
        noSelection();
        QTest::keyClick(input, Qt::Key_Up);
        selected(4);
        complete(); // Even a same-query model replacement clears navigation.
        noSelection();
        model->clear();
        QTest::keyClick(input, Qt::Key_Down);
        QTest::keyClick(input, Qt::Key_Up);
        QTest::keyClick(input, Qt::Key_Return);
        noSelection();
        CHECK(opened.size() == 3);

        // Directory/status/disabled-only results cannot activate via input.
        complete();
        model->item(1)->setEnabled(false);
        model->item(4)->setEnabled(false);
        QTest::keyClick(input, Qt::Key_Down);
        QTest::keyClick(input, Qt::Key_Up);
        QTest::keyClick(input, Qt::Key_Enter);
        noSelection();
        CHECK(opened.size() == 3);
        complete();
        QTest::keyClick(input, Qt::Key_Up);
        CHECK(QFile::remove(last));
        QTest::keyClick(input, Qt::Key_Return);
        CHECK(opened.size() == 3); // No fallback to the first file.
        CHECK(QDir().mkpath(last)); // A replaced file must not open as a folder.
        QTest::keyClick(input, Qt::Key_Return);
        CHECK(opened.size() == 3 && !input->text().isEmpty());
        CHECK(QDir().rmdir(last));
        model->removeRow(4);
        QTest::keyClick(input, Qt::Key_Up);
        selected(1);

        // A real list activation uses the same path and still opens folders
        // in the tree instead of emitting a file activation.
        list->activated(model->index(1, 0));
        CHECK(opened.size() == 4 && opened.last().first().toString() == first);
        list->activated(model->index(0, 0));
        CHECK(input->text().isEmpty());
        noSelection();
        CHECK(opened.size() == 4);

        input->setText(QStringLiteral("test"));
        complete();
        controller.focusSearch();
        QTest::keyClick(input, Qt::Key_Down);
        controller.setVisible(false, false);
        noSelection();
        controller.setVisible(true, false);
        controller.focusSearch();
        QTest::keyClick(input, Qt::Key_Down);
        controller.setPanelMode(WorkspacePanelController::PanelMode::Search, false);
        noSelection();
        controller.setPanelMode(WorkspacePanelController::PanelMode::Workspace, false);
        controller.focusSearch();
        QTest::keyClick(input, Qt::Key_Down);
        const quint64 beforeRootChange = controller.m_searchGeneration;
        controller.setRoot(otherRoot.path(), false);
        noSelection();
        controller.applySearchResults(beforeRootChange, hits);
        CHECK(model->rowCount() == 0 && input->text().isEmpty());

        // Finally exercise the real debounce/worker/completion path.
        controller.setRoot(files.path(), false);
        controller.focusSearch();
        input->setText(QStringLiteral("first.md"));
        CHECK(QTest::qWaitFor([&] { return controller.m_searchResultsCurrent; }, 3000));
        CHECK(model->rowCount() == 1);
        noSelection();
        QTest::keyClick(input, Qt::Key_Return);
        CHECK(opened.size() == 5 && opened.last().first().toString() == first);
        input->setText(QStringLiteral("does-not-exist"));
        CHECK(QTest::qWaitFor([&] { return controller.m_searchResultsCurrent; }, 3000));
        CHECK(model->rowCount() == 0);
        QTest::keyClick(input, Qt::Key_Return);
        CHECK(opened.size() == 5);
        controller.clearSearch();
        window.hide();
        settle();
    }
};

int main(int argc, char** argv)
{
    // The offscreen platform is not deployed beside the test executables, so it
    // is loaded from the Qt kit itself; forcing it without that path fails to start.
    if (!qEnvironmentVariableIsSet("FASTMD_TEST_NATIVE")
        && QDir(QStringLiteral(FASTMD_QT_PLATFORMS_DIR)).exists(QStringLiteral("qoffscreen.dll"))) {
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QStringLiteral(FASTMD_QT_PLATFORMS_DIR).toLocal8Bit());
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);
    QTemporaryDir settingsDir;
    CHECK(settingsDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("FastMDTests"));
    QCoreApplication::setApplicationName(QStringLiteral("WorkspaceOutlinePersistence"));
    for (double ratio : {0.15, 0.72}) {
        sequence("A: restart while visible", false, false, ratio);
        sequence("B: hide then restart", true, false, ratio);
        sequence("C: hide, restart, Git first", true, true, ratio);
    }
    defaults();
    modeToggleBehavior();
    searchSeeding();
    WorkspaceFileSearchNavigationTest::run();
    startupTreeBuild();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
