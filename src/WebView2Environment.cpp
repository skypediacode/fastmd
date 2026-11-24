#include "WebView2Environment.h"
#include "StartupProfiler.h"

#include <QDir>
#include <QList>
#include <QStandardPaths>
#include <QStringLiteral>

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

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

struct EnvState {
    ComPtr<ICoreWebView2Environment> env;
    bool creating = false;
    bool failed   = false;
    bool shuttingDown = false;
    QList<std::function<void(ICoreWebView2Environment*)>> waiters;
};

EnvState& envState()
{
    static EnvState s;
    return s;
}

} // namespace

namespace WebView2Environment {

void requestEnvironment(std::function<void(ICoreWebView2Environment*)> cb)
{
    EnvState& s = envState();
    if (s.shuttingDown) { cb(nullptr); return; }
    if (s.env) { cb(s.env.Get()); return; }
    if (s.failed) { cb(nullptr); return; }

    s.waiters.append(std::move(cb));
    if (s.creating)
        return;
    s.creating = true;

    // Writable user-data folder (the exe dir may be read-only, e.g. Program Files).
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDir.isEmpty())
        dataDir = QDir::tempPath() + QStringLiteral("/FastMD");
    dataDir += QStringLiteral("/WebView2");
    QDir().mkpath(dataDir);
    const std::wstring userData = QDir::toNativeSeparators(dataDir).toStdWString();

    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT result, ICoreWebView2Environment* created) -> HRESULT {
                EnvState& st = envState();
                st.creating = false;
                if (st.shuttingDown) {
                    st.waiters.clear();
                    return S_OK;
                }
                if (SUCCEEDED(result) && created) {
                    st.env = created;
                    FASTMD_MARK("webview2 environment ready");
                } else {
                    st.failed = true;
                }
                auto waiters = st.waiters;
                st.waiters.clear();
                for (auto& w : waiters)
                    w(st.env.Get());
                return S_OK;
            }).Get());

    if (FAILED(hr)) {
        s.creating = false;
        s.failed = true;
        auto waiters = s.waiters;
        s.waiters.clear();
        for (auto& w : waiters)
            w(nullptr);
    }
}

ICoreWebView2Environment* existing()
{
    return envState().env.Get();
}

void warm()
{
    requestEnvironment([](ICoreWebView2Environment*) {});
}

void shutdown()
{
    EnvState& state = envState();
    state.shuttingDown = true;
    state.waiters.clear();
    state.env.Reset();
}

} // namespace WebView2Environment
