#include "ConPtySession.h"
#include "AppShutdown.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>
#include <QWinEventNotifier>

#include <windows.h>

#include <atomic>
#include <cstdlib>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// ConPTY entry points, resolved dynamically. They ship in kernel32 from Windows
// 10 1809; resolving at runtime keeps FastMD loadable on anything older, where
// the Terminal panel simply reports itself unavailable.
// ---------------------------------------------------------------------------
using CreatePseudoConsoleFn = HRESULT (WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
using ResizePseudoConsoleFn = HRESULT (WINAPI*)(HPCON, COORD);
using ClosePseudoConsoleFn  = void    (WINAPI*)(HPCON);

struct ConPtyApi {
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn  close  = nullptr;
    bool ok = false;
};

const ConPtyApi& conPtyApi()
{
    static const ConPtyApi api = [] {
        ConPtyApi a;
        HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
        if (!kernel)
            return a;
        a.create = reinterpret_cast<CreatePseudoConsoleFn>(
            ::GetProcAddress(kernel, "CreatePseudoConsole"));
        a.resize = reinterpret_cast<ResizePseudoConsoleFn>(
            ::GetProcAddress(kernel, "ResizePseudoConsole"));
        a.close = reinterpret_cast<ClosePseudoConsoleFn>(
            ::GetProcAddress(kernel, "ClosePseudoConsole"));
        a.ok = a.create && a.resize && a.close;
        return a;
    }();
    return api;
}

// ---------------------------------------------------------------------------
// One KILL_ON_JOB_CLOSE job per session. Per-session ownership matters: closing
// one terminal must kill that shell's complete process tree without touching
// the other terminals. Windows also closes these handles if FastMD crashes.
// ---------------------------------------------------------------------------
HANDLE createTerminalJob()
{
    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        ::CloseHandle(job);
        return nullptr;
    }
    return job;
}

QString lastErrorText(DWORD code)
{
    LPWSTR buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text = length && buffer
        ? QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed()
        : QString();
    if (buffer)
        ::LocalFree(buffer);
    if (text.isEmpty())
        text = ConPtySession::tr("Windows error %1").arg(code);
    return text;
}

// CreateProcessW takes one command line and the child re-splits it with the
// standard argv rules, so anything that could contain a space or a quote has to
// be quoted the way the C runtime expects to read it back.
QString quoteArgument(const QString& argument)
{
    if (!argument.isEmpty()
        && !argument.contains(QLatin1Char(' '))
        && !argument.contains(QLatin1Char('\t'))
        && !argument.contains(QLatin1Char('"'))) {
        return argument;
    }
    QString quoted;
    quoted.reserve(argument.size() + 8);
    quoted += QLatin1Char('"');
    int backslashes = 0;
    for (const QChar ch : argument) {
        if (ch == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            quoted += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            backslashes = 0;
        } else if (backslashes) {
            quoted += QString(backslashes, QLatin1Char('\\'));
            backslashes = 0;
        }
        quoted += ch;
    }
    quoted += QString(backslashes * 2, QLatin1Char('\\'));
    quoted += QLatin1Char('"');
    return quoted;
}

// How long the shell's own exit may wait for the reader to reach EOF, which
// is the path that delivers the last of its output. Long enough for a pipe
// that is about to close, short enough to be invisible when it never does.
constexpr int kExitGraceMs = 150;

COORD clampSize(int columns, int rows)
{
    COORD size;
    size.X = static_cast<SHORT>(qBound(1, columns, 9999));
    size.Y = static_cast<SHORT>(qBound(1, rows, 9999));
    return size;
}

} // namespace

