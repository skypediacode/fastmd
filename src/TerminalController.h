#pragma once

// Owns the terminal sessions for one window and the persisted default profile.
//
// Knows nothing about the panel widget or MainWindow: it emits intent and the
// panel wires it up, the same separation SourceControlPanel keeps from
// MainWindow.

#include "TerminalProfiles.h"

#include <QList>
#include <QObject>
#include <QString>

class TerminalSession;

class TerminalController : public QObject {
    Q_OBJECT

public:
    explicit TerminalController(QObject* parent = nullptr);
    ~TerminalController() override;

    const QList<TerminalSession*>& sessions() const { return m_sessions; }
    TerminalSession* sessionById(int id) const;
    TerminalSession* activeSession() const;
    int activeSessionId() const { return m_activeId; }

    QList<TerminalProfile> profiles() const;
    TerminalProfile defaultProfile() const;
    void setDefaultProfileId(const QString& id);
    QString defaultProfileId() const { return m_defaultProfileId; }
    // The persisted default, for a window whose Terminal has not been opened
    // yet and so has no controller to ask.
    static QString persistedDefaultProfileId();
    static void persistDefaultProfileId(const QString& id);

    // Returns nullptr and fills errorMessage when the shell cannot start.
    TerminalSession* createSession(const TerminalProfile& profile,
                                   const QString& workingDirectory,
                                   int columns, int rows,
                                   QString* errorMessage = nullptr);

    void closeSession(int id);
    void setActiveSession(int id);
    // Changes presentation only; the ConPTY process and its state remain intact.
    void renameSession(int id, const QString& title);

    // Terminates every session in every window. Called from main() once the
    // event loop has returned.
    static void shutdownAll();

signals:
    void sessionsChanged();
    void activeSessionChanged(int id);
    void sessionOutput(int sessionId, const QByteArray& data);
    void sessionFinished(int sessionId, int exitCode);
private:
    QList<TerminalSession*> m_sessions;
    int m_activeId = -1;
    int m_nextId = 1;
    QString m_defaultProfileId;
};
