#include "WorkspaceFileOps.h"
#include "FastMDFileTypes.h"
#include "ImageFormats.h"
#include "AppShutdown.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QSet>
#include <QAtomicInt>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>

#if defined(Q_OS_WIN)
#include <QProcess>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shlguid.h>
#include <string>
#else
#include <QDesktopServices>
#include <QUrl>
#endif

namespace WorkspaceFileOps {

struct WorkspaceCopyCancellation::Impl {
    QAtomicInt cancelled{0};
#if defined(Q_OS_WIN)
    QMutex mutex;
    HANDLE workerThread = nullptr;
#endif
};

WorkspaceCopyCancellation::WorkspaceCopyCancellation()
    : m_impl(std::make_shared<Impl>())
{
}

WorkspaceCopyCancellation::~WorkspaceCopyCancellation()
{
    cancel();
#if defined(Q_OS_WIN)
    QMutexLocker locker(&m_impl->mutex);
    if (m_impl->workerThread) {
        CloseHandle(m_impl->workerThread);
        m_impl->workerThread = nullptr;
    }
#endif
}

void WorkspaceCopyCancellation::cancel()
{
    m_impl->cancelled.storeRelease(1);
#if defined(Q_OS_WIN)
    QMutexLocker locker(&m_impl->mutex);
    if (m_impl->workerThread)
        CancelSynchronousIo(m_impl->workerThread);
#endif
}

bool WorkspaceCopyCancellation::isCancelled() const
{
    return m_impl->cancelled.loadAcquire() != 0;
}

bool WorkspaceCopyCancellation::beginBlockingOperation()
{
#if defined(Q_OS_WIN)
    HANDLE threadHandle = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                         &threadHandle, THREAD_TERMINATE, FALSE, 0)) {
        return false;
    }
    QMutexLocker locker(&m_impl->mutex);
    if (m_impl->workerThread)
        CloseHandle(m_impl->workerThread);
    m_impl->workerThread = threadHandle;
    if (isCancelled())
        CancelSynchronousIo(m_impl->workerThread);
#endif
    return !isCancelled();
}

void WorkspaceCopyCancellation::endBlockingOperation()
{
#if defined(Q_OS_WIN)
    QMutexLocker locker(&m_impl->mutex);
    if (m_impl->workerThread) {
        CloseHandle(m_impl->workerThread);
        m_impl->workerThread = nullptr;
    }
#endif
}

namespace {

#if defined(Q_OS_WIN)

// Qt's Windows platform plugin already puts the GUI thread in an STA, so this
// normally just balances a reference; RPC_E_CHANGED_MODE means somebody else
// owns the apartment and we must not uninitialize it.
class ComScope {
public:
    ComScope()
        : m_owned(SUCCEEDED(CoInitializeEx(
              nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {}
    ~ComScope() { if (m_owned) CoUninitialize(); }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
private:
    bool m_owned;
};

void bringWindowToFront(HWND hwnd)
{
    if (!hwnd)
        return;
    if (IsIconic(hwnd))
        ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

// SHOpenFolderAndSelectItems() reuses an Explorer window that already shows the
// folder, and a reused window keeps whatever scroll offset it had -- the item
// gets selected somewhere off-screen. Driving the live IShellView directly is
// the only way to ask for SVSI_ENSUREVISIBLE on that existing view.
bool selectInOpenExplorerWindow(PCIDLIST_ABSOLUTE folder, PCUITEMID_CHILD child)
{
    IShellWindows* windows = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(&windows))) || !windows)
        return false;

    bool selected = false;
    long count = 0;
    if (SUCCEEDED(windows->get_Count(&count))) {
        for (long i = 0; i < count && !selected; ++i) {
            VARIANT index;
            VariantInit(&index);
            index.vt = VT_I4;
            index.lVal = i;

            IDispatch* dispatch = nullptr;
            const HRESULT itemHr = windows->Item(index, &dispatch);
            VariantClear(&index);
            if (FAILED(itemHr) || !dispatch)
                continue;

            IServiceProvider* provider = nullptr;
            IShellBrowser* browser = nullptr;
            if (SUCCEEDED(dispatch->QueryInterface(IID_PPV_ARGS(&provider)))) {
                provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser));
                provider->Release();
            }
            dispatch->Release();
            if (!browser)
                continue;

            IShellView* view = nullptr;
            if (SUCCEEDED(browser->QueryActiveShellView(&view)) && view) {
                IFolderView* folderView = nullptr;
                if (SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&folderView)))) {
                    IPersistFolder2* persist = nullptr;
                    if (SUCCEEDED(folderView->GetFolder(IID_PPV_ARGS(&persist)))) {
                        PIDLIST_ABSOLUTE current = nullptr;
                        if (SUCCEEDED(persist->GetCurFolder(&current)) && current) {
                            if (ILIsEqual(current, folder)
                                && SUCCEEDED(view->SelectItem(
                                       child, SVSI_SELECT | SVSI_DESELECTOTHERS
                                                  | SVSI_ENSUREVISIBLE | SVSI_FOCUSED))) {
                                selected = true;
                                HWND hwnd = nullptr;
                                if (SUCCEEDED(browser->GetWindow(&hwnd)))
                                    bringWindowToFront(hwnd);
                            }
                            CoTaskMemFree(current);
                        }
                        persist->Release();
                    }
                    folderView->Release();
                }
                view->Release();
            }
            browser->Release();
        }
    }

    windows->Release();
    return selected;
}