// ---------------------------------------------------------------------------
// Reader: blocking ReadFile on the pseudoconsole's output pipe.
// ---------------------------------------------------------------------------
class ConPtyReader : public QThread {
    Q_OBJECT
public:
    ConPtyReader(HANDLE output, QObject* parent = nullptr)
        : QThread(parent), m_output(output) {}

signals:
    void chunkRead(const QByteArray& data);
    void reachedEof();

protected:
    void run() override
    {
        std::vector<char> buffer(16 * 1024);
        for (;;) {
            DWORD read = 0;
            const BOOL ok = ::ReadFile(m_output, buffer.data(),
                                       static_cast<DWORD>(buffer.size()), &read, nullptr);
            if (!ok || read == 0)
                break;
            emit chunkRead(QByteArray(buffer.data(), static_cast<int>(read)));
            if (AppShutdown::started())
                break;
        }
        emit reachedEof();
    }

private:
    HANDLE m_output;
};

// ---------------------------------------------------------------------------
// Writer: an ordered queue drained on its own thread. Keystrokes have to keep
// their order, which is why this is a dedicated thread and not the global pool.
// ---------------------------------------------------------------------------
class ConPtyWriter : public QThread {
    Q_OBJECT
public:
    ConPtyWriter(HANDLE input, QObject* parent = nullptr)
        : QThread(parent), m_input(input) {}

    void enqueue(const QByteArray& data)
    {
        QMutexLocker locker(&m_mutex);
        if (m_stopping)
            return;
        m_queue.append(data);
        m_wait.wakeOne();
    }

    void stop()
    {
        QMutexLocker locker(&m_mutex);
        m_stopping = true;
        m_wait.wakeAll();
    }

protected:
    void run() override
    {
        for (;;) {
            QByteArray pending;
            {
                QMutexLocker locker(&m_mutex);
                while (m_queue.isEmpty() && !m_stopping && !AppShutdown::started())
                    m_wait.wait(&m_mutex, 200);
                if (m_stopping || AppShutdown::started())
                    return;
                pending = m_queue.takeFirst();
            }
            qsizetype offset = 0;
            while (offset < pending.size()) {
                DWORD written = 0;
                const BOOL ok = ::WriteFile(m_input, pending.constData() + offset,
                                            static_cast<DWORD>(pending.size() - offset),
                                            &written, nullptr);
                if (!ok || written == 0)
                    return;
                offset += written;
            }
        }
    }

private:
    HANDLE m_input;
    QMutex m_mutex;
    QWaitCondition m_wait;
    QList<QByteArray> m_queue;
    bool m_stopping = false;
};

// ---------------------------------------------------------------------------
// ConPtySession::Impl
// ---------------------------------------------------------------------------
class ConPtySession::Impl {
public:
    HPCON pty = nullptr;
    HANDLE inputWrite = nullptr;   // we write -> shell reads
    HANDLE outputRead = nullptr;   // shell writes -> we read
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    HANDLE job = nullptr;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
    ConPtyReader* reader = nullptr;
    ConPtyWriter* writer = nullptr;
    QWinEventNotifier* processExitNotifier = nullptr;
    std::atomic<bool> running{false};
    bool finishedEmitted = false;
    bool exitGraceStarted = false;

    void closeHandle(HANDLE& handle)
    {
        if (handle && handle != INVALID_HANDLE_VALUE)
            ::CloseHandle(handle);
        handle = nullptr;
    }

    void freeAttributes()
    {
        if (!attributes)
            return;
        ::DeleteProcThreadAttributeList(attributes);
        std::free(attributes);
        attributes = nullptr;
    }
};

ConPtySession::ConPtySession(QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
}

ConPtySession::~ConPtySession()
{
    terminate();
}

bool ConPtySession::isSupported()
{
    return conPtyApi().ok;
}

