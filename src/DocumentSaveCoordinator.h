#pragma once

#include <QByteArray>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QString>

#include <atomic>
#include <memory>

namespace DocumentSaveCoordinator {

struct State {
    QMutex commitMutex;
    std::atomic<quint64> epoch{0};
};

using StatePtr = std::shared_ptr<State>;

enum class WriteResult {
    Success,
    Failed,
    Superseded,
};

inline quint64 beginWrite(const StatePtr& state)
{
    return state->epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
}

inline void invalidate(const StatePtr& state)
{
    state->epoch.fetch_add(1, std::memory_order_acq_rel);
}

inline WriteResult writeLatest(const QString& path, const QByteArray& bytes,
                               const StatePtr& state, quint64 ticket)
{
    QMutexLocker lock(&state->commitMutex);
    if (state->epoch.load(std::memory_order_acquire) != ticket)
        return WriteResult::Superseded;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return WriteResult::Failed;
    }
    return file.commit() ? WriteResult::Success : WriteResult::Failed;
}

inline WriteResult writeLatestInBackground(const QString& path, const QByteArray& bytes,
                                           const StatePtr& state, quint64 ticket)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return WriteResult::Failed;
    }

    QMutexLocker lock(&state->commitMutex);
    if (state->epoch.load(std::memory_order_acquire) != ticket) {
        file.cancelWriting();
        return WriteResult::Superseded;
    }
    return file.commit() ? WriteResult::Success : WriteResult::Failed;
}

} // namespace DocumentSaveCoordinator
