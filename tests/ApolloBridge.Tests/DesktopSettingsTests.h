#pragma once
#include "DesktopSettings.h"
namespace apollo
{
inline std::string RunDesktopSettingsTests()
{
    size_t count = 0;
    const auto check = [&](bool v) {
        ++count;
        if (!v)
            throw std::runtime_error("Desktop settings regression failed");
    };
    const auto folder = std::filesystem::temp_directory_path() /
                        (L"UADConsoleBridge-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                         std::to_wstring(GetTickCount64()));
    if (!CreateDirectoryW(folder.c_str(), nullptr))
        throw std::runtime_error("Cannot create isolated settings test folder");
    const auto file = folder / L"settings.json";
    const auto temp = folder / (L"settings." + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    const auto cleanup = [&] {
        // Only these two generated files, never a recursive delete.
        DeleteFileW(temp.c_str());
        DeleteFileW(file.c_str());
        RemoveDirectoryW(folder.c_str());
    };
    try
    {
        auto p = LoadDesktopPreferencesFile(file);
        check(!p.access.Any() && !p.restorePermissions);
        p.language = "en-US";
        p.monitorCeiling = 0;
        p.access = {true, true, true, true};
        p.trustedSystem = "synthetic confirmed interface";
        SaveDesktopPreferencesFile(file, p);
        auto loaded = LoadDesktopPreferencesFile(file);
        check(loaded.language == "en-US" && loaded.monitorCeiling == 0 && loaded.access == p.access);
        check(loaded.trustedSystem == p.trustedSystem && !loaded.restorePermissions);
        check(!std::filesystem::exists(temp));
        p.monitorCeiling = -12.5;
        SaveDesktopPreferencesFile(file, p);
        check(LoadDesktopPreferencesFile(file).monitorCeiling == -12.5);
        HANDLE held = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        check(held != INVALID_HANDLE_VALUE);
        p.monitorCeiling = -30;
        bool rejected = false;
        try
        {
            SaveDesktopPreferencesFile(file, p);
        }
        catch (...)
        {
            rejected = true;
        }
        CloseHandle(held);
        check(rejected);
        check(LoadDesktopPreferencesFile(file).monitorCeiling == -12.5);
        check(!std::filesystem::exists(temp));
        check(MakeStartupCommand(L"C:\\Program Files\\UAD Console Bridge\\bridge.exe") ==
              L"\"C:\\Program Files\\UAD Console Bridge\\bridge.exe\" --background");
        rejected = false;
        try
        {
            MakeStartupCommand(L"bad\"path");
        }
        catch (...)
        {
            rejected = true;
        }
        check(rejected);
        cleanup();
        check(!std::filesystem::exists(folder));
        return "Desktop settings: " + std::to_string(count) +
               " checks passed. Isolated temporary files only; no registry, SDK, sockets or hardware "
               "writes.\n";
    }
    catch (...)
    {
        cleanup();
        throw;
    }
}
} // namespace apollo
