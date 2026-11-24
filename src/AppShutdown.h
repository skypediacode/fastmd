#pragma once

#include <QAtomicInt>

// Process-wide "the application is exiting" flag, set once from main() as soon
// as the event loop returns.
//
// Why this exists: QThreadPool::globalInstance() is destroyed during static
// destruction and its destructor calls waitForDone() with no timeout, so any
// QtConcurrent task still running when main() returns blocks process exit
// forever. The Workspace panel's recursive directory walks are unbounded by
// design (see WorkspaceTreeFilterModel::scanForVisibleDescendant) and can run
// for minutes on a large or network tree, which turns a normal quit into a
// hung, windowless process. Long-running workers poll this and bail out.
namespace AppShutdown {

inline QAtomicInt& flag()
{
    static QAtomicInt s_flag(0);
    return s_flag;
}

inline void begin() { flag().storeRelease(1); }

inline bool started() { return flag().loadAcquire() != 0; }

} // namespace AppShutdown
