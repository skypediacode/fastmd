#include "PdfExportHost.h"
#include "AppShutdown.h"

#include <QThread>
#include <QObject>
#include <QEventLoop>
#include <QTimer>
#include <QMetaObject>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QPageSize>
#include <QSizeF>
#include <QMarginsF>
#include <QPointer>
#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

#include <windows.h>
#include <wrl.h>
#include "WebView2.h"
#ifdef _MSC_VER
#  define __attribute__(x)
#endif
#include "WebView2EnvironmentOptions.h"
#ifdef _MSC_VER
#  undef __attribute__
#endif

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

namespace {

// A tiny script relayed to the host as soon as the page signals it has
// finished rendering. Registered once via AddScriptToExecuteOnDocumentCreated
// so it runs before any of the exported page's own scripts (including the
// one that dispatches the event) on every future navigation. Independent of
// preview.js's own hostBridge/postMessage protocol (which stays off for a
// standalone export page, see buildPreviewHtml's data-fastmd-standalone) --
// this listens for the plain DOM event preview.js already dispatches in that
// mode (window.fastmdPreviewRendered / 'fastmd-preview-rendered', see
// reportRendered() in resources/preview/preview.js) and forwards it to us.
const wchar_t* kRelayScript = LR"JS(
(function () {
  function relay() {
    try { window.chrome.webview.postMessage('fastmd-rendered'); } catch (e) {}
  }
  if (window.fastmdPreviewRendered) { relay(); return; }
  window.addEventListener('fastmd-preview-rendered', relay, { once: true });
}());
)JS";

const wchar_t* kHostWindowClass = L"FastMDPdfExportHost";

// Off-screen (never shown) top-level window that hosts the export
// controller. WebView2's own IsVisible flag -- not whether the underlying
// HWND was ever shown -- is what controls frame production/throttling, so
// parking this off the visible desktop is enough to keep it invisible to the
// user without starving requestAnimationFrame, which preview.js's
// reportRendered() depends on to signal completion.
HWND createHostWindow()
{
    static const bool registered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kHostWindowClass;
        return RegisterClassExW(&wc) != 0;
    }();
    if (!registered)
        return nullptr;
    return CreateWindowExW(WS_EX_TOOLWINDOW, kHostWindowClass, L"", WS_POPUP,
                           -32000, -32000, 1240, 1754, nullptr, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

QString persistentWebView2ExportProfileDir()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (path.isEmpty())
        path = QDir::tempPath() + QStringLiteral("/FastMD");
    path += QStringLiteral("/WebView2Export");
    QDir().mkpath(path);
    return path;
}

struct PageInches {
    double width, height, top, bottom, left, right;
};

// Mirrors ExportManager::buildPageCss()'s size/margin math exactly (same
// landscape transpose), so the print pipeline's own page geometry agrees with
// the @page CSS also injected into the document.
PageInches pageInches(const ExportManager::PdfExportOptions& options)
{
    QSizeF sizeMm = options.pageSize.size(QPageSize::Millimeter);
    if (options.orientation == QPageLayout::Landscape && sizeMm.width() < sizeMm.height())
        sizeMm.transpose();
    constexpr double kMmToInch = 1.0 / 25.4;
    const QMarginsF m = options.marginsMm;
    return { sizeMm.width() * kMmToInch, sizeMm.height() * kMmToInch,
             m.top() * kMmToInch, m.bottom() * kMmToInch,
             m.left() * kMmToInch, m.right() * kMmToInch };
}

