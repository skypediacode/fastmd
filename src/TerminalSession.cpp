#include "TerminalSession.h"
#include "StartupProfiler.h"

#include <QTimer>

namespace {
// Long enough to batch a burst of small reads, short enough that interactive
// echo still feels immediate.
constexpr int kFlushIntervalMs = 12;
// A single message carrying more than this is split, so one enormous burst
// cannot stall the bridge.
constexpr int kMaxFlushBytes = 256 * 1024;
}

TerminalSession::TerminalSession(int id, const TerminalProfile& profile,
                                 QString workingDirectory, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_profile(profile)
    , m_workingDirectory(std::move(workingDirectory))
    , m_title(profile.name)
{
    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &TerminalSession::flushPending);
}

TerminalSession::~TerminalSession()
{
    terminate();
}

bool TerminalSession::isRunning() const
{
    return m_pty && m_pty->isRunning();
}

bool TerminalSession::start(int columns, int rows, QString* errorMessage)
{
    if (m_pty)
        return false;

    m_pty = new ConPtySession(this);
    connect(m_pty, &ConPtySession::outputReady, this, [this](const QByteArray& data) {
        m_pending.append(data);
        if (m_pending.size() >= kMaxFlushBytes) {
            flushPending();
            return;
        }
        if (!m_flushTimer->isActive())
            m_flushTimer->start();
    });
    connect(m_pty, &ConPtySession::finished, this, [this](int exitCode) {
        flushPending();
        m_exited = true;
        m_exitCode = exitCode;
        emit finished(m_id, exitCode);
    });

    if (!m_pty->start(m_profile.program, m_profile.arguments, m_workingDirectory,
                      columns, rows, errorMessage)) {
        delete m_pty;
        m_pty = nullptr;
        return false;
    }
    FASTMD_MARK("terminal process started");
    return true;
}

void TerminalSession::write(const QByteArray& utf8)
{
    if (m_pty)
        m_pty->write(utf8);
}

void TerminalSession::resize(int columns, int rows)
{
    if (m_pty)
        m_pty->resize(columns, rows);
}

void TerminalSession::terminate()
{
    if (m_flushTimer)
        m_flushTimer->stop();
    if (m_pty) {
        m_pty->terminate();
        // terminate() has already released every handle and joined both
        // threads, so this object holds nothing; deleting it later keeps it
        // safe to call terminate() from anywhere in its own signal chain.
        m_pty->deleteLater();
        m_pty = nullptr;
    }
    m_pending.clear();
}

void TerminalSession::flushPending()
{
    m_flushTimer->stop();
    if (m_pending.isEmpty())
        return;
    const QByteArray data = m_pending;
    m_pending.clear();
    emit outputReady(m_id, data);
}
