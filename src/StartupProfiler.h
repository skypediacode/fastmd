#pragma once
// ---------------------------------------------------------------------------
// StartupProfiler — zero-cost-when-disabled startup timing.
//
// Enable at runtime by setting the environment variable FASTMD_STARTUP_PROFILE
// to any non-empty value, e.g.:
//
//     $env:FASTMD_STARTUP_PROFILE = "1"; .\FastMD.exe
//
// When disabled (the default) every mark() is a single atomic-free bool test
// plus an early return, so there is no measurable impact on real launches.
//
// Each mark() prints "<elapsed since main()>  +<delta since previous>  label"
// to stderr. The clock starts the moment StartupProfiler::init() is called,
// which should be the very first statement in main().
// ---------------------------------------------------------------------------
#include <QElapsedTimer>
#include <QByteArray>
#include <QString>
#include <cstdio>

namespace StartupProfiler {

inline QElapsedTimer& timer()
{
    static QElapsedTimer t;
    return t;
}

inline bool& enabledRef()
{
    static bool enabled = false;
    return enabled;
}

inline qint64& lastRef()
{
    static qint64 last = 0;
    return last;
}

// Call once, first thing in main().
inline void init()
{
    enabledRef() = !qEnvironmentVariableIsEmpty("FASTMD_STARTUP_PROFILE");
    if (!enabledRef())
        return;
    timer().start();
    lastRef() = 0;
    std::fprintf(stderr, "[startup]   total      delta   phase\n");
}

inline void mark(const char* label)
{
    if (!enabledRef())
        return;
    const qint64 now = timer().elapsed();
    const qint64 delta = now - lastRef();
    lastRef() = now;
    std::fprintf(stderr, "[startup] %7lld ms  +%6lld   %s\n",
                 static_cast<long long>(now), static_cast<long long>(delta), label);
    std::fflush(stderr);
}

inline bool enabled() { return enabledRef(); }

} // namespace StartupProfiler

// Convenience macro keeps call sites terse and self-documenting.
#define FASTMD_MARK(label) ::StartupProfiler::mark(label)