// The same page as buildPrintSettings(), for the DevTools Page.printToPDF
// call. Only that call can ask Chromium for a document outline: each h1-h6
// becomes a bookmark at its rendered position. The outline is derived from
// the tagged-PDF structure tree, so an untagged print has none.
// preferCSSPageSize lets buildPageCss()'s @page size win, as it does under
// PrintToPdf; without it the paper size is rounded differently and A4 comes
// out about one point wider.
QByteArray printToPdfParams(const ExportManager::PdfExportOptions& options)
{
    const PageInches page = pageInches(options);
    const QJsonObject params {
        { QStringLiteral("paperWidth"), page.width },
        { QStringLiteral("paperHeight"), page.height },
        { QStringLiteral("marginTop"), page.top },
        { QStringLiteral("marginBottom"), page.bottom },
        { QStringLiteral("marginLeft"), page.left },
        { QStringLiteral("marginRight"), page.right },
        { QStringLiteral("scale"), 1.0 },
        { QStringLiteral("landscape"), false },
        { QStringLiteral("printBackground"), true },
        { QStringLiteral("displayHeaderFooter"), false },
        { QStringLiteral("generateTaggedPDF"), true },
        { QStringLiteral("generateDocumentOutline"), true },
        { QStringLiteral("preferCSSPageSize"), true },
        { QStringLiteral("transferMode"), QStringLiteral("ReturnAsStream") },
    };
    return QJsonDocument(params).toJson(QJsonDocument::Compact);
}

ComPtr<ICoreWebView2PrintSettings> buildPrintSettings(ICoreWebView2Environment* env,
                                                       const ExportManager::PdfExportOptions& options)
{
    ComPtr<ICoreWebView2Environment6> env6;
    if (FAILED(env->QueryInterface(IID_PPV_ARGS(&env6))) || !env6)
        return nullptr;
    ComPtr<ICoreWebView2PrintSettings> settings;
    if (FAILED(env6->CreatePrintSettings(&settings)) || !settings)
        return nullptr;

    const PageInches page = pageInches(options);
    settings->put_PageWidth(page.width);
    settings->put_PageHeight(page.height);
    settings->put_MarginTop(page.top);
    settings->put_MarginBottom(page.bottom);
    settings->put_MarginLeft(page.left);
    settings->put_MarginRight(page.right);
    settings->put_ScaleFactor(1.0);
    settings->put_ShouldPrintHeaderAndFooter(FALSE);
    settings->put_ShouldPrintSelectionOnly(FALSE);
    // WebView2's print pipeline defaults this to FALSE, unlike headless
    // Chrome's --print-to-pdf CLI switch (which always prints backgrounds) --
    // leaving it off would silently drop code-block/callout backgrounds and
    // the whole dark-theme page background from every export.
    settings->put_ShouldPrintBackgrounds(TRUE);
    return settings;
}

} // namespace

// ---------------------------------------------------------------------------
class PdfExportHostWorker : public QObject {
    Q_OBJECT
public:
    // Runs entirely on the dedicated export host thread (invoked via
    // BlockingQueuedConnection -- see PdfExportHost::renderToPdf()).
    bool render(const QString& navigateUrl, const QString& outputPath,
               const ExportManager::PdfExportOptions& options, bool documentOutline, QString& error)
    {
        if (!ensureReady(error))
            return false;

        // A navigation issued immediately after the controller/webview was
        // just created can occasionally abort on its own (observed in
        // practice: intermittent, only ever on the first export against a
        // freshly created host) before the browser process has fully
        // settled -- a known WebView2 quirk, not specific to this page. One
        // retry clears it; a second real failure is treated as genuine.
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (navigateAndWait(navigateUrl))
                break;
            if (attempt == 1) {
                error = QStringLiteral("The page failed to load for PDF export.");
                return false;
            }
        }

        if (documentOutline) {
            QString devToolsFailure;
            switch (printViaDevTools(outputPath, options, devToolsFailure, error)) {
            case DevToolsPrint::Done:
                return true;
            case DevToolsPrint::TimedOut:
                return false;
            case DevToolsPrint::Unavailable:
                qWarning("FastMD: DevTools PDF print unavailable (%s); exporting without bookmarks",
                         qPrintable(devToolsFailure));
                break;
            }
        }

        ComPtr<ICoreWebView2_7> wv7;
        if (FAILED(webview.As(&wv7)) || !wv7) {
            error = QStringLiteral("The installed WebView2 Runtime is too old to export PDFs.");
            return false;
        }
        ComPtr<ICoreWebView2PrintSettings> settings = buildPrintSettings(env.Get(), options);

        QFile::remove(outputPath);
        const std::wstring outW = QDir::toNativeSeparators(outputPath).toStdWString();

