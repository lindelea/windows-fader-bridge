#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Project-owned implementation. Source contracts and interoperability limits:
// docs/MACKIE_RESEARCH.md. No SDK/vendor source is embedded here.
namespace mackie
{
using Bytes = std::vector<std::uint8_t>;
constexpr int Strips = 8;
constexpr int Master = 8;
constexpr int FaderMax = 16383;
constexpr std::uint8_t McuModel = 0x14;

inline float Unit(float v) { return std::isfinite(v) ? std::clamp(v, 0.F, 1.F) : 0.F; }
inline int FaderValue(float v) { return static_cast<int>(std::lround(Unit(v) * FaderMax)); }
inline float FaderScalar(int v) { return std::clamp(v, 0, FaderMax) / float(FaderMax); }
inline int Delta(int value) { return (value & 0x40) ? -(value & 0x3F) : value & 0x3F; }
inline Bytes Fader(int channel, float value)
{
    if (channel < 0 || channel > Master) return {};
    const int encoded = FaderValue(value);
    return { static_cast<std::uint8_t>(0xE0 | channel),
        static_cast<std::uint8_t>(encoded & 0x7F), static_cast<std::uint8_t>(encoded >> 7) };
}
inline Bytes Led(int note, bool on)
{
    if (note < 0 || note > 127) return {};
    return {0x90, static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(on ? 127 : 0)};
}
inline Bytes Ring(int channel, float pan, bool available, bool volumeMode = false)
{
    if (channel < 0 || channel >= Strips) return {};
    const float position = volumeMode ? Unit(pan) : Unit((pan + 1.F) * .5F);
    const int step = static_cast<int>(std::lround(position * 10)) + 1;
    // Spread/bar mode for volume, single-dot mode + center indicator for pan.
    const int value = available ? (step | (volumeMode ? 0x20 :
        (std::abs(pan) < .01F ? 0x40 : 0))) : 0;
    return {0xB0, static_cast<std::uint8_t>(0x30 + channel), static_cast<std::uint8_t>(value)};
}
inline int MeterLevel(float db)
{
    if (!std::isfinite(db) || db <= -60) return 0;
    return std::clamp(static_cast<int>(std::ceil((db + 60.F) / 5.F)), 1, 12);
}
inline Bytes Meter(int channel, int level)
{
    if (channel < 0 || channel >= Strips || level < 0 || level > 15 || level == 13) return {};
    return {0xD0, static_cast<std::uint8_t>((channel << 4) | level)};
}
inline Bytes Lcd(int offset, const std::string& ascii)
{
    if (offset < 0 || offset >= 112 || ascii.empty()) return {};
    Bytes result{0xF0, 0, 0, 0x66, McuModel, 0x12, static_cast<std::uint8_t>(offset)};
    for (std::size_t i = 0; i < ascii.size() && offset + static_cast<int>(i) < 112; ++i)
    {
        const auto c = static_cast<unsigned char>(ascii[i]);
        result.push_back(c >= 32 && c <= 126 ? c : '?');
    }
    result.push_back(0xF7);
    return result;
}
inline std::string Label(const std::wstring& value)
{
    std::string label;
    for (wchar_t c : value)
    {
        if (label.size() == 6) break;
        if (c >= 32 && c <= 126) label.push_back(static_cast<char>(c));
    }
    if (label.empty()) label = "Track";
    label.resize(7, ' ');
    return label;
}
inline bool ValidShort(std::uint32_t raw)
{
    const auto status = raw & 255;
    const auto d1 = (raw >> 8) & 255;
    const auto d2 = (raw >> 16) & 255;
    return status >= 0x80 && status < 0xF0 && d1 < 128 &&
        (((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) || d2 < 128);
}
}
