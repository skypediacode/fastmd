#pragma once

#include <QThreadPool>

// A dedicated pool for export tasks (PDF/HTML/DOCX/Pandoc), separate from
// QThreadPool::globalInstance(). Export tasks block their worker for as long
// as a headless Chrome/Edge or Pandoc subprocess takes to finish -- up to
// tens of seconds (see waitForProcessBounded's 60s bound in
// ExportManagerPdf.cpp) -- not CPU work. The global pool is also used by
// MainWindow::updateCurrentHtmlAsync() for large-document preview conversion
// and by Workspace search/tree filtering; a slow export sitting on that same
// pool could starve those latency-sensitive tasks of a worker slot.
//
// This is a function-local static, so like QThreadPool::globalInstance() it
// is destroyed at static destruction time -- after main() has already called
// AppShutdown::begin() -- which is what lets waitForProcessBounded's polling
// loop bail out on shutdown instead of blocking process exit.
inline QThreadPool& exportThreadPool()
{
    static QThreadPool pool;
    static const bool initialized = [] {
        pool.setMaxThreadCount(2);
        return true;
    }();
    Q_UNUSED(initialized);
    return pool;
}
