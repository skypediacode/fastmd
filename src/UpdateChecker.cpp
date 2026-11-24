#include "UpdateChecker.h"
#include "MainWindow.h"
#include "ThemedMessageBox.h"
#include "UpdateProgressDialog.h"

#include <QAbstractButton>
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMainWindow>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QTimer>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <appmodel.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shldisp.h>
#include <shlguid.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Services.Store.h>

using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Services::Store;
#endif

struct UpdateChecker::Impl {
#ifdef Q_OS_WIN
    StoreContext context{ nullptr };
    std::vector<StorePackageUpdate> updates;
    IAsyncOperationWithProgress<StorePackageUpdateResult, StorePackageUpdateStatus> download{ nullptr };
#endif
};

namespace {

UpdateChecker* s_instance = nullptr;

// The metadata query is a short call; anything past this is a wedged Store
// update service, not slow progress.
constexpr int kUpdateCheckTimeoutMs = 60000;
// The package is already downloaded when installation starts, so this bounds
// a stalled install rather than a slow one. Every window is closed by then,
// and without it a wedged Store would leave FastMD invisible but running.
constexpr int kInstallTimeoutMs = 5 * 60 * 1000;
// Installing an already-downloaded package on exit normally takes seconds;
// past this the Store is left to finish it on its own schedule.
constexpr int kExitInstallTimeoutMs = 2 * 60 * 1000;

QMainWindow* promptParent(QMainWindow* preferred)
{
    if (auto* active = qobject_cast<MainWindow*>(QApplication::activeWindow()))
        return active;
    if (preferred && preferred->isVisible())
        return preferred;
    for (QWidget* topLevel : QApplication::topLevelWidgets()) {
        if (auto* window = qobject_cast<MainWindow*>(topLevel); window && window->isVisible())
            return window;
    }
    return nullptr;
}

#ifdef Q_OS_WIN
std::wstring currentAppUserModelId()
{
    UINT32 length = 0;
    if (GetCurrentApplicationUserModelId(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length == 0)
        return {};
    std::wstring aumid(length, L'\0');
    if (GetCurrentApplicationUserModelId(&length, aumid.data()) != ERROR_SUCCESS)
        return {};
    aumid.resize(wcslen(aumid.c_str()));
    return aumid;
}

// Runs a program as a child of Explorer rather than of FastMD. A process
// FastMD creates itself inherits the package identity, and installing the
// update terminates every process of the package -- including one meant to
// reopen FastMD afterwards.
bool shellExecuteFromExplorer(const std::wstring& file, const std::wstring& parameters,
                              const std::wstring& directory)
{
    winrt::com_ptr<IShellWindows> shellWindows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(shellWindows.put()))))
        return false;

    VARIANT desktopLocation{};
    desktopLocation.vt = VT_I4;
    desktopLocation.lVal = CSIDL_DESKTOP;
    VARIANT emptyRoot{};
    long desktopHwnd = 0;
    winrt::com_ptr<IDispatch> desktopDispatch;
    if (shellWindows->FindWindowSW(&desktopLocation, &emptyRoot, SWC_DESKTOP, &desktopHwnd,
                                   SWFO_NEEDDISPATCH, desktopDispatch.put()) != S_OK
        || !desktopDispatch) {
        return false;
    }

    const auto serviceProvider = desktopDispatch.try_as<IServiceProvider>();
    winrt::com_ptr<IShellBrowser> browser;
    winrt::com_ptr<IShellView> view;
    winrt::com_ptr<IDispatch> background;
    if (!serviceProvider
        || FAILED(serviceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(browser.put())))
        || FAILED(browser->QueryActiveShellView(view.put()))
        || FAILED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(background.put())))) {
        return false;
    }
    const auto folderView = background.try_as<IShellFolderViewDual>();
    winrt::com_ptr<IDispatch> application;
    if (!folderView || FAILED(folderView->get_Application(application.put())))
        return false;
    const auto shell = application.try_as<IShellDispatch2>();
    if (!shell)
        return false;

    BSTR fileString = SysAllocString(file.c_str());
    VARIANT arguments{};
    arguments.vt = VT_BSTR;
    arguments.bstrVal = SysAllocString(parameters.c_str());
    VARIANT workingDirectory{};
    workingDirectory.vt = VT_BSTR;
    workingDirectory.bstrVal = SysAllocString(directory.c_str());
    VARIANT operation{};
    operation.vt = VT_BSTR;
    operation.bstrVal = SysAllocString(L"open");
    VARIANT show{};
    show.vt = VT_I4;
    show.lVal = SW_HIDE;
    const HRESULT executed = shell->ShellExecute(fileString, arguments, workingDirectory, operation, show);
    SysFreeString(fileString);
    VariantClear(&arguments);
    VariantClear(&workingDirectory);
    VariantClear(&operation);
    return SUCCEEDED(executed);
}

