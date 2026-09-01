#pragma once
#include "Model.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace apollo
{
// Read-only presentation. No control queues, SDK objects or writes belong here.
struct DesktopChannelStatus
{
    Channel channel;
    std::wstring format, level, pan, signal, peak, record;
    std::optional<bool> mute, solo;
    std::optional<double> signalDb;
    bool clipped = false;
};
inline std::wstring StatusNumber(std::optional<double> value, bool fader = false)
{
    if (!value || !std::isfinite(*value))
        return L"—";
    if (fader && *value <= -143.9)
        return L"−∞";
    std::wostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(1) << *value;
    return text.str();
}
inline std::optional<double> StatusValue(const std::optional<Parameter> &value)
{
    if (!value || value->value.kind != Json::Kind::Number)
        return {};
    const auto number = value->value.Number(NAN);
    return std::isfinite(number) ? std::make_optional(number) : std::nullopt;
}
inline std::optional<bool> StatusFlag(const std::optional<Parameter> &value)
{
    return value && value->value.kind == Json::Kind::Boolean ? std::make_optional(value->value.Bool())
                                                             : std::nullopt;
}
inline std::wstring StatusPan(const std::optional<Parameter> &value)
{
    const auto number = StatusValue(value);
    if (!number || *number < -1 || *number > 1)
        return L"—";
    const auto percent = static_cast<int>(std::lround(*number * 100));
    return percent == 0 ? L"C" : std::to_wstring(std::abs(percent)) + (*number < 0 ? L" L" : L" R");
}
inline std::vector<DesktopChannelStatus> DesktopChannels(const Snapshot &snapshot, bool fresh)
{
    std::vector<DesktopChannelStatus> result;
    if (!fresh || !snapshot.connected || !snapshot.onlineDevices)
        return result;
    for (auto channel : SurfaceChannels(snapshot))
    {
        DesktopChannelStatus row;
        row.format = channel.stereo ? L"STEREO" : L"MONO";
        row.level = StatusNumber(StatusValue(channel.level), true);
        row.pan = StatusPan(channel.pan);
        if (channel.panRight)
            row.pan += L" / " + StatusPan(channel.panRight);
        row.signalDb = MeterMaximum(channel.meters, false);
        row.signal = StatusNumber(row.signalDb);
        row.peak = StatusNumber(MeterMaximum(channel.meters, true));
        row.mute = StatusFlag(channel.mute);
        row.solo = StatusFlag(channel.solo);
        const auto dryRecord = StatusFlag(channel.recordPreEffects);
        row.record = !dryRecord ? L"—" : *dryRecord ? L"MON" : L"REC";
        for (const auto &meter : channel.meters)
            row.clipped |= meter.clip.value_or(false);
        row.channel = std::move(channel);
        result.push_back(std::move(row));
    }
    return result;
}
// One elastic name column; the remaining fields retain aligned readable widths.
inline std::array<int, 10> DesktopChannelWidths(int width)
{
    return {32, std::max(1, width - 710), 66, 72, 116, 86, 86, 86, 58, 108};
}
} // namespace apollo