        struct PrintState {
            bool done = false;
            HRESULT hr = E_FAIL;
            BOOL result = FALSE;
        };
        auto printState = std::make_shared<PrintState>();
        QEventLoop printLoop;
        const QPointer<QEventLoop> printLoopGuard(&printLoop);
        const HRESULT startHr = wv7->PrintToPdf(outW.c_str(), settings.Get(),
            Callback<ICoreWebView2PrintToPdfCompletedHandler>(
                [printState, printLoopGuard](HRESULT errorCode, BOOL result) -> HRESULT {
#ifdef FASTMD_PDF_TEST_HOOKS
                    bool delayOk = false;
                    const int delayMs = qEnvironmentVariableIntValue(
                        "FASTMD_PDF_TEST_COMPLETION_DELAY_MS", &delayOk);
                    if (delayOk && delayMs > 0 && printLoopGuard) {
                        QTimer::singleShot(delayMs, printLoopGuard.data(),
                            [printState, printLoopGuard, errorCode, result] {
                                printState->hr = errorCode;
                                printState->result = result;
                                printState->done = true;
                                if (printLoopGuard)
                                    printLoopGuard->quit();
                            });
                        return S_OK;
                    }
#endif
                    printState->hr = errorCode;
                    printState->result = result;
                    printState->done = true;
                    if (printLoopGuard)
                        printLoopGuard->quit();
                    return S_OK;
                }).Get());
        if (FAILED(startHr)) {
            error = QStringLiteral("The WebView2 print pipeline could not be started.");
            return false;
        }
        const bool completed = waitForCompletion(printLoop, printState->done, printTimeoutMs());

        if (!completed) {
            // PrintToPdf has no cancellation API. The caller always supplies an
            // isolated staged output path, so even a misbehaving late runtime
            // write cannot touch the browser fallback or final destination.
            retireAfterTimeout(error);
            return false;
        }
        if (FAILED(printState->hr) || !printState->result) {
            error = QStringLiteral("The WebView2 print pipeline failed to generate the PDF.");
            return false;
        }
        if (!QFileInfo::exists(outputPath) || QFileInfo(outputPath).size() <= 0) {
            error = QStringLiteral("The WebView2 print pipeline produced an empty PDF.");
            return false;
        }
        return true;
    }

    void teardown()
    {
        if (controller)
            controller->Close();
        controller.Reset();
        webview.Reset();
        env.Reset();
        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }

