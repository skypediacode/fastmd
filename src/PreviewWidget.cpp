#include "PreviewWidget.h"
#include "DarkTheme.h"
#include "ExportManager.h"
#include "FontPreferences.h"
#include "WebView2Environment.h"
#include "StartupProfiler.h"
#include "CommandShortcutRegistry.h"
#include "NativeWindowCursorSync.h"

#include <QWheelEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QLabel>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QTimer>
#include <QPointer>
#include <QPalette>
#include <QRegularExpression>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QFileDialog>
#include <QImage>
#include <QFile>
#include <QSaveFile>
#include <functional>
#include <cmath>
#include <cstdio>
#include <utility>

#include <windows.h>
#include <wrl.h>
#include "WebView2.h"
// The 1.0.4078 SDK's WebView2EnvironmentOptions.h guards a GCC/Clang
// __attribute__((annotate(...))) with `#if defined(__has_attribute)`. MSVC
// *does* define __has_attribute but not __attribute__, so the attribute leaks
// through as a syntax error. Neutralize __attribute__ for this include only.
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

QImage imageFromPngDataUrl(const QString& dataUrl)
{
    const QString prefix = QStringLiteral("data:image/png;base64,");
    if (!dataUrl.startsWith(prefix, Qt::CaseInsensitive))
        return {};

    const QByteArray bytes = QByteArray::fromBase64(dataUrl.mid(prefix.size()).toLatin1());
    QImage image;
    image.loadFromData(bytes, "PNG");
    return image;
}

QString savePathWithSuffix(QString path, const QString& suffix)
{
    if (QFileInfo(path).suffix().isEmpty())
        path += suffix;
    return path;
}

}

// ---------------------------------------------------------------------------
// The shared WebView2 environment now lives in WebView2Environment so the
// Terminal panel can share the same browser process + cache. kShellOrigin stays
// here because only the preview's navigation policy uses it.
// ---------------------------------------------------------------------------
namespace {

const wchar_t* kShellOrigin = L"https://fastmd.assets/";

using WebView2Environment::requestEnvironment;

} // namespace

// ---------------------------------------------------------------------------
// PreviewWidget::Impl — per-widget WebView2 controller + event plumbing.
// ---------------------------------------------------------------------------
class PreviewWidget::Impl {
public:
    explicit Impl(PreviewWidget* owner) : q(owner) {}

    PreviewWidget* q;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2>           webview;
    HWND hwnd = nullptr;
    bool shellReady = false;
    long long seq = 0;
    quint64 hostGeneration = 0;
    bool documentFindEventsHooked = false;
    QElapsedTimer documentLoadElapsed;

    // Resolved on demand rather than cached: the Find object belongs to the
    // WebView, which is recreated on reparent (closeForReparent), and a stale
    // ComPtr would then drive a dead view. Returns null on a runtime older than
    // the Find API.
    ComPtr<ICoreWebView2Find> findApi()
    {
        if (!webview)
            return nullptr;
        ComPtr<ICoreWebView2_28> wv28;
        if (FAILED(webview.As(&wv28)) || !wv28)
            return nullptr;
        ComPtr<ICoreWebView2Find> find;
        if (FAILED(wv28->get_Find(&find)))
            return nullptr;
        return find;
    }

    // Registered on first use and only for PDF tabs, so an ordinary preview pays
    // nothing. The handlers live as long as the WebView they were added to.
    void hookDocumentFindEvents(ICoreWebView2Find* find)
    {
        if (documentFindEventsHooked)
            return;
        documentFindEventsHooked = true;
        QPointer<PreviewWidget> guard(q);
        EventRegistrationToken token;
        find->add_MatchCountChanged(
            Callback<ICoreWebView2FindMatchCountChangedEventHandler>(
                [this, guard](ICoreWebView2Find* sender, IUnknown*) -> HRESULT {
                    if (guard)
                        reportDocumentFind(sender);
                    return S_OK;
                }).Get(), &token);
        find->add_ActiveMatchIndexChanged(
            Callback<ICoreWebView2FindActiveMatchIndexChangedEventHandler>(
                [this, guard](ICoreWebView2Find* sender, IUnknown*) -> HRESULT {
                    if (guard)
                        reportDocumentFind(sender);
                    return S_OK;
                }).Get(), &token);
    }

    void reportDocumentFind(ICoreWebView2Find* find)
    {
        if (!q->m_documentFindActive)
            return;
        INT32 count = 0;
        INT32 active = 0;
        if (FAILED(find->get_MatchCount(&count)) || FAILED(find->get_ActiveMatchIndex(&active)))
            return;
        // The API counts from 1 and reports -1 for no active match.
        emit q->documentFindChanged(count, active > 0 ? active - 1 : -1);
    }

    // NavigationCompleted fires before the PDF viewer has loaded the file, and
    // a search started in that gap reports no matches and is never revisited;
    // nothing signals that loading finished. So an empty result is searched again
    // on a short backoff, only while the file may still be loading.
    void retryDocumentFindWhileLoading(const QString& term, int attempt = 0)
    {
        static constexpr int kDelaysMs[] = { 250, 500, 1000, 2000, 4000 };
        static constexpr qint64 kLoadWindowMs = 10000;
        if (attempt >= int(std::size(kDelaysMs)) || term.isEmpty())
            return;
        if (documentLoadElapsed.isValid() && documentLoadElapsed.elapsed() > kLoadWindowMs)
            return;
        const quint64 generation = hostGeneration;
        QTimer::singleShot(kDelaysMs[attempt], q, [this, term, attempt, generation] {
            if (generation != hostGeneration || !q->m_documentFindActive
                || q->m_documentFindTerm != term)
                return;
            ComPtr<ICoreWebView2Find> find = findApi();
            INT32 count = 0;
            if (find && SUCCEEDED(find->get_MatchCount(&count)) && count > 0)
                return;
            q->startFindSession(term, true);
            retryDocumentFindWhileLoading(term, attempt + 1);
        });
    }

    void start()
    {
        hwnd = reinterpret_cast<HWND>(q->winId());
        NativeWindowCursorSync::schedule(q);
        QPointer<PreviewWidget> guard(q);
        const quint64 generation = ++hostGeneration;
        requestEnvironment([this, guard, generation](ICoreWebView2Environment* env) {
            if (!guard || generation != hostGeneration) return;
            if (!env) { q->showFallback(environmentErrorMessage()); return; }
            createController(env, generation);
        });
    }

    static QString environmentErrorMessage()
    {
        return PreviewWidget::tr(
            "The Microsoft Edge WebView2 Runtime could not be started, so the "
            "preview is unavailable.\n\n"
            "Editing still works normally. To restore the preview, install the "
            "Evergreen WebView2 Runtime from Microsoft and restart FastMD.");
    }

