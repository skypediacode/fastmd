// Records what a console-input reader actually receives through a real ConPTY
// for the byte sequences terminal.js writes. The child reads KEY_EVENT_RECORDs
// with ReadConsoleInputW in the mode crossterm (Codex) uses -- no VT input --
// which is where a bare ESC byte goes wrong once ConPTY has seen any Win32
// input-mode sequence.

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL (line %d): %s\n", line, what);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

// Exactly what terminal.js sends: win32KeyEvent(vk, scan, char, down, state).
const char kShiftEnter[] =
    "\x1b[16;42;0;1;16;1_\x1b[13;28;13;1;16;1_\x1b[13;28;13;0;16;1_\x1b[16;42;0;0;0;1_";
const char kEscape[] = "\x1b[27;1;27;1;0;1_\x1b[27;1;27;0;0;1_";

struct Key {
    bool down = false;
    unsigned vk = 0;
    unsigned scan = 0;
    unsigned ch = 0;
    unsigned long state = 0;
};

int runChild(const wchar_t* logPath)
{
    HANDLE input = ::CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                 nullptr);
    if (input == INVALID_HANDLE_VALUE)
        return 2;
    ::SetConsoleMode(input, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    FILE* log = _wfopen(logPath, L"w");
    if (!log)
        return 3;
    std::fprintf(log, "ready\n");
    std::fflush(log);
    for (;;) {
        INPUT_RECORD record{};
        DWORD read = 0;
        if (!::ReadConsoleInputW(input, &record, 1, &read) || read == 0)
            return 0;
        if (record.EventType != KEY_EVENT)
            continue;
        const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
        std::fprintf(log, "%d %u %u %u %lu\n", key.bKeyDown ? 1 : 0, key.wVirtualKeyCode,
                     key.wVirtualScanCode, static_cast<unsigned>(key.uChar.UnicodeChar),
                     key.dwControlKeyState & 0x1f);
        std::fflush(log);
    }
}

class PtyChild {
public:
    bool start(const std::wstring& exe, const std::wstring& logPath)
    {
        m_logPath = logPath;
        HANDLE inputRead = nullptr;
        HANDLE outputWrite = nullptr;
        if (!::CreatePipe(&inputRead, &m_input, nullptr, 0)
            || !::CreatePipe(&m_output, &outputWrite, nullptr, 0))
            return false;
        if (FAILED(::CreatePseudoConsole({ 80, 25 }, inputRead, outputWrite, 0, &m_pty)))
            return false;
        ::CloseHandle(inputRead);
        ::CloseHandle(outputWrite);
        // ConPTY blocks once its output pipe fills, so it must always be drained.
        m_drain = std::thread([this] {
            char buffer[4096];
            DWORD read = 0;
            while (::ReadFile(m_output, buffer, sizeof(buffer), &read, nullptr) && read) {}
        });

        SIZE_T size = 0;
        ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        m_attributes.resize(size);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(m_attributes.data());
        if (!::InitializeProcThreadAttributeList(attributes, 1, 0, &size)
            || !::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                            m_pty, sizeof(HPCON), nullptr, nullptr))
            return false;

        m_job = ::CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits,
                                  sizeof(limits));

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.lpAttributeList = attributes;
        std::wstring command = L"\"" + exe + L"\" --child \"" + logPath + L"\"";
        PROCESS_INFORMATION info{};
        if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                              EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr, nullptr,
                              &startup.StartupInfo, &info))
            return false;
        m_process = info.hProcess;
        ::AssignProcessToJobObject(m_job, m_process);
        ::ResumeThread(info.hThread);
        ::CloseHandle(info.hThread);
        return waitForLines(1, 10000);
    }

    ~PtyChild()
    {
        if (m_job)
            ::CloseHandle(m_job);
        if (m_process) {
            ::WaitForSingleObject(m_process, 5000);
            ::CloseHandle(m_process);
        }
        if (m_pty)
            ::ClosePseudoConsole(m_pty);
        if (m_input)
            ::CloseHandle(m_input);
        if (m_drain.joinable())
            m_drain.join();
        if (m_output)
            ::CloseHandle(m_output);
        ::DeleteFileW(m_logPath.c_str());
    }

    void write(const char* data)
    {
        DWORD written = 0;
        ::WriteFile(m_input, data, static_cast<DWORD>(std::strlen(data)), &written, nullptr);
    }

    std::vector<std::string> lines() const
    {
        std::vector<std::string> result;
        std::ifstream file(m_logPath);
        std::string line;
        while (std::getline(file, line))
            result.push_back(line);
        return result;
    }

    bool waitForLines(size_t count, int timeoutMs) const
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            if (lines().size() >= count)
                return true;
            ::Sleep(10);
        }
        return false;
    }

    // Writes data and returns the key records it produced, waiting at most
    // timeoutMs for the expected number to arrive and a further settle period
    // to catch any extra (duplicate) records.
    std::vector<Key> send(const char* data, size_t expected, int timeoutMs, long long* elapsedMs)
    {
        const size_t before = lines().size();
        const auto started = std::chrono::steady_clock::now();
        write(data);
        waitForLines(before + expected, timeoutMs);
        *elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - started).count();
        ::Sleep(150);
        std::vector<Key> keys;
        const std::vector<std::string> all = lines();
        for (size_t i = before; i < all.size(); ++i) {
            Key key;
            int down = 0;
            std::istringstream(all[i]) >> down >> key.vk >> key.scan >> key.ch >> key.state;
            key.down = down != 0;
            keys.push_back(key);
        }
        return keys;
    }

