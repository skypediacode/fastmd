#include <QApplication>
#include <QIcon>
#include <QFileInfo>
#include <QDir>
#include <QMetaObject>
#include <QTimer>
#include <QThread>
#include <QThreadPool>
#include <QRunnable>
#include <QFontDatabase>
#include <QEvent>
#include <QDataStream>
#include <QToolBar>
#include <QToolButton>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSharedPointer>
#include "MainWindow.h"
#include "ApplicationTheme.h"
#include "FontPreferences.h"
#include "StartupProfiler.h"
#include "AppShutdown.h"
#include "UpdateChecker.h"
#include "RatePrompt.h"
#include "PreviewWidget.h"
#include "TerminalController.h"
#include "PdfExportHost.h"
#include "IconHelper.h"
#include "CliExport.h"
#include "QtGpuProbe.h"
#include <QProxyStyle>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#include <appmodel.h>
#endif

namespace {
constexpr auto kInstanceServerName = "FastMD.SingleInstance.v1";

#ifdef Q_OS_WIN
struct InstanceMutex {
    InstanceMutex()
        : handle(CreateMutexW(nullptr, FALSE, L"Local\\FastMD.SingleInstance.v1"))
        , alreadyExists(handle && GetLastError() == ERROR_ALREADY_EXISTS)
    {
    }

    HANDLE handle = nullptr;
    bool alreadyExists = false;
};

InstanceMutex& instanceMutex()
{
    static InstanceMutex mutex;
    return mutex;
}

// Returns false while another FastMD instance still holds the claim.
bool releaseInstanceMutex()
{
    InstanceMutex& mutex = instanceMutex();
    if (mutex.handle) {
        CloseHandle(mutex.handle);
        mutex.handle = nullptr;
    }
    HANDLE other = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\FastMD.SingleInstance.v1");
    if (!other)
        return true;
    CloseHandle(other);
    return false;
}

bool waitForExistingInstanceToExit()
{
    for (int attempt = 0; attempt != 200; ++attempt) {
        HANDLE handle = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\FastMD.SingleInstance.v1");
        if (!handle)
            return true;
        CloseHandle(handle);
        QThread::msleep(50);
    }
    return false;
}
#endif

QStringList launchPaths(const QStringList& args)
{
    QStringList paths;
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--new-window") || args.at(i) == QStringLiteral("--notepad"))
            continue;

        const QString argument = args.at(i);
        const QFileInfo fileInfo(argument);
        if (fileInfo.exists()) {
            paths.append(fileInfo.absoluteFilePath());
            continue;
        }

        if (!argument.contains(QChar('*')) && !argument.contains(QChar('?')))
            continue;

        const QFileInfo patternInfo(QDir::current(), argument);
        const QDir directory = patternInfo.dir();
        const QFileInfoList matches = directory.entryInfoList(
            { patternInfo.fileName() }, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
            QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& match : matches)
            paths.append(match.absoluteFilePath());
    }
    return paths;
}

bool forwardToExistingInstance(const QStringList& files)
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kInstanceServerName));
    if (!socket.waitForConnected(300))
        return false;

#ifdef Q_OS_WIN
    // This process was just launched (e.g. via the packaged "Edit with
    // FastMD" shell extension's ActivateApplication broker call, one hop
    // removed from a direct explorer.exe CreateProcess) and may itself be
    // briefly allowed to call SetForegroundWindow. It never creates a window
    // and is about to hand the file off to the already-running instance and
    // exit, so that grant would otherwise be wasted - forward it to any
    // process so the existing window's own SetForegroundWindow call (see
    // forceForeground() in MainWindowFile.cpp) succeeds instead of silently
    // failing.
    AllowSetForegroundWindow(ASFW_ANY);
#endif

    QByteArray payload;
    QDataStream payloadStream(&payload, QIODevice::WriteOnly);
    payloadStream << files;

    QByteArray frame;
    QDataStream frameStream(&frame, QIODevice::WriteOnly);
    frameStream << quint32(payload.size());
    frame.append(payload);
    socket.write(frame);
    // waitForBytesWritten() returns false when the write already completed
    // synchronously (nothing left pending to wait for) - a small frame over a
    // local socket routinely does. Treat "nothing left to flush" as success;
    // only loop/wait while bytes are actually still queued.
    while (socket.bytesToWrite() > 0) {
        if (!socket.waitForBytesWritten(500))
            break;
    }
    return socket.bytesToWrite() == 0;
}

class FastToolTipStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr, QStyleHintReturn* returnData = nullptr) const override
    {
        if (hint == QStyle::SH_ToolTip_WakeUpDelay)
            return 300; // Vastly faster initial popup (default is 700ms)
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override
    {
        // QToolBarLayout sizes the "More" overflow button from this metric on
        // every relayout, silently undoing any setFixedSize()/QSS margin the
        // button itself is given (see ToolbarOverflow.cpp) -- the native
        // value made it visibly shorter than the other toolbar buttons.
        // Measured via a temporary geometry dump: regular action QToolButtons
        // render at 38x38 (== the toolbar row's own fixed height, not the
        // 24px icon + padding + border box the QSS alone would suggest --
        // QToolBarLayout stretches action buttons to fill the row's cross
        // size), so match that instead of computing from the QSS box model.
        if (metric == QStyle::PM_ToolBarExtensionExtent) {
            if (const QToolBar* tb = qobject_cast<const QToolBar*>(widget)) {
                for (QToolButton* btn : tb->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) {
                    if (qstrcmp(btn->metaObject()->className(), "QToolButton") == 0) {
                        int h = btn->height();
                        if (h > 10)
                            return h;
                        h = btn->sizeHint().height();
                        if (h > 10)
                            return h;
                    }
                }
            }
            return 38;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    void drawControl(ControlElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget = nullptr) const override
    {
        // The scrollbar trough (the area above/below the handle) is drawn by the
        // stylesheet's opaque background. Let the native windowsvista style paint
        // it and it composites a light "groove" hatch onto the sub-page region,
        // which shows through as a light strip in the dark theme. Suppress the
        // native trough entirely - the QSS background is all we want here.
        if (element == CE_ScrollBarSubPage || element == CE_ScrollBarAddPage)
            return;
        QProxyStyle::drawControl(element, option, painter, widget);
    }

};

}

int main(int argc, char* argv[])
{
    StartupProfiler::init();
    FASTMD_MARK("main() entered");

#ifdef Q_OS_WIN
    // Recent Files only shows up in the taskbar Jump List when the running
    // process' AppUserModelID matches the one on the shortcut used to launch
    // it. Unpackaged builds (portable exe, Inno Setup install) don't get one
    // assigned automatically the same way twice - the shell derives an
    // implicit ID from the exe path, which can differ between the Start
    // Menu/desktop shortcut and the running process. Pin an explicit, stable
    // ID here (kept in sync with the "AppUserModelID" shortcut parameter in
    // fastmd.iss) so both sides always agree.
    //
    // MSIX-packaged launches already get a correct AppUserModelID assigned by
    // the OS from the package identity - overriding it here would be wrong,
    // so only do this when running unpackaged.
    UINT32 packageNameLength = 0;
    const bool isPackaged = GetCurrentPackageFullName(&packageNameLength, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
    if (!isPackaged)
        SetCurrentProcessExplicitAppUserModelID(L"Skypedia.FastMD");
#endif

    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    FASTMD_MARK("QApplication object created");
    QtGpuProbe::suppress();
    FASTMD_MARK("Qt GPU probe suppressed");
    app.setApplicationName("FastMD");
    app.setApplicationVersion("3.7.5");
    app.setOrganizationName("FastMD");

    // Command-line PDF export converts in this process and exits. It must
    // stay ahead of the single-instance mutex/forwarding, launch counting,
    // theme priming and MainWindow, so it neither reaches nor blocks a GUI.
    if (CliExport::isRequested(app.arguments()))
        return CliExport::run(app.arguments());

    // Enumerate the system font database off the UI thread.
    //
    // The first font query in the process makes Qt register every installed
    // family through DirectWrite (~33 ms here, 287 families). Nothing asks for
    // it explicitly: the Windows 11 style draws QToolBar's overflow chevron
    // from "Segoe Fluent Icons", so
    // QStyle::standardIcon(SP_ToolBarHorizontalExtensionButton) -- called from
    // QToolBarExtension's constructor, i.e. from the very first `new QToolBar`
    // -- is what pays for it. It cannot be avoided, but it does not have to be
    // serial: the main thread spends the next ~20 ms priming the platform
    // window and building the menus before that first QToolBar, and
    // QFontDatabase serializes on its own mutex, so a main-thread query that
    // arrives first simply waits instead of racing.
    //
    // This deliberately does *not* realize a font engine any more. It used to
    // build QFontMetrics(monospaceFont()), back when that font named five
    // families -- which made Qt open every installed family to assemble its own
    // Unicode fallback list, and the point of the worker was to absorb that
    // ~120 ms. The editor's font is single-family now (see
    // MarkdownEditor::monospaceFont() and the QPlainTextEdit rules in
    // Stylesheet.h), so no such list is ever built and there is nothing left to
    // absorb; measured, adding the QFontMetrics call back changes nothing.
    // Dropping the worker outright is not free, though: it costs ~8 ms of first
    // paint, because the enumeration then happens on the main thread.
    class FontWarmUp : public QRunnable {
    public:
        FontWarmUp() { setAutoDelete(true); }
        void run() override
        {
            (void)QFontDatabase::families();
        }
    };
    QThreadPool::globalInstance()->start(new FontWarmUp);
    FASTMD_MARK("font database warm-up dispatched");

    app.setStyle(new FastToolTipStyle(app.style()));
    FASTMD_MARK("application proxy style set");
    RatePrompt::recordLaunch();
    FASTMD_MARK("launch count recorded");
    
    app.setEffectEnabled(Qt::UI_FadeTooltip, false);
    app.setEffectEnabled(Qt::UI_AnimateTooltip, false);
    // Qt enables these from the Windows "fade or slide menus into view" setting.
    // When on, QMenuPrivate::popup() routes the popup through qFadeEffect()/
    // qScrollEffect(), which show a separate opaque helper top-level window
    // (QAlphaWidget / QRollEffect) before the real menu is mapped. That helper
    // bypasses AlignedMenu's own show animation entirely and flickers as an
    // opaque menu that opens and closes just before the animated one.
    app.setEffectEnabled(Qt::UI_AnimateMenu, false);
    app.setEffectEnabled(Qt::UI_FadeMenu, false);
    FASTMD_MARK("QApplication constructed");

    // Earliest reliable "we are quitting" signal. Set before any teardown so
    // the Workspace directory walkers stop while there is still an event loop,
    // rather than only once main() is on its way out.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [] { AppShutdown::begin(); });

    const StartupSettings startupSettings = StartupSettings::load();
    FASTMD_MARK("settings loaded");
    const QString startupTheme = startupSettings.value(
        QStringLiteral("theme"), QStringLiteral("system")).toString();
    ApplicationTheme::setEditorFontFamily(
        startupSettings.value(FontPreferences::EditorKey).toString());
    ApplicationTheme::apply(startupTheme);
    FASTMD_MARK("theme applied");

    const QStringList args = app.arguments();
    const bool newWindow = args.contains(QStringLiteral("--new-window"));
    const bool restart = args.contains(QStringLiteral("--restart"));
    const bool notepadMode = args.contains(QStringLiteral("--notepad"));
    const QStringList pathsToOpen = launchPaths(args);
    QLocalServer instanceServer;

    if (!newWindow) {
#ifdef Q_OS_WIN
        if (restart && !waitForExistingInstanceToExit())
            return 1;
        const InstanceMutex& mutex = instanceMutex();
        if (mutex.handle && mutex.alreadyExists) {
            if (restart)
                return 1;
            for (int attempt = 0; attempt != 10; ++attempt) {
                if (forwardToExistingInstance(pathsToOpen))
                    return 0;
                QThread::msleep(25);
            }
            return 1;
        }
#endif

        QLocalServer::removeServer(QString::fromLatin1(kInstanceServerName));
        if (!instanceServer.listen(QString::fromLatin1(kInstanceServerName))) {
            if (forwardToExistingInstance(pathsToOpen))
                return 0;
            return 1;
        }
    }
    FASTMD_MARK("single-instance resolved");

    QIcon appIcon(QStringLiteral(":/fastmd.ico"));
    app.setWindowIcon(appIcon);
    FASTMD_MARK("window icon loaded");

#ifdef Q_OS_WIN
    // Force Qt to register its shared QWindowIcon WNDCLASS by creating a dummy
    // top-level widget and invoking winId(). Then immediately override its
    // hbrBackground brush before the real MainWindow is created.
    // This perfectly fixes the Windows DWM white flash during the scale-up launch animation.
    {
        FASTMD_MARK("platform window prime begin");
        QWidget dummy;
        dummy.setWindowOpacity(0.0);
        HWND hwnd = reinterpret_cast<HWND>(dummy.winId());
        
        const bool dark = ApplicationTheme::isDark();

        static HBRUSH s_darkBrush = CreateSolidBrush(RGB(0x19, 0x1a, 0x1b));
        static HBRUSH s_lightBrush = CreateSolidBrush(RGB(0xf7, 0xf7, 0xf7));
        SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(dark ? s_darkBrush : s_lightBrush));
        FASTMD_MARK("platform window primed");
    }
#endif

    int exitCode = 0;
    {
    MainWindow window(nullptr, MainWindow::StartupMode::Normal, startupSettings, notepadMode);
    window.setWindowIcon(appIcon);
    FASTMD_MARK("MainWindow constructed");

    if (!newWindow) {
        QObject::connect(&instanceServer, &QLocalServer::newConnection, &window, [&]() {
            while (instanceServer.hasPendingConnections()) {
                QLocalSocket* socket = instanceServer.nextPendingConnection();
                auto buffer = QSharedPointer<QByteArray>::create();
                QObject::connect(socket, &QLocalSocket::readyRead, &window,
                    [socket, buffer, &window]() {
                        buffer->append(socket->readAll());
                        if (buffer->size() < static_cast<int>(sizeof(quint32)))
                            return;

                        QDataStream headerStream(*buffer);
                        quint32 payloadSize = 0;
                        headerStream >> payloadSize;
                        const qsizetype frameSize = sizeof(quint32) + payloadSize;
                        if (buffer->size() < frameSize)
                            return;

                        const QByteArray payload = buffer->mid(sizeof(quint32), payloadSize);
                        QDataStream payloadStream(payload);
                        QStringList paths;
                        payloadStream >> paths;
                        window.openStartupPaths(paths);
                        socket->disconnectFromServer();
                        socket->deleteLater();
                    });
            }
        });
    }

    // First-paint probe: the first paint event delivered to the window is the
    // closest in-process proxy for "the user sees something".
    if (StartupProfiler::enabled()) {
        struct FirstPaintProbe : QObject {
            using QObject::QObject;
            bool eventFilter(QObject* o, QEvent* e) override {
                if (!fired && e->type() == QEvent::Paint) {
                    fired = true;
                    FASTMD_MARK("first window paint");
                    o->removeEventFilter(this);
                    deleteLater();
                }
                return false;
            }
            bool fired = false;
        };
        window.installEventFilter(new FirstPaintProbe(&window));
        // First event-loop idle: queued behind everything posted during show().
        QTimer::singleShot(0, &app, [] { FASTMD_MARK("first event-loop idle"); });
    }

    window.show();
    FASTMD_MARK("window.show() returned");
    RatePrompt::scheduleStartupReview(&window);

    // Reproducible preview-render benchmark harness (Phase 1 / Phase 3).
    // FASTMD_BENCH=<markdown file> runs runPreviewBenchmark after the window is
    // shown and exits. FASTMD_BENCH_ITERS overrides the iteration count.
    const QByteArray benchFile = qgetenv("FASTMD_BENCH");
    if (!benchFile.isEmpty()) {
        int iters = 30;
        bool ok = false;
        const int envIters = qEnvironmentVariableIntValue("FASTMD_BENCH_ITERS", &ok);
        if (ok && envIters > 0)
            iters = envIters;
        QTimer::singleShot(0, &window, [&window, benchFile, iters]() {
            window.runPreviewBenchmark(QString::fromLocal8Bit(benchFile), iters);
        });
        exitCode = app.exec();
    } else {
        if (!pathsToOpen.isEmpty()) {
            window.enqueueStartupTask([&window, pathsToOpen]() {
                window.openStartupPaths(pathsToOpen);
            }, 2);
        }

        exitCode = app.exec();
    }
    }
    // The event loop is done and the window is gone. Tell the long-running
    // Workspace directory walkers to stop before the global QThreadPool's
    // destructor blocks on waitForDone() during static destruction.
    AppShutdown::begin();
    // Kill every shell before the WebView2 environment goes: the job object
    // would catch them at process exit anyway, but tearing them down here keeps
    // a normal quit free of any window where a conhost outlives the UI.
    TerminalController::shutdownAll();
    PreviewWidget::shutdownSharedEnvironment();
    PdfExportHost::shutdown();
    IconHelper::clearCaches();

    // A downloaded update the user postponed installs now, while nothing of
    // FastMD is open. The next launch must not be forwarded to this
    // windowless process, so the single-instance claim is released first;
    // installing would terminate any other instance, so one still open
    // (a --new-window process, or the one it was opened from) defers it.
    if (UpdateChecker* updater = UpdateChecker::existingInstance();
        updater && updater->state() == UpdateChecker::State::Ready) {
        instanceServer.close();
#ifdef Q_OS_WIN
        if (releaseInstanceMutex())
            updater->installOnExit();
#endif
    }
    return exitCode;
}