    void createController(ICoreWebView2Environment* env, quint64 generation)
    {
        QPointer<PreviewWidget> guard(q);
        env->CreateCoreWebView2Controller(hwnd,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this, guard, generation](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                    if (!guard) return S_OK;
                    if (generation != hostGeneration) {
                        if (ctrl)
                            ctrl->Close();
                        return S_OK;
                    }
                    if (FAILED(hr) || !ctrl) {
                        q->showFallback(environmentErrorMessage());
                        return S_OK;
                    }
                    controller = ctrl;
                    controller->get_CoreWebView2(&webview);
                    if (!webview) { q->showFallback(environmentErrorMessage()); return S_OK; }
                    setup();
                    return S_OK;
                }).Get());
    }

    void setup()
    {
        // Avoid a white flash: paint the controller with the themed background.
        applyBackgroundColor();
        applyPreferredColorScheme();

        if (ComPtr<ICoreWebView2Settings> settings; SUCCEEDED(webview->get_Settings(&settings)) && settings) {
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(q->isReadOnlyDocumentViewer());
            // HTML/SVG viewers use FastMD's smooth Ctrl+wheel handling below.
            settings->put_IsZoomControlEnabled(FALSE);
            // Markdown previews route shortcuts to Qt; HTML/SVG viewers retain
            // WebView2's document-only controls without enabling dev tools.
            if (ComPtr<ICoreWebView2Settings3> settings3; SUCCEEDED(settings.As(&settings3)) && settings3)
                settings3->put_AreBrowserAcceleratorKeysEnabled(q->isReadOnlyDocumentViewer());
        }

        // Map the executable directory as a virtual host so the shell + KaTeX +
        // highlight.js load from disk, fully offline.
        if (ComPtr<ICoreWebView2_3> wv3; SUCCEEDED(webview.As(&wv3)) && wv3) {
            const std::wstring exeDir =
                QDir::toNativeSeparators(QCoreApplication::applicationDirPath()).toStdWString();
            wv3->SetVirtualHostNameToFolderMapping(
                L"fastmd.assets", exeDir.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        }
        applyLocalDriveMappings();
        applyDocMapping();

        installHandlers();
        resize();
        controller->put_IsVisible(TRUE);
        navigateCurrentContent();

        // The controller's environment/creation is fully async (COM
        // callbacks) and can take anywhere from tens of ms to well over a
        // second on a cold start -- if the user moves/resizes the window
        // while this is still in flight, that geometry change is invisible
        // to us (resize() above is a no-op before this point: see its own
        // early-out) and there's no later trigger to catch up once the
        // controller *does* exist, since our move/resize handlers only fire
        // off actual Qt events, not off "the controller just became ready".
        // Also re-push the background color: on setups where the two
        // monitors are on different GPU adapters, moving a window across
        // them can force WebView2's DirectComposition surface to be
        // recreated, and per-surface state like put_DefaultBackgroundColor
        // doesn't necessarily survive that -- it can silently revert to
        // WebView2's own default (white), independent of the widget's own
        // geometry being correct. Retry both across a spread of delays so
        // at least one lands after whatever transition is in progress has
        // settled.
        QPointer<PreviewWidget> guard(q);
        for (int delayMs : { 50, 200, 500, 1000, 2000 }) {
            QTimer::singleShot(delayMs, q, [this, guard]() {
                if (!guard) return;
                resize();
                applyBackgroundColor();
                guard->pushTheme();
            });
        }
    }

    void applyBackgroundColor()
    {
        // controller is null until the async CreateCoreWebView2Controller
        // callback fires; unlike resize(), this had no guard for that, and
        // ComPtr::As() on a null ComPtr dereferences a null pointer instead
        // of failing gracefully. The original single call site (inside
        // setup(), right after controller is assigned) was always safe, but
        // refreshNativeViewBounds() can now call this before that happens.
        if (!controller) return;
        if (ComPtr<ICoreWebView2Controller2> c2; SUCCEEDED(controller.As(&c2)) && c2) {
            COREWEBVIEW2_COLOR col = q->m_dark
                ? COREWEBVIEW2_COLOR{ 255, 0x12, 0x13, 0x14 }   // #121314
                : COREWEBVIEW2_COLOR{ 255, 0xff, 0xff, 0xff };  // #ffffff
            c2->put_DefaultBackgroundColor(col);
        }
    }

    void applyPreferredColorScheme()
    {
        ComPtr<ICoreWebView2_13> wv13;
        if (!webview || FAILED(webview.As(&wv13)) || !wv13)
            return;
        ComPtr<ICoreWebView2Profile> profile;
        if (SUCCEEDED(wv13->get_Profile(&profile)) && profile) {
            profile->put_PreferredColorScheme(q->m_dark
                ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK
                : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT);
        }
    }

    void applyDocMapping()
    {
        ComPtr<ICoreWebView2_3> wv3;
        if (FAILED(webview.As(&wv3)) || !wv3)
            return;
        wv3->ClearVirtualHostNameToFolderMapping(L"fastmd.doc");
        QString folder;
        if (!q->m_basePath.isEmpty()) {
            const QFileInfo fi(q->m_basePath);
            folder = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
        } else {
            folder = QDir::tempPath();
        }
        if (!folder.isEmpty()) {
            const std::wstring w = QDir::toNativeSeparators(folder).toStdWString();
            wv3->SetVirtualHostNameToFolderMapping(
                L"fastmd.doc", w.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        }
    }

    void applyLocalDriveMappings()
    {
        // Chromium blocks file: subresources in the HTTPS preview shell.
        // Absolute Windows image paths are instead addressed through a
        // per-drive virtual host (see prepareBodyForBrowser()).
        ComPtr<ICoreWebView2_3> wv3;
        if (FAILED(webview.As(&wv3)) || !wv3)
            return;
        for (const QFileInfo& drive : QDir::drives()) {
            const QString root = drive.absoluteFilePath();
            if (root.size() < 2 || root.at(1) != QLatin1Char(':'))
                continue;
            const std::wstring host = QStringLiteral("fastmd.drive-%1")
                .arg(root.at(0).toLower()).toStdWString();
            const std::wstring folder = QDir::toNativeSeparators(root).toStdWString();
            wv3->SetVirtualHostNameToFolderMapping(
                host.c_str(), folder.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        }
    }

    void installHandlers()
    {
        QPointer<PreviewWidget> guard(q);

        // Web messages from the page (rendered / scroll / openExternal / ready).
        EventRegistrationToken t{};
        webview->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this, guard](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    if (!guard) return S_OK;
                    LPWSTR raw = nullptr;
                    if (SUCCEEDED(args->get_WebMessageAsJson(&raw)) && raw) {
                        q->handleWebMessage(QString::fromWCharArray(raw));
                        CoTaskMemFree(raw);
                    }
                    return S_OK;
                }).Get(), &t);

        // Read-only HTML and PDF tabs do not run FastMD's preview shell, so
        // their links cannot use its web-message hover protocol. WebView2
        // already exposes the same destination text it would put in its own
        // (disabled) browser status bar; forward that to MainWindow instead.
        // Keep this off the Markdown shell, where footnotes deliberately own
        // their richer popover and the page reports ordinary links itself.
        if (ComPtr<ICoreWebView2_12> wv12; SUCCEEDED(webview.As(&wv12)) && wv12) {
            wv12->add_StatusBarTextChanged(
                Callback<ICoreWebView2StatusBarTextChangedEventHandler>(
                    [this, guard](ICoreWebView2*, IUnknown*) -> HRESULT {
                        if (!guard || guard->m_readOnlyFilePath.isEmpty() || !webview)
                            return S_OK;
                        ComPtr<ICoreWebView2_12> statusWebview;
                        if (FAILED(webview.As(&statusWebview)) || !statusWebview)
                            return S_OK;
                        LPWSTR raw = nullptr;
                        const HRESULT hr = statusWebview->get_StatusBarText(&raw);
                        const QString text = SUCCEEDED(hr) && raw
                            ? QString::fromWCharArray(raw) : QString();
                        if (raw)
                            CoTaskMemFree(raw);
                        QMetaObject::invokeMethod(guard, [guard, text]() {
                            if (guard)
                                emit guard->linkHoverChanged(text);
                        }, Qt::QueuedConnection);
                        return S_OK;
                    }).Get(), &t);
        }

        // HTML/SVG tabs are document viewers: keep the opened file (and its
        // anchors) in FastMD, but hand every other navigation to the OS.
        webview->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [this, guard](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                    if (!guard) return S_OK;
                    LPWSTR uri = nullptr;
                    if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                        const std::wstring u(uri);
                        CoTaskMemFree(uri);
                        const QString uri = QString::fromStdWString(u);
                        const QUrl target(uri);
                        if (q->isReadOnlyDocumentViewer()) {
                            const QUrl openedFile = QUrl::fromLocalFile(q->m_readOnlyFilePath);
                            if (target.adjusted(QUrl::RemoveFragment) == openedFile)
                                return S_OK;
                            args->put_Cancel(TRUE);
                            QDesktopServices::openUrl(target);
                        } else if (!q->isReadOnlyDocumentViewer() && u.rfind(kShellOrigin, 0) != 0 && u.rfind(L"about:", 0) != 0
                                   && target != QUrl::fromLocalFile(q->m_readOnlyFilePath)) {
                            args->put_Cancel(TRUE);
                            QDesktopServices::openUrl(target);
                        }
                    }
                    return S_OK;
                }).Get(), &t);

        // WebView2 owns mouse-wheel input in its native child HWND, so Qt's
        // wheelEvent cannot tune Ctrl+wheel for HTML/SVG viewer tabs. Install a
        // page listener after every navigation and route the delta back to
        // the controller's ZoomFactor instead.
        webview->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [this, guard](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                    // A reload dropped the session with the old document, and a
                    // search begun before the first load never had one.
                    if (guard)
                        documentLoadElapsed.start();
                    if (guard && guard->m_documentFindActive)
                        guard->startDocumentFind(guard->m_documentFindTerm);
                    if (!guard || !guard->isReadOnlyDocumentViewer() || !webview)
                        return S_OK;
                    static const wchar_t kSmoothHtmlWheelZoom[] = LR"JS(
                        (function () {
                          if (window.__fastmdSmoothWheelZoom) return;
                          window.__fastmdSmoothWheelZoom = true;
                          window.addEventListener('wheel', function (event) {
                            // Interactive HTML can declare ownership so its
                            // canvas receives Ctrl+wheel instead of the
                            // viewer-level document zoom.
                            if (!event.ctrlKey || window.__fastmdOwnWheelZoom) return;
                            event.preventDefault();
                            event.stopImmediatePropagation();
                            if (window.chrome && window.chrome.webview)
                              window.chrome.webview.postMessage({ type: 'htmlViewerZoom', deltaY: event.deltaY });
                          }, { capture: true, passive: false });
                        }());
                    )JS";
                    webview->ExecuteScript(kSmoothHtmlWheelZoom, nullptr);
                    return S_OK;
                }).Get(), &t);

        // target=_blank / window.open -> open externally, never a popup.
        webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [guard](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    args->put_Handled(TRUE);
                    LPWSTR uri = nullptr;
                    if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                        QDesktopServices::openUrl(QUrl(QString::fromWCharArray(uri)));
                        CoTaskMemFree(uri);
                    }
                    return S_OK;
                }).Get(), &t);

        if (ComPtr<ICoreWebView2_11> wv11; SUCCEEDED(webview.As(&wv11)) && wv11) {
            wv11->add_ContextMenuRequested(
                Callback<ICoreWebView2ContextMenuRequestedEventHandler>(
                    [guard](ICoreWebView2*, ICoreWebView2ContextMenuRequestedEventArgs* args) -> HRESULT {
                        if (!guard || !guard->isReadOnlyDocumentViewer())
                            return S_OK;
                        BOOL hasSelection = FALSE;
                        if (ComPtr<ICoreWebView2ContextMenuTarget> target;
                            SUCCEEDED(args->get_ContextMenuTarget(&target)) && target) {
                            target->get_HasSelection(&hasSelection);
                        }
                        args->put_Handled(TRUE);
                        QMetaObject::invokeMethod(guard, [guard, hasSelection]() {
                            if (guard) {
                                emit guard->contextMenuRequested(QCursor::pos(), hasSelection, -1,
                                                                 {}, {});
                            }
                        }, Qt::QueuedConnection);
                        return S_OK;
                    }).Get(), &t);
        }

        // Markdown previews route browser accelerators to Qt, because a native
        // WebView2 child does not participate in Qt's shortcut map. HTML viewer
        // tabs keep WebView2's document accelerators instead.
        controller->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [this, guard](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                    if (!guard) return S_OK;
                    COREWEBVIEW2_KEY_EVENT_KIND kind;
                    if (FAILED(args->get_KeyEventKind(&kind))) return S_OK;
                    if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
                        kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)
                        return S_OK;

                    UINT vk = 0;
                    if (FAILED(args->get_VirtualKey(&vk))) return S_OK;

                    const bool ctrl  = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    const bool shift = (::GetKeyState(VK_SHIFT)   & 0x8000) != 0;
                    const bool alt   = (::GetKeyState(VK_MENU)    & 0x8000) != 0;

                    if (q->isReadOnlyDocumentViewer()) {
                        if (CommandShortcutRegistry::matchesNativeAccelerator(CommandId::EditSelectAll, ctrl, shift, alt, vk)) {
                            args->put_Handled(TRUE);
                            q->selectAll();
                        } else if (CommandShortcutRegistry::matchesNativeAccelerator(CommandId::FilePrint, ctrl, shift, alt, vk)) {
                            args->put_Handled(TRUE);
                            q->printReadOnlyFile();
                        } else if (CommandShortcutRegistry::matchesNativeAccelerator(CommandId::ViewZoomIn, ctrl, shift, alt, vk)) {
                            args->put_Handled(TRUE);
                            q->htmlZoomIn();
                        } else if (CommandShortcutRegistry::matchesNativeAccelerator(CommandId::ViewZoomOut, ctrl, shift, alt, vk)) {
                            args->put_Handled(TRUE);
                            q->htmlZoomOut();
                        } else if (CommandShortcutRegistry::matchesNativeAccelerator(CommandId::ViewResetZoom, ctrl, shift, alt, vk)) {
                            args->put_Handled(TRUE);
                            q->resetHtmlZoom();
                        }
                        return S_OK;
                    }

                    // The page holds focus after a click into the PDF; Esc must
                    // still close the search, as it does from the find field.
                    if (vk == VK_ESCAPE && q->m_documentFindActive) {
                        args->put_Handled(TRUE);
                        emit q->commandRequested(PreviewWidget::Command::CloseDocumentFind);
                        return S_OK;
                    }

                    using Command = PreviewWidget::Command;
                    static const struct PreviewCommandMap {
                        CommandId id;
                        Command cmd;
                    } kPreviewCommands[] = {
                        { CommandId::EditFind,                  Command::Find },
                        { CommandId::EditReplace,               Command::Replace },
                        { CommandId::EditFindNext,              Command::FindNext },
                        { CommandId::EditFindPrevious,          Command::FindPrevious },
                        { CommandId::FileSave,                  Command::Save },
                        { CommandId::FileSaveAs,                Command::SaveAs },
                        { CommandId::FileSaveAll,               Command::SaveAll },
                        { CommandId::FilePrint,                 Command::Print },
                        { CommandId::FileExportHtml,            Command::ExportHtml },
                        { CommandId::FileExportDocx,            Command::ExportDocx },
                        { CommandId::FileExportPdf,             Command::ExportPdf },
                        { CommandId::FilePreferences,           Command::Preferences },
                        { CommandId::ViewZoomIn,                Command::ZoomIn },
                        { CommandId::ViewZoomOut,               Command::ZoomOut },
                        { CommandId::FileCloseTab,              Command::CloseTab },
                        { CommandId::ViewCommandPalette,        Command::CommandPalette },
                        { CommandId::ViewFocusFileSearch,       Command::WorkspaceSearch },
                        { CommandId::EditFindInFiles,           Command::FindInFiles },
                        { CommandId::EditReplaceInFiles,        Command::ReplaceInFiles },
                        { CommandId::ViewTogglePreview,         Command::TogglePreview },
                        { CommandId::ViewReadingMode,           Command::ToggleReadingMode },
                        { CommandId::ViewMindMap,               Command::ToggleMindMap },
                        { CommandId::EditBack,                  Command::Back },
                        { CommandId::EditForward,               Command::Forward },
                        { CommandId::ViewSwitchEditorMode,      Command::SwitchEditorMode },
                        { CommandId::ViewWordWrap,              Command::ToggleWordWrap },
                        { CommandId::FormatAutoFormatDocument,  Command::FormatDocument },
                        { CommandId::PreviewRefresh,            Command::RefreshPreview },
                    };

                    for (const auto& entry : kPreviewCommands) {
                        if (CommandShortcutRegistry::matchesNativeAccelerator(entry.id, ctrl, shift, alt, vk)) {
                            args->put_Handled(TRUE);
                            emit q->commandRequested(entry.cmd);
                            return S_OK;
                        }
                    }
                    return S_OK;
                }).Get(), &t);

    }

    void navigateShell()
    {
        const QString shell = q->isMindMap() ? QStringLiteral("mindmap.html") : QStringLiteral("preview.html");
        QString url = QStringLiteral("https://fastmd.assets/preview/%1?theme=%2&fs=%3")
                          .arg(shell)
                          .arg(q->m_dark ? QStringLiteral("dark") : QStringLiteral("light"))
                          .arg(q->m_baseFontSize);
        const std::wstring w = url.toStdWString();
        webview->Navigate(w.c_str());
    }

    void navigateReadOnlyFile()
    {
        if (!webview || q->m_readOnlyFilePath.isEmpty())
            return;
        const std::wstring url = QUrl::fromLocalFile(q->m_readOnlyFilePath).toString(QUrl::FullyEncoded).toStdWString();
        webview->Navigate(url.c_str());
    }

    void navigateCurrentContent()
    {
        shellReady = false;
        q->m_postedBlocks.reset();
        // A reload drops any find session with the document it was searching.
        q->m_findSessionActive = false;
        if (q->m_readOnlyFilePath.isEmpty())
            navigateShell();
        else
            navigateReadOnlyFile();
    }

    void postJson(const QJsonObject& obj)
    {
        if (!webview) return;
        const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        const std::wstring w = QString::fromUtf8(json).toStdWString();
        webview->PostWebMessageAsJson(w.c_str());
    }

    // A message already in JSON form. QString is UTF-16 and NUL-terminated,
    // which is exactly the LPCWSTR WebView2 wants, so nothing is converted.
    void postRaw(const QString& json)
    {
        if (!webview) return;
        webview->PostWebMessageAsJson(reinterpret_cast<LPCWSTR>(json.utf16()));
    }

    void execScript(const QString& js)
    {
        if (!webview) return;
        const std::wstring w = js.toStdWString();
        webview->ExecuteScript(w.c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [](HRESULT, LPCWSTR) -> HRESULT { return S_OK; }).Get());
    }

    void showPrintUi()
    {
        if (ComPtr<ICoreWebView2_16> wv16; webview && SUCCEEDED(webview.As(&wv16)) && wv16)
            wv16->ShowPrintUI(COREWEBVIEW2_PRINT_DIALOG_KIND_BROWSER);
    }

    void resize()
    {
        if (!controller || !hwnd) return;
        RECT rc;
        GetClientRect(hwnd, &rc);
        controller->put_Bounds(rc);
    }

    void closeForReparent()
    {
        ++hostGeneration;
        if (controller)
            controller->Close();
        controller.Reset();
        webview.Reset();
        hwnd = nullptr;
        shellReady = false;
        q->m_postedBlocks.reset();
        // The session died with the WebView; leaving the flag set would make
        // F3 call FindNext() on the recreated view, which silently does
        // nothing instead of reopening the bar.
        q->m_findSessionActive = false;
        q->m_documentFindActive = false;
        documentFindEventsHooked = false;
        documentLoadElapsed.invalidate();
    }
};