private:
    std::wstring m_logPath;
    HPCON m_pty = nullptr;
    HANDLE m_input = nullptr;
    HANDLE m_output = nullptr;
    HANDLE m_process = nullptr;
    HANDLE m_job = nullptr;
    std::vector<unsigned char> m_attributes;
    std::thread m_drain;
};

std::wstring childLogPath(const wchar_t* tag)
{
    wchar_t directory[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, directory);
    return std::wstring(directory) + L"fastmd-conpty-input-" + tag + L"-"
        + std::to_wstring(::GetCurrentProcessId()) + L".log";
}

bool isEscape(const Key& key, bool down)
{
    return key.down == down && key.vk == VK_ESCAPE && key.scan == 1 && key.ch == 0x1b
        && key.state == 0;
}

void testEscapeAfterWin32InputModeSequence(const std::wstring& exe)
{
    PtyChild child;
    CHECK(child.start(exe, childLogPath(L"escape")));

    long long elapsed = 0;
    const std::vector<Key> shiftEnter = child.send(kShiftEnter, 4, 3000, &elapsed);
    CHECK(shiftEnter.size() == 4);

    // ConPTY now treats a lone ESC byte as a sequence prefix; the full record
    // pair must still arrive at once, as exactly one Escape press.
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::vector<Key> escape = child.send(kEscape, 2, 3000, &elapsed);
        CHECK(escape.size() == 2);
        CHECK(escape.size() == 2 && isEscape(escape[0], true) && isEscape(escape[1], false));
        CHECK(elapsed < 1000);
    }

    // Plain VT input after the switch keeps its meaning.
    const std::vector<Key> altK = child.send("\x1bk", 2, 3000, &elapsed);
    bool sawAltK = false;
    for (const Key& key : altK)
        sawAltK = sawAltK || (key.down && key.vk == 'K' && key.ch == 'k' && (key.state & LEFT_ALT_PRESSED));
    CHECK(sawAltK);

    const std::vector<Key> ctrlC = child.send("\x03", 2, 3000, &elapsed);
    bool sawCtrlC = false;
    for (const Key& key : ctrlC)
        sawCtrlC = sawCtrlC || (key.down && key.vk == 'C' && key.ch == 0x03);
    CHECK(sawCtrlC);

    // Documents the platform behaviour the encoding works around. Not asserted:
    // a future ConPTY may stop holding the byte, which would not be a FastMD bug.
    const std::vector<Key> rawEscape = child.send("\x1b", 2, 400, &elapsed);
    std::printf("note: bare 0x1B after a Win32 input-mode sequence produced %zu record(s) "
                "within 400 ms\n", rawEscape.size());
}

void testEscapeInFreshSession(const std::wstring& exe)
{
    PtyChild child;
    CHECK(child.start(exe, childLogPath(L"fresh")));
    long long elapsed = 0;
    const std::vector<Key> escape = child.send(kEscape, 2, 3000, &elapsed);
    CHECK(escape.size() == 2 && isEscape(escape[0], true) && isEscape(escape[1], false));
    CHECK(elapsed < 1000);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc >= 3 && std::wcscmp(argv[1], L"--child") == 0)
        return runChild(argv[2]);

    wchar_t exe[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    testEscapeInFreshSession(exe);
    testEscapeAfterWin32InputModeSequence(exe);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
