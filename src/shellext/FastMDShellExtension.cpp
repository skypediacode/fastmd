// "Edit with FastMD" Explorer context-menu command, registered purely through
// the MSIX manifest (windows.comServer + windows.fileExplorerContextMenus --
// see scripts/build-msix.ps1). No Qt, no CRT-heavy work, no blocking calls:
// this DLL is loaded into Explorer's (or a DllHost surrogate's) process, so it
// must stay small and fast. It never opens a file or folder itself -- it
// starts FastMD.exe <path> and lets the existing single-instance forwarding
// (src/main.cpp) and startup-path handling (MainWindow::openFilePaths) do the
// rest. See dev-docs/SHELL_EXTENSION.md.

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shlguid.h>
#include <servprov.h>
#include <objbase.h>
#include <appmodel.h>

#include "../WinCommandLine.h"

#include <atomic>
#include <new>
#include <string>

using WinCommandLine::quoteArgument;

namespace {

// {74C889E4-66D1-4D59-B0FB-114B1740A1F8} -- keep in sync with
// $ShellExtensionClsid in scripts/build-msix.ps1.
const CLSID CLSID_FastMDExplorerCommand = {
    0x74c889e4, 0x66d1, 0x4d59, { 0xb0, 0xfb, 0x11, 0x4b, 0x17, 0x40, 0xa1, 0xf8 }
};

HMODULE g_hModule = nullptr;
std::atomic<LONG> g_moduleRefCount{ 0 };

void ModuleAddRef() { ++g_moduleRefCount; }
void ModuleRelease() { --g_moduleRefCount; }

// Resolves the single selected item to a local filesystem path, applying the
// exact same eligibility rules Invoke() must re-check before launching:
// exactly one item, backed by a real file or folder path. Like Notepad++, the
// command supports every file regardless of extension -- there is no
// extension allowlist here. Folders are eligible too: FastMD.exe opens a
// folder path by setting it as the Workspace root (MainWindow::openFilePaths),
// so "Edit with FastMD" on a folder opens it as the workspace. Returns an
// empty string when any check fails.
std::wstring resolveEligiblePath(IShellItemArray* items)
{
    if (!items)
        return {};

    DWORD count = 0;
    if (FAILED(items->GetCount(&count)) || count != 1)
        return {};

    IShellItem* item = nullptr;
    if (FAILED(items->GetItemAt(0, &item)) || !item)
        return {};

    std::wstring result;
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        result.assign(path);
        CoTaskMemFree(path);
    }
    item->Release();
    return result;
}

// Right-clicking empty space in a folder (the "Directory\Background" item
// type) selects nothing, so Explorer invokes GetState/Invoke with a null or
// empty IShellItemArray -- resolveEligiblePath() then has nothing to resolve
// and the command stays ECS_HIDDEN. The folder being browsed is only
// reachable through the site Explorer hands the command via IObjectWithSite:
// IServiceProvider -> SID_SFolderView (IFolderView2) -> GetFolder ->
// IPersistFolder2::GetCurFolder gives the PIDL of the folder on screen.
std::wstring resolveBackgroundPath(IUnknown* site)
{
    if (!site)
        return {};

    IServiceProvider* serviceProvider = nullptr;
    if (FAILED(site->QueryInterface(IID_PPV_ARGS(&serviceProvider))) || !serviceProvider)
        return {};

    IFolderView2* folderView = nullptr;
    HRESULT hr = serviceProvider->QueryService(SID_SFolderView, IID_PPV_ARGS(&folderView));
    serviceProvider->Release();
    if (FAILED(hr) || !folderView)
        return {};

    IPersistFolder2* persistFolder = nullptr;
    hr = folderView->GetFolder(IID_PPV_ARGS(&persistFolder));
    folderView->Release();
    if (FAILED(hr) || !persistFolder)
        return {};

    LPITEMIDLIST pidl = nullptr;
    hr = persistFolder->GetCurFolder(&pidl);
    persistFolder->Release();
    if (FAILED(hr) || !pidl)
        return {};

    std::wstring result;
    WCHAR buffer[MAX_PATH];
    if (SHGetPathFromIDListW(pidl, buffer))
        result = buffer;
    CoTaskMemFree(pidl);
    return result;
}

std::wstring siblingExecutablePath()
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(g_hModule, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};

    path.resize(length);
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return {};
    path.resize(separator + 1);
    path += L"FastMD.exe";
    return path;
}

void launchFastMD(const std::wstring& filePath)
{
    if (filePath.empty())
        return;

    // Primary: packaged activation via AUMID, so the launched process keeps
    // its package identity regardless of install location.
    IApplicationActivationManager* activationManager = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER,
                                    IID_PPV_ARGS(&activationManager)))) {
        WCHAR familyName[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1] = {};
        UINT32 familyNameLength = ARRAYSIZE(familyName);
        if (GetCurrentPackageFamilyName(&familyNameLength, familyName) == ERROR_SUCCESS) {
            std::wstring aumid(familyName);
            aumid += L"!FASTMD";

            const std::wstring quotedPath = quoteArgument(filePath);
            DWORD processId = 0;
            const HRESULT activated = activationManager->ActivateApplication(
                aumid.c_str(), quotedPath.c_str(), AO_NONE, &processId);
            activationManager->Release();
            if (SUCCEEDED(activated))
                return;
        } else {
            activationManager->Release();
        }
    }

    // Fallback: launch the sibling exe directly (covers a surrogate that
    // somehow lacks package identity). Fail silently either way -- this is a
    // context-menu command, it must never pop UI of its own.
    const std::wstring executablePath = siblingExecutablePath();
    if (executablePath.empty())
        return;

    std::wstring commandLine = quoteArgument(executablePath);
    commandLine.push_back(L' ');
    commandLine += quoteArgument(filePath);

    STARTUPINFOW startupInfo{ sizeof(startupInfo) };
    PROCESS_INFORMATION processInfo{};
    if (CreateProcessW(executablePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr, nullptr,
                        &startupInfo, &processInfo)) {
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
    }
}

