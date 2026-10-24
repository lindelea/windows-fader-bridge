#include "WindowsMediaReadback.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <stdexcept>

// Actual null WinRT projections, without activating a manager/player or COM.
int WinrtMediaNullReadbackTests()
{
    using namespace winrt::Windows::Media::Control;
    WindowsMediaState state;
    state.available = state.canSeek = true;
    GlobalSystemMediaTransportControlsSessionTimelineProperties timeline{nullptr};
    bridge::ReadMediaTimeline<winrt::hresult_error>(timeline, state, true, 0);
    if (!state.available || state.canSeek || state.hasPosition || state.timeline.available)
        throw std::runtime_error("Real null WinRT timeline was not treated as unavailable");
    GlobalSystemMediaTransportControlsSessionPlaybackInfo playback{nullptr};
    bridge::ReadMediaPlayback(playback, state);
    if (!state.available || state.playing || state.canPlayPause)
        throw std::runtime_error("Real null WinRT playback did not preserve the safe session");
    if (bridge::MediaPlaybackScore(playback) != 0)
        throw std::runtime_error("Real null WinRT playback should not exclude a matched session");
    return 3;
}
