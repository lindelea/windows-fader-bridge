#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

// Windows-only data contract, independent of either surface protocol.
// Durations and UTC dates use WinRT's 100 ns units. No seek capability implied.
struct WindowsMediaTimeline
{
    bool available = false;
    std::int64_t start = 0, end = 0, position = 0;
    std::int64_t seekStart = 0, seekEnd = 0; // Optional; only populated by the opt-in timeline read.
    std::int64_t updatedUtc = 0, sampledUtc = 0;
    std::optional<double> rate;
};

struct WindowsPlaybackTime
{
    bool available = false;
    double elapsedSeconds = 0, durationSeconds = 0;
};

inline WindowsPlaybackTime PlaybackTime(const WindowsMediaTimeline& timeline, bool playing)
{
    if (!timeline.available || timeline.start < 0 || timeline.end <= timeline.start || timeline.position < 0)
        return {};
    constexpr double ticksPerSecond = 10000000.0;
    const auto position = std::clamp(timeline.position, timeline.start, timeline.end);
    const double duration = static_cast<double>(timeline.end - timeline.start) / ticksPerSecond;
    double elapsed = static_cast<double>(position - timeline.start) / ticksPerSecond;
    if (playing && timeline.rate && std::isfinite(*timeline.rate) &&
        timeline.updatedUtc > 0 && timeline.sampledUtc >= timeline.updatedUtc)
    {
        const double age = static_cast<double>(timeline.sampledUtc - timeline.updatedUtc) / ticksPerSecond;
        const double projected = elapsed + age * *timeline.rate;
        if (std::isfinite(projected)) elapsed = std::clamp(projected, 0.0, duration);
    }
    return {true, elapsed, duration};
}
