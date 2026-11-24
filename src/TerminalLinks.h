#pragma once

// Host-side authority for Terminal Ctrl+Click links.
//
// The page (resources/terminal/terminal-links.js) only finds text that looks
// like a link. Everything that decides what a click does is here, because both
// the terminal output and the page that renders it are untrusted: the scheme
// allowlist, path parsing, rejection of device/stream/network paths, and
// resolution of relative paths against the shell's directory. Qt Core only, so
// TerminalLinksTest can drive it without Widgets or WebView2.

#include <QString>
#include <QStringList>
#include <QUrl>

namespace TerminalLinks {

struct Target {
    // NotFound: a safe local path that resolved to nothing on disk; `path` is
    // where it was looked for, so the user can see which directory was used.
    enum class Kind { Invalid, Web, File, Directory, NotFound };
    Kind kind = Kind::Invalid;
    QUrl url;      // Web
    QString path;  // File / Directory / NotFound: absolute, cleaned, '/' separators
    int line = 0;  // 1-based; 0 means no position
    int column = 0;
};

struct LocalLink {
    QString path;
    int line = 0;
    int column = 0;
};

// Pure parsing, no filesystem access. `text` is a link as the page sends it:
// a URL, a path, a quoted path, optionally followed by :line[:col],
// (line[,col]) or, after a quoted path, ", line N".
bool parseLocal(const QString& text, LocalLink* out);

// Absolute or relative path that is safe to touch at all: no control
// characters, no \\?\ or \\.\ device namespace, no UNC/network share (merely
// probing one sends credentials to whatever host the output named), no
// alternate data stream, no reserved device name, no drive-relative C:foo.
bool isSafeLocalPath(const QString& path);

// http/https with a host, or mailto. Every other scheme is refused.
bool isAllowedWebUrl(const QUrl& url);

// Windows Shell shortcuts and similar indirections. Opening one runs its
// target, so they are revealed in Explorer rather than opened.
bool isShellShortcut(const QString& path);

// Resolves a link to something that exists, or to NotFound with the absolute
// path that was missing (Invalid when it could not be parsed or was refused,
// or a relative path had no usable base). A relative path is tried against
// exactly one directory: `reportedCwd` (what the shell last announced through
// OSC 7 / 9;9 / 633) when that is a usable directory, otherwise
// `startDirectory`, where the session was launched. Never both: a cwd the shell
// did report means the start directory is stale, and guessing there could open
// a different file of the same name. An unquoted path with spaces that does not
// exist falls back to its longest existing prefix ending at a space, since
// terminal output cannot say where such a path ends. A file URL may end in
// :line[:col]. Touches the filesystem, so call it off the UI thread.
Target resolve(const QString& text, const QString& reportedCwd,
               const QString& startDirectory);

} // namespace TerminalLinks
