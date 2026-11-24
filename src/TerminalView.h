#pragma once

// The Terminal panel's WebView2 host: one native child window running the
// xterm.js shell, with every session living as its own xterm instance inside
// that single page.
//
// One WebView2 for the whole panel rather than one per session is deliberate.
// AGENTS.md records a WebView2 controller at ~240 MB private for the first one,
// and a terminal-per-controller would add that cost per open shell; keeping the
// sessions in one page makes switching a DOM toggle instead of a widget swap.
//
// Independent of PreviewWidget by design: a terminal reload or failure must
// never disturb the Markdown preview. Only the shared browser *environment* is
// common to both.

#include <QString>
#include <QWidget>

class QLabel;

class TerminalView : public QWidget {
    Q_OBJECT

public:
    static constexpr int DefaultFontSize = 15;
    static constexpr int MinimumFontSize = 6;
    static constexpr int MaximumFontSize = 48;

    explicit TerminalView(QWidget* parent = nullptr);
    ~TerminalView() override;

    // Commands the page hands back to Qt because the WebView2 child owns the
    // keyboard while it has focus.
    enum class Command {
        ToggleTerminal,
        CommandPalette,
        ReadingMode,
        NotepadMode,
        IncreaseFontSize,
        DecreaseFontSize,
        ResetFontSize,
        RenameSession,
        WorkspaceSearch,
        FindInFiles,
        ReplaceInFiles,
    };
    Q_ENUM(Command)

    void ensureInitialized();
    bool isReady() const { return m_shellReady; }

    void setDarkMode(bool dark);
    void setFontSize(int pixelSize);

    // --- page control -----------------------------------------------------
    void createTerminal(int sessionId);
    void destroyTerminal(int sessionId);
    void showTerminal(int sessionId);
    void writeOutput(int sessionId, const QByteArray& data);
    void focusTerminal();
    void clearTerminal(int sessionId);
    void copySelection(int sessionId);
    void pasteText(int sessionId, const QString& text);
    void selectAll(int sessionId);

signals:
    // Keystrokes from xterm, already UTF-8.
    void inputReady(int sessionId, const QByteArray& utf8);
    void resized(int sessionId, int columns, int rows);
    void shellReady();
    void commandRequested(TerminalView::Command command);
    void selectionCopyRequested(const QString& text);
    void pasteRequested(int sessionId);
    // A Ctrl+Click on a link. `text` and `reportedCwd` come straight from the
    // page and are untrusted; TerminalLinks::resolve() is what validates them.
    void linkActivated(int sessionId, const QString& text, const QString& reportedCwd);

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    class Impl;

    void handleWebMessage(const QString& json);
    void showFallback(const QString& message);

    Impl* d = nullptr;
    QLabel* m_fallbackLabel = nullptr;
    bool m_shellReady = false;
    bool m_dark = false;
    int m_fontSize = DefaultFontSize;
};
