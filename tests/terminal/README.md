# Terminal key-encoding regression test

`keytest.html` loads the real `resources/terminal/terminal.js` and xterm.js with
a stub WebView2 host, dispatches keydown events, and checks the exact bytes the
page posts for the ConPTY input pipe: bare Escape as a Win32 input-mode
KEY_EVENT_RECORD pair once ConPTY has requested `CSI ?9001h`, and the existing
encodings for Alt+key, Ctrl+[, Ctrl+C, Shift+Enter, Ctrl+Backspace and paste.

It references the source tree directly, so nothing is copied into `build/`:

```sh
"/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" \
  --headless=new --disable-gpu --allow-file-access-from-files \
  --virtual-time-budget=5000 --user-data-dir=/tmp/fastmd-keytest \
  --dump-dom "file:///$(pwd)/tests/terminal/keytest.html" \
  | sed -n '/KEYTEST_BEGIN/,/KEYTEST_END/p'
```

Every line should read `PASS`, ending in `KEYTEST_VERDICT ALL PASSED`.

`TerminalConPtyInputTest` (built with the other tests) is the native half: it
writes those byte sequences into a real pseudoconsole and records the
KEY_EVENT_RECORDs a console-input reader such as Codex receives.

# Terminal right-click paste test

`pastetest.html` drives the real xterm.js and `terminal.js`, with a stub native
clipboard reply. It checks that a right-button press/drag/release plus
`contextmenu` produces one paste request and no application mouse records, both
without tracking and with modes 1000/1002/1003. It checks exact bytes with and
without bracketed paste, selection-copy (including Shift-drag in mouse mode),
and continued left/middle-button and wheel reporting. Run headlessly, as above,
using `pastetest.html`, a fresh profile, and a 5000 ms virtual-time budget;
expect `PASTETEST_VERDICT ALL PASSED`.

The regression is two owners of one right-click: xterm reports the mouse press
to a mouse-enabled application, while FastMD's `contextmenu` handler requests
a native clipboard paste. An application that pastes on right-button input can
therefore insert the clipboard twice. Ctrl+V only takes the clipboard path.
This is console-mode dependent: [PSReadLine clears mouse input at the prompt
and restores the previous input mode when it completes](https://github.com/PowerShell/PSReadLine/blob/master/PSReadLine/PlatformWindows.cs).
That explains why testing only the PowerShell prompt misses the overlap; shell
and application versions may differ in the modes they leave enabled. The test
reproduces the overlapping paths without depending on any particular CLI.
Before the fix, its mouse-enabled cases emit both mouse input and a paste;
after the fix the reserved right-click gesture reaches only the clipboard path.

**Manual verification required** (no GUI/CLI end-to-end verification claimed):

- Copy `abc`; right-click and Ctrl+V separately at a PowerShell prompt and in
  Codex CLI, Claude Code, and `agy` launched from PowerShell. Each must insert
  exactly `abc`. Repeat in Git Bash and cmd; also check multiline clipboard text.
- Drag-select terminal text, then right-click: it copies and clears the selection
  without pasting. The next right-click pastes once. Check Shift-drag in a TUI.
- In a mouse-enabled TUI, check left-click, middle-click, and wheel behavior;
  right-click remains FastMD's copy/paste gesture. Check Ctrl+Click links too.

# Terminal link test

`linktest.html` checks the Ctrl+Click link feature the same way: the pure
detector in `resources/terminal/terminal-links.js` against a table of URLs,
paths, positions and prose that must stay plain, then real `terminal.js` and
xterm.js driven with synthetic mouse and key events. A plain hover underlines
exactly the link (wrapped rows, wide glyphs and OSC 8 included) and keeps its
color in both themes; Ctrl only adds the hand cursor; a plain click never
opens; a Ctrl+Click posts exactly one `openLink` and its press never reaches
xterm's selection or a TUI's mouse reporting.

```sh
"/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" \
  --headless=new --disable-gpu --allow-file-access-from-files \
  --virtual-time-budget=30000 --user-data-dir=/tmp/fastmd-linktest \
  --dump-dom "file:///$(pwd)/tests/terminal/linktest.html" \
  | sed -n '/LINKTEST_BEGIN/,/LINKTEST_END/p'
```

Every line should read `PASS`, ending in `LINKTEST_VERDICT ALL PASSED`. The
page replaces `requestAnimationFrame` with a timer before xterm loads: headless
virtual time does not drive frames reliably, and without that some runs never
render. `TerminalLinksTest` is the native half -- the scheme allowlist, path
safety and relative-path resolution the host applies to every `openLink`.
`TerminalShellIntegrationTest` starts each installed PowerShell and cmd
through a real ConPTY with FastMD's profile arguments, changes directory, and
checks that the shell reports it with OSC 9;9, which is what relative links
resolve against.
