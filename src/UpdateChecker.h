#pragma once
#include <QObject>
#include <QPointer>

#include <memory>

class QMainWindow;
class QMessageBox;
class UpdateProgressDialog;

// Process-wide Microsoft Store update flow. The update is downloaded in the
// background while the user keeps working; installing it is a separate,
// explicit "Restart to Update" step that closes every window cleanly and
// reopens FastMD once the Store has replaced the package. A postponed update
// is installed when FastMD exits. Unpackaged builds open the Store listing
// instead.
//
// Constructed lazily on the first update command: nothing here runs at startup.
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Checking, Downloading, Ready, Installing };

    static UpdateChecker* instance();
    // Returns null when no update command has run in this process yet.
    static UpdateChecker* existingInstance();
    ~UpdateChecker() override;

    State state() const { return m_state; }
    double downloadProgress() const { return m_progress; }

    void checkForUpdates(QMainWindow* window);
    void cancelDownload();
    void restartToUpdate(QMainWindow* window);
    // Called by main() once every window is gone. Installs an update the user
    // postponed, so the next launch starts the new version; returns when the
    // Store finishes or gives up, unless the install terminates the process.
    void installOnExit();

signals:
    void stateChanged();

private:
    explicit UpdateChecker(QObject* parent);
    void setState(State state);
    void startDownload();
    void finishDownload(bool downloaded, bool canceled);
    void promptRestart();
    void install();
    void finishInstall(bool installed);

    struct Impl;
    std::unique_ptr<Impl> d;
    State m_state = State::Idle;
    double m_progress = 0.0;
    bool m_silentInstall = false;
    bool m_relaunchWatcherStarted = false;
    // Bumped whenever an operation is superseded (timeout, cancel, a new
    // phase), so a late Store callback can never act on a newer state.
    quint64 m_generation = 0;
    QPointer<QMainWindow> m_originWindow;
    QPointer<QMessageBox> m_readyPrompt;
    QPointer<UpdateProgressDialog> m_installDialog;
};
