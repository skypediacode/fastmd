#include "SourceControlPanel.h"

#include "GitRunner.h"
#include "IconHelper.h"

#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

namespace {
constexpr ushort kIconSyncGlyph = 0xE627;  // sync -- matches SourceControlPanel.cpp's kIconSync
constexpr int kSyncSpinIntervalMs = 16;
constexpr int kSyncSpinDegreesPerTick = 6;
// Must match m_btnCommit->setIconSize() in SourceControlPanel.cpp. Rendering
// the rotated glyph directly at this logical size (rather than baking a
// larger pixmap and downscaling it) avoids the extra blur pass that made the
// spinning icon look shrunken and washed out compared to the static one.
constexpr int kSyncIconSize = 16;
}

// ---------------------------------------------------------------------------
// Busy state

QString SourceControlPanel::networkOperationLabel(NetworkOpKind kind)
{
    switch (kind) {
    case NetworkOpKind::Pull:    return tr("Pulling…");
    case NetworkOpKind::Push:    return tr("Pushing…");
    case NetworkOpKind::Sync:    return tr("Syncing Changes…");
    case NetworkOpKind::Publish: return tr("Publishing…");
    }
    return QString();
}

QPixmap SourceControlPanel::rotatedSyncPixmap(int angleDeg) const
{
    // Draw the glyph itself under a rotated painter transform -- the same
    // single-pass approach MaterialIconEngine::paint() uses for the static
    // icon -- instead of rendering flat then rotating the raster. Rotating a
    // pre-rendered pixmap (and scaling it to fit) softens/shrinks thin
    // strokes; rotating the text draw keeps the same weight and size as the
    // non-spinning icon, just turning.
    const qreal dpr = devicePixelRatioF();
    const int deviceSize = qRound(kSyncIconSize * dpr);

    QPixmap pm(deviceSize, deviceSize);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter painter(&pm);
    painter.setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);

    const qreal center = kSyncIconSize / 2.0;
    painter.translate(center, center);
    painter.rotate(angleDeg);
    painter.translate(-center, -center);

    QFont font(IconHelper::materialFontFamily());
    font.setPixelSize(qMax(1, kSyncIconSize - 2));
    font.setHintingPreference(QFont::PreferNoHinting);
    painter.setFont(font);
    // Matches QPushButton#gitCommitButton[gitBusy="true"]'s text color in
    // Stylesheet.h -- the button reads as its normal grey :disabled look
    // while busy, so the icon must match that grey rather than the white
    // used against the button's blue idle background.
    painter.setPen(m_dark ? QColor(0x77, 0x77, 0x77) : QColor(0x88, 0x88, 0x88));
    painter.drawText(QRect(0, 0, kSyncIconSize, kSyncIconSize), Qt::AlignCenter,
                      QString(QChar(kIconSyncGlyph)));

    return pm;
}

void SourceControlPanel::setBusy(bool busy)
{
    m_busy = busy;

    if (busy) {
        m_syncSpinAngle = 0;
        m_btnCommit->setIcon(QIcon(rotatedSyncPixmap(0)));
        if (!m_syncSpinTimer) {
            m_syncSpinTimer = new QTimer(this);
            connect(m_syncSpinTimer, &QTimer::timeout, this, [this] {
                m_syncSpinAngle = (m_syncSpinAngle + kSyncSpinDegreesPerTick) % 360;
                m_btnCommit->setIcon(QIcon(rotatedSyncPixmap(m_syncSpinAngle)));
            });
        }
        m_syncSpinTimer->start(kSyncSpinIntervalMs);
    } else if (m_syncSpinTimer) {
        m_syncSpinTimer->stop();
    }

    updateActionStates();
}

QString SourceControlPanel::defaultRemote() const
{
    if (m_snapshot.branch.hasUpstream) {
        const QString upstream = m_snapshot.branch.upstream;
        const int slash = upstream.indexOf(QLatin1Char('/'));
        if (slash > 0)
            return upstream.left(slash);
    }
    for (const GitRemote& remote : m_snapshot.remotes) {
        if (remote.name == QLatin1String("origin"))
            return remote.name;
    }
    return m_snapshot.remotes.isEmpty() ? QString() : m_snapshot.remotes.first().name;
}