// Windows does not reliably restart a packaged desktop app after a Store
// update, even with RegisterApplicationRestart. This starts a hidden watcher
// outside the package that waits for this process to exit and for the newer
// version to be registered, then opens FastMD -- unless something (Windows'
// own restart, or finishInstall()'s fallback) already has. If the new version
// never appears it reopens the current one, so FastMD never stays closed.
bool startRelaunchWatcher()
{
    const std::wstring aumid = currentAppUserModelId();
    if (aumid.empty())
        return false;
    QString packageName;
    QString packageVersion;
    try {
        const PackageId id = Package::Current().Id();
        const PackageVersion version = id.Version();
        packageName = QString::fromStdWString(std::wstring(id.Name()));
        packageVersion = QStringLiteral("%1.%2.%3.%4")
            .arg(version.Major).arg(version.Minor).arg(version.Build).arg(version.Revision);
    } catch (const winrt::hresult_error&) {
        return false;
    }

    wchar_t systemDirectory[MAX_PATH] = {};
    const UINT systemLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (systemLength == 0 || systemLength >= MAX_PATH)
        return false;
    const std::wstring system(systemDirectory, systemLength);

    // Passed inside a double-quoted -Command argument, so the script itself
    // uses single quotes only.
    const QString script = QStringLiteral(
        "$ErrorActionPreference='SilentlyContinue';"
        "Wait-Process -Id %1 -Timeout 600;"
        "$old=[version]'%2';"
        "$session=(Get-Process -Id $PID).SessionId;"
        "$deadline=(Get-Date).AddSeconds(120);"
        "while((Get-Date) -lt $deadline){"
        "if(Get-Process -Name FastMD | Where-Object SessionId -eq $session){exit}"
        "if(Get-AppxPackage -Name '%3' | Where-Object {[version]$_.Version -gt $old}){break}"
        "Start-Sleep -Seconds 2};"
        "if(-not (Get-Process -Name FastMD | Where-Object SessionId -eq $session))"
        "{Start-Process 'shell:AppsFolder\\%4'}")
        .arg(GetCurrentProcessId())
        .arg(packageVersion, packageName, QString::fromStdWString(aumid));
    const std::wstring parameters = L"-NoProfile -NonInteractive -WindowStyle Hidden -Command \""
        + script.toStdWString() + L"\"";
    return shellExecuteFromExplorer(system + L"\\WindowsPowerShell\\v1.0\\powershell.exe",
                                    parameters, system);
}
#endif

// Launches the installed (possibly just-updated) package rather than this
// executable's path: after an update that path belongs to the old package
// version. "--restart" makes the new process wait for this one to exit.
bool relaunchApplication()
{
#ifdef Q_OS_WIN
    const std::wstring aumid = currentAppUserModelId();
    IApplicationActivationManager* activationManager = nullptr;
    if (!aumid.empty()
        && SUCCEEDED(CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER,
                                      IID_PPV_ARGS(&activationManager)))) {
        DWORD processId = 0;
        const HRESULT activated = activationManager->ActivateApplication(
            aumid.c_str(), L"--restart", AO_NOERRORUI, &processId);
        activationManager->Release();
        if (SUCCEEDED(activated))
            return true;
    }
