#pragma once

#include <QSettings>

// The destructive close-all prompt is opt-out. Keep this separate from the
// panel visibility preference: hiding a terminal must never alter sessions.
namespace TerminalCloseAllPreferences {

inline constexpr auto Key = "terminal/confirmCloseAll";

inline bool confirmationEnabled(QSettings& settings)
{
    return settings.value(QString::fromLatin1(Key), true).toBool();
}

inline void setConfirmationEnabled(QSettings& settings, bool enabled)
{
    settings.setValue(QString::fromLatin1(Key), enabled);
}

// A lone terminal has no meaningful "all" operation to confirm. Keep this
// decision beside the opt-out preference so it remains independently testable
// from the WebView2-backed terminal UI.
inline bool shouldConfirmCloseAll(int sessionCount, QSettings& settings)
{
    return sessionCount >= 2 && confirmationEnabled(settings);
}

} // namespace TerminalCloseAllPreferences