bool selectAndScrollToInExplorer(const QString& nativePath)
{
    const std::wstring wide = nativePath.toStdWString();

    ComScope com;

    PIDLIST_ABSOLUTE item = nullptr;
    if (FAILED(SHParseDisplayName(wide.c_str(), nullptr, &item, 0, nullptr)) || !item)
        return false;

    bool handled = false;
    PIDLIST_ABSOLUTE folder = ILCloneFull(item);
    if (folder && ILRemoveLastID(folder))
        handled = selectInOpenExplorerWindow(folder, ILFindLastID(item));

    // No window on that folder yet: a fresh one scrolls to the selection itself.
    if (!handled)
        handled = SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0));

    if (folder)
        CoTaskMemFree(folder);
    CoTaskMemFree(item);
    return handled;
}

#endif // Q_OS_WIN

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toCaseFolded();
}

bool isSameOrChildPath(const QString& path, const QString& possibleParent)
{
    const QString child = normalizedPath(path);
    const QString parent = normalizedPath(possibleParent);
    return child == parent || child.startsWith(parent + QLatin1Char('/'));
}

// Built once: isWritingFile() runs per directory entry while filtering and
// searching large trees, so it must not allocate a list on every call.
const QStringList& extensionList()
{
#define FASTMD_EXTENSION_ENTRY(ext) QStringLiteral(ext),
    static const QStringList kExtensions = {
        FASTMD_WRITING_EXTENSIONS(FASTMD_EXTENSION_ENTRY)
    };
#undef FASTMD_EXTENSION_ENTRY
    return kExtensions;
}

// Export formats produced by FastMD that should also show up in the
// Workspace tree/search filter alongside writing files, even though they
// aren't themselves editable writing files (see isWritingFile()).
const QStringList& exportExtensionList()
{
    static const QStringList kExtensions = {
        // svg here is FastMD's own standalone Mind Map SVG export (see
        // PreviewWidget::saveMindMapSvgAs) -- no longer covered by
        // imageExtensionList() below now that .svg has its own WebView2
        // viewer mode (EditorMode::Svg) instead of being a raster image.
        QStringLiteral("pdf"), QStringLiteral("docx"), QStringLiteral("html"), QStringLiteral("svg")
    };
    return kExtensions;
}

// Images shown by the Workspace tree/search filter when Show All is off.
const QStringList& imageExtensionList()
{
    static const QStringList kExtensions = [] {
        QStringList list;
        const QSet<QString>& extensions = ImageFormats::supportedExtensions();
        list.reserve(extensions.size());
        for (const QString& ext : extensions)
            list << ext;
        return list;
    }();
    return kExtensions;
}