#endif
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), { QStringLiteral("--restart") });
}

} // namespace

UpdateChecker* UpdateChecker::instance()
{
    if (!s_instance) {
        s_instance = new UpdateChecker(qApp);
        for (QWidget* topLevel : QApplication::topLevelWidgets()) {
            if (auto* window = qobject_cast<MainWindow*>(topLevel))
                window->attachUpdateChecker(s_instance);
        }
    }
    return s_instance;
}

UpdateChecker* UpdateChecker::existingInstance()
{
    return s_instance;
}

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>())
{
}

UpdateChecker::~UpdateChecker()
{
    if (s_instance == this)
        s_instance = nullptr;
}

void UpdateChecker::setState(State state)
{
    m_state = state;
    if (state != State::Downloading)
        m_progress = 0.0;
    emit stateChanged();
}

void UpdateChecker::checkForUpdates(QMainWindow* window)
{
    if (m_state != State::Idle || !window)
        return;
#ifdef Q_OS_WIN
    StoreContext context{ nullptr };
    try {
        Q_UNUSED(Package::Current());
        context = StoreContext::GetDefault();
        context.as<::IInitializeWithWindow>()->Initialize(reinterpret_cast<HWND>(window->winId()));
    } catch (const winrt::hresult_error&) {
        MainWindow::openUrl(Urls::StoreProtocol);
        return;
    }

    d->context = context;
    d->updates.clear();
    m_originWindow = window;
    const quint64 generation = ++m_generation;
    setState(State::Checking);

    // The check is short and user-initiated, so it keeps a modal progress
    // dialog; the long download that follows it does not.
    auto* checkingDialog = new UpdateProgressDialog(window, tr("Check for Updates"),
        tr("Checking for updates..."), tr("Contacting Microsoft Store..."));
    checkingDialog->setAttribute(Qt::WA_DeleteOnClose);
    checkingDialog->show();
    QPointer<UpdateProgressDialog> checkingDialogPtr(checkingDialog);

    IAsyncOperation<winrt::Windows::Foundation::Collections::IVectorView<StorePackageUpdate>> operation = nullptr;
    try {
        operation = context.GetAppAndOptionalStorePackageUpdatesAsync();
    } catch (const winrt::hresult_error&) {
        ++m_generation;
        checkingDialogPtr->dismiss();
        setState(State::Idle);
        themedWarning(window, tr("Update Check Failed"),
            tr("Microsoft Store could not check for updates."));
        return;
    }

    // UpdateProgressDialog refuses every dismissal but dismiss(), so a Store
    // update service that never answers must not hold it open forever.
    QTimer::singleShot(kUpdateCheckTimeoutMs, this, [this, generation, checkingDialogPtr]() {
        if (generation != m_generation || m_state != State::Checking)
            return;
        ++m_generation;
        if (checkingDialogPtr)
            checkingDialogPtr->dismiss();
        setState(State::Idle);
        if (QMainWindow* parent = promptParent(m_originWindow)) {
            themedWarning(parent, tr("Update Check Failed"),
                tr("Microsoft Store did not respond. Please try again later."));
        }
    });

    QPointer<UpdateChecker> self(this);
    operation.Completed([self, generation, checkingDialogPtr](const auto& completed, AsyncStatus status) {
        QMetaObject::invokeMethod(qApp, [self, generation, checkingDialogPtr, completed, status]() {
            // A late answer after the watchdog gave up must not resurrect the flow.
            if (!self || generation != self->m_generation || self->m_state != State::Checking)
                return;
            ++self->m_generation;
            if (checkingDialogPtr)
                checkingDialogPtr->dismiss();

            std::vector<StorePackageUpdate> updates;
            bool succeeded = status == AsyncStatus::Completed;
            if (succeeded) {
                try {
                    const auto updateView = completed.GetResults();
                    updates.reserve(updateView.Size());
                    for (const StorePackageUpdate& update : updateView)
                        updates.push_back(update);
                } catch (const winrt::hresult_error&) {
                    succeeded = false;
                }
            }

            QMainWindow* parent = promptParent(self->m_originWindow);
            if (!succeeded || updates.empty() || !parent) {
                self->setState(State::Idle);
                if (!parent)
                    return;
                if (!succeeded) {
                    themedWarning(parent, tr("Update Check Failed"),
                        tr("Microsoft Store could not check for updates."));
                } else {
                    themedInformation(parent, tr("Up to Date"),
                        tr("You're using the latest version of FastMD."));
                }
                return;
            }

            ThemedMessageBox box(parent);
            box.setIcon(QMessageBox::Question);
            box.setWindowTitle(tr("Update Available"));
            box.setText(tr("A new version of FastMD is available from Microsoft Store."));
            box.setInformativeText(tr("It downloads in the background while you keep working. "
                                      "FastMD asks before restarting to finish the update."));
            box.polishContentLayout();
            auto* downloadButton = box.addButton(tr("Download"), QMessageBox::AcceptRole);
            auto* notNowButton = box.addButton(tr("Not Now"), QMessageBox::RejectRole);
            box.setDefaultButton(downloadButton);
            box.setEscapeButton(notNowButton);
            bindDialogKey(&box, downloadButton, Qt::Key_D);
            bindDialogKey(&box, notNowButton, Qt::Key_N);
            box.exec();

            if (!self)
                return;
            if (box.clickedButton() != downloadButton) {
                self->setState(State::Idle);
                return;
            }
            self->d->updates = std::move(updates);
            self->startDownload();
        });
    });
#else
    Q_UNUSED(window);
    MainWindow::openUrl(Urls::StoreFallback);
#endif
}