// ---------------------------------------------------------------------------
// PreviewWidget
// ---------------------------------------------------------------------------
PreviewWidget::PreviewWidget(QWidget* parent)
    : QWidget(parent), d(new Impl(this))
{
    // The WebView2 is hosted on a native child window of this widget, but that
    // HWND is created by Impl::start()'s winId() call when a controller is
    // actually requested -- not here. Setting WA_NativeWindow in the
    // constructor instead made the window exist by the end of QWidget::show(),
    // and Qt then has to promote the whole surrounding subtree so a native
    // child cannot punch through its alien siblings: measured at 19 HWNDs
    // created inside show() (menu bar, toolbar container, every splitter and
    // handle, TabWidget, its stacked widget, the tab bar, MarkdownEditor, the
    // status bar), ~1 ms each, plus a second per-QWidgetWindow paint pass.
    // Leaving it alien until the controller is requested took MainWindow ->
    // show() return from 30 ms to 6 ms with no visible difference: the themed
    // background below is painted into the parent's backing store instead of
    // its own window, and frames captured through startup are pixel-identical.
    //
    // WA_DontCreateNativeAncestors still matters -- it applies when winId() is
    // eventually called -- so it stays set here.
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    setAutoFillBackground(true);

    // Themed blank background shown before/while WebView2 initializes.
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0xff, 0xff, 0xff));
    setPalette(pal);
}

