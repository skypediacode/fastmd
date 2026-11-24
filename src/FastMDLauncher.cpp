#include <windows.h>
#include <shellapi.h>

#include "WinCommandLine.h"

#include <string>
#include <string_view>

// The App Execution Alias target (`fastmd`). It is a console-subsystem image
// on purpose: cmd and PowerShell decide whether to wait for a program, and
// whether to report its exit code, from the PE subsystem of the image they
// start, so only a console image lets `fastmd a.md -o a.pdf` behave like a
// normal command. FastMDLauncher.manifest's `detached` console allocation
// policy keeps Win+R and shortcut launches from allocating a console on
// Windows 11 24H2+; releaseOwnConsole() covers older systems.
namespace {

using WinCommandLine::quoteArgument;

HANDLE g_exportJob = nullptr;

std::wstring siblingExecutablePath()
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
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

// Started without a terminal on a system that ignores the manifest policy,
// Windows gave this process a console of its own; drop it at once so the
// window is a brief flash rather than lingering.
void releaseOwnConsole()
{
    DWORD processes[2];
    if (GetConsoleProcessList(processes, 2) == 1)
        FreeConsole();
}

bool isRedirected(HANDLE handle)
{
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return false;
    const DWORD type = GetFileType(handle);
    return type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE;
}

void writeError(const std::wstring& message)
{
    const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (!error || error == INVALID_HANDLE_VALUE)
        return;
    const std::wstring line = L"FastMD: " + message + L"\n";
    DWORD written = 0;
    DWORD mode = 0;
    if (GetConsoleMode(error, &mode)) {
        WriteConsoleW(error, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        return;
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                                          nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), utf8.data(), bytes,
                        nullptr, nullptr);
    WriteFile(error, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

// Ctrl+C, Ctrl+Break or closing the terminal ends the whole export tree --
// FastMD.exe and the WebView2 or Edge processes it started -- instead of
// leaving it running headless after the shell has moved on.
BOOL WINAPI onConsoleControl(DWORD)
{
    TerminateJobObject(g_exportJob, STATUS_CONTROL_C_EXIT);
    return TRUE;
}

HANDLE createKillOnCloseJob()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

}

int wmain()
{
    releaseOwnConsole();

    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments)
        return static_cast<int>(GetLastError());

    const std::wstring executablePath = siblingExecutablePath();
    if (executablePath.empty()) {
        LocalFree(arguments);
        return ERROR_FILE_NOT_FOUND;
    }

    // Matches CliExport::isRequested(): routing only, FastMD.exe parses.
    std::wstring commandLine = quoteArgument(executablePath);
    bool commandLineExport = false;
    for (int index = 1; index < argumentCount; ++index) {
        const std::wstring_view argument(arguments[index]);
        if (argument == L"-o" || argument == L"--output")
            commandLineExport = true;
        commandLine.push_back(L' ');
        commandLine += quoteArgument(argument);
    }
    LocalFree(arguments);

    STARTUPINFOW startupInfo{ sizeof(startupInfo) };
    PROCESS_INFORMATION processInfo{};
    // GUI launches must return to the prompt at once and leave FastMD.exe
    // with no tie to this console.
    DWORD creationFlags = CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS;
    BOOL inheritHandles = FALSE;
    if (commandLineExport) {
        // FastMD.exe is a GUI image, so it never inherits this console; it
        // attaches to it (through this process) for error output. Only a
        // stderr the caller redirected to a file or pipe is handed over.
        creationFlags = CREATE_SUSPENDED;
        const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
        if (isRedirected(error)) {
            SetHandleInformation(error, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            startupInfo.dwFlags |= STARTF_USESTDHANDLES;
            startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            startupInfo.hStdError = error;
            inheritHandles = TRUE;
        }
        g_exportJob = createKillOnCloseJob();
    }
    const BOOL started = CreateProcessW(
        executablePath.c_str(), commandLine.data(), nullptr, nullptr, inheritHandles,
        creationFlags, nullptr, nullptr, &startupInfo, &processInfo);
    if (!started) {
        const DWORD error = GetLastError();
        if (commandLineExport)
            writeError(L"Could not start " + executablePath + L" (error " + std::to_wstring(error) + L").");
        return static_cast<int>(error);
    }

    DWORD exitCode = 0;
    if (commandLineExport) {
        if (g_exportJob && AssignProcessToJobObject(g_exportJob, processInfo.hProcess))
            SetConsoleCtrlHandler(onConsoleControl, TRUE);
        ResumeThread(processInfo.hThread);
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        if (!GetExitCodeProcess(processInfo.hProcess, &exitCode))
            exitCode = 1;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return static_cast<int>(exitCode);
}
