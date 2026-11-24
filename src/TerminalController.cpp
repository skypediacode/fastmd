#include "TerminalController.h"
#include "TerminalSession.h"

#include <QSettings>

namespace {

// Every controller registers here so main() can tear down all shells after the
// event loop ends, including windows that were never closed individually.
QList<TerminalController*>& liveControllers()
{
    static QList<TerminalController*> controllers;
    return controllers;
}

// Read lazily, not through the StartupSettings snapshot: the terminal never
// opens on the startup path, so a launch that never opens it should not pay for
// reading its keys. This mirrors workspace/panelMode.
constexpr auto kDefaultProfileKey = "terminal/defaultProfileId";

} // namespace

TerminalController::TerminalController(QObject* parent)
    : QObject(parent)
{
    liveControllers().append(this);
    m_defaultProfileId = persistedDefaultProfileId();
}

TerminalController::~TerminalController()
{
    liveControllers().removeAll(this);
    for (TerminalSession* session : std::as_const(m_sessions))
        session->terminate();
    qDeleteAll(m_sessions);
    m_sessions.clear();
}

QList<TerminalProfile> TerminalController::profiles() const
{
    return TerminalProfiles::available();
}

TerminalProfile TerminalController::defaultProfile() const
{
    return TerminalProfiles::resolvePreferred(TerminalProfiles::available(), m_defaultProfileId);
}

void TerminalController::setDefaultProfileId(const QString& id)
{
    if (m_defaultProfileId == id)
        return;
    m_defaultProfileId = id;
    persistDefaultProfileId(id);
}

QString TerminalController::persistedDefaultProfileId()
{
    return QSettings().value(QString::fromLatin1(kDefaultProfileKey)).toString();
}

void TerminalController::persistDefaultProfileId(const QString& id)
{
    QSettings().setValue(QString::fromLatin1(kDefaultProfileKey), id);
}

TerminalSession* TerminalController::sessionById(int id) const
{
    for (TerminalSession* session : m_sessions) {
        if (session->id() == id)
            return session;
    }
    return nullptr;
}

TerminalSession* TerminalController::activeSession() const
{
    return sessionById(m_activeId);
}

TerminalSession* TerminalController::createSession(const TerminalProfile& profile,
                                                   const QString& workingDirectory,
                                                   int columns, int rows,
                                                   QString* errorMessage)
{
    if (!profile.isValid()) {
        if (errorMessage)
            *errorMessage = tr("No shell is available on this system.");
        return nullptr;
    }

    auto* session = new TerminalSession(m_nextId, profile, workingDirectory, this);
    if (!session->start(columns, rows, errorMessage)) {
        delete session;
        return nullptr;
    }
    ++m_nextId;

    connect(session, &TerminalSession::outputReady, this, &TerminalController::sessionOutput);
    connect(session, &TerminalSession::finished, this, &TerminalController::sessionFinished);

    m_sessions.append(session);
    m_activeId = session->id();
    emit sessionsChanged();
    emit activeSessionChanged(m_activeId);
    return session;
}

void TerminalController::closeSession(int id)
{
    TerminalSession* session = sessionById(id);
    if (!session)
        return;

    const int index = m_sessions.indexOf(session);
    m_sessions.removeAll(session);
    session->terminate();
    session->deleteLater();

    if (m_activeId == id) {
        // Prefer the neighbour that took its place, else the one before it --
        // the same "stay where you were" feel as closing an editor tab.
        TerminalSession* next = nullptr;
        if (index < m_sessions.size())
            next = m_sessions.at(index);
        else if (!m_sessions.isEmpty())
            next = m_sessions.last();
        m_activeId = next ? next->id() : -1;
        emit activeSessionChanged(m_activeId);
    }
    emit sessionsChanged();
}

void TerminalController::setActiveSession(int id)
{
    if (m_activeId == id || !sessionById(id))
        return;
    m_activeId = id;
    emit activeSessionChanged(id);
}

void TerminalController::renameSession(int id, const QString& title)
{
    TerminalSession* session = sessionById(id);
    const QString trimmed = title.trimmed();
    if (!session || trimmed.isEmpty() || session->title() == trimmed)
        return;
    session->setTitle(trimmed);
    emit sessionsChanged();
}

void TerminalController::shutdownAll()
{
    const QList<TerminalController*> controllers = liveControllers();
    for (TerminalController* controller : controllers) {
        for (TerminalSession* session : std::as_const(controller->m_sessions))
            session->terminate();
    }
}