PreviewWidget::~PreviewWidget()
{
    if (d) {
        if (d->controller)
            d->controller->Close();
        delete d;
        d = nullptr;
    }
}

void PreviewWidget::shutdownSharedEnvironment()
{
    WebView2Environment::shutdown();
}

void PreviewWidget::warmEnvironment()
{
    WebView2Environment::warm();
}

void PreviewWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // The WebView2 lives in a native child HWND whose bounds are only pushed to
    // the controller on resizeEvent. When this pane is re-shown after a tab
    // switch at a size that didn't change while it was hidden, no resizeEvent
    // fires, so the controller keeps stale (often narrower) bounds and renders
    // the page at the wrong width. Re-sync now, and again once layout settles.
    if (d) d->resize();
    QTimer::singleShot(0, this, [this] { if (d) d->resize(); });
}

void PreviewWidget::ensureInitialized()
{
    if (m_initializationRequested && d && !d->hwnd && !m_fallbackLabel)
        d->start();
}

void PreviewWidget::focusContent()
{
    if (!d || !d->controller)
        return;
    d->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

bool PreviewWidget::showFindBar(const QString& term)
{
    // FALSE = show the runtime's own find bar. The Markdown preview deliberately
    // does not suppress it in favour of a native overlay: the bar carries its
    // own match counter, next/previous, and options, and it is what HTML/SVG
    // viewer tabs already show, so Find looks the same everywhere the Preview is
    // the document.
    return startFindSession(term, false);
}

bool PreviewWidget::startDocumentFind(const QString& term)
{
    m_documentFindActive = true;
    m_documentFindTerm = term;
    if (ComPtr<ICoreWebView2Find> find = d ? d->findApi() : nullptr)
        d->hookDocumentFindEvents(find.Get());
    if (term.isEmpty()) {
        stopFind();
        return true;
    }
    // TRUE: FindOverlay is the UI here. It can sit below the PDF toolbar, which
    // the runtime's bar cannot be told to do.
    const bool started = startFindSession(term, true);
    if (d)
        d->retryDocumentFindWhileLoading(term);
    return started;
}

void PreviewWidget::stopDocumentFind()
{
    m_documentFindActive = false;
    m_documentFindTerm.clear();
    stopFind();
}

bool PreviewWidget::startFindSession(const QString& term, bool suppressBar)
{
    ComPtr<ICoreWebView2Find> find = d ? d->findApi() : nullptr;
    if (!find)
        return false;

    // FindOptions come from the environment, not the WebView -- both interfaces
    // postdate the runtime FastMD's minimum, so either QueryInterface failing is
    // the "runtime too old" signal the caller falls back on.
    ComPtr<ICoreWebView2Environment> env = WebView2Environment::existing();
    ComPtr<ICoreWebView2Environment15> env15;
    if (!env || FAILED(env.As(&env15)) || !env15)
        return false;
    ComPtr<ICoreWebView2FindOptions> options;
    if (FAILED(env15->CreateFindOptions(&options)) || !options)
        return false;

    const std::wstring findTerm = term.toStdWString();
    options->put_FindTerm(findTerm.c_str());
    options->put_IsCaseSensitive(FALSE);
    options->put_ShouldMatchWord(FALSE);
    options->put_ShouldHighlightAllMatches(TRUE);
    options->put_SuppressDefaultFindDialog(suppressBar);

    // Without the Stop(), a second Start() with the same term continues the
    // existing session (documented as behaving like Find Next) instead of
    // searching from the top -- so Ctrl+F would silently advance the match
    // rather than reopen the bar on the first hit.
    //
    // This makes every Ctrl+F restart the session, which does discard a term
    // the user typed into the bar itself. That is the deliberate trade: the
    // runtime raises no "find bar closed" event, so a session flag cannot tell
    // an open bar from one the user dismissed with Escape -- and treating a
    // stale flag as "already open" would make Ctrl+F do nothing at all after
    // the bar was closed, which is the worse failure.
    find->Stop();
    m_findSessionActive = false;

    QPointer<PreviewWidget> guard(this);
    const HRESULT hr = find->Start(options.Get(),
        Callback<ICoreWebView2FindStartCompletedHandler>(
            [guard, find](HRESULT result) -> HRESULT {
                if (!guard)
                    return S_OK;
                if (FAILED(result))
                    guard->m_findSessionActive = false;
                // A restart that lands on the same count raises no change event.
                guard->d->reportDocumentFind(find.Get());
                return S_OK;
            }).Get());
    if (FAILED(hr))
        return false;

    m_findSessionActive = true;
    return true;
}

bool PreviewWidget::findNextMatch()
{
    if (!m_findSessionActive)
        return false;
    ComPtr<ICoreWebView2Find> find = d ? d->findApi() : nullptr;
    return find && SUCCEEDED(find->FindNext());
}

bool PreviewWidget::findPreviousMatch()
{
    if (!m_findSessionActive)
        return false;
    ComPtr<ICoreWebView2Find> find = d ? d->findApi() : nullptr;
    return find && SUCCEEDED(find->FindPrevious());
}

void PreviewWidget::stopFind()
{
    if (!m_findSessionActive)
        return;
    m_findSessionActive = false;
    if (ComPtr<ICoreWebView2Find> find = d ? d->findApi() : nullptr)
        find->Stop();
}

void PreviewWidget::prepareForReparent()
{
    if (d)
        d->closeForReparent();
}

void PreviewWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (d) d->resize();
}