private:
    enum class DevToolsPrint { Done, Unavailable, TimedOut };
    enum class Call { Ok, Failed, TimedOut };

    static int printTimeoutMs()
    {
        int timeoutMs = 30000;
#ifdef FASTMD_PDF_TEST_HOOKS
        bool timeoutOk = false;
        const int testTimeoutMs = qEnvironmentVariableIntValue(
            "FASTMD_PDF_TEST_TIMEOUT_MS", &timeoutOk);
        if (timeoutOk && testTimeoutMs > 0)
            timeoutMs = testTimeoutMs;
#endif
        return timeoutMs;
    }

    // Neither print API can be cancelled. Closing the controller is the only
    // supported way to retire an operation whose completion can no longer be
    // trusted; the next export recreates the host.
    void retireHost()
    {
        teardown();
        ready = false;
        setupFailed = false;
    }

    void retireAfterTimeout(QString& error)
    {
        retireHost();
        error = AppShutdown::started()
            ? QStringLiteral("PDF generation was cancelled during shutdown.")
            : QStringLiteral("PDF generation timed out.");
    }

    Call callDevTools(const wchar_t* method, const QByteArray& params, const QDeadlineTimer& deadline,
                      QJsonObject& result, QString& failure)
    {
        struct CallState {
            bool done = false;
            HRESULT hr = E_FAIL;
            QByteArray json;
        };
        auto state = std::make_shared<CallState>();
        QEventLoop loop;
        const QPointer<QEventLoop> loopGuard(&loop);
        const std::wstring paramsW = QString::fromUtf8(params).toStdWString();
        const HRESULT startHr = webview->CallDevToolsProtocolMethod(method, paramsW.c_str(),
            Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                [state, loopGuard](HRESULT errorCode, LPCWSTR json) -> HRESULT {
                    state->hr = errorCode;
                    if (json)
                        state->json = QString::fromWCharArray(json).toUtf8();
                    state->done = true;
                    if (loopGuard)
                        loopGuard->quit();
                    return S_OK;
                }).Get());
        const QString name = QString::fromWCharArray(method);
        if (FAILED(startHr)) {
            failure = name + QStringLiteral(" could not be started");
            return Call::Failed;
        }
        if (!waitForCompletion(loop, state->done, int(qMax<qint64>(deadline.remainingTime(), 0))))
            return Call::TimedOut;
        if (FAILED(state->hr)) {
            failure = name + QStringLiteral(": ") + QString::fromUtf8(state->json);
            return Call::Failed;
        }
        result = QJsonDocument::fromJson(state->json).object();
        return Call::Ok;
    }

    // Page.printToPDF is the only print API that can emit the heading outline
    // (PrintToPdf has no such setting). Its result is streamed in bounded
    // chunks rather than returned as one base64 string, so a large PDF is
    // never held in memory several times over. Anything short of a complete
    // PDF reports Unavailable: the page is still loaded, so the caller prints
    // it through PrintToPdf instead, without bookmarks.
    DevToolsPrint printViaDevTools(const QString& outputPath, const ExportManager::PdfExportOptions& options,
                                   QString& failure, QString& error)
    {
        const wchar_t* method = L"Page.printToPDF";
#ifdef FASTMD_PDF_TEST_HOOKS
        // Stands in for a runtime without the method: the real call fails.
        if (qEnvironmentVariableIsSet("FASTMD_PDF_TEST_NO_DEVTOOLS_PRINT"))
            method = L"Page.printToPDFUnsupported";
#endif
        const QDeadlineTimer deadline(printTimeoutMs());
        QJsonObject result;
        switch (callDevTools(method, printToPdfParams(options), deadline, result, failure)) {
        case Call::TimedOut:
            retireAfterTimeout(error);
            return DevToolsPrint::TimedOut;
        case Call::Failed:
            return DevToolsPrint::Unavailable;
        case Call::Ok:
            break;
        }
        const QString stream = result.value(QStringLiteral("stream")).toString();
        if (stream.isEmpty()) {
            failure = QStringLiteral("Page.printToPDF returned no stream");
            return DevToolsPrint::Unavailable;
        }

        constexpr int kChunkBytes = 1024 * 1024;
        const QByteArray readParams = QJsonDocument(QJsonObject {
            { QStringLiteral("handle"), stream }, { QStringLiteral("size"), kChunkBytes } })
            .toJson(QJsonDocument::Compact);
        QFile::remove(outputPath);
        QFile file(outputPath);
        bool written = file.open(QIODevice::WriteOnly);
        if (!written)
            failure = QStringLiteral("could not open the staged PDF");
        bool eof = false;
        while (written && !eof) {
            const Call read = callDevTools(L"IO.read", readParams, deadline, result, failure);
            if (read == Call::TimedOut) {
                file.remove();
                retireAfterTimeout(error);
                return DevToolsPrint::TimedOut;
            }
            if (read == Call::Failed) {
                written = false;
                break;
            }
            const QString data = result.value(QStringLiteral("data")).toString();
            const QByteArray bytes = result.value(QStringLiteral("base64Encoded")).toBool()
                ? QByteArray::fromBase64(data.toLatin1())
                : data.toUtf8();
            written = file.write(bytes) == bytes.size();
            if (!written)
                failure = QStringLiteral("could not write the staged PDF");
            eof = result.value(QStringLiteral("eof")).toBool();
        }

        QJsonObject closed;
        QString closeFailure;
        const QByteArray closeParams = QJsonDocument(QJsonObject {
            { QStringLiteral("handle"), stream } }).toJson(QJsonDocument::Compact);
        if (callDevTools(L"IO.close", closeParams, deadline, closed, closeFailure) == Call::TimedOut) {
            if (!written) {
                file.remove();
                retireAfterTimeout(error);
                return DevToolsPrint::TimedOut;
            }
            retireHost();
        }

        file.close();
        if (!written || file.size() <= 0) {
            if (written)
                failure = QStringLiteral("Page.printToPDF produced an empty PDF");
            file.remove();
            return DevToolsPrint::Unavailable;
        }
        return DevToolsPrint::Done;
    }

    static bool waitForCompletion(QEventLoop& loop, const bool& done, int timeoutMs)
    {
        if (done)
            return true;

        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(timeoutMs);

        QTimer shutdownPoll;
        shutdownPoll.setInterval(100);
        QObject::connect(&shutdownPoll, &QTimer::timeout, &loop, [&loop]() {
            if (AppShutdown::started())
                loop.quit();
        });
        shutdownPoll.start();
        loop.exec();
        return done && !AppShutdown::started();
    }

    // Navigates to `url` and waits (bounded) for either the page's own
    // "finished rendering" signal or a navigation failure. Returns false only
    // on an actual navigation failure -- a timeout without a failure still
    // returns true, matching the old headless-Chrome path's best-effort
    // --virtual-time-budget wait.
    bool navigateAndWait(const QString& url)
    {
        navFailed = false;
        renderSignaled = false;
        pendingLoop = &navLoop;

        if (!webview) {
            pendingLoop = nullptr;
            return false;
        }
        const std::wstring w = url.toStdWString();
        if (FAILED(webview->Navigate(w.c_str()))) {
            pendingLoop = nullptr;
            return false;
        }

        // Bounded wait for the page's own "finished rendering" signal
        // (KaTeX/Mermaid/highlight.js settled). Best-effort, like the old
        // headless Chrome path's --virtual-time-budget: a document that never
        // signals (a hung diagram) still gets printed once the timeout fires,
        // it just may be missing that one piece of content.
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        QObject::connect(&timeoutTimer, &QTimer::timeout, &navLoop, &QEventLoop::quit);
        timeoutTimer.start(15000);
        QTimer shutdownPoll;
        shutdownPoll.setInterval(100);
        QObject::connect(&shutdownPoll, &QTimer::timeout, &navLoop, [this]() {
            if (AppShutdown::started())
                navLoop.quit();
        });
        shutdownPoll.start();
        // See the identical comment on the environment-creation wait in
        // ensureReady(): the render signal (or a navigation failure) can
        // arrive before this point, and QEventLoop::quit() called while the
        // loop isn't running yet is silently dropped.
        if (!renderSignaled && !navFailed)
            navLoop.exec();
        pendingLoop = nullptr;

        return !navFailed && !AppShutdown::started();
    }

    ComPtr<ICoreWebView2Environment> env;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    HWND hwnd = nullptr;
    bool ready = false;
    bool setupFailed = false;

    // Per-navigation wait state. Only one render() call is ever in flight at a
    // time -- BlockingQueuedConnection calls into this worker are serialized
    // by its own thread's event loop -- so plain members are safe.
    QEventLoop navLoop;
    QEventLoop* pendingLoop = nullptr;
    bool navFailed = false;
    bool renderSignaled = false;

    bool ensureReady(QString& error)
    {
        if (ready)
            return true;
        if (setupFailed) {
            error = QStringLiteral("The WebView2 export host failed to initialize earlier.");
            return false;
        }

        hwnd = createHostWindow();
        if (!hwnd) {
            setupFailed = true;
            error = QStringLiteral("Could not create the off-screen PDF export window.");
            return false;
        }

        const std::wstring userData = QDir::toNativeSeparators(persistentWebView2ExportProfileDir()).toStdWString();
        auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
        // Same switches as fastHeadlessFlags() in ExportManagerPdf.cpp, passed
        // straight through to the underlying Chromium process:
        // --allow-file-access-from-files lets the exported temp HTML load
        // sibling document images by relative file:// path; the rest skip a
        // brand-new profile's normal first-run network activity (component
        // update ping, variations seed fetch, Safe Browsing list download),
        // which otherwise turned this environment's *first* creation into a
        // multi-second-to-indefinite network-bound stall on this dedicated
        // profile the very first time it is ever used.
        options->put_AdditionalBrowserArguments(
            L"--allow-file-access-from-files --no-first-run --no-default-browser-check "
            L"--disable-background-networking --disable-component-update --disable-sync "
            L"--disable-default-apps --disable-extensions "
            L"--disable-backgrounding-occluded-windows --metrics-recording-only --mute-audio");

        struct EnvironmentState {
            bool done = false;
            HRESULT hr = E_FAIL;
            ComPtr<ICoreWebView2Environment> value;
        };
        auto envState = std::make_shared<EnvironmentState>();
        QEventLoop envLoop;
        const QPointer<QEventLoop> envLoopGuard(&envLoop);
        HRESULT createHr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, userData.c_str(), options.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [envState, envLoopGuard](HRESULT result, ICoreWebView2Environment* created) -> HRESULT {
                    envState->hr = result;
                    if (SUCCEEDED(result) && created)
                        envState->value = created;
                    envState->done = true;
                    if (envLoopGuard)
                        envLoopGuard->quit();
                    return S_OK;
                }).Get());
        if (FAILED(createHr)) {
            setupFailed = true;
            error = QStringLiteral("Could not create the WebView2 environment for PDF export.");
            return false;
        }
        // The completion handler can fire synchronously/reentrantly from
        // within CreateCoreWebView2EnvironmentWithOptions() itself (observed
        // in practice, not just a documented edge case) -- QEventLoop::quit()
        // is a no-op when the loop is not yet running, so without this check
        // a synchronous completion is silently dropped and the exec() below
        // would wait forever for an event that already happened.
        if (!waitForCompletion(envLoop, envState->done, 15000)
            || FAILED(envState->hr) || !envState->value) {
            setupFailed = true;
            error = QStringLiteral("Could not create the WebView2 environment for PDF export.");
            return false;
        }
        env = envState->value;

        struct ControllerState {
            bool done = false;
            HRESULT hr = E_FAIL;
            ComPtr<ICoreWebView2Controller> value;
        };
        auto ctrlState = std::make_shared<ControllerState>();
        QEventLoop ctrlLoop;
        const QPointer<QEventLoop> ctrlLoopGuard(&ctrlLoop);
        const HRESULT controllerStartHr = env->CreateCoreWebView2Controller(hwnd,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [ctrlState, ctrlLoopGuard](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                    ctrlState->hr = hr;
                    if (SUCCEEDED(hr) && ctrl)
                        ctrlState->value = ctrl;
                    ctrlState->done = true;
                    if (ctrlLoopGuard)
                        ctrlLoopGuard->quit();
                    return S_OK;
                }).Get());
        if (FAILED(controllerStartHr)
            || !waitForCompletion(ctrlLoop, ctrlState->done, 15000)
            || FAILED(ctrlState->hr) || !ctrlState->value) {
            setupFailed = true;
            error = QStringLiteral("Could not create the WebView2 controller for PDF export.");
            return false;
        }
        controller = ctrlState->value;
        if (FAILED(controller->get_CoreWebView2(&webview)) || !webview) {
            setupFailed = true;
            error = QStringLiteral("Could not create the WebView2 controller for PDF export.");
            return false;
        }

        RECT rc;
        GetClientRect(hwnd, &rc);
        controller->put_Bounds(rc);
        controller->put_IsVisible(TRUE);

        if (ComPtr<ICoreWebView2Settings> settings; SUCCEEDED(webview->get_Settings(&settings)) && settings) {
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(FALSE);
        }

        EventRegistrationToken token{};
        webview->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    LPWSTR raw = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                        if (QString::fromWCharArray(raw) == QStringLiteral("fastmd-rendered")) {
                            renderSignaled = true;
                            if (pendingLoop)
                                pendingLoop->quit();
                        }
                        CoTaskMemFree(raw);
                    }
                    return S_OK;
                }).Get(), &token);

        webview->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                    BOOL success = FALSE;
                    args->get_IsSuccess(&success);
                    if (!success) {
                        navFailed = true;
                        if (pendingLoop)
                            pendingLoop->quit();
                    }
                    return S_OK;
                }).Get(), &token);

        // Never let target=_blank/window.open content open a second, visible
        // window during an export.
        webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    args->put_Handled(TRUE);
                    return S_OK;
                }).Get(), &token);

        struct ScriptState {
            bool done = false;
            HRESULT hr = E_FAIL;
        };
        auto scriptState = std::make_shared<ScriptState>();
        QEventLoop scriptLoop;
        const QPointer<QEventLoop> scriptLoopGuard(&scriptLoop);
        const HRESULT scriptStartHr = webview->AddScriptToExecuteOnDocumentCreated(kRelayScript,
            Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
                [scriptState, scriptLoopGuard](HRESULT hr, LPCWSTR) -> HRESULT {
                    scriptState->hr = hr;
                    scriptState->done = true;
                    if (scriptLoopGuard)
                        scriptLoopGuard->quit();
                    return S_OK;
                }).Get());
        if (FAILED(scriptStartHr)
            || !waitForCompletion(scriptLoop, scriptState->done, 5000)
            || FAILED(scriptState->hr)) {
            setupFailed = true;
            error = QStringLiteral("Could not initialize the WebView2 PDF export page.");
            return false;
        }

        ready = true;
        return true;
    }
};

