#pragma once
#include "WindowsMediaController.h"
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace bridge
{
template<class Playback>
int MediaPlaybackScore(const Playback& playback)
{
    if (!playback) return 0; // Missing status must not discard an identified session.
    using Status = decltype(playback.PlaybackStatus());
    const auto status = playback.PlaybackStatus();
    return status == Status::Playing ? 30 : status == Status::Paused ? 10 : 0;
}

// Shared read-only projection logic. Kept independent of WinRT activation so
// absent foreign objects can be exercised without a live player or device.
template<class Playback>
void ReadMediaPlayback(const Playback& playback, WindowsMediaState& state)
{
    state.playing = state.canPlayPause = state.canPrevious = state.canNext = false;
    state.canStop = state.canSeek = state.canShuffle = state.canRepeat = false;
    state.shuffle = false;
    state.repeatMode = 0;
    if (!playback) return;
    using Status = decltype(playback.PlaybackStatus());
    state.playing = playback.PlaybackStatus() == Status::Playing;
    const auto controls = playback.Controls();
    if (controls)
    {
        state.canPlayPause = controls.IsPlayPauseToggleEnabled() || controls.IsPlayEnabled() || controls.IsPauseEnabled();
        state.canPrevious = controls.IsPreviousEnabled();
        state.canNext = controls.IsNextEnabled();
        state.canStop = controls.IsStopEnabled();
        state.canSeek = controls.IsPlaybackPositionEnabled();
        state.canShuffle = controls.IsShuffleEnabled();
        state.canRepeat = controls.IsRepeatEnabled();
    }
    if (const auto shuffle = playback.IsShuffleActive()) state.shuffle = shuffle.Value();
    if (const auto repeat = playback.AutoRepeatMode())
        state.repeatMode = std::clamp(static_cast<int>(repeat.Value()), 0, 2);
}

template<class OptionalError = std::runtime_error, class Timeline, class ReadRate = std::nullptr_t>
void ReadMediaTimeline(const Timeline& timeline, WindowsMediaState& state,
                       bool includeTimeline, std::int64_t sampledUtc, ReadRate readRate = nullptr)
{
    state.hasPosition = false;
    state.position = 0;
    state.timeline = {};
    if (!timeline)
    {
        state.canSeek = false;
        return;
    }
    const auto start = timeline.MinSeekTime();
    const auto end = timeline.MaxSeekTime();
    if (end > start)
    {
        state.hasPosition = true;
        const auto position = std::clamp(timeline.Position(), start, end);
        state.position = static_cast<float>(static_cast<double>((position - start).count()) /
                                            static_cast<double>((end - start).count()));
    }
    if (includeTimeline)
    {
        try
        {
            WindowsMediaTimeline time;
            time.start = timeline.StartTime().count();
            time.end = timeline.EndTime().count();
            time.position = timeline.Position().count();
            time.seekStart = start.count();
            time.seekEnd = end.count();
            time.updatedUtc = timeline.LastUpdatedTime().time_since_epoch().count();
            time.sampledUtc = sampledUtc;
            if constexpr (!std::is_same_v<ReadRate, std::nullptr_t>) time.rate = readRate();
            time.available = time.start >= 0 && time.end > time.start && time.position >= 0;
            state.timeline = time;
        }
        catch (const OptionalError&)
        {
            // Display-only fields/rate must never invalidate confirmed seeking.
            state.timeline = {};
        }
    }
}
}