bool matchesExtension(const QString& filePath, const QStringList& extensions)
{
    // Deliberately string-only (no QFileInfo): this is called for every entry
    // of every scanned directory. Accepts a bare file name as well as a path.
    const qsizetype separator = std::max(filePath.lastIndexOf(QLatin1Char('/')),
                                         filePath.lastIndexOf(QLatin1Char('\\')));
    const qsizetype dot = filePath.lastIndexOf(QLatin1Char('.'));
    if (dot <= separator)
        return false;
    const QStringView suffix = QStringView(filePath).mid(dot + 1);
    for (const QString& ext : extensions) {
        if (suffix.compare(ext, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

// The visible set (writing + export + image extensions, ~22 entries) is
// checked per directory entry during tree filtering and recursive search, so
// it is precomputed once into a lowercase QSet for O(1) lookups rather than
// the linear QStringList scan matchesExtension() uses for the smaller
// writing-only set.
const QSet<QString>& visibleExtensionSet()
{
    static const QSet<QString> kSet = [] {
        QSet<QString> set;
        for (const QString& ext : extensionList())
            set.insert(ext.toLower());
        for (const QString& ext : exportExtensionList())
            set.insert(ext.toLower());
        for (const QString& ext : imageExtensionList())
            set.insert(ext.toLower());
        return set;
    }();
    return kSet;
}

bool matchesExtensionSet(const QString& filePath, const QSet<QString>& extensions)
{
    const qsizetype separator = std::max(filePath.lastIndexOf(QLatin1Char('/')),
                                         filePath.lastIndexOf(QLatin1Char('\\')));
    const qsizetype dot = filePath.lastIndexOf(QLatin1Char('.'));
    if (dot <= separator)
        return false;
    return extensions.contains(QStringView(filePath).mid(dot + 1).toString().toLower());
}

}

QStringList writingFileExtensions()
{
    return extensionList();
}

QStringList writingFileNameFilters()
{
    QStringList filters;
    const QStringList& extensions = extensionList();
    filters.reserve(extensions.size());
    for (const QString& ext : extensions)
        filters << QStringLiteral("*.") + ext;
    return filters;
}

bool isWritingFile(const QString& filePath)
{
    return matchesExtension(filePath, extensionList());
}

QStringList visibleFileExtensions()
{
    // Built once: the three source lists are compile-time constants, and this is
    // reached from WorkspaceTreeFilterModel's descendant probe — a path that can
    // run while the tree is painting.
    static const QStringList kExtensions =
        extensionList() + exportExtensionList() + imageExtensionList();
    return kExtensions;
}

QStringList visibleFileNameFilters()
{
    static const QStringList kFilters = []() {
        QStringList filters;
        const QStringList extensions = visibleFileExtensions();
        filters.reserve(extensions.size());
        for (const QString& ext : extensions)
            filters << QStringLiteral("*.") + ext;
        return filters;
    }();
    return kFilters;
}

bool isVisibleFile(const QString& filePath)
{
    return matchesExtensionSet(filePath, visibleExtensionSet());
}

QString uniqueDestinationPath(const QString& desiredPath)
{
    if (!QFileInfo::exists(desiredPath))
        return desiredPath;

    const QFileInfo info(desiredPath);
    const QString dir = info.absolutePath();
    const QString baseName = info.completeBaseName();
    const QString suffix = info.suffix();

    for (int n = 2; ; ++n) {
        const QString candidateName = suffix.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(baseName).arg(n)
            : QStringLiteral("%1 (%2).%3").arg(baseName).arg(n).arg(suffix);
        const QString candidate = QDir(dir).filePath(candidateName);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
}

static QString collisionCandidate(const QString& desiredPath, int ordinal)
{
    if (ordinal == 1)
        return desiredPath;
    const QFileInfo info(desiredPath);
    const QString suffix = info.suffix();
    const QString candidateName = suffix.isEmpty()
        ? QStringLiteral("%1 (%2)").arg(info.completeBaseName()).arg(ordinal)
        : QStringLiteral("%1 (%2).%3").arg(info.completeBaseName()).arg(ordinal).arg(suffix);
    return QDir(info.absolutePath()).filePath(candidateName);
}

static std::unique_ptr<QFile> reserveDestinationFile(const QString& desiredPath,
                                                     QString& reservedPath)
{
    for (int ordinal = 1; ; ++ordinal) {
        const QString candidate = collisionCandidate(desiredPath, ordinal);
        auto file = std::make_unique<QFile>(candidate);
        if (file->open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            reservedPath = candidate;
            return file;
        }
        if (!QFileInfo::exists(candidate))
            return {};
    }
}

static QString reserveDestinationDirectory(const QString& desiredPath)
{
    for (int ordinal = 1; ; ++ordinal) {
        const QString candidate = collisionCandidate(desiredPath, ordinal);
        if (QDir().mkdir(candidate))
            return candidate;
        if (!QFileInfo::exists(candidate))
            return {};
    }
}

static bool copyFileContents(const QString& sourcePath, QFile& destination,
                             WorkspaceCopyCancellation* cancelled)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
        return false;
    constexpr qsizetype kChunkSize = 1024 * 1024;
    while (!source.atEnd()) {
        if ((cancelled && cancelled->isCancelled()) || AppShutdown::started())
            return false;
        const QByteArray chunk = source.read(kChunkSize);
        if ((chunk.isEmpty() && source.error() != QFileDevice::NoError)
            || destination.write(chunk) != chunk.size()) {
            return false;
        }
    }
    return destination.flush();
}

static bool copyRecursively(const QString& sourcePath, const QString& destPath,
                            QSet<QString>& visitedDirectories,
                            WorkspaceCopyCancellation* cancelled,
                            bool destinationReserved = false)
{
    if ((cancelled && cancelled->isCancelled()) || AppShutdown::started())
        return false;
    const QFileInfo srcInfo(sourcePath);
    // Following a directory link can walk outside the selected tree or loop
    // back into an ancestor. A failed item is surfaced by the caller.
    if (srcInfo.isSymLink() || !srcInfo.symLinkTarget().isEmpty()
#if defined(Q_OS_WIN)
        || srcInfo.isJunction() || !srcInfo.junctionTarget().isEmpty()
#endif
    )
        return false;
    if (srcInfo.isDir()) {
        QString identity = srcInfo.canonicalFilePath();
        if (identity.isEmpty())
            identity = srcInfo.absoluteFilePath();
        identity = QDir::cleanPath(identity).toCaseFolded();
        if (visitedDirectories.contains(identity))
            return false;
        visitedDirectories.insert(identity);

        if (!destinationReserved && !QDir().mkdir(destPath))
            return false;

        QDirIterator it(sourcePath, QDir::AllEntries | QDir::NoDotAndDotDot);
        bool ok = true;
        while (it.hasNext()) {
            if ((cancelled && cancelled->isCancelled()) || AppShutdown::started())
                return false;
            it.next();
            const QString childDest = QDir(destPath).filePath(it.fileName());
            ok = copyRecursively(it.filePath(), childDest, visitedDirectories, cancelled) && ok;
        }
        return ok;
    }

    QFile destination(destPath);
    if (!destination.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return false;
    if (!copyFileContents(sourcePath, destination, cancelled)) {
        destination.close();
        QFile::remove(destPath);
        return false;
    }
    destination.close();
    QFile::setPermissions(destPath, srcInfo.permissions());
    QFile copied(destPath);
    if (copied.open(QIODevice::ReadWrite)) {
        copied.setFileTime(srcInfo.fileTime(QFileDevice::FileAccessTime),
                           QFileDevice::FileAccessTime);
        copied.setFileTime(srcInfo.fileTime(QFileDevice::FileModificationTime),
                           QFileDevice::FileModificationTime);
    }
    return true;
}

QString copyToDirectory(const QString& sourcePath, const QString& destDir,
                        WorkspaceCopyCancellation* cancelled)
{
    const QFileInfo srcInfo(sourcePath);
    const QFileInfo destInfo(destDir);
    if (!srcInfo.exists() || !destInfo.isDir() || !destInfo.isWritable())
        return QString();
    if (srcInfo.isDir() && isSameOrChildPath(destDir, sourcePath))
        return QString();
    const QString desired = QDir(destDir).filePath(srcInfo.fileName());

    struct BlockingOperation {
        WorkspaceCopyCancellation* cancellation = nullptr;
        ~BlockingOperation() { if (cancellation) cancellation->endBlockingOperation(); }
    } blockingOperation { cancelled };
    if (cancelled && !cancelled->beginBlockingOperation())
        return QString();

    if (srcInfo.isSymLink() || !srcInfo.symLinkTarget().isEmpty()
#if defined(Q_OS_WIN)
        || srcInfo.isJunction() || !srcInfo.junctionTarget().isEmpty()
#endif
    )
        return QString();

    QSet<QString> visitedDirectories;
    if (srcInfo.isDir()) {
        const QString dest = reserveDestinationDirectory(desired);
        if (dest.isEmpty()
            || !copyRecursively(sourcePath, dest, visitedDirectories, cancelled, true)) {
            return QString();
        }
        return dest;
    }

    QString dest;
    std::unique_ptr<QFile> destination = reserveDestinationFile(desired, dest);
    if (!destination)
        return QString();
    if (!copyFileContents(sourcePath, *destination, cancelled)) {
        destination->close();
        QFile::remove(dest);
        return QString();
    }
    destination->close();
    QFile::setPermissions(dest, srcInfo.permissions());
    QFile copied(dest);
    if (copied.open(QIODevice::ReadWrite)) {
        copied.setFileTime(srcInfo.fileTime(QFileDevice::FileAccessTime),
                           QFileDevice::FileAccessTime);
        copied.setFileTime(srcInfo.fileTime(QFileDevice::FileModificationTime),
                           QFileDevice::FileModificationTime);
    }
    return dest;
}

QString moveToDirectory(const QString& sourcePath, const QString& destDir)
{
    const QFileInfo srcInfo(sourcePath);
    const QFileInfo destInfo(destDir);
    if (!srcInfo.exists() || !destInfo.isDir() || !destInfo.isWritable())
        return QString();
    if (srcInfo.isDir() && isSameOrChildPath(destDir, sourcePath))
        return QString();
    const QString desired = QDir(destDir).filePath(srcInfo.fileName());
    const QString dest = uniqueDestinationPath(desired);
    if (srcInfo.isDir()) {
        if (!QDir().rename(sourcePath, dest))
            return QString();
    } else {
        if (!QFile::rename(sourcePath, dest))
            return QString();
    }
    return dest;
}

QString renamePath(const QString& sourcePath, const QString& newName)
{
    const QFileInfo info(sourcePath);
    const QString dest = QDir(info.absolutePath()).filePath(newName);
    if (QFileInfo::exists(dest))
        return QString();
    if (info.isDir()) {
        if (!QDir().rename(sourcePath, dest))
            return QString();
    } else {
        if (!QFile::rename(sourcePath, dest))
            return QString();
    }
    return dest;
}

bool deletePath(const QString& path)
{
    return QFile::moveToTrash(path);
}

QString createFile(const QString& dir, const QString& baseName)
{
    const QString dest = uniqueDestinationPath(QDir(dir).filePath(baseName));
    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.close();
    return dest;
}

QString createFolder(const QString& dir, const QString& baseName)
{
    const QString dest = uniqueDestinationPath(QDir(dir).filePath(baseName));
    if (!QDir().mkpath(dest))
        return QString();
    return dest;
}

void revealInExplorer(const QString& path, bool isDir)
{
#if defined(Q_OS_WIN)
    if (isDir) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                 { QDir::toNativeSeparators(path) });
        return;
    }

    const QString native =
        QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());

    if (selectAndScrollToInExplorer(native))
        return;

    QStringList args;
    args << QStringLiteral("/select,") << native;
    QProcess::startDetached(QStringLiteral("explorer.exe"), args);
#else
    Q_UNUSED(isDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

bool openFolderInTerminal(const QString& folderPath)
{
#if defined(Q_OS_WIN)
    if (!QFileInfo(folderPath).isDir())
        return false;
    const QString folder = QFileInfo(folderPath).absoluteFilePath();
    const QString nativeFolder = QDir::toNativeSeparators(folder);
    const QStringList terminalArgs{ QStringLiteral("-d"), nativeFolder };
    if (QProcess::startDetached(QStringLiteral("wt.exe"), terminalArgs, folder))
        return true;

    // The Store installation exposes wt.exe as a per-user app execution alias.
    // It can be installed even when WindowsApps is absent from this process's PATH.
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        const QString wtAlias = QDir(localAppData).filePath(
            QStringLiteral("Microsoft/WindowsApps/wt.exe"));
        if (QProcess::startDetached(wtAlias, terminalArgs, folder))
            return true;
    }

    // Let Windows Shell create a visible console. cmd.exe has its own argument
    // quoting rules, so pass the folder as the working directory instead.
    return reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr, L"open", L"cmd.exe", nullptr,
        reinterpret_cast<LPCWSTR>(nativeFolder.utf16()), SW_SHOWNORMAL)) > 32;
#else
    Q_UNUSED(folderPath);
    return false;
#endif
}

void revealFileInExplorer(const QString& filePath)
{
    if (QFileInfo(filePath).isFile())
        revealInExplorer(filePath, false);
}

void revealWorkspaceInExplorer(const QString& workspacePath)
{
    if (QFileInfo(workspacePath).isDir())
        revealInExplorer(workspacePath, true);
}

bool isValidFileName(const QString& name)
{
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        return false;
    if (name.endsWith(QLatin1Char(' ')) || name.endsWith(QLatin1Char('.')))
        return false;
    static const QString invalid = QStringLiteral("\\/:*?\"<>|");
    for (const QChar& c : name) {
        if (invalid.contains(c) || c.unicode() < 32)
            return false;
    }
    const QString baseName = name.section(QLatin1Char('.'), 0, 0).toUpper();
    static const QStringList reserved = {
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9")
    };
    if (reserved.contains(baseName))
        return false;
    return true;
}

}