void UpdateChecker::startDownload()
{
#ifdef Q_OS_WIN
    const quint64 generation = ++m_generation;
    setState(State::Downloading);

    QPointer<UpdateChecker> self(this);
    try {
        std::vector<StorePackageUpdate> updates = d->updates;
        const auto updatesForDownload = winrt::single_threaded_vector<StorePackageUpdate>(std::move(updates));
        // Download only: installing replaces the running package, which
        // terminates FastMD, so that waits for an explicit restart or exit.
        m_silentInstall = d->context.CanSilentlyDownloadStorePackageUpdates();
        d->download = m_silentInstall
            ? d->context.TrySilentDownloadStorePackageUpdatesAsync(updatesForDownload)
            : d->context.RequestDownloadStorePackageUpdatesAsync(updatesForDownload);
        d->download.Progress([self, generation](const auto&, const StorePackageUpdateStatus& status) {
            QMetaObject::invokeMethod(qApp, [self, generation, progress = status.TotalDownloadProgress]() {
                if (!self || generation != self->m_generation || self->m_state != State::Downloading)
                    return;
                self->m_progress = qBound(0.0, progress, 1.0);
                emit self->stateChanged();
            });
        });
        d->download.Completed([self, generation](const auto& completed, AsyncStatus status) {
            QMetaObject::invokeMethod(qApp, [self, generation, completed, status]() {
                if (!self || generation != self->m_generation || self->m_state != State::Downloading)
                    return;
                self->d->download = nullptr;
                bool downloaded = false;
                if (status == AsyncStatus::Completed) {
                    try {
                        downloaded = completed.GetResults().OverallState() == StorePackageUpdateState::Completed;
                    } catch (const winrt::hresult_error&) {
                        downloaded = false;
                    }
                }
                self->finishDownload(downloaded, status == AsyncStatus::Canceled);
            });
        });
    } catch (const winrt::hresult_error&) {
        d->download = nullptr;
        finishDownload(false, false);
    }
#endif
}

void UpdateChecker::cancelDownload()
{
    if (m_state != State::Downloading)
        return;
    ++m_generation;
#ifdef Q_OS_WIN
    if (d->download) {
        try {
            d->download.Cancel();
        } catch (const winrt::hresult_error&) {
        }
        d->download = nullptr;
    }
#endif
    setState(State::Idle);
}

