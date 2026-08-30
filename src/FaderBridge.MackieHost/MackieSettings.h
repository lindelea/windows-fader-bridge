#pragma once
#include <Windows.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct MackieSettings
{
    std::wstring input, output, profile = L"mcu";
    bool touch = true, lcd = true, meters = true;
    std::map<int, std::wstring> bindings;
    std::vector<std::wstring> trackOrder;
    static std::filesystem::path Path();
    static MackieSettings Load(const std::filesystem::path& path = Path());
    bool Save(const std::filesystem::path& path = Path()) const;
    static bool Bindable(int channel, int note)
    {
        return channel >= 0 && channel <= 15 && note >= 0 && note < 128 &&
            (channel != 0 || (note >= 0x36 && note < 0x68));
    }
};

// Device-specific safety belongs here, never in the MCU codec/Windows model.
inline int IconDawPort(const std::wstring& name)
{
    if (name.find(L"P1-Nano") == std::wstring::npos && name.find(L"P1 Nano") == std::wstring::npos) return 0;
    for (int n = 2; n <= 4; ++n)
    {
        if (name.find(L"MIDIIN" + std::to_wstring(n)) != std::wstring::npos ||
            name.find(L"MIDIOUT" + std::to_wstring(n)) != std::wstring::npos ||
            (name.size() >= 2 && name.substr(name.size() - 2) == L" " + std::to_wstring(n))) return n;
    }
    return 1;
}