void SourceControlPanel::startNetworkOperation(NetworkOpKind kind, const QStringList& args,
                                      bool touchesWorktree)
{
    if (m_root.isEmpty() || m_busy || m_localRunning)
        return;
    if (m_networkWatcher->isRunning())
        return;

    m_networkTouchesWorktree = touchesWorktree;
    m_networkOpKind = kind;
    setBusy(true);

    const QString root = m_root;
    // Cancellation stays sticky until a newly published process observes it,
    // so clear the previous operation's state only once its watcher is idle.
    m_networkCancel.reset();
    // Captured raw, which is safe because ~SourceControlPanel waits for this
    // future before the handle is destroyed.
    GitRunner::CancelHandle* const cancelHandle = &m_networkCancel;
    m_networkWatcher->setFuture(QtConcurrent::run([root, args, cancelHandle] {
        return GitRunner::runCancellable(root, args, cancelHandle);
    }));
}

void SourceControlPanel::finishNetworkOperation()
{
    const GitRunner::Result result = m_networkWatcher->result();
    const bool wasSyncPull = m_syncPendingPush;
    m_syncPendingPush = false;

    if (result.ok()) {
        // No "done" message: like VS Code, a successful operation is shown
        // by the refreshed state (updated ahead/behind, cleared changes),
        // not by a status line.
        if (m_networkTouchesWorktree)
            emit worktreeChanged();
        if (wasSyncPull) {
            // Sync Changes: the pull leg succeeded, so chain into the push
            // leg now rather than waiting for a second click. Stays labelled
            // "Syncing Changes…" for this leg too.
            // startNetworkOperation() rejects a request while busy, so drop
            // the pull's busy state only long enough to begin the push. Both
            // calls happen in this slot before Qt can repaint the button.
            setBusy(false);
            scheduleRefresh();
            pushToUpstream(NetworkOpKind::Sync);
            return;
        }

        // Do not calculate the idle label from m_snapshot yet: it still
        // carries the branch/file state from before the network operation.
        // Leaving the button busy until the scheduled refresh applies avoids
        // a one-frame, stale "Sync Changes" action after a successful sync.
        m_clearBusyAfterRefresh = true;
    } else {
        // A failed operation did not establish a new authoritative state, so
        // restore the usual action immediately and show Git's diagnostic.
        setBusy(false);
        QString message = result.err.isEmpty() ? tr("The operation failed.") : result.err;
        // Turn git's most common push rejection into the one instruction
        // that actually resolves it. Force-push is never offered.
        if (message.contains(QLatin1String("non-fast-forward")) ||
            message.contains(QLatin1String("fetch first")) ||
            message.contains(QLatin1String("rejected"))) {
            message = tr("The remote has commits you do not have. Pull first, then push.\n\n%1")
                          .arg(result.err);
        } else if (message.contains(QLatin1String("could not read Username")) ||
                   message.contains(QLatin1String("Authentication failed")) ||
                   message.contains(QLatin1String("terminal prompts disabled"))) {
            message = tr("Git could not authenticate with the remote. "
                         "Check your credentials, then try again.\n\n%1").arg(result.err);
        }
        setStatusMessage(message, true);
    }

    scheduleRefresh();
}

// ---------------------------------------------------------------------------
// Pull / push / sync

void SourceControlPanel::pullFromUpstream(NetworkOpKind kind)
{
    if (m_snapshot.merging) {
        setStatusMessage(tr("Finish the current merge before pulling."), true);
        return;
    }
    if (!m_snapshot.branch.hasUpstream) {
        setStatusMessage(tr("This branch has no upstream to pull from."), true);
        return;
    }

    const QString upstream = m_snapshot.branch.upstream;
    const int slash = upstream.indexOf(QLatin1Char('/'));
    const QString remote = slash > 0 ? upstream.left(slash) : defaultRemote();
    const QString branch = slash > 0 ? upstream.mid(slash + 1) : m_snapshot.branch.branch;

    // Pinned to --no-rebase on purpose: honouring pull.rebase=true could drop
    // the user into an interactive rebase this window has no UI to finish.
    startNetworkOperation(
        kind,
        { QStringLiteral("pull"), QStringLiteral("--no-rebase"), QStringLiteral("--no-edit"),
          remote, branch },
        true);
}