void PreviewWidget::setBodyHtml(const QString& html, const QString& preparedHtml)
{
    // Tab activation asks the active preview to refresh even when this page's
    // rendered body has not changed. Avoid replacing the shell DOM in that
    // case: the replacement invalidates the full layout and is particularly
    // visible while Mermaid yields asynchronously. A base-path change remains
    // a real refresh because it can change relative image resolution.
    const bool sameBody = m_bodyHtml == html;
    if (m_readOnlyFilePath.isEmpty() && !m_bodyNeedsRefresh && sameBody)
        return;
    // The same body keeps its preparation and block split.
    if (sameBody && !m_preparedBodyHtml.isNull())
        adoptBody(html, m_preparedBodyHtml, m_bodyBlocks, QString());
    else
        adoptBody(html, preparedHtml, nullptr, QString());
}

void PreviewWidget::adoptBody(const QString& html, const QString& prepared,
                              PreviewBlocksPtr blocks, const QString& fullFields)
{
    if (!m_readOnlyFilePath.isEmpty()) {
        setReadOnlyFilePath(QString());
        if (d && d->webview)
            d->navigateCurrentContent();
    }

    m_bodyHtml = html;
    m_preparedBodyHtml = prepared;
    m_bodyBlocks = std::move(blocks);
    m_fullFields = fullFields;
    m_bodyNeedsRefresh = false;
    if (!html.isEmpty())
        m_initializationRequested = true;
    updateBrowserAccelerators();
    pushContent();
}

void PreviewWidget::applyBodyUpdate(const PreviewBodyUpdate& update)
{
    // The patch describes the step from update.base to this body, so it is
    // only meaningful while update.base is still exactly what the page holds.
    // Anything else -- a reload, a reparent, an intervening whole-body push, a
    // read-only file or mind map in between -- sends the whole body instead.
    const bool patchable = update.hasPatch && update.base && update.base == m_postedBlocks
        && m_readOnlyFilePath.isEmpty() && !m_bodyNeedsRefresh && !isMindMap()
        && d && d->shellReady;
    if (!patchable) {
        if (m_readOnlyFilePath.isEmpty() && !m_bodyNeedsRefresh && m_bodyHtml == update.html)
            return;
        adoptBody(update.html, update.prepared, update.blocks, update.fullFields);
        return;
    }

    m_bodyHtml = update.html;
    m_preparedBodyHtml = update.prepared;
    m_bodyBlocks = update.blocks;
    m_fullFields.clear();
    if (!update.html.isEmpty())
        m_initializationRequested = true;
    m_postedBlocks = update.blocks;
    // Nothing on the page would change (a tab activation re-convert, an edit
    // that renders the same): same as setBodyHtml() skipping an equal body.
    if (update.patchEmpty)
        return;

    m_refreshing = true;
    const long long seq = ++d->seq;
    d->postRaw(QStringLiteral("{\"type\":\"patch\",\"seq\":%1,\"base\":%2,")
                   .arg(seq).arg(m_postedSeq) + update.patchFields);
    m_postedSeq = seq;
}

void PreviewWidget::setPresentation(Presentation presentation)
{
    if (m_presentation == presentation)
        return;
    m_presentation = presentation;
    m_bodyNeedsRefresh = true;
    m_initializationRequested = true;
    if (!m_readOnlyFilePath.isEmpty())
        return;
    // Only re-navigate an already-live shell. Creating one is deliberately not
    // done here: session restore calls this for every restored tab, and WebView2
    // creation must stay on the deferred, m_firstPainted-gated path in
    // MainWindow::updatePreview() so a restored mind-map tab cannot pull a
    // browser process onto the startup path.
    if (d && d->webview)
        d->navigateCurrentContent();
}

