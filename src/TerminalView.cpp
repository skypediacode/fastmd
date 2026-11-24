#include "TerminalView.h"
#include "ThemePalette.h"
#include "WebView2Environment.h"
#include "StartupProfiler.h"
#include "CommandShortcutRegistry.h"
#include "NativeWindowCursorSync.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>
#include <wrl.h>
#include "WebView2.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {
const wchar_t* kTerminalOrigin = L"https://fastmd.terminal/";

void openExternalUrl(const QString& value)
{
    const QUrl url(value);
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")
        || scheme == QLatin1String("mailto"))
        QDesktopServices::openUrl(url);
}
}

// ---------------------------------------------------------------------------
// TerminalView::Impl -- WebView2 controller + event plumbing.
// ---------------------------------------------------------------------------
class TerminalView::Impl {
public:
    explicit Impl(TerminalView* owner) : q(owner) {}

    TerminalView* q;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2>           webview;
    HWND hwnd = nullptr;
    // Guards every async callback: a callback that started before a teardown
    // must not touch the view that replaced it.
    quint64 hostGeneration = 0;

    static QString environmentErrorMessage()
    {
        return TerminalView::tr(
            "The Microsoft Edge WebView2 Runtime could not be started, so the "
            "integrated terminal is unavailable.\n\n"
            "Editing still works normally. To restore the terminal, install the "
            "Evergreen WebView2 Runtime from Microsoft and restart FastMD.");
    }

    void start()
    {
        hwnd = reinterpret_cast<HWND>(q->winId());
        NativeWindowCursorSync::schedule(q);
        QPointer<TerminalView> guard(q);
        const quint64 generation = ++hostGeneration;
        WebView2Environment::requestEnvironment(
            [this, guard, generation](ICoreWebView2Environment* env) {
                if (!guard || generation != hostGeneration) return;
                if (!env) { q->showFallback(environmentErrorMessage()); return; }
                createController(env, generation);
            });
    }

