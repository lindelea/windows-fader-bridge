#pragma once
#include "Preferences.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <shlobj.h>

namespace apollo
{
inline std::filesystem::path DesktopFolder()
{
    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)))
        throw std::runtime_error("Local application-data folder is unavailable");
    const auto path = std::filesystem::path(base) / L"UAD Console Bridge" / L"EUCON";
    CoTaskMemFree(base);
    return path;
}
inline Preferences LoadDesktopPreferencesFile(const std::filesystem::path &path)
{
    Preferences p;
    p.language = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE ? "zh-CN" : "en-US";
    if (!std::filesystem::exists(path))
        return p;
    if (std::filesystem::file_size(path) > 32768)
        throw std::runtime_error("Settings file too large");
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read settings");
    return DecodePreferences(std::string(std::istreambuf_iterator<char>(file), {}));
}
inline Preferences LoadDesktopPreferences()
{
    return LoadDesktopPreferencesFile(DesktopFolder() / L"settings.json");
}
inline void SaveDesktopPreferencesFile(const std::filesystem::path &target, const Preferences &p)
{
    const auto content = EncodePreferences(p);
    const auto folder = target.parent_path();
    std::filesystem::create_directories(folder);
    const auto temporary = folder / (L"settings." + std::to_wstring(GetCurrentProcessId()) + L".tmp");
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create settings file");
    DWORD written = 0;
    const bool success =
        WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) &&
        written == content.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!success ||
        !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Cannot save settings atomically");
    }
}
inline void SaveDesktopPreferences(const Preferences &p)
{
    SaveDesktopPreferencesFile(DesktopFolder() / L"settings.json", p);
}
constexpr wchar_t StartupKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t StartupName[] = L"UADConsoleBridgeForEUCON";
inline std::wstring DesktopExecutable()
{
    std::wstring path(32768, L'\0');
    const auto n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size())
        throw std::runtime_error("Cannot resolve application path");
    path.resize(n);
    return path;
}
inline std::optional<std::wstring> DesktopStartupCommand()
{
    DWORD bytes = 0, type = 0;
    const auto status =
        RegGetValueW(HKEY_CURRENT_USER, StartupKey, StartupName, RRF_RT_REG_SZ, &type, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND)
        return std::nullopt;
    if (status != ERROR_SUCCESS)
        throw std::runtime_error("Cannot read startup registration");
    if (bytes < sizeof(wchar_t) || bytes > 65536 || bytes % sizeof(wchar_t))
        throw std::runtime_error("Invalid startup entry");
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, StartupKey, StartupName, RRF_RT_REG_SZ, &type, value.data(),
                     &bytes) != ERROR_SUCCESS)
        throw std::runtime_error("Cannot read startup entry");
    while (!value.empty() && value.back() == L'\0')
        value.pop_back();
    return value;
}
inline bool DesktopStartupEnabled()
{
    return DesktopStartupCommand().has_value();
}
inline std::wstring MakeStartupCommand(const std::wstring &executable)
{
    if (executable.empty() || executable.find_first_of(L"\"\r\n") != std::wstring::npos)
        throw std::invalid_argument("Invalid startup executable path");
    return L"\"" + executable + L"\" --background";
}
inline void RestoreDesktopStartup(const std::optional<std::wstring> &command)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, StartupKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        throw std::runtime_error("Cannot update Windows startup registration");
    LONG result;
    if (command)
    {
        result = RegSetValueExW(key, StartupName, 0, REG_SZ, reinterpret_cast<const BYTE *>(command->c_str()),
                                static_cast<DWORD>((command->size() + 1) * sizeof(wchar_t)));
    }
    else
        result = RegDeleteValueW(key, StartupName);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
        throw std::runtime_error("Windows rejected startup registration");
}
inline void SetDesktopStartup(bool enabled)
{
    RestoreDesktopStartup(enabled ? std::make_optional(MakeStartupCommand(DesktopExecutable()))
                                  : std::nullopt);
}
} // namespace apollo