void PreviewWidget::setMindMapMarkdown(const QString& markdown)
{
    if (m_mindMapMarkdown == markdown && !m_bodyNeedsRefresh)
        return;
    m_mindMapMarkdown = markdown;
    m_bodyNeedsRefresh = false;
    m_initializationRequested = true;
    pushContent();
}

void PreviewWidget::requestMindMapSvg()
{
    if (!d || !d->shellReady || !isMindMap())
        return;
    QJsonObject msg;
    msg["type"] = "mindMapSvg";
    d->postJson(msg);
}

void PreviewWidget::fitMindMap()
{
    if (!d || !d->shellReady || !isMindMap())
        return;
    QJsonObject msg;
    msg["type"] = "mindMapFit";
    d->postJson(msg);
}

void PreviewWidget::refreshPreview()
{
    m_bodyNeedsRefresh = true;
    if (d && d->webview)
        d->navigateCurrentContent();
    else
        ensureInitialized();
}

void PreviewWidget::setBasePath(const QString& path)
{
    if (m_basePath == path)
        return;
    m_basePath = path;
    m_bodyNeedsRefresh = true;
    if (d && d->webview) {
        d->applyDocMapping();
        if (m_readOnlyFilePath.isEmpty() && d->shellReady)
            d->navigateCurrentContent();
    }
}

void PreviewWidget::showReadOnlyFile(const QString& path)
{
    setReadOnlyFilePath(path);
    m_initializationRequested = !path.isEmpty();
    updateBrowserAccelerators();
    if (d && d->webview)
        d->navigateCurrentContent();
    else
        ensureInitialized();
}

void PreviewWidget::printReadOnlyFile()
{
    if (!m_readOnlyFilePath.isEmpty() && d)
        d->showPrintUi();
}

// The viewer kind is resolved once per opened file rather than on demand: the
// WebView2 accelerator-key and wheel handlers consult it on every keystroke and
// every wheel notch, and re-deriving it there built two QFileInfo objects per
// call for an answer that only changes when the open document changes.
void PreviewWidget::setReadOnlyFilePath(const QString& path)
{
    m_readOnlyFilePath = path;
    const QString suffix = QFileInfo(path).suffix();
    m_readOnlyKind = suffix.compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0
                  || suffix.compare(QStringLiteral("htm"), Qt::CaseInsensitive) == 0
        ? ReadOnlyKind::Html
        : suffix.compare(QStringLiteral("svg"), Qt::CaseInsensitive) == 0
            ? ReadOnlyKind::Svg
            : ReadOnlyKind::Other;
}

namespace {
constexpr double kHtmlZoomMinimum = 0.25;
constexpr double kHtmlZoomMaximum = 5.0;
constexpr double kHtmlZoomStep = 0.1;
}

void PreviewWidget::htmlZoomIn()
{
    if (!d || !d->controller || !isReadOnlyDocumentViewer()) return;
    double zoom = 1.0;
    if (SUCCEEDED(d->controller->get_ZoomFactor(&zoom)))
        d->controller->put_ZoomFactor(qMin(kHtmlZoomMaximum, zoom + kHtmlZoomStep));
}

void PreviewWidget::htmlZoomOut()
{
    if (!d || !d->controller || !isReadOnlyDocumentViewer()) return;
    double zoom = 1.0;
    if (SUCCEEDED(d->controller->get_ZoomFactor(&zoom)))
        d->controller->put_ZoomFactor(qMax(kHtmlZoomMinimum, zoom - kHtmlZoomStep));
}

void PreviewWidget::htmlZoomByWheel(double deltaY)
{
    if (!d || !d->controller || !isReadOnlyDocumentViewer() || qFuzzyIsNull(deltaY)) return;
    double zoom = 1.0;
    if (SUCCEEDED(d->controller->get_ZoomFactor(&zoom))) {
        // Roughly 5% per ordinary 120-unit wheel notch, while preserving the
        // fine-grained deltas generated by precision touchpads.
        const double factor = std::pow(1.0004, -deltaY);
        d->controller->put_ZoomFactor(qBound(kHtmlZoomMinimum, zoom * factor, kHtmlZoomMaximum));
    }
}

void PreviewWidget::resetHtmlZoom()
{
    if (d && d->controller && isReadOnlyDocumentViewer())
        d->controller->put_ZoomFactor(1.0);
}

void PreviewWidget::updateBrowserAccelerators()
{
    if (!d || !d->webview) return;
    ComPtr<ICoreWebView2Settings> settings;
    ComPtr<ICoreWebView2Settings3> settings3;
    if (SUCCEEDED(d->webview->get_Settings(&settings)) && settings) {
        // Markdown content supplies its own Qt context menu through the preview
        // shell. Standalone HTML/SVG does not, so retain WebView2's native menu
        // as a fallback when ContextMenuRequested is unavailable.
        settings->put_AreDefaultContextMenusEnabled(isReadOnlyDocumentViewer());
        settings->put_IsZoomControlEnabled(FALSE);
    }
    if (settings && SUCCEEDED(settings.As(&settings3)) && settings3) {
        settings3->put_AreBrowserAcceleratorKeysEnabled(isReadOnlyDocumentViewer());
    }
}

void PreviewWidget::setDarkMode(bool dark)
{
    m_dark = dark;

    QPalette pal = palette();
    pal.setColor(QPalette::Window, dark ? DarkTheme::color(DarkTheme::Editor) : QColor(0xff, 0xff, 0xff));
    setPalette(pal);

    if (d && d->controller)
        d->applyBackgroundColor();
    if (d && d->webview)
        d->applyPreferredColorScheme();
    pushTheme();
}

void PreviewWidget::setBaseFontSize(int size)
{
    m_baseFontSize = size;
    pushTheme();
}

void PreviewWidget::setBodyFontFamily(const QString& family)
{
    // Only the name is kept here: this runs as every tab is created, and the
    // stack (with its font database lookup) is built when it is posted.
    const QString name = FontPreferences::normalizedFamily(family);
    if (name == m_bodyFontFamily)
        return;
    m_bodyFontFamily = name;
    pushBodyFont();
}

void PreviewWidget::setDocumentSpacing(const DocumentSpacing::Settings& spacing)
{
    const DocumentSpacing::Settings normalized = DocumentSpacing::normalized(spacing);
    if (normalized == m_documentSpacing)
        return;
    m_documentSpacing = normalized;
    pushDocumentSpacing();
}

void PreviewWidget::setOutlinePopupOpen(bool open)
{
    if (!d || !d->shellReady) return;
    QJsonObject msg;
    msg["type"] = "outlinePopupOpen";
    msg["open"] = open;
    d->postJson(msg);
}

QRect PreviewWidget::outlineButtonGeometry() const
{
    // Mirrors #outline-btn's CSS: top/right 24px, height 40px, expanded
    // width 118px (see resources/preview/preview.css).
    const int padding = 24, height = 40, expandedWidth = 118;
    const QPoint topLeft = mapToGlobal(QPoint(width() - expandedWidth - padding, padding));
    return QRect(topLeft, QSize(expandedWidth, height));
}