    void createController(ICoreWebView2Environment* env, quint64 generation)
    {
        QPointer<TerminalView> guard(q);
        env->CreateCoreWebView2Controller(hwnd,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this, guard, generation](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                    if (!guard) return S_OK;
                    if (generation != hostGeneration) {
                        if (ctrl) ctrl->Close();
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

    void applyBackgroundColor()
    {
        ComPtr<ICoreWebView2Controller2> controller2;
        if (!controller || FAILED(controller.As(&controller2)) || !controller2)
            return;
        // The Terminal is a secondary region: it shares the application
        // surface rather than the deeper document color. terminal.css --bg
        // and terminal.js's xterm theme must match.
        const QColor surface = ThemePalette::color(ThemePalette::Surface, q->m_dark);
        const COREWEBVIEW2_COLOR color{ 255, BYTE(surface.red()), BYTE(surface.green()),
                                        BYTE(surface.blue()) };
        controller2->put_DefaultBackgroundColor(color);
    }

    void setup()
    {
        applyBackgroundColor();

        if (ComPtr<ICoreWebView2Settings> settings;
            SUCCEEDED(webview->get_Settings(&settings)) && settings) {
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(FALSE);
            settings->put_IsZoomControlEnabled(FALSE);
            // The inverse of the Markdown preview's policy, and the whole point
            // of a terminal: with browser accelerators off, Chromium never
            // claims Ctrl+P/Ctrl+S/Ctrl+R/F5, so those key events reach the
            // page's own handler and go to the shell. Page keydown still fires
            // normally -- this flag only disables the *browser's* shortcuts.
            if (ComPtr<ICoreWebView2Settings3> settings3;
                SUCCEEDED(settings.As(&settings3)) && settings3)
                settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
        }

        // Serve the shell + xterm.js from disk, fully offline.
        if (ComPtr<ICoreWebView2_3> wv3; SUCCEEDED(webview.As(&wv3)) && wv3) {
            const std::wstring terminalDir = QDir::toNativeSeparators(
                QCoreApplication::applicationDirPath() + QStringLiteral("/terminal")).toStdWString();
            wv3->SetVirtualHostNameToFolderMapping(
                L"fastmd.terminal", terminalDir.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        }

        installHandlers();
        resize();
        controller->put_IsVisible(TRUE);
        navigate();

        // Controller creation is fully async and the panel may have been
        // resized while it was in flight. A stale bounds here is not cosmetic:
        // it makes xterm compute the wrong cols/rows and the shell wraps at the
        // wrong column. Same ladder the preview uses.
        QPointer<TerminalView> guard(q);
        for (int delayMs : { 50, 200, 500, 1000, 2000 }) {
            QTimer::singleShot(delayMs, q, [this, guard]() {
                if (!guard) return;
                resize();
                applyBackgroundColor();
            });
        }
    }

    void navigate()
    {
        const QString url = QStringLiteral("https://fastmd.terminal/terminal.html?theme=%1&fs=%2")
                                .arg(q->m_dark ? QStringLiteral("dark") : QStringLiteral("light"))
                                .arg(q->m_fontSize);
        const std::wstring w = url.toStdWString();
        webview->Navigate(w.c_str());
    }

    void installHandlers()
    {
        QPointer<TerminalView> guard(q);
        EventRegistrationToken token{};

        webview->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this, guard](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    if (!guard) return S_OK;
                    LPWSTR source = nullptr;
                    const bool trusted = SUCCEEDED(args->get_Source(&source)) && source
                        && std::wstring(source).rfind(kTerminalOrigin, 0) == 0;
                    if (source)
                        CoTaskMemFree(source);
                    if (!trusted)
                        return S_OK;
                    LPWSTR raw = nullptr;
                    if (SUCCEEDED(args->get_WebMessageAsJson(&raw)) && raw) {
                        q->handleWebMessage(QString::fromWCharArray(raw));
                        CoTaskMemFree(raw);
                    }
                    return S_OK;
                }).Get(), &token);

        // Keep the privileged WebMessage bridge on FastMD's local shell. Links
        // may open in the user's browser, but no external page may replace the
        // terminal document and then send input to a ConPTY session.
        webview->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [guard](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                    if (!guard) return S_OK;
                    LPWSTR raw = nullptr;
                    if (FAILED(args->get_Uri(&raw)) || !raw)
                        return S_OK;
                    const QString uri = QString::fromWCharArray(raw);
                    const std::wstring wide(raw);
                    CoTaskMemFree(raw);
                    if (wide.rfind(kTerminalOrigin, 0) == 0 || wide.rfind(L"about:blank", 0) == 0)
                        return S_OK;
                    args->put_Cancel(TRUE);
                    openExternalUrl(uri);
                    return S_OK;
                }).Get(), &token);

