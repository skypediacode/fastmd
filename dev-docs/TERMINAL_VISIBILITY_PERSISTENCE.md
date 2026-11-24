# Terminal visibility persistence (2026-09-15)

## Root cause

`terminal/visible` represents terminal-specific user intent, but it was also
written by `MainWindow::writeSettings()`, the broad settings/session sweep.
That sweep is invoked by unrelated UI and session activity, including deferred
startup work. Terminal construction and restoration are deliberately deferred
until after first paint, so the broad sweep could rewrite the preference during
that interval from a window-local value rather than from a terminal show/hide
event. A restored visible terminal could therefore consume its persisted `true`
value on one launch and start hidden on the next.

## Fix and lifecycle

- `StartupSettings` snapshots `terminal/visible`; `MainWindow::readSettings()`
  reads it into `m_terminalVisible`.
- The normal primary window uses that value only to enqueue the existing
  post-first-paint terminal restoration task. No startup path writes the key.
- `MainWindow::setTerminalVisible()` is now the sole write authority. A real
  user show or hide writes the preference immediately; terminal tabs and their
  runtime-only session behaviour are unchanged.
- `MainWindow::writeSettings()` no longer writes this terminal-specific key,
  while shutdown continues to persist every other window/session setting.

This retains the lazy terminal panel, WebView2, and shell creation introduced
by `ef0fd02`'s startup work: the fix adds no terminal work before first paint.
