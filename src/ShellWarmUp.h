#pragma once

class QString;

// The first shell calls in a process -- a drive's display name, an icon
// -- load and initialize the Windows shell. Left alone, that happens
// on the GUI thread inside QFileSystemModel::setRootPath() and the Workspace
// tree's first paint. This makes the same calls once on a background thread, so
// the GUI thread's own calls find the shell ready. It is only a warm-up: nothing
// is kept, and every result is looked up again by whoever needs it.
namespace ShellWarmUp {

// Starts the thread for the drive `folder` is on. Only the first call in the
// process does anything. GUI thread only.
void start(const QString& folder);

// True once there is nothing left to wait for. Stays false while a shell call is
// blocked (an unavailable network drive), so a caller must bound its wait.
bool finished();

}