        // The terminal owns almost every key combination -- that is what makes
        // it a terminal. Only the handful of window-level commands that would
        // otherwise be unreachable while it has focus are taken back.
        controller->add_AcceleratorKeyPressed(
            Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                [this, guard](ICoreWebView2Controller*,
                              ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                    if (!guard) return S_OK;
                    COREWEBVIEW2_KEY_EVENT_KIND kind;
                    if (FAILED(args->get_KeyEventKind(&kind))) return S_OK;
                    if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN
                        && kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)
                        return S_OK;
                    UINT vk = 0;
                    if (FAILED(args->get_VirtualKey(&vk))) return S_OK;

                    const bool ctrl  = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    const bool shift = (::GetKeyState(VK_SHIFT)   & 0x8000) != 0;
                    const bool alt   = (::GetKeyState(VK_MENU)    & 0x8000) != 0;

                    static const struct TerminalCommandMap {
                        CommandId id;
                        Command cmd;
                    } kTerminalCommands[] = {
                        { CommandId::ViewTerminal,          Command::ToggleTerminal },
                        { CommandId::TerminalIncreaseFont,  Command::IncreaseFontSize },
                        { CommandId::TerminalDecreaseFont,  Command::DecreaseFontSize },
                        { CommandId::TerminalResetFont,     Command::ResetFontSize },
                        { CommandId::TerminalRenameSession, Command::RenameSession },
                        { CommandId::ViewCommandPalette,    Command::CommandPalette },
                        { CommandId::ViewFocusFileSearch,   Command::WorkspaceSearch },
                        { CommandId::EditFindInFiles,       Command::FindInFiles },
                        { CommandId::EditReplaceInFiles,    Command::ReplaceInFiles },
                        { CommandId::ViewReadingMode,       Command::ReadingMode },
                        { CommandId::ViewNotepadMode,       Command::NotepadMode },
                    };

                    for (const auto& entry : kTerminalCommands) {
                        if (CommandShortcutRegistry::matchesNativeAccelerator(entry.id, ctrl, shift, alt, vk)) {
                            args->put_Handled(TRUE);
                            const Command sent = entry.cmd;
                            QMetaObject::invokeMethod(guard, [guard, sent]() {
                                if (guard) emit guard->commandRequested(sent);
                            }, Qt::QueuedConnection);
                            return S_OK;
                        }
                    }
                    return S_OK;
                }).Get(), &token);

        // The shell is a local page; nothing in it should ever navigate away or
        // open a window. Web links are handed to the OS instead.
        webview->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    args->put_Handled(TRUE);
                    LPWSTR raw = nullptr;
                    if (SUCCEEDED(args->get_Uri(&raw)) && raw) {
                        openExternalUrl(QString::fromWCharArray(raw));
                        CoTaskMemFree(raw);
                    }
                    return S_OK;
                }).Get(), &token);
    }

    void resize()
    {
        if (!controller || !hwnd) return;
        RECT rc;
        GetClientRect(hwnd, &rc);
        controller->put_Bounds(rc);
    }

    void postJson(const QJsonObject& object)
    {
        if (!webview || !q->m_shellReady) return;
        const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
        const std::wstring w = QString::fromUtf8(json).toStdWString();
        webview->PostWebMessageAsJson(w.c_str());
    }
};

// ---------------------------------------------------------------------------

TerminalView::TerminalView(QWidget* parent)
    : QWidget(parent), d(new Impl(this))
{
    // Host the WebView2 on a native child window of this widget.
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0xff, 0xff, 0xff));
    setPalette(pal);
}

TerminalView::~TerminalView()
{
    if (d) {
        if (d->controller)
            d->controller->Close();
        delete d;
        d = nullptr;
    }
}

void TerminalView::ensureInitialized()
{
    if (d && !d->hwnd && !m_fallbackLabel)
        d->start();
}

void TerminalView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    ensureInitialized();
    if (d) d->resize();
    QTimer::singleShot(0, this, [this] { if (d) d->resize(); });
}

void TerminalView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (d) d->resize();
}

void TerminalView::setDarkMode(bool dark)
{
    m_dark = dark;
    QPalette pal = palette();
    pal.setColor(QPalette::Window, ThemePalette::color(ThemePalette::Surface, dark));
    setPalette(pal);
    if (d) d->applyBackgroundColor();
    if (d) d->postJson({ { QStringLiteral("type"), QStringLiteral("theme") },
                         { QStringLiteral("dark"), dark },
                         { QStringLiteral("fs"), m_fontSize } });
}

void TerminalView::setFontSize(int pixelSize)
{
    m_fontSize = qBound(MinimumFontSize, pixelSize, MaximumFontSize);
    if (d) d->postJson({ { QStringLiteral("type"), QStringLiteral("theme") },
                         { QStringLiteral("dark"), m_dark },
                         { QStringLiteral("fs"), m_fontSize } });
}

void TerminalView::createTerminal(int sessionId)
{
    if (d) d->postJson({ { QStringLiteral("type"), QStringLiteral("create") },
                         { QStringLiteral("id"), sessionId } });
}

