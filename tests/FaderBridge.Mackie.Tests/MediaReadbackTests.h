#include "WindowsMediaReadback.h"

// Value fixtures model nullable WinRT projections. Reading a null fixture
// throws instead of terminating the test process with an access violation.
namespace media_fixture
{
using Span = std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>;
struct Date { Span time_since_epoch() const { return Span(1000); } };
struct Timeline
{
    bool present = true;
    bool failOptional = false;
    std::int64_t begin = 0, end = 1000, position = 250;
    explicit operator bool() const { return present; }
    Span Read(std::int64_t value) const
    {
        if (!present) throw std::runtime_error("null timeline was dereferenced");
        return Span(value);
    }
    Span StartTime() const
    {
        if (failOptional) throw std::runtime_error("optional display timeline is unavailable");
        return Read(begin);
    }
    Span EndTime() const { return Read(end); }
    Span Position() const { return Read(position); }
    Span MinSeekTime() const { return Read(begin); }
    Span MaxSeekTime() const { return Read(end); }
    Date LastUpdatedTime() const { Read(0); return {}; }
};
enum class Status { Playing, Paused };
struct Controls
{
    bool present = true;
    explicit operator bool() const { return present; }
    bool Enabled() const
    {
        if (!present) throw std::runtime_error("null playback controls were dereferenced");
        return true;
    }
    bool IsPlayPauseToggleEnabled() const { return Enabled(); }
    bool IsPlayEnabled() const { return Enabled(); }
    bool IsPauseEnabled() const { return Enabled(); }
    bool IsPreviousEnabled() const { return Enabled(); }
    bool IsNextEnabled() const { return Enabled(); }
    bool IsStopEnabled() const { return Enabled(); }
    bool IsPlaybackPositionEnabled() const { return Enabled(); }
    bool IsShuffleEnabled() const { return Enabled(); }
    bool IsRepeatEnabled() const { return Enabled(); }
};
template<class T> struct Optional
{
    T value;
    explicit operator bool() const { return true; }
    T Value() const { return value; }
};
struct Playback
{
    bool present = true;
    Status status = Status::Playing;
    media_fixture::Controls controls;
    explicit operator bool() const { return present; }
    void Require() const { if (!present) throw std::runtime_error("null playback info was dereferenced"); }
    Status PlaybackStatus() const { Require(); return status; }
    media_fixture::Controls Controls() const { Require(); return controls; }
    Optional<bool> IsShuffleActive() const { Require(); return {true}; }
    Optional<int> AutoRepeatMode() const { Require(); return {2}; }
};
}

void MediaReadbackTests()
{
    WindowsMediaState state;
    state.available = true;
    media_fixture::Timeline absent; absent.present = false;
    state.canSeek = true;
    bridge::ReadMediaTimeline(absent, state, true, 1200);
    Check(state.available && !state.hasPosition && !state.timeline.available && !state.canSeek,
          "Absent timeline must not crash or remove the media session");
    media_fixture::Playback noPlayback; noPlayback.present = false;
    bridge::ReadMediaPlayback(noPlayback, state);
    Check(state.available && !state.playing && !state.canPlayPause,
          "Absent playback information leaves safe capabilities");
    Check(bridge::MediaPlaybackScore(noPlayback) == 0,
          "A matching session without playback status retains its identity match score");
    media_fixture::Playback paused; paused.status = media_fixture::Status::Paused;
    Check(bridge::MediaPlaybackScore(paused) == 10 &&
          bridge::MediaPlaybackScore(media_fixture::Playback{}) == 30,
          "Session ranking retains existing playing and paused preferences");
    media_fixture::Playback noControls; noControls.controls.present = false;
    bridge::ReadMediaPlayback(noControls, state);
    Check(state.playing && !state.canPlayPause && !state.canSeek,
          "Absent controls do not prevent playback status or cause a dereference");
    bridge::ReadMediaPlayback(media_fixture::Playback{}, state);
    Check(state.canPlayPause && state.canSeek && state.canShuffle && state.shuffle && state.repeatMode == 2,
          "Valid playback capabilities retain existing semantics");
    bridge::ReadMediaTimeline(media_fixture::Timeline{}, state, true, 1200);
    Check(state.hasPosition && Near(state.position, .25F) && state.timeline.available &&
          state.timeline.position == 250 && state.timeline.seekEnd == 1000 &&
          state.timeline.updatedUtc == 1000 && state.timeline.sampledUtc == 1200,
          "Valid timelines preserve normalized position and native timestamps");
    WindowsMediaState legacy;
    bridge::ReadMediaTimeline(media_fixture::Timeline{}, legacy, false, 1200);
    Check(legacy.hasPosition && Near(legacy.position, .25F) && !legacy.timeline.available,
          "Default EUCON read does not acquire optional timeline semantics");
    media_fixture::Timeline zero; zero.end = 0;
    WindowsMediaState empty;
    bridge::ReadMediaTimeline(zero, empty, true, 1200);
    Check(!empty.hasPosition && !empty.timeline.available, "Zero duration has no usable position");
    media_fixture::Timeline over; over.position = 1200;
    bridge::ReadMediaTimeline(over, empty, false, 1200);
    Check(empty.hasPosition && empty.position == 1.F, "Position remains clamped to the seek range");
    media_fixture::Timeline partial; partial.failOptional = true;
    WindowsMediaState seekOnly; seekOnly.canSeek = true;
    bridge::ReadMediaTimeline(partial, seekOnly, true, 1200);
    Check(seekOnly.canSeek && seekOnly.hasPosition && Near(seekOnly.position, .25F) &&
          !seekOnly.timeline.available,
          "Optional display failure preserves confirmed seek bounds and normalized position");
    WindowsMediaState noRate; noRate.canSeek = true;
    bridge::ReadMediaTimeline(media_fixture::Timeline{}, noRate, true, 1200,
        []() -> std::optional<double> { throw std::runtime_error("rate unavailable"); });
    Check(noRate.hasPosition && noRate.canSeek && Near(noRate.position, .25F) && !noRate.timeline.available,
          "Optional playback-rate failure does not remove working seeking");
    bridge::ReadMediaTimeline(media_fixture::Timeline{}, noRate, true, 1200,
        []() -> std::optional<double> { return 1.25; });
    Check(noRate.timeline.available && noRate.timeline.rate == 1.25,
          "A valid playback rate remains available for display projection");
}
