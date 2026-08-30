#pragma once
#include "WindowsMediaController.h"
#include <optional>

// Use the seek range, not song duration: live streams and offset timelines can
// expose different bounds. Unknown/readonly time must never become a seek.
inline std::optional<float> MackieSeekSeconds(const WindowsMediaState& state, double seconds)
{
    const auto& t = state.timeline;
    if (!state.available || !state.canSeek || !state.hasPosition || !std::isfinite(seconds) ||
        t.seekStart < 0 || t.seekEnd <= t.seekStart || t.position < 0) return {};
    double position = static_cast<double>(std::clamp(t.position, t.seekStart, t.seekEnd));
    if (state.playing && t.rate && std::isfinite(*t.rate) && t.updatedUtc > 0 && t.sampledUtc >= t.updatedUtc)
        position += static_cast<double>(t.sampledUtc - t.updatedUtc) * *t.rate;
    const double target = (position + seconds * 10000000.0 - t.seekStart) / static_cast<double>(t.seekEnd - t.seekStart);
    if (!std::isfinite(target)) return {};
    return static_cast<float>(std::clamp(target, 0.0, 1.0));
}