// ---------------------------------------------------------------------------
namespace {

class PdfExportHostThread : public QThread {
protected:
    void run() override
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        exec();
        CoUninitialize();
    }
};

struct HostState {
    PdfExportHostThread* thread = nullptr;
    PdfExportHostWorker* worker = nullptr;
    bool shuttingDown = false;
};

HostState& hostState()
{
    static HostState s;
    return s;
}

// exportThreadPool() allows up to 2 concurrent export tasks, so two PDF
// exports can call renderToPdf() for the very first time at once -- guard
// first-time creation or both threads could construct their own QThread and
// leak one.
QMutex& ensureWorkerMutex()
{
    static QMutex m;
    return m;
}

QMutex& renderMutex()
{
    static QMutex m;
    return m;
}

PdfExportHostWorker* ensureWorker()
{
    QMutexLocker locker(&ensureWorkerMutex());
    HostState& s = hostState();
    if (s.shuttingDown)
        return nullptr;
    if (s.worker)
        return s.worker;
    s.thread = new PdfExportHostThread;
    s.thread->setObjectName(QStringLiteral("FastMD PDF Export"));
    s.thread->start();
    s.worker = new PdfExportHostWorker;
    s.worker->moveToThread(s.thread);
    return s.worker;
}

} // namespace

bool PdfExportHost::renderToPdf(const QString& navigateUrl, const QString& outputPath,
                                const ExportManager::PdfExportOptions& options, bool documentOutline,
                                QString* errorMessage)
{
    QMutexLocker renderLocker(&renderMutex());
    if (AppShutdown::started()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("PDF generation was cancelled during shutdown.");
        return false;
    }
    PdfExportHostWorker* worker = ensureWorker();
    if (!worker)
        return false;
    bool ok = false;
    QString err;
    QMetaObject::invokeMethod(worker, [worker, &navigateUrl, &outputPath, &options, documentOutline,
                                       &ok, &err]() {
        ok = worker->render(navigateUrl, outputPath, options, documentOutline, err);
    }, Qt::BlockingQueuedConnection);
    if (errorMessage)
        *errorMessage = err;
    return ok;
}

void PdfExportHost::shutdown()
{
    QMutexLocker renderLocker(&renderMutex());
    QMutexLocker locker(&ensureWorkerMutex());
    HostState& s = hostState();
    s.shuttingDown = true;
    if (!s.thread)
        return;
    if (s.worker) {
        PdfExportHostWorker* worker = s.worker;
        QMetaObject::invokeMethod(worker, [worker]() { worker->teardown(); }, Qt::BlockingQueuedConnection);
    }
    s.thread->quit();
    s.thread->wait();
    delete s.worker;
    delete s.thread;
    s.worker = nullptr;
    s.thread = nullptr;
}

#include "PdfExportHost.moc"