void TerminalView::destroyTerminal(int sessionId)
{
    if (d) d->postJson({ { QStringLiteral("type"), QStringLiteral("destroy") },
                         { QStringLiteral("id"), sessionId } });
}

void TerminalView::showTerminal(int sessionId)
{
    if (d) d->postJson({ { QStringLiteral("type"), QStringLiteral("show") },
                         { QStringLiteral("id"), sessionId } });
}

void TerminalView::writeOutput(int sessionId, const QByteArray& data)
{
    // Base64, not a decoded QString: ConPTY output is UTF-8 and a multi-byte
    // sequence can straddle two reads, so the page decodes with a streaming
    // TextDecoder instead.
    if (d) d->postJson({ { QStringLiteral("type"), QStringLiteral("output") },
                         { QStringLiteral("id"), sessionId },
                         { QStringLiteral("data"), QString::fromLatin1(data.toBase64()) } });
}

void TerminalView::clearTerminal(int sessionId)
{
    if (d) d->postJson({ { QStringLiteral("type"), QStringLiteral("clear") },
                         { QStringLiteral("id"), sessionId } });
}

void TerminalView::copySelection(int sessionId)
{
    if (d) d->postJson({ { QStringLiteral("type"), QStringLiteral("copy") },
                         { QStringLiteral("id"), sessionId } });
}

void TerminalView::pasteText(int sessionId, const QString& text)
{
    if (d) d->postJson({ { QStringLiteral("type"), QStringLiteral("paste") },
                         { QStringLiteral("id"), sessionId },
                         { QStringLiteral("text"), text } });
}

void TerminalView::selectAll(int sessionId)
{
    if (d) d->postJson({ { QStringLiteral("type"), QStringLiteral("selectAll") },
                         { QStringLiteral("id"), sessionId } });
}

void TerminalView::focusTerminal()
{
    // A Qt setFocus() alone never reaches a native WebView2 child window.
    if (d && d->controller)
        d->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

void TerminalView::handleWebMessage(const QString& json)
{
    const QJsonObject object = QJsonDocument::fromJson(json.toUtf8()).object();
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type.isEmpty())
        return;

    if (type == QLatin1String("ready")) {
        m_shellReady = true;
        FASTMD_MARK("terminal webview shell ready");
        setDarkMode(m_dark);
        emit shellReady();
        return;
    }
    if (type == QLatin1String("input")) {
        const int id = object.value(QStringLiteral("id")).toInt();
        const QByteArray data =
            QByteArray::fromBase64(object.value(QStringLiteral("data")).toString().toLatin1());
        emit inputReady(id, data);
        return;
    }
    if (type == QLatin1String("resize")) {
        emit resized(object.value(QStringLiteral("id")).toInt(),
                     object.value(QStringLiteral("cols")).toInt(),
                     object.value(QStringLiteral("rows")).toInt());
        return;
    }
    if (type == QLatin1String("copy")) {
        emit selectionCopyRequested(object.value(QStringLiteral("text")).toString());
        return;
    }
    if (type == QLatin1String("requestPaste")) {
        emit pasteRequested(object.value(QStringLiteral("id")).toInt());
        return;
    }
    if (type == QLatin1String("openLink")) {
        // Bounded here so an oversized message never reaches the resolver.
        constexpr qsizetype kMaxLinkMessage = 4096;
        const QString text = object.value(QStringLiteral("text")).toString();
        const QString cwd = object.value(QStringLiteral("cwd")).toString();
        if (!text.isEmpty() && text.size() <= kMaxLinkMessage && cwd.size() <= kMaxLinkMessage)
            emit linkActivated(object.value(QStringLiteral("id")).toInt(), text, cwd);
        return;
    }
}

void TerminalView::showFallback(const QString& message)
{
    if (m_fallbackLabel)
        return;
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    m_fallbackLabel = new QLabel(message, this);
    m_fallbackLabel->setObjectName(QStringLiteral("terminalFallbackLabel"));
    m_fallbackLabel->setWordWrap(true);
    m_fallbackLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_fallbackLabel);
}
