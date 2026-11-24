#pragma once
// Qt-free MSVC-style argv quoting, shared by FastMDLauncher.cpp and the
// FastMDShellExtension DLL -- both need to build a quoted command line for
// CreateProcessW without linking anything beyond the Win32 CRT.
#include <string>
#include <string_view>

namespace WinCommandLine {

inline std::wstring quoteArgument(std::wstring_view argument)
{
    if (argument.find_first_of(L" \t\"") == std::wstring_view::npos)
        return std::wstring(argument);

    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(character);
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
            backslashes = 0;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

}