bool ConPtySession::start(const QString& program,
                          const QStringList& arguments,
                          const QString& workingDirectory,
                          int columns,
                          int rows,
                          QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message) {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (d->running.load())
        return fail(tr("This terminal session is already running."));
    if (!conPtyApi().ok)
        return fail(tr("This version of Windows does not provide the console API "
                       "(ConPTY) that the integrated terminal requires."));

    // Two anonymous pipes, one per direction. The pseudoconsole keeps the ends
    // we hand it and we keep the other two.
    HANDLE inputRead = nullptr;
    HANDLE outputWrite = nullptr;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = FALSE;

    if (!::CreatePipe(&inputRead, &d->inputWrite, &sa, 0))
        return fail(lastErrorText(::GetLastError()));
    if (!::CreatePipe(&d->outputRead, &outputWrite, &sa, 0)) {
        const DWORD code = ::GetLastError();
        d->closeHandle(inputRead);
        d->closeHandle(d->inputWrite);
        return fail(lastErrorText(code));
    }

    // Win32 input mode is negotiated in-band with CSI ?9001h after the
    // pseudoconsole starts. It is not a CreatePseudoConsole flag; passing the
    // old, undocumented flag value here is ignored by current ConPTY builds.
    const HRESULT hr = conPtyApi().create(clampSize(columns, rows), inputRead, outputWrite,
                                          0, &d->pty);
    // The pseudoconsole duplicates what it needs. Our copies of its two ends are
    // dead weight and, worse, would keep the pipes alive past the child's exit,
    // so the reader would never see EOF.
    d->closeHandle(inputRead);
    d->closeHandle(outputWrite);
    if (FAILED(hr) || !d->pty) {
        d->closeHandle(d->inputWrite);
        d->closeHandle(d->outputRead);
        return fail(tr("The pseudoconsole could not be created (0x%1).")
                        .arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
    }

    SIZE_T attributeSize = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    d->attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(std::malloc(attributeSize));
    if (!d->attributes
        || !::InitializeProcThreadAttributeList(d->attributes, 1, 0, &attributeSize)) {
        if (d->attributes) {
            std::free(d->attributes);
            d->attributes = nullptr;
        }
        terminate();
        return fail(tr("The terminal process attributes could not be prepared."));
    }
    if (!::UpdateProcThreadAttribute(d->attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                     d->pty, sizeof(HPCON), nullptr, nullptr)) {
        const DWORD code = ::GetLastError();
        terminate();
        return fail(lastErrorText(code));
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    startup.lpAttributeList = d->attributes;

    QString commandLine = quoteArgument(QDir::toNativeSeparators(program));
    for (const QString& argument : arguments)
        commandLine += QLatin1Char(' ') + quoteArgument(argument);
    std::wstring commandLineBuffer = commandLine.toStdWString();
    commandLineBuffer.push_back(L'\0');

    QString directory = workingDirectory;
    if (directory.isEmpty() || !QDir(directory).exists())
        directory = QDir::homePath();
    const std::wstring nativeDirectory = QDir::toNativeSeparators(directory).toStdWString();

    PROCESS_INFORMATION processInfo{};
    const BOOL created = ::CreateProcessW(
        nullptr, commandLineBuffer.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
        nullptr, nativeDirectory.c_str(),
        &startup.StartupInfo, &processInfo);

    if (!created) {
        const DWORD code = ::GetLastError();
        terminate();
        return fail(tr("%1 could not be started: %2")
                        .arg(QDir::toNativeSeparators(program), lastErrorText(code)));
    }

    d->process = processInfo.hProcess;
    d->thread = processInfo.hThread;
    d->job = createTerminalJob();
    if (d->job && !::AssignProcessToJobObject(d->job, d->process))
        d->closeHandle(d->job);
    ::ResumeThread(d->thread);

    d->running.store(true);
    d->finishedEmitted = false;
    d->exitGraceStarted = false;

    const auto reportFinished = [this] {
        if (d->finishedEmitted)
            return;
        d->finishedEmitted = true;
        DWORD exitCode = 0;
        if (d->process)
            ::GetExitCodeProcess(d->process, &exitCode);
        d->running.store(false);
        emit finished(static_cast<int>(exitCode));
    };

    // ConPTY's output pipe is not guaranteed to reach EOF when the shell
    // exits.  In particular, a normal `exit` can leave the terminal looking
    // dead while the reader is still blocked, so watching the shell process
    // itself is the authoritative completion signal.
    //
    // It is not, however, the *prompt* one: the process can be gone while its
    // last output is still in the pipe or in an already-queued chunkRead, and
    // the panel closes the tab as soon as this is reported. So give the reader
    // a short grace period to reach EOF -- that path delivers every chunk
    // first, in order -- and only report the exit ourselves if it does not.
    d->processExitNotifier = new QWinEventNotifier(d->process, this);
    connect(d->processExitNotifier, &QWinEventNotifier::activated, this,
            [this, reportFinished](HANDLE) {
        if (d->finishedEmitted || d->exitGraceStarted)
            return;
        d->exitGraceStarted = true;
        QTimer::singleShot(kExitGraceMs, this, reportFinished);
    });

    d->reader = new ConPtyReader(d->outputRead);
    connect(d->reader, &ConPtyReader::chunkRead, this, &ConPtySession::outputReady,
            Qt::QueuedConnection);
    // Queued, so every chunkRead the reader posted ahead of it has already been
    // delivered by the time this runs.
    connect(d->reader, &ConPtyReader::reachedEof, this, reportFinished,
            Qt::QueuedConnection);
    d->reader->start();

    d->writer = new ConPtyWriter(d->inputWrite);
    d->writer->start();

    return true;
}

bool ConPtySession::isRunning() const
{
    return d->running.load();
}

void ConPtySession::write(const QByteArray& data)
{
    if (d->writer && d->running.load())
        d->writer->enqueue(data);
}

void ConPtySession::resize(int columns, int rows)
{
    if (d->pty && conPtyApi().resize)
        conPtyApi().resize(d->pty, clampSize(columns, rows));
}

void ConPtySession::terminate()
{
    d->running.store(false);

    if (d->processExitNotifier) {
        d->processExitNotifier->setEnabled(false);
        // deleteLater, not delete: terminate() can be reached from a slot the
        // notifier itself is emitting, and QWinEventNotifier touches its own
        // private data after activated() returns.
        d->processExitNotifier->deleteLater();
        d->processExitNotifier = nullptr;
    }
    // Closing a session is intentional termination, not a shell completion.
    // Prevent a later reader callback from reporting it as a natural exit.
    d->finishedEmitted = true;

    if (d->writer) {
        d->writer->stop();
        d->writer->wait(2000);
        delete d->writer;
        d->writer = nullptr;
    }

    // Kill the complete tree before closing ConPTY. The reader remains active
    // to drain conhost while ClosePseudoConsole completes.
    if (d->job)
        ::TerminateJobObject(d->job, 0);

    // Order matters, and this is the classic ConPTY deadlock: ClosePseudoConsole
    // waits for its conhost to flush, which it cannot do unless somebody is
    // still draining the output pipe. So close the pseudoconsole *first*, while
    // the reader thread is still running -- that is what makes its blocking
    // ReadFile return -- and only then join the reader.
    if (d->pty && conPtyApi().close) {
        conPtyApi().close(d->pty);
        d->pty = nullptr;
    }

    if (d->reader) {
        if (!d->reader->wait(3000)) {
            d->reader->terminate();
            d->reader->wait(500);
        }
        delete d->reader;
        d->reader = nullptr;
    }

    // The job object kills the tree at process exit, but a tab the user closed
    // explicitly should not leave its shell running for the rest of the session.
    if (d->process) {
        if (::WaitForSingleObject(d->process, 0) == WAIT_TIMEOUT)
            ::TerminateProcess(d->process, 0);
    }

    d->closeHandle(d->inputWrite);
    d->closeHandle(d->outputRead);
    d->closeHandle(d->thread);
    d->closeHandle(d->process);
    d->closeHandle(d->job);
    d->freeAttributes();
}

#include "ConPtySession.moc"
