#pragma once

// Shared WebView2 environment for the whole process. Every WebView2 host in
// FastMD (the Markdown preview, the read-only viewers, the Terminal panel)
// goes through here so they all share one browser process family and one
// on-disk cache -- a second environment means a second Chromium cold start,
// which AGENTS.md records as a measured multi-second startup regression.
//
// Creation is asynchronous: callers register a continuation that fires with
// the environment, or with nullptr when the runtime is unavailable.

#include <functional>

struct ICoreWebView2Environment;

namespace WebView2Environment {

void requestEnvironment(std::function<void(ICoreWebView2Environment*)> callback);

// Non-blocking accessor: the environment if it is already built, else nullptr.
ICoreWebView2Environment* existing();

// Starts creation without waiting for it, so the first real user of a WebView2
// does not pay the cold start.
void warm();

// Called once after the event loop ends. Drops the environment and makes every
// later request fail fast instead of resurrecting Chromium during shutdown.
void shutdown();

} // namespace WebView2Environment