void UpdateChecker::finishDownload(bool downloaded, bool canceled)
{
    ++m_generation;
    if (canceled) {
        setState(State::Idle);
        return;
    }
    if (!downloaded) {
        setState(State::Idle);
        if (QMainWindow* parent = promptParent(m_originWindow)) {
            themedWarning(parent, tr("Update Failed"),
                tr("Microsoft Store could not download the update."));
        }
        return;
    }
    setState(State::Ready);
    promptRestart();
}

void UpdateChecker::promptRestart()
{
    if (m_readyPrompt)
        return;
    QMainWindow* parent = promptParent(m_originWindow);
    if (!parent)
        return;

    // The download finishes while the user may be typing. The prompt is
    // therefore non-modal and shown without activation, so no keystroke
    // meant for the editor can land on Restart Now.
    auto* box = new ThemedMessageBox(parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setAttribute(Qt::WA_ShowWithoutActivating);
    box->setWindowModality(Qt::NonModal);
    box->setIcon(QMessageBox::Information);
    box->setWindowTitle(tr("Update Ready"));
    box->setText(tr("The FastMD update has been downloaded."));
    box->setInformativeText(m_silentInstall
        ? tr("Restart FastMD to install it now, or choose Later and it installs "
             "when you close FastMD.")
        : tr("Restart FastMD to finish installing it. "
             "FastMD reopens automatically after the update."));
    box->polishContentLayout();
    auto* restartButton = box->addButton(tr("Restart Now"), QMessageBox::AcceptRole);
    auto* laterButton = box->addButton(tr("Later"), QMessageBox::RejectRole);
    box->setDefaultButton(restartButton);
    box->setEscapeButton(laterButton);
    bindDialogKey(box, restartButton, Qt::Key_R);
    bindDialogKey(box, laterButton, Qt::Key_L);

    const QPointer<QMainWindow> parentPtr(parent);
    connect(box, &QMessageBox::buttonClicked, this, [this, restartButton, parentPtr](QAbstractButton* button) {
        if (button != restartButton)
            return;
        // Leave the prompt's own click handling before windows start closing.
        QTimer::singleShot(0, this, [this, parentPtr]() {
            restartToUpdate(parentPtr ? parentPtr.data() : promptParent(nullptr));
        });
    });
    m_readyPrompt = box;
    box->show();
}

void UpdateChecker::restartToUpdate(QMainWindow* window)
{
    if (m_state != State::Ready || !window)
        return;
    if (m_readyPrompt)
        m_readyPrompt->close();

    // Installing terminates this process, so unsaved work must be resolved
    // first; the clean window closes below then raise no further prompts.
    if (auto* mainWindow = qobject_cast<MainWindow*>(window); mainWindow && !mainWindow->confirmSaveBeforeUpdate())
        return;

    // Close every window through its normal path so session state, the
    // Terminal and recovery files are handled exactly as on Exit. The
    // process stays alive to run the install.
    qApp->setQuitOnLastWindowClosed(false);
    const QList<QWidget*> topLevels = QApplication::topLevelWidgets();
    for (QWidget* topLevel : topLevels) {
        auto* mainWindow = qobject_cast<MainWindow*>(topLevel);
        if (mainWindow && mainWindow->isVisible() && !mainWindow->close()) {
            qApp->setQuitOnLastWindowClosed(true);
            return;
        }
    }
    install();
}

void UpdateChecker::install()
{
    const quint64 generation = ++m_generation;
    setState(State::Installing);

    auto* dialog = new UpdateProgressDialog(nullptr, tr("Updating FastMD"),
        tr("Installing update..."),
        tr("FastMD will reopen automatically when the installation finishes."));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    m_installDialog = dialog;

#ifdef Q_OS_WIN
    // Windows terminates the running package to replace it; registering
    // only for that case (not crash/hang/reboot) has it start the new
    // version afterwards.
    RegisterApplicationRestart(L"--restart", RESTART_NO_CRASH | RESTART_NO_HANG | RESTART_NO_REBOOT);
    m_relaunchWatcherStarted = startRelaunchWatcher();

    QTimer::singleShot(kInstallTimeoutMs, this, [this, generation]() {
        if (generation == m_generation && m_state == State::Installing)
            finishInstall(false);
    });

    QPointer<UpdateChecker> self(this);
    try {
        // The window this context was first bound to is closed now.
        d->context.as<::IInitializeWithWindow>()->Initialize(reinterpret_cast<HWND>(dialog->winId()));
        std::vector<StorePackageUpdate> updates = d->updates;
        const auto updatesForInstall = winrt::single_threaded_vector<StorePackageUpdate>(std::move(updates));
        const auto installOperation = d->context.CanSilentlyDownloadStorePackageUpdates()
            ? d->context.TrySilentDownloadAndInstallStorePackageUpdatesAsync(updatesForInstall)
            : d->context.RequestDownloadAndInstallStorePackageUpdatesAsync(updatesForInstall);
        installOperation.Completed([self, generation](const auto& completed, AsyncStatus status) {
            QMetaObject::invokeMethod(qApp, [self, generation, completed, status]() {
                if (!self || generation != self->m_generation || self->m_state != State::Installing)
                    return;
                bool installed = false;
                if (status == AsyncStatus::Completed) {
                    try {
                        installed = completed.GetResults().OverallState() == StorePackageUpdateState::Completed;
                    } catch (const winrt::hresult_error&) {
                        installed = false;
                    }
                }
                self->finishInstall(installed);
            });
        });
    } catch (const winrt::hresult_error&) {
        finishInstall(false);
    }
#else
    Q_UNUSED(generation);
    finishInstall(false);
#endif
}

// Reached only when the Store finished (or failed) without terminating
// FastMD. Either way every window is already closed, so the application is
// reopened -- the new version if it installed, the current one otherwise.
void UpdateChecker::finishInstall(bool installed)
{
    ++m_generation;
#ifdef Q_OS_WIN
    // Relaunching below replaces the Windows restart; keeping both
    // registered could start two instances.
    UnregisterApplicationRestart();
#endif
    if (m_installDialog)
        m_installDialog->dismiss();
    if (!installed) {
        themedWarning(nullptr, tr("Update Failed"),
            tr("Microsoft Store could not install the update. "
               "FastMD will reopen with the current version."));
    }
    setState(State::Idle);
    // After a successful install the package may only be re-registered once
    // this process exits; launching now could start the old version and hold
    // the update back again. The watcher waits for the new one instead.
    if (!installed || !m_relaunchWatcherStarted)
        relaunchApplication();
    qApp->quit();
}

void UpdateChecker::installOnExit()
{
#ifdef Q_OS_WIN
    // Only the silent path can run with no window left to host Store UI.
    if (m_state != State::Ready || !m_silentInstall || GetSystemMetrics(SM_SHUTTINGDOWN))
        return;
    ++m_generation;
    m_state = State::Installing;

    const auto finished = std::make_shared<std::atomic_bool>(false);
    try {
        std::vector<StorePackageUpdate> updates = d->updates;
        const auto updatesForInstall = winrt::single_threaded_vector<StorePackageUpdate>(std::move(updates));
        const auto installOperation = d->context.TrySilentDownloadAndInstallStorePackageUpdatesAsync(updatesForInstall);
        installOperation.Completed([finished](const auto&, AsyncStatus) { finished->store(true); });
    } catch (const winrt::hresult_error&) {
        return;
    }

    // Polled rather than signalled: the completion can arrive after the
    // timeout, when this event loop no longer exists.
    QEventLoop loop;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer poll;
    poll.setInterval(100);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (finished->load() || elapsed.hasExpired(kExitInstallTimeoutMs))
            loop.quit();
    });
    poll.start();
    loop.exec();
#endif
}
