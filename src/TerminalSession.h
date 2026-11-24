#pragma once

// One terminal session: a profile, the directory it was started in, and the
// ConPTY carrying its shell.
//
// Output is coalesced here rather than forwarded per read. A command like
// `dir /s` produces hundreds of small chunks, and one PostWebMessageAsJson per
// chunk floods the WebView2 bridge; a short timer turns that into a handful of
// larger messages with no visible latency.

#include "ConPtySession.h"
#include "TerminalProfiles.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QTimer;

class TerminalSession : public QObject {
    Q_OBJECT

public:
    TerminalSession(int id, const TerminalProfile& profile, QString workingDirectory,
                    QObject* parent = nullptr);
    ~TerminalSession() override;

    int id() const { return m_id; }
    const TerminalProfile& profile() const { return m_profile; }
    QString workingDirectory() const { return m_workingDirectory; }
    QString title() const { return m_title; }
    void setTitle(const QString& title) { m_title = title; }
    bool isRunning() const;
    bool hasExited() const { return m_exited; }
    int exitCode() const { return m_exitCode; }

    bool start(int columns, int rows, QString* errorMessage = nullptr);
    void write(const QByteArray& utf8);
    void resize(int columns, int rows);
    void terminate();

signals:
    // Coalesced raw bytes; still undecoded, because a UTF-8 sequence can span
    // two reads and only the page has the streaming decoder.
    void outputReady(int sessionId, const QByteArray& data);
    void finished(int sessionId, int exitCode);

private:
    void flushPending();

    int m_id;
    TerminalProfile m_profile;
    QString m_workingDirectory;
    QString m_title;
    ConPtySession* m_pty = nullptr;
    QTimer* m_flushTimer = nullptr;
    QByteArray m_pending;
    bool m_exited = false;
    int m_exitCode = 0;
};
