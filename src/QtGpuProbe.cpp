#include "QtGpuProbe.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstring>
#endif

// Qt's Windows platform plugin positions every top-level Qt::Window through
// calcPosition(), which runs the QTBUG-50371 workaround once per process (and
// again after each display or settings change): GpuDescription::detect()
// calls Direct3DCreate9() to read the default adapter's vendor. That loads,
// signature-checks and initializes the full Direct3D 9 user-mode display
// driver -- ~100 ms on an NVIDIA machine, on the GUI thread, before the first
// window can exist -- and the driver is unloaded again as soon as the handle
// is released. The answer only matters to OpenGL windows on AMD-default
// multi-GPU systems. FastMD never creates an OpenGL or RHI surface in-process
// (the preview renders inside WebView2's own processes), so the plugin's
// import is pointed at a stub reporting "no Direct3D 9". detect() then returns
// an empty description, exactly as on a machine without D3D9.
namespace QtGpuProbe {

#ifdef Q_OS_WIN
namespace {

void* WINAPI noDirect3D9(UINT)
{
    return nullptr;
}

}
#endif

bool suppress()
{
#ifdef Q_OS_WIN
    auto* base = reinterpret_cast<BYTE*>(GetModuleHandleW(L"qwindows.dll"));
    if (!base)
        return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress)
        return false;

    bool patched = false;
    for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
         desc->Name; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), "d3d9.dll") != 0
            || !desc->OriginalFirstThunk)
            continue;
        auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), "Direct3DCreate9") != 0)
                continue;
            DWORD protection = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &protection))
                return false;
            slots->u1.Function = reinterpret_cast<ULONG_PTR>(&noDirect3D9);
            VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), protection, &protection);
            patched = true;
        }
    }
    return patched;
#else
    return false;
#endif
}

}
