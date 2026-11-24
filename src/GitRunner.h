#pragma once

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QStringList>


class QProcess;

// Thin, stateless wrapper around the `git` executable resolved from PATH.
// git is never bundled (unlike Pandoc/AnyDoc); a machine without it simply
// gets the Source Control panel's "git not found" empty state.
namespace GitRunner {

struct Result {
    int        exitCode = -1;
    QByteArray out;
    QString    err;
    bool       ok() const { return exitCode == 0; }
};

// Network commands can take minutes and must never run on the UI thread;
// they also get a far longer timeout than a local plumbing call.
enum class Kind { Local, Network };

// Runs `git <args>` with repoRoot as the working directory. Blocking, with a
// bounded wait -- callers on the UI thread must only use Kind::Local, and
// SourceControlPanel dispatches reads and every Kind::Network call onto a worker.
Result run(const QString& repoRoot, const QStringList& args,
           const QByteArray& stdinData = QByteArray(), Kind kind = Kind::Local);

class CancelHandle;

// Same as run(), but publishes the live QProcess through the handle before
// waiting, so a long network operation can be cancelled from another thread.
Result runCancellable(const QString& repoRoot, const QStringList& args,
                      CancelHandle* handle);

// Owns the "which process is cancellable right now" slot for one caller.
// The mutex is the point: the QProcess lives on the worker thread as a stack
// local, so an atomic pointer alone leaves a window in which a canceller has
// loaded a pointer the worker is already destroying. Publishing, clearing and
// cancelling all happen under this lock, and the slot is cleared before the
// QProcess is destroyed.
class CancelHandle {
public:
    // Starts a later operation after the previous one has finished.
    void reset();
    // Callable from any thread. If startup has not published the process yet,
    // the request is applied when it does.
    void cancel();

private:
    friend Result runCancellable(const QString& repoRoot, const QStringList& args,
                                 CancelHandle* handle);
    void publish(QProcess* process);

    QMutex    m_mutex;
    QProcess* m_process = nullptr;
    bool      m_cancelled = false;
};

// Cached `git --version` probe. Safe to call from any thread after the first
// call has completed; the first call is made from SourceControlPanel's worker.
bool isAvailable();

}
