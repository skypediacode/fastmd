#include "ShellWarmUp.h"
#include "AppShutdown.h"

#include <QString>

#include <atomic>
#include <memory>
#include <process.h>
#include <string>

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

namespace {

// Trivially destructible, so the thread may still touch it while the process
// exits.
std::atomic<bool> s_finished { false };

// The flags Qt's icon engine uses: the overlays are part of the cost. By
// attributes alone, so it needs no file system access. One icon is enough;
// warming a file type's icon as well gained nothing.
void warmIcons()
{
    SHFILEINFOW info {};
    if (SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof info,
                       SHGFI_ICON | SHGFI_SMALLICON | SHGFI_SYSICONINDEX | SHGFI_ADDOVERLAYS
                           | SHGFI_OVERLAYINDEX | SHGFI_USEFILEATTRIBUTES)
        && info.hIcon)
        DestroyIcon(info.hIcon);
}

// QFileSystemModel asks the shell for the display name of the root's drive.
void warmDriveName(const std::wstring& drive)
{
    IShellItem* item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(drive.c_str(), nullptr, IID_PPV_ARGS(&item))))
        return;
    wchar_t* name = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)))
        CoTaskMemFree(name);
    item->Release();
}

// Win32 only, on a plain thread rather than a QThreadPool one: a shell call
// cannot be interrupted, and the pool is waited for at exit. It owns everything
// it uses and reports through s_finished alone, so no object's lifetime matters.
unsigned __stdcall run(void* argument)
{
    const std::unique_ptr<std::wstring> drive(static_cast<std::wstring*>(argument));
    // Like Qt's own shell icon thread: shell extensions expect an STA.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!AppShutdown::started())
        warmIcons();
    // Last: the one call that can block on the drive itself.
    if (!drive->empty() && !AppShutdown::started())
        warmDriveName(*drive);

    if (SUCCEEDED(com))
        CoUninitialize();
    s_finished.store(true, std::memory_order_release);
    return 0;
}

}

void ShellWarmUp::start(const QString& folder)
{
    static bool started = false;
    if (started)
        return;
    started = true;

    // Only a lettered drive has a display name that Qt asks the shell for.
    auto drive = std::make_unique<std::wstring>();
    if (folder.size() >= 2 && folder.at(1) == QLatin1Char(':'))
        *drive = folder.left(2).toStdWString() + L'\\';

    const auto thread = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, run, drive.get(), 0, nullptr));
    if (!thread) {
        s_finished.store(true, std::memory_order_release);
        return;
    }
    (void)drive.release();
    CloseHandle(thread);
}

bool ShellWarmUp::finished()
{
    return s_finished.load(std::memory_order_acquire);
}
