#include "MackieSettings.h"
#include "CommandCatalog.h"
#include "DiagnosticLog.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
std::string Utf8(const std::wstring& text)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::wstring Wide(const std::string& text)
{
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
}
std::filesystem::path MackieSettings::Path()
{
    wchar_t local[32768]{};
    const auto n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
    if (!n || n >= 32768) return {};
    return std::filesystem::path(local) / L"Windows Fader Bridge" / L"Mackie" / L"settings.txt";
}
MackieSettings MackieSettings::Load(const std::filesystem::path& path)
{
    MackieSettings result;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    for (int count = 0; count < 8192 && std::getline(in, line); ++count)
    {
        if (line.size() > 16384) continue;
        std::istringstream row(line);
        std::string key, text;
        row >> key;
        if (key == "input" || key == "output" || key == "profile" || key == "track")
        {
            if (!(row >> std::quoted(text))) continue;
            auto value = Wide(text);
            if (key == "input") result.input = value;
            if (key == "output") result.output = value;
            if (key == "profile" && (value == L"mcu" || value == L"p1-nano")) result.profile = value;
            if (key == "track" && !value.empty()) result.trackOrder.push_back(value);
        }
        else if (key == "touch") row >> result.touch;
        else if (key == "lcd") row >> result.lcd;
        else if (key == "meters") row >> result.meters;
        else if (key == "bind")
        {
            int channel = -1, note = -1;
            if (row >> channel >> note >> std::quoted(text))
            {
                const auto id = Wide(text);
                if (Bindable(channel, note) && FindMackieCommand(id)) result.bindings[channel * 128 + note] = id;
            }
        }
    }
    return result;
}
bool MackieSettings::Save(const std::filesystem::path& path) const
{
    try
    {
        if (path.empty()) return false;
        std::filesystem::create_directories(path.parent_path());
        auto temporary = path; temporary += L".new";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out << "version 1\ninput " << std::quoted(Utf8(input)) << "\noutput " << std::quoted(Utf8(output))
                << "\nprofile " << std::quoted(Utf8(profile)) << "\ntouch " << touch << "\nlcd " << lcd << "\nmeters " << meters << '\n';
            for (const auto& [key, command] : bindings) out << "bind " << key / 128 << ' ' << key % 128 << ' ' << std::quoted(Utf8(command)) << '\n';
            for (const auto& key : trackOrder) out << "track " << std::quoted(Utf8(key)) << '\n';
            out.flush();
            if (!out) return false;
        }
        return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    catch (const std::exception& error) { FB_TRACE("MACKIE_SETTINGS_ERROR %s", error.what()); return false; }
}