// Convert the raw MD4C body (KaTeX mode preserves LaTeX; browser renders it) for
// browser display: turn the export page-break div into the visual separator and
// point relative/local image sources at virtual hosts. Absolute Windows paths
// use their drive-root host because an HTTPS shell cannot load file: resources.
QString PreviewWidget::prepareBodyForBrowser(QString body)
{
    // The editor inserts this raw-HTML marker. Match the element rather than
    // one byte-for-byte serialization: MD4C/preview source mapping can preserve
    // harmless whitespace or attribute formatting changes around it.
    static const QRegularExpression pageBreakRe(
        QStringLiteral(R"(<div\b(?=[^>]*\bbreak-after\s*:\s*page\s*;?)[^>]*>\s*</div\s*>)"),
        QRegularExpression::CaseInsensitiveOption);
    body.replace(pageBreakRe,
                 QStringLiteral("<div class=\"fastmd-page-break\">Page Break</div>"));

    static const QRegularExpression rxImg(
        QStringLiteral(R"RX(<img\b[^>]*\bsrc\s*=\s*"([^"]*)")RX"),
        QRegularExpression::CaseInsensitiveOption);
    QString out;
    out.reserve(body.size());
    int last = 0;
    auto it = rxImg.globalMatch(body);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int srcStart = m.capturedStart(1);
        const int srcEnd   = m.capturedEnd(1);
        const QString src  = m.captured(1);
        out += body.mid(last, srcStart - last);

        QString mapped = src;
        const QString lower = src.toLower();
        if (lower.startsWith("http://") || lower.startsWith("https://") ||
            lower.startsWith("data:") || src.startsWith('#')) {
            // leave as-is
        } else if (lower.startsWith("file:")) {
            const QUrl fileUrl(src);
            const QString localPath = fileUrl.isLocalFile()
                ? QDir::fromNativeSeparators(fileUrl.toLocalFile()) : QString();
            if (localPath.size() >= 3 && localPath.at(1) == QLatin1Char(':')
                && (localPath.at(2) == QLatin1Char('/') || localPath.at(2) == QLatin1Char('\\'))) {
                mapped = QStringLiteral("https://fastmd.drive-%1/").arg(localPath.at(0).toLower())
                    + QString(QUrl::toPercentEncoding(localPath.mid(3), "/"));
            }
        } else if (QDir::isAbsolutePath(src)) {
            const QString absoluteSrc = QDir::fromNativeSeparators(src);
            if (absoluteSrc.size() >= 3 && absoluteSrc.at(1) == QLatin1Char(':')
                && absoluteSrc.at(2) == QLatin1Char('/')) {
                mapped = QStringLiteral("https://fastmd.drive-%1/").arg(absoluteSrc.at(0).toLower())
                    + QString(QUrl::toPercentEncoding(absoluteSrc.mid(3), "/"));
            } else {
                mapped = QUrl::fromLocalFile(absoluteSrc).toString();
            }
        } else {
            // Relative path -> serve from the document folder virtual host.
            QString rel = src;
            rel.replace('\\', '/');
            while (rel.startsWith("./")) rel = rel.mid(2);
            mapped = QStringLiteral("https://fastmd.doc/") + QString(QUrl::toPercentEncoding(rel, "/"));
        }
        out += mapped;
        last = srcEnd;
    }
    out += body.mid(last);
    return out;
}

void PreviewWidget::pushContent()
{
    if (!d || !d->shellReady)
        return; // buffered; sent on 'ready'
    m_refreshing = true;
    if (isMindMap()) {
        QJsonObject mapMessage;
        mapMessage["type"] = "content";
        mapMessage["markdown"] = m_mindMapMarkdown;
        mapMessage["seq"] = double(++d->seq);
        d->postJson(mapMessage);
        return;
    }
    if (m_preparedBodyHtml.isNull())
        m_preparedBodyHtml = prepareBodyForBrowser(m_bodyHtml);
    if (m_fullFields.isNull()) {
        m_fullFields = PreviewPatch::contentFields(
            m_preparedBodyHtml, m_bodyBlocks ? m_bodyBlocks->count() : -1);
    }
    const long long seq = ++d->seq;
    d->postRaw(QStringLiteral("{\"type\":\"content\",\"seq\":%1,").arg(seq) + m_fullFields);
    m_fullFields = QString();
    // "blocks" in the message lets the page confirm the split; a page that
    // disagrees says so ('blocksMismatch') and patching stops for this body.
    m_postedBlocks = m_bodyBlocks;
    m_postedSeq = seq;
    m_lastFullSeq = seq;
}

void PreviewWidget::pushTheme()
{
    if (!d || !d->shellReady)
        return;
    QJsonObject msg;
    msg["type"] = "theme";
    msg["dark"] = m_dark;
    msg["fs"]   = m_baseFontSize;
    d->postJson(msg);
}

void PreviewWidget::pushBodyFont()
{
    // The mind-map shell keeps its own font, and a read-only viewer has no shell.
    if (!d || !d->shellReady || isMindMap() || !m_readOnlyFilePath.isEmpty())
        return;
    const QString vietnameseRange = FontPreferences::missingVietnameseRange(m_bodyFontFamily);
    QJsonObject msg;
    msg["type"] = "font";
    msg["family"] = FontPreferences::bodyFontStack(m_bodyFontFamily, !vietnameseRange.isEmpty());
    msg["faces"] = FontPreferences::vietnameseFallbackFaces(vietnameseRange);
    d->postJson(msg);
}

void PreviewWidget::pushDocumentSpacing()
{
    // Same exclusions as the font: the mind-map shell has its own layout and a
    // read-only viewer has no shell.
    if (!d || !d->shellReady || isMindMap() || !m_readOnlyFilePath.isEmpty())
        return;
    QJsonObject msg;
    msg["type"] = "spacing";
    msg["lh"] = DocumentSpacing::lineValue(m_documentSpacing);
    msg["pspace"] = DocumentSpacing::paragraphValue(m_documentSpacing);
    msg["ligap"] = DocumentSpacing::listValue(m_documentSpacing);
    d->postJson(msg);
}

void PreviewWidget::setScrollFraction(double f)
{
    const double bounded = qBound(0.0, f, 1.0);
    // Each post is a cross-process WebView2 message; a scroll that lands on the
    // same fraction (a horizontal scroll, a clamped edge, or an echo of a
    // preview-initiated scroll) has nothing to tell the page.
    const bool unchanged = m_scrollFractionPosted
        && qFuzzyCompare(bounded + 1.0, m_lastPostedScrollFraction + 1.0);
    m_lastScrollFraction = bounded;
    if (!d || !d->shellReady) return;
    if (unchanged) return;
    m_lastPostedScrollFraction = bounded;
    m_scrollFractionPosted = true;
    QJsonObject msg;
    msg["type"] = "scrollToFraction";
    msg["fraction"] = m_lastScrollFraction;
    d->postJson(msg);
}

void PreviewWidget::smoothScrollToFraction(double f)
{
    m_lastScrollFraction = qBound(0.0, f, 1.0);
    if (!d || !d->shellReady) return;
    m_lastPostedScrollFraction = m_lastScrollFraction;
    m_scrollFractionPosted = true;
    m_refreshing = true;
    QJsonObject msg;
    msg["type"] = "scrollToFraction";
    msg["fraction"] = m_lastScrollFraction;
    msg["smooth"] = true;
    d->postJson(msg);
}

void PreviewWidget::scrollToAnchor(const QString& id)
{
    if (!d || !d->shellReady) return;
    QJsonObject msg;
    msg["type"] = "scrollToAnchor";
    msg["id"]   = id;
    d->postJson(msg);
}

void PreviewWidget::scrollToSourceLine(int line, bool flash)
{
    if (!d || !d->shellReady) return;
    QJsonObject msg;
    msg["type"] = "scrollToSourceLine";
    msg["line"] = line;
    msg["flash"] = flash;
    d->postJson(msg);
}

void PreviewWidget::handleWebMessage(const QString& json)
{
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    const QString type = o.value("type").toString();
    if (type == "ready") {
        static bool s_readyMarked = false;
        if (!s_readyMarked) {
            s_readyMarked = true;
            FASTMD_MARK("webview2 preview ready");
        }
        d->shellReady = true;
        pushTheme();
        // A fresh page holds the stylesheet's font; restyle it before the body
        // arrives, so the first layout of the content is already the final one.
        if (!m_bodyFontFamily.isEmpty())
            pushBodyFont();
        if (!m_documentSpacing.isDefault())
            pushDocumentSpacing();
        pushContent();
    } else if (type == "resync") {
        // The page refused a patch: its body is not the one the patch was
        // computed against. Answer with the whole body unless a whole-body
        // push already followed the refused patch.
        if (o.value("seq").toDouble() > double(m_lastFullSeq) && !isMindMap()
            && m_readOnlyFilePath.isEmpty()) {
            m_postedBlocks.reset();
            pushContent();
        }
    } else if (type == "blocksMismatch") {
        // The browser parsed that body into other top-level elements than the
        // C++ split found. Its DOM is right; only patching against it is not.
        if (o.value("seq").toDouble() == double(m_postedSeq))
            m_postedBlocks.reset();
    } else if (type == "rendered") {
        m_refreshing = false;
        emit previewRendered();
    } else if (type == "scrollRestoreFinished") {
        m_refreshing = false;
    } else if (type == "scroll") {
        m_lastScrollFraction = o.value("fraction").toDouble();
        // The page moved on its own, so the last fraction we posted no longer
        // describes where it is: the next host-side request must be sent.
        m_scrollFractionPosted = false;
        if (!m_refreshing)
            emit scrolled(m_lastScrollFraction);
    } else if (type == "openExternal") {
        const QString url = o.value("url").toString();
        if (!url.isEmpty())
            emit linkRequested(url);
    } else if (type == "linkHover") {
        emit linkHoverChanged(o.value("url").toString());
    } else if (type == "zoom") {
        emit zoomRequested(o.value("delta").toInt());
    } else if (type == "htmlViewerZoom") {
        htmlZoomByWheel(o.value("deltaY").toDouble());
    } else if (type == "outlineClicked") {
        emit outlineRequested();
    } else if (type == "previewFlash") {
        const int line = o.value("line").toInt();
        if (line > 0)
            emit previewFlashRequested(line);
    } else if (type == "mindMapSource") {
        const int line = o.value("line").toInt();
        if (line > 0)
            emit previewFlashRequested(line);
    } else if (type == "mindMapSvg") {
        const QString svg = o.value("svg").toString();
        if (!svg.isEmpty())
            emit mindMapSvgReady(svg);
    } else if (type == "taskToggle") {
        const int start = o.value("start").toInt();
        const int end = o.value("end").toInt();
        if (start > 0 && end >= start)
            emit taskToggleRequested(start, end, o.value("checked").toBool());
    } else if (type == "focusEditor") {
        emit editorFocusRequested();
    } else if (type == "contextMenu") {
        const int x = static_cast<int>(o.value("x").toDouble());
        const int y = static_cast<int>(o.value("y").toDouble());
        const bool hasSelection = o.value("hasSelection").toBool();
        const int sourceLine = o.value("sourceLine").toInt();
        emit contextMenuRequested(mapToGlobal(QPoint(x, y)), hasSelection, sourceLine,
                                  o.value("mermaidSvg").toString(),
                                  o.value("mermaidPngDataUrl").toString());
    }
}

void PreviewWidget::showFallback(const QString& message)
{
    // WebView2 unavailable: keep the editor usable and explain inside the pane.
    if (m_fallbackLabel)
        return;
    m_fallbackLabel = new QLabel(this);
    m_fallbackLabel->setWordWrap(true);
    m_fallbackLabel->setAlignment(Qt::AlignCenter);
    m_fallbackLabel->setMargin(28);
    m_fallbackLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_fallbackLabel->setText(message);
    const QString fg = m_dark ? QStringLiteral("#c9d1d9") : QStringLiteral("#24292f");
    m_fallbackLabel->setStyleSheet(QStringLiteral("QLabel{color:%1;font-size:15px;}").arg(fg));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(m_fallbackLabel);
    m_fallbackLabel->show();
    // Rendering is "done" (nothing to render) — unblock the benchmark harness.
    m_refreshing = false;
    emit previewRendered();
}

void PreviewWidget::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y();
        if (isReadOnlyDocumentViewer()) {
            if (delta > 0) htmlZoomIn();
            else if (delta < 0) htmlZoomOut();
        } else if (delta > 0) {
            emit zoomRequested(1);
        } else if (delta < 0) {
            emit zoomRequested(-1);
        }
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void PreviewWidget::zoomIn(int range)
{
    m_baseFontSize += range;
    pushTheme();
}

void PreviewWidget::zoomOut(int range)
{
    m_baseFontSize -= range;
    if (m_baseFontSize < FastMdDefaults::MinimumPreviewFontSize)
        m_baseFontSize = FastMdDefaults::MinimumPreviewFontSize;
    pushTheme();
}

void PreviewWidget::copy()
{
    if (!d) return;
    d->execScript(QStringLiteral("document.execCommand('copy');"));
}

void PreviewWidget::selectAll()
{
    if (!d) return;
    if (isReadOnlyDocumentViewer()) {
        d->execScript(QStringLiteral("document.execCommand('selectAll');"));
        return;
    }
    d->execScript(QStringLiteral(
        "(function(){"
        "var c=document.getElementById('content');if(!c)return;"
        "var s=window.getSelection();var r=document.createRange();"
        "r.selectNodeContents(c);s.removeAllRanges();s.addRange(r);"
        "})();"));
}

void PreviewWidget::copyMermaidImage(const QString& pngDataUrl)
{
    const QImage image = imageFromPngDataUrl(pngDataUrl);
    if (!image.isNull())
        QApplication::clipboard()->setImage(image);
}

void PreviewWidget::copyMermaidSvg(const QString& svg)
{
    if (!svg.isEmpty())
        QApplication::clipboard()->setText(svg);
}

void PreviewWidget::saveMermaidImageAs(const QString& pngDataUrl)
{
    const QImage image = imageFromPngDataUrl(pngDataUrl);
    if (image.isNull())
        return;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Image As"), QString(), tr("PNG Image (*.png)"));
    if (path.isEmpty())
        return;
    image.save(savePathWithSuffix(std::move(path), QStringLiteral(".png")), "PNG");
}

void PreviewWidget::saveMermaidSvgAs(const QString& svg)
{
    if (svg.isEmpty())
        return;

    QString path = QFileDialog::getSaveFileName(
        this, tr("Save SVG As"), QString(), tr("SVG Image (*.svg)"));
    if (path.isEmpty())
        return;

    QFile file(savePathWithSuffix(std::move(path), QStringLiteral(".svg")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(svg.toUtf8());
}

void PreviewWidget::saveMindMapSvgAs(const QString& svg, const QString& initialPath)
{
    if (svg.isEmpty())
        return;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Mind Map as SVG"), initialPath, tr("SVG Image (*.svg)"));
    if (path.isEmpty())
        return;
    QSaveFile file(savePathWithSuffix(std::move(path), QStringLiteral(".svg")));
    if (file.open(QIODevice::WriteOnly) && file.write(svg.toUtf8()) >= 0)
        file.commit();
}
