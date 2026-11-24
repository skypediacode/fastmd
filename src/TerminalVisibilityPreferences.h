#pragma once

#include <QSettings>

// The Terminal's visible/hidden choice is independent from the general window
// and session settings sweep.  Keeping its key and accessors together makes
// that ownership explicit and gives the focused regression test a small,
// platform-neutral seam.
namespace TerminalVisibilityPreferences {

inline constexpr auto Key = "terminal/visible";

inline bool read(const QVariant& value)
{
    return value.isValid() && value.toBool();
}

inline bool read(QSettings& settings)
{
    return read(settings.value(QLatin1String(Key)));
}

inline void write(QSettings& settings, bool visible)
{
    settings.setValue(QLatin1String(Key), visible);
}

} // namespace TerminalVisibilityPreferences
