#pragma once

#include "WindowsMediaController.h"
#include <cstdint>

namespace mackie
{
struct DisplayTime
{
    double seconds = -1;
    bool systemClock = true;
};

// Application policy only; both sources use the same standard MCU time digits.
// Inject clocks for deterministic tests. Wall-clock adjustments must not change
// the pause timeout, which uses monotonic milliseconds instead.
class DisplayClock
{
public:
    DisplayTime Update(const WindowsMediaState& media, std::uint64_t nowMs, int localSeconds)
    {
        const DisplayTime clock{localSeconds >= 0 && localSeconds < 86400 ? double(localSeconds) : -1, true};
        const auto time = PlaybackTime(media.timeline, media.playing);
        if (!media.available || !time.available || !std::isfinite(time.elapsedSeconds) ||
            time.elapsedSeconds < 0 || time.elapsedSeconds >= 360000)
        {
            hadMedia_ = false;
            previous_ = {};
            return clock;
        }

        // Do not use polling timestamps as activity: many providers update them
        // while paused. A real position/track change starts a new 10-second hold.
        const bool activity = !hadMedia_ || media.playing || previous_.playing ||
            media.sourceAppId != previous_.sourceAppId || media.title != previous_.title ||
            media.artist != previous_.artist || media.timeline.start != previous_.timeline.start ||
            media.timeline.end != previous_.timeline.end || media.timeline.position != previous_.timeline.position;
        if (activity || nowMs < lastActivityMs_) lastActivityMs_ = nowMs;
        hadMedia_ = true;
        previous_ = media;
        if (!media.playing && nowMs - lastActivityMs_ >= 10000) return clock;
        return {time.elapsedSeconds, false};
    }

private:
    WindowsMediaState previous_;
    std::uint64_t lastActivityMs_ = 0;
    bool hadMedia_ = false;
};
}