class FastMDExplorerCommand final : public IExplorerCommand, public IObjectWithSite {
public:
    FastMDExplorerCommand() { ModuleAddRef(); }
    ~FastMDExplorerCommand()
    {
        if (m_site)
            m_site->Release();
        ModuleRelease();
    }

    // IUnknown
    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IExplorerCommand) {
            *ppv = static_cast<IExplorerCommand*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == IID_IObjectWithSite) {
            *ppv = static_cast<IObjectWithSite*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return ++m_refCount; }
    ULONG __stdcall Release() override
    {
        const ULONG remaining = --m_refCount;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    // IObjectWithSite
    HRESULT __stdcall SetSite(IUnknown* site) override
    {
        if (m_site)
            m_site->Release();
        m_site = site;
        if (m_site)
            m_site->AddRef();
        return S_OK;
    }

    HRESULT __stdcall GetSite(REFIID riid, void** site) override
    {
        if (!site)
            return E_POINTER;
        *site = nullptr;
        if (!m_site)
            return E_FAIL;
        return m_site->QueryInterface(riid, site);
    }

    // IExplorerCommand
    HRESULT __stdcall GetTitle(IShellItemArray*, LPWSTR* name) override
    {
        return SHStrDupW(L"Edit with FastMD", name);
    }

    HRESULT __stdcall GetIcon(IShellItemArray*, LPWSTR* icon) override
    {
        const std::wstring modulePath = [] {
            std::wstring path(32768, L'\0');
            const DWORD length = GetModuleFileNameW(g_hModule, path.data(), static_cast<DWORD>(path.size()));
            path.resize(length);
            return path;
        }();
        std::wstring iconLocation = modulePath;
        iconLocation += L",-101";
        return SHStrDupW(iconLocation.c_str(), icon);
    }

    HRESULT __stdcall GetToolTip(IShellItemArray*, LPWSTR* infoTip) override
    {
        if (infoTip)
            *infoTip = nullptr;
        return E_NOTIMPL;
    }

    HRESULT __stdcall GetCanonicalName(GUID* commandName) override
    {
        if (commandName)
            *commandName = GUID_NULL;
        return E_NOTIMPL;
    }

    HRESULT __stdcall GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override
    {
        if (!state)
            return E_POINTER;
        std::wstring path = resolveEligiblePath(items);
        if (path.empty())
            path = resolveBackgroundPath(m_site);
        *state = path.empty() ? ECS_HIDDEN : ECS_ENABLED;
        return S_OK;
    }

    HRESULT __stdcall GetFlags(EXPCMDFLAGS* flags) override
    {
        if (!flags)
            return E_POINTER;
        *flags = ECF_DEFAULT;
        return S_OK;
    }

    HRESULT __stdcall EnumSubCommands(IEnumExplorerCommand** enumCommands) override
    {
        if (enumCommands)
            *enumCommands = nullptr;
        return E_NOTIMPL;
    }

    HRESULT __stdcall Invoke(IShellItemArray* items, IBindCtx*) override
    {
        std::wstring path = resolveEligiblePath(items);
        if (path.empty())
            path = resolveBackgroundPath(m_site);
        launchFastMD(path);
        return S_OK;
    }

private:
    std::atomic<ULONG> m_refCount{ 1 };
    IUnknown* m_site = nullptr;
};

class FastMDClassFactory final : public IClassFactory {
public:
    FastMDClassFactory() { ModuleAddRef(); }
    ~FastMDClassFactory() { ModuleRelease(); }

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return ++m_refCount; }
    ULONG __stdcall Release() override
    {
        const ULONG remaining = --m_refCount;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    HRESULT __stdcall CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        *ppv = nullptr;
        if (outer)
            return CLASS_E_NOAGGREGATION;

        auto* command = new (std::nothrow) FastMDExplorerCommand();
        if (!command)
            return E_OUTOFMEMORY;
        const HRESULT hr = command->QueryInterface(riid, ppv);
        command->Release();
        return hr;
    }

    HRESULT __stdcall LockServer(BOOL lock) override
    {
        if (lock)
            ModuleAddRef();
        else
            ModuleRelease();
        return S_OK;
    }

private:
    std::atomic<ULONG> m_refCount{ 1 };
};

}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv)
{
    if (!ppv)
        return E_POINTER;
    *ppv = nullptr;
    if (clsid != CLSID_FastMDExplorerCommand)
        return CLASS_E_CLASSNOTAVAILABLE;

    auto* factory = new (std::nothrow) FastMDClassFactory();
    if (!factory)
        return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow()
{
    return g_moduleRefCount.load() == 0 ? S_OK : S_FALSE;
}
