#include "MackieSettings.h"
#include "CommandCatalog.h"
#include "Windows80Preset.h"
#include "DiagnosticLog.h"
#include "MackieJogInput.h"
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
bool MackieSettings::ApplyWindows80Preset()
{
    auto next = bindings;
    for (int note = 0; note < static_cast<int>(Windows80Commands.size()); ++note)
    {
        const auto id = Windows80Commands[note];
        const int key = 15 * 128 + note;
        if (!Bindable(15, note) || !FindMackieCommand(id)) return false;
        const auto existing = next.find(key);
        if (existing != next.end() && existing->second != id) return false;
        next[key] = id;
    }
    bindings.swap(next);
    return true;
}
MackieSettings MackieSettings::Load(const std::filesystem::path& path)
{
    MackieSettings result;
    result.storage = path;
    std::array<std::array<std::wstring, 2>, 4> legacyCursor{};
    std::array<std::array<bool, 2>, 5> explicitCursor{};
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
        else if (key == "language")
        { if (row >> std::quoted(text) && (text == "zh" || text == "en")) result.language = Wide(text); }
        else if (key == "closeToTray")
        { int value = -1; if (row >> value && (value == 0 || value == 1)) result.closeToTray = value != 0; }
        else if (key == "autoConnect")
        { int value = -1; if (row >> value && (value == 0 || value == 1)) result.autoConnect = value != 0; }
        else if (key == "deviceName")
        { if (row >> std::quoted(text)) { auto value = Wide(text); if (value.size() <= 80) result.deviceName = value; } }
        else if (key == "jog")
        {
            mackie::JogSettings value;
            // Read both old four-field and new seconds-only records. Retired
            // Focus/host-mode sensitivity cannot change native device behavior.
            if (row >> value.seekSeconds && value.Valid()) result.jog = value;
        }
        else if (key == "jogZoom")
        {
            int direction = -1;
            if (row >> direction >> std::quoted(text))
            {
                const auto id = Wide(text);
                const auto command = FindMackieCommand(id);
                if (direction >= 0 && direction < 2 && (id.empty() || (command && command->special == 0))) legacyCursor[3][direction] = id;
            }
        }
        else if (key == "cursor")
        {
            int axis = -1, direction = -1;
            if (row >> axis >> direction >> std::quoted(text))
            {
                const auto id = Wide(text);
                if (direction >= 0 && direction < 2 && mackie::ValidDirectionCommand(axis, id))
                { result.cursorCommands[axis][direction] = id; explicitCursor[axis][direction] = true; }
            }
        }
        else if (key == "cursorTicks")
        {
            int axis = -1, ticks = 0;
            if (row >> axis >> ticks && mackie::ValidCursorAxis(axis) && mackie::ValidCursorTicks(ticks)) result.cursorTicks[axis] = ticks;
        }
        else if (key == "encoder")
        {
            int encoder = -1, gesture = -1;
            if (row >> encoder >> gesture >> std::quoted(text))
            {
                const auto id = Wide(text);
                if (encoder >= 3 && encoder <= 8 && gesture >= 0 && gesture < 3 && ValidEncoderCommand(id))
                    result.encoders[encoder - 3][gesture] = id;
            }
        }
        else if (key == "bind")
        {
            int channel = -1, note = -1;
            if (row >> channel >> note >> std::quoted(text))
            {
                const auto id = Wide(text);
                if (channel == 0 && note >= 0x60 && note <= 0x63 && mackie::ValidCursorCommand(id))
                    legacyCursor[(note - 0x60) / 2][(note - 0x60) % 2] = id;
                else if (Bindable(channel, note) && FindMackieCommand(id)) result.bindings[channel * 128 + note] = id;
            }
        }
    }
    for (int axis = 0; axis < 4; ++axis) for (int direction = 0; direction < 2; ++direction)
        if (!explicitCursor[axis][direction]) result.cursorCommands[axis][direction] = legacyCursor[axis][direction];
    return result;
}
bool MackieSettings::Save(const std::filesystem::path& requestedPath) const
{
    try
    {
        const auto path = requestedPath.empty() ? (storage.empty() ? Path() : storage) : requestedPath;
        if (path.empty()) return false;
        std::filesystem::create_directories(path.parent_path());
        auto temporary = path; temporary += L".new";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out << "version 1\ninput " << std::quoted(Utf8(input)) << "\noutput " << std::quoted(Utf8(output))
                << "\nprofile " << std::quoted(Utf8(profile)) << "\ntouch " << touch << "\nlcd " << lcd << "\nmeters " << meters << '\n';
            for (const auto& [key, command] : bindings) if (Bindable(key / 128, key % 128) && FindMackieCommand(command))
                out << "bind " << key / 128 << ' ' << key % 128 << ' ' << std::quoted(Utf8(command)) << '\n';
            out << "language " << std::quoted(language == L"en" ? "en" : "zh") << "\ncloseToTray " << closeToTray << '\n';
            out << "autoConnect " << autoConnect << "\ndeviceName " << std::quoted(Utf8(deviceName)) << '\n';
            for (const auto& key : trackOrder) out << "track " << std::quoted(Utf8(key)) << '\n';
            if (jog.Valid()) out << "jog " << jog.seekSeconds << '\n';
            for (int axis = 0; axis < 5; ++axis)
            {
                if (mackie::ValidCursorTicks(cursorTicks[axis])) out << "cursorTicks " << axis << ' ' << cursorTicks[axis] << '\n';
                for (int direction = 0; direction < 2; ++direction)
                    if (mackie::ValidDirectionCommand(axis, cursorCommands[axis][direction]))
                        out << "cursor " << axis << ' ' << direction << ' ' << std::quoted(Utf8(cursorCommands[axis][direction])) << '\n';
            }
            for (int encoder = 0; encoder < 6; ++encoder) for (int gesture = 0; gesture < 3; ++gesture)
                if (!encoders[encoder][gesture].empty() && ValidEncoderCommand(encoders[encoder][gesture]))
                    out << "encoder " << encoder + 3 << ' ' << gesture << ' ' << std::quoted(Utf8(encoders[encoder][gesture])) << '\n';
            out.flush();
            if (!out) return false;
        }
        return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    catch (const std::exception& error) { FB_TRACE("MACKIE_SETTINGS_ERROR %s", error.what()); return false; }
}