void SourceControlPanel::promptAddRemote(NetworkOpKind kind)
{
    // Nested event loop: hold the refresh for its duration (see RefreshHold).
    RefreshHold hold(this);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add Remote"));
    auto* form = new QVBoxLayout(&dialog);
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(8);

    form->addWidget(new QLabel(
        tr("This repository has no remotes yet. Add one to publish to:"), &dialog));

    form->addWidget(new QLabel(tr("Name:"), &dialog));
    auto* nameEdit = new QLineEdit(QStringLiteral("origin"), &dialog);
    form->addWidget(nameEdit);

    form->addWidget(new QLabel(tr("URL:"), &dialog));
    auto* urlEdit = new QLineEdit(&dialog);
    urlEdit->setPlaceholderText(tr("https://github.com/user/repo.git"));
    form->addWidget(urlEdit);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addWidget(box);

    urlEdit->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString name = nameEdit->text().trimmed();
    const QString url = urlEdit->text().trimmed();
    if (!GitRepository::isValidRemoteName(name)) {
        setStatusMessage(tr("\"%1\" is not a valid remote name.").arg(name), true);
        return;
    }
    if (url.isEmpty()) {
        setStatusMessage(tr("A remote URL is required."), true);
        return;
    }
    const QString branch = m_snapshot.branch.branch;
    runLocal({ QStringLiteral("remote"), QStringLiteral("add"), name, url }, {},
             [this, kind, name, branch] {
        if (branch.isEmpty())
            return;
        const NetworkOpKind effectiveKind = kind == NetworkOpKind::Push
            ? NetworkOpKind::Publish : kind;
        startNetworkOperation(effectiveKind,
            { QStringLiteral("push"), QStringLiteral("-u"), name, branch }, false);
    });
}

void SourceControlPanel::pushToUpstream(NetworkOpKind kind)
{
    if (m_snapshot.branch.detached) {
        setStatusMessage(tr("HEAD is detached. Check out a branch before pushing."), true);
        return;
    }

    QString remote = defaultRemote();
    if (remote.isEmpty()) {
        promptAddRemote(kind);
        return;
    }
    const QString branch = m_snapshot.branch.branch;
    if (branch.isEmpty())
        return;

    // First push of a new branch also sets its upstream, which is the only
    // reason this window ever writes branch configuration.
    QStringList args { QStringLiteral("push") };
    if (!m_snapshot.branch.hasUpstream)
        args << QStringLiteral("-u");
    args << remote << branch;

    // A plain Push with no upstream yet is really a Publish -- reflect that
    // in the button label even though the caller didn't know to ask for it
    // (e.g. Commit and Push on a brand-new branch).
    NetworkOpKind effectiveKind = kind;
    if (effectiveKind == NetworkOpKind::Push && !m_snapshot.branch.hasUpstream)
        effectiveKind = NetworkOpKind::Publish;

    startNetworkOperation(effectiveKind, args, false);
}

void SourceControlPanel::syncChanges()
{
    if (m_snapshot.merging) {
        setStatusMessage(tr("Finish the current merge before syncing."), true);
        return;
    }
    // No upstream means the primary action is really Publish Branch; pushing
    // straight away both creates and syncs it.
    if (!m_snapshot.branch.hasUpstream) {
        pushToUpstream();
        return;
    }
    // Pull first, then push -- finishNetworkOperation() chains the push leg
    // once the pull succeeds. Both legs stay labelled "Syncing Changes…".
    m_syncPendingPush = true;
    pullFromUpstream(NetworkOpKind::Sync);
    if (!m_busy)
        m_syncPendingPush = false;  // pullFromUpstream() declined to run
}
