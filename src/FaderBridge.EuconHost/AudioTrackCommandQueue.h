#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>

// Windows-only, protocol-independent command identity. The legacy EUCON slot
// entry points remain unchanged; newer adapters resolve keys on the audio worker.
enum class AudioTrackControl { Volume, Pan, Mute, SetDefault };
struct AudioTrackCommand { AudioTrackControl control; std::wstring key; float value; };
class AudioTrackCommandQueue final
{
public:
    bool Push(AudioTrackControl control, const std::wstring& key, float value)
    {
        if (key.empty() || key.size() > 4096 || !std::isfinite(value)) return false;
        switch (control)
        {
        case AudioTrackControl::Volume: value = std::clamp(value, 0.F, 1.F); break;
        case AudioTrackControl::Pan: value = std::clamp(value, -1.F, 1.F); break;
        case AudioTrackControl::Mute: value = value > .5F ? 1.F : 0.F; break;
        case AudioTrackControl::SetDefault: value = 1.F; break;
        default: return false;
        }
        const std::scoped_lock lock(mutex_);
        // Move coalesced commands to the tail: latest Rec selection must win in
        // press order, never endpoint-name/alphabetical order.
        const auto previous = std::find_if(pending_.begin(), pending_.end(), [&](const auto& c) { return c.control == control && c.key == key; });
        if (previous != pending_.end()) pending_.erase(previous);
        if (pending_.size() >= 512) return false;
        pending_.push_back({control, key, value}); hasPending_ = true; return true;
    }
    bool HasPending() const { return hasPending_.load(); }
    std::deque<AudioTrackCommand> Take()
    {
        std::deque<AudioTrackCommand> commands;
        const std::scoped_lock lock(mutex_); commands.swap(pending_); hasPending_ = false; return commands;
    }
private:
    std::mutex mutex_;
    std::atomic_bool hasPending_ = false;
    std::deque<AudioTrackCommand> pending_;
};
template <typename Slots> int ResolveAudioTrackSlot(const Slots& slots, const std::wstring& key)
{
    if (key.empty()) return -1;
    const auto found = std::find_if(slots.begin(), slots.end(), [&](const auto& slot) { return slot.key == key; });
    return found == slots.end() ? -1 : static_cast<int>(std::distance(slots.begin(), found));
}
