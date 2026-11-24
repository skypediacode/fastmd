#pragma once

// One Windows pseudoconsole (ConPTY) session: a child shell process plus the
// two pipes that carry its terminal I/O.
//
// Deliberately free of Qt Widgets so the process layer stays testable and
// separate from the panel, mirroring how GitRunner is separate from
// SourceControlPanel.
//
// Threading: a ConPTY's output pipe can only be drained with a blocking
// ReadFile, and a large write can block when the input pipe fills. Neither may
// run on the UI thread, so each session owns a reader thread and a writer
// thread. Both poll AppShutdown::started() so a session can never hold up
// process exit.

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class ConPtySession : public QObject {
    Q_OBJECT

public:
    explicit ConPtySession(QObject* parent = nullptr);
    ~ConPtySession() override;

    // True when this Windows build exposes the ConPTY API at all. Resolved
    // dynamically from kernel32, so an older Windows disables the feature
    // cleanly instead of failing to load the executable.
    static bool isSupported();

    // Launches `program` with `arguments` in `workingDirectory` at the given
    // size. Returns false and fills `errorMessage` when the shell cannot start
    // (a missing executable is the common case).
    bool start(const QString& program,
               const QStringList& arguments,
               const QString& workingDirectory,
               int columns,
               int rows,
               QString* errorMessage = nullptr);

    bool isRunning() const;

    // Queued onto the writer thread; never blocks the caller.
    void write(const QByteArray& data);

    void resize(int columns, int rows);

    // Idempotent. Safe to call from the destructor or from a close button.
    void terminate();

signals:
    // Raw bytes exactly as the pseudoconsole produced them. Never decoded here:
    // a UTF-8 sequence can straddle two reads, so decoding is the page's job.
    void outputReady(const QByteArray& data);

    // Emitted once, after the output pipe reaches EOF.
    void finished(int exitCode);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};
