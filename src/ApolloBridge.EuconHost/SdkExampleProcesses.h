#pragma once
#include <Windows.h>
#include <psapi.h>
#include <cwchar>
#include <initializer_list>
#include <vector>

namespace apollo
{
// No Toolhelp snapshot/section is created. This is a connection preflight,
// never a control callback or part of an established audio-feedback path.
inline bool AnyProcessNamed(std::initializer_list<const wchar_t*> names)
{
    if (names.size() == 0) return false;
    std::vector<DWORD> processes(1024);
    DWORD used = 0;
    for (;;)
    {
        const auto bytes = static_cast<DWORD>(processes.size() * sizeof(DWORD));
        if (!K32EnumProcesses(processes.data(), bytes, &used)) return true;
        if (used < bytes) break;
        if (processes.size() >= 65536) return true;
        processes.resize(processes.size() * 2);
    }
    for (std::size_t index = 0; index < used / sizeof(DWORD); ++index)
    {
        if (!processes[index]) continue;
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processes[index]);
        if (!process) continue; // Exited or protected OS process.
        wchar_t path[32768]{};
        DWORD length = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
        const bool readable = QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
        CloseHandle(process);
        if (!readable) continue;
        const auto separator = std::wcsrchr(path, L'\\');
        const auto filename = separator ? separator + 1 : path;
        for (const auto name : names)
            if (name && _wcsicmp(filename, name) == 0) return true;
    }
    return false;
}
}
