#pragma once

#include <QString>
#include <QStringList>
#include <memory>

class QWidget;
namespace WorkspaceFileOps {

class WorkspaceCopyCancellation {
public:
    WorkspaceCopyCancellation();
    ~WorkspaceCopyCancellation();

    void cancel();
    bool isCancelled() const;

    // Publishes the calling worker thread so cancel() can interrupt a
    // synchronous Windows filesystem request with CancelSynchronousIo().
    // Kept public so the cancellation boundary can be tested without a real
    // failing network device; copyToDirectory() is the production caller.
    bool beginBlockingOperation();
    void endBlockingOperation();

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

// Free-function file operations used by the Workspace panel (and by any
// other code that wants an "Reveal in Explorer" affordance): copy, move,
// delete, create, and collision-safe naming. All paths are absolute. Widget
// parameters are only used to parent confirmation dialogs.
// Supported writing-related file extensions (lower-case, no leading dot).
// Used by the Workspace tree/search filter when "Show All" is off. Kept as
// a single list here so more writing file types can be added later without
// touching filtering logic elsewhere.
QStringList writingFileExtensions();

// Returns nameFilters for QFileSystemModel/QDirIterator, e.g. {"*.md", ...}.
QStringList writingFileNameFilters();

// True if the given (file, not directory) path has a writing-related extension.
bool isWritingFile(const QString& filePath);

// Writing extensions plus FastMD's own export formats (PDF, DOCX, HTML).
// Used by the Workspace tree/search filter so exported files remain visible
// alongside the writing files they were produced from.
QStringList visibleFileExtensions();

// Returns nameFilters for QFileSystemModel/QDirIterator covering
// visibleFileExtensions(), e.g. {"*.md", ..., "*.pdf", "*.docx", "*.html"}.
QStringList visibleFileNameFilters();

// True if the given (file, not directory) path has a writing-related
// extension or a supported export extension (PDF/DOCX/HTML).
bool isVisibleFile(const QString& filePath);

// Given a desired destination path, returns a path guaranteed not to collide
// with an existing file/folder by appending " (2)", " (3)", etc. before the
// extension (Explorer-style), or the original path unchanged if it's free.
QString uniqueDestinationPath(const QString& desiredPath);

// Recursively copies a file or directory to destDir (a directory that must
// already exist). Returns the final destination path (collision-resolved),
// or an empty string on failure.
QString copyToDirectory(const QString& sourcePath, const QString& destDir,
                        WorkspaceCopyCancellation* cancelled = nullptr);

// Moves (renames) a file or directory to destDir, resolving name collisions.
// Returns the final destination path, or an empty string on failure.
QString moveToDirectory(const QString& sourcePath, const QString& destDir);

// Renames a file or directory in place. Returns the new path, or empty on failure.
QString renamePath(const QString& sourcePath, const QString& newName);

// Sends a file or directory to the Recycle Bin (Windows Shell trash), rather
// than deleting it permanently.
bool deletePath(const QString& path);

// Creates an empty file named baseName (collision-resolved) inside dir.
// Returns the created path, or empty on failure.
QString createFile(const QString& dir, const QString& baseName = QStringLiteral("Untitled.md"));

// Creates a new folder named baseName (collision-resolved) inside dir.
// Returns the created path, or empty on failure.
QString createFolder(const QString& dir, const QString& baseName = QStringLiteral("New folder"));

// Reveals a file (selected) or opens a folder in Windows Explorer.
void revealInExplorer(const QString& path, bool isDir);
void revealFileInExplorer(const QString& filePath);
void revealWorkspaceInExplorer(const QString& workspacePath);

// Opens a directory in the user's external Windows Terminal when available,
// falling back to Command Prompt. This is the Command Palette's deliberate
// "Open Active File Folder in External Terminal" exception; every other folder-terminal
// action in the app opens FastMD's own Internal Terminal instead.
// Returns false when the directory is invalid or neither terminal launches.
bool openFolderInTerminal(const QString& folderPath);

// True if name contains none of \ / : * ? " < > | and is non-empty/non-"."/"..".
bool isValidFileName(const QString& name);

}
