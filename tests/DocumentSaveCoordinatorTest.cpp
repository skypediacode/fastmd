#include "DocumentSaveCoordinator.h"
#include "FileChangeSuppressionTracker.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <cstdio>
#include <thread>

namespace {

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

} // namespace

int main()
{
    QTemporaryDir dir;
    if (!check(dir.isValid(), "temporary directory should be available"))
        return 1;

    const QString path = dir.filePath(QStringLiteral("document.md"));
    const auto state = std::make_shared<DocumentSaveCoordinator::State>();
    const quint64 autoSaveTicket = DocumentSaveCoordinator::beginWrite(state);

    state->commitMutex.lock();
    std::atomic<DocumentSaveCoordinator::WriteResult> autoSaveResult{
        DocumentSaveCoordinator::WriteResult::Failed};
    std::thread autoSave([&]() {
        autoSaveResult.store(DocumentSaveCoordinator::writeLatestInBackground(
            path, QByteArrayLiteral("stale autosave"), state, autoSaveTicket));
    });

    const quint64 explicitSaveTicket = DocumentSaveCoordinator::beginWrite(state);
    state->commitMutex.unlock();
    autoSave.join();

    bool ok = true;
    ok &= check(autoSaveResult.load() == DocumentSaveCoordinator::WriteResult::Superseded,
                "an older autosave must not commit after a newer save begins");
    ok &= check(DocumentSaveCoordinator::writeLatest(
                    path, QByteArrayLiteral("explicit save"), state, explicitSaveTicket)
                    == DocumentSaveCoordinator::WriteResult::Success,
                "the newer explicit save should commit");

    QFile file(path);
    ok &= check(file.open(QIODevice::ReadOnly), "saved document should be readable");
    ok &= check(file.readAll() == QByteArrayLiteral("explicit save"),
                "the explicit save must be the final on-disk content");

    const QString discardedPath = dir.filePath(QStringLiteral("discarded.md"));
    const auto discardState = std::make_shared<DocumentSaveCoordinator::State>();
    const quint64 discardedTicket = DocumentSaveCoordinator::beginWrite(discardState);
    discardState->commitMutex.lock();
    std::atomic<DocumentSaveCoordinator::WriteResult> discardedResult{
        DocumentSaveCoordinator::WriteResult::Failed};
    std::thread discardedAutoSave([&] {
        discardedResult.store(DocumentSaveCoordinator::writeLatestInBackground(
            discardedPath, QByteArrayLiteral("discard me"), discardState, discardedTicket));
    });
    DocumentSaveCoordinator::invalidate(discardState);
    discardState->commitMutex.unlock();
    discardedAutoSave.join();
    ok &= check(discardedResult.load() == DocumentSaveCoordinator::WriteResult::Superseded,
                "discard must supersede an autosave waiting at the commit boundary");
    ok &= check(!QFileInfo::exists(discardedPath),
                "discarded autosave must not create the destination");

    FileChangeSuppressionTracker suppressions;
    suppressions.arm(QStringLiteral("old.md"), 10);
    suppressions.arm(QStringLiteral("old.md"), 11);
    suppressions.clear(QStringLiteral("old.md"), 10);
    ok &= check(suppressions.matches(QStringLiteral("old.md"), 11),
                "an older completion must not clear a newer watcher suppression");
    suppressions.clear(QStringLiteral("old.md"), 11);
    ok &= check(!suppressions.consume(QStringLiteral("old.md")),
                "Save As cleanup must remove the original path suppression");
    return ok ? 0 : 1;
}
