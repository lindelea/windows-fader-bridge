#include "MackieSurface.h"
#include <cstdio>

namespace mackie
{
const Track* Surface::Find(const std::wstring& key) const
{
    const auto it = tracks_.find(key);
    return it != tracks_.end() && it->second.active ? &it->second : nullptr;
}
const Track* Surface::At(int physical) const
{
    if (physical == Master)
    {
        for (const auto& [key, track] : tracks_) if (track.active && track.master) return &track;
        return nullptr;
    }
    const auto index = bank_ + physical;
    return physical >= 0 && physical < Strips && index < static_cast<int>(order_.size())
        ? Find(order_[index]) : nullptr;
}
const Track* Surface::Selected() const { return Find(selected_); }
bool Surface::Touched() const { return std::any_of(touched_.begin(), touched_.end(), [](bool b) { return b; }); }
void Surface::RestoreOrder(const std::vector<std::wstring>& order)
{
    std::set<std::wstring> seen;
    order_.clear();
    for (const auto& key : order) if (!key.empty() && seen.insert(key).second) order_.push_back(key);
    bank_ = 0;
    dirty_ = true;
}
void Surface::Update(const std::vector<Track>& tracks, std::uint64_t)
{
    for (auto& [key, track] : tracks_) track.active = false;
    for (const auto& track : tracks)
    {
        if (track.key.empty() || !track.active) continue;
        if (std::find(order_.begin(), order_.end(), track.key) == order_.end()) order_.push_back(track.key);
        tracks_[track.key] = track;
    }
    if (selected_.empty()) for (const auto& key : order_) if (Find(key)) { selected_ = key; break; }
}
bool Surface::Bank(int delta)
{
    if (Touched()) { status_ = L"Release the fader before banking"; return false; }
    const auto last = order_.empty() ? 0 : ((static_cast<int>(order_.size()) - 1) / Strips) * Strips;
    bank_ = std::clamp(bank_ + delta, 0, last);
    dirty_ = true;
    status_ = L"Bank " + std::to_wstring(bank_ + 1);
    return true;
}
bool Surface::Select(const std::wstring& key, bool focus)
{
    if (Touched() || !Find(key)) return false;
    selected_ = key;
    const auto it = std::find(order_.begin(), order_.end(), key);
    const auto index = static_cast<int>(std::distance(order_.begin(), it));
    if (index < bank_ || index >= bank_ + Strips) bank_ = (index / Strips) * Strips;
    dirty_ = true;
    if (focus) act_({ActionKind::Focus, key});
    return true;
}
void Surface::ResetConnection()
{
    touched_.fill(false);
    touchKey_.fill({});
    pressed_.fill(false);
    pending_.clear();
    cache_.clear();
    meterDue_ = 0;
    lcdDue_ = ringDue_ = 0;
    dirty_ = true;
}
float Surface::Value(const Track& track, ActionKind kind, std::uint64_t now)
{
    const float actual = kind == ActionKind::Pan ? track.pan :
        (kind == ActionKind::Mute ? (track.muted ? 1.F : 0.F) : track.volume);
    const auto it = pending_.find({track.key, kind});
    return it == pending_.end() ? actual : it->second.Resolve(actual, now, kind == ActionKind::Pan ? .01F : .002F);
}
void Surface::Request(const Track& track, ActionKind kind, float value, std::uint64_t now)
{
    pending_[{track.key, kind}] = {value, now + 180};
    act_({kind, track.key, value});
}
void Surface::Input(std::uint32_t raw, std::uint64_t now)
{
    if (!ValidShort(raw)) return;
    const int status = raw & 255, type = status & 0xF0, channel = status & 15;
    const int a = (raw >> 8) & 127, b = (raw >> 16) & 127;
    if (type == 0xE0 && channel <= Master)
    {
        if (RequireTouch && !touched_[channel]) { status_ = L"Ignored fader without touch"; return; }
        const auto track = touched_[channel] ? Find(touchKey_[channel]) : At(channel);
        if (!track) return;
        const bool panFader = flip_ && channel != Master;
        if (panFader && !track->canPan) return;
        const auto v = FaderScalar(a | (b << 7));
        Request(*track, panFader ? ActionKind::Pan : ActionKind::Volume, panFader ? v * 2 - 1 : v, now);
        return;
    }
    if (channel != 0) return; // Custom note bindings are a separate host layer.
    if (type == 0xB0)
    {
        const int delta = Delta(b);
        if (!delta) return;
        if (a >= 0x10 && a <= 0x17)
        {
            const auto track = At(a - 0x10);
            if (!track) return;
            const bool volume = volumeKnobs_ || flip_;
            if (!volume && !track->canPan) return;
            const auto kind = volume ? ActionKind::Volume : ActionKind::Pan;
            const float v = Value(*track, kind, now) + delta * (volume ? .01F : .02F);
            Request(*track, kind, std::clamp(v, volume ? 0.F : -1.F, 1.F), now);
        }
        else if (a == 0x3C) act_({ActionKind::Seek, selected_, delta * .01F});
        return;
    }
    if (type != 0x90 && type != 0x80) return;
    const bool down = type == 0x90 && b != 0;
    if (a >= 0x68 && a <= 0x70)
    {
        const int fader = a - 0x68;
        if (down && !touched_[fader]) touchKey_[fader] = At(fader) ? At(fader)->key : L"";
        touched_[fader] = down;
        if (!down) { touchKey_[fader].clear(); cache_.erase(fader); }
        return;
    }
    const bool wasDown = pressed_[a];
    pressed_[a] = down;
    if (!down || wasDown) return;
    if (a < 0x28)
    {
        const auto track = At(a % Strips);
        if (!track) return;
        if (a < 8 && track->defaultSelectable) act_({ActionKind::DefaultDevice, track->key});
        else if (a >= 8 && a < 16 && track->application) act_({ActionKind::Solo, track->key});
        else if (a >= 16 && a < 24) Request(*track, ActionKind::Mute, 1.F - Value(*track, ActionKind::Mute, now), now);
        else if (a >= 24 && a < 32) Select(track->key, true);
        else if (a >= 32 && track->canPan && !volumeKnobs_ && !flip_) Request(*track, ActionKind::Pan, 0, now);
        return;
    }
    switch (a)
    {
    case 0x28: SetEncoderVolume(true); break;
    case 0x2A: SetEncoderVolume(false); break;
    case 0x2E: Bank(-Strips); break;
    case 0x2F: Bank(Strips); break;
    case 0x30: Bank(-1); break;
    case 0x31: Bank(1); break;
    case 0x32: if (!Touched() && !volumeKnobs_) { flip_ = !flip_; dirty_ = true; } break;
    case 0x5A: act_({ActionKind::ClearSolo, {}}); break;
    case 0x5B: act_({ActionKind::Previous, selected_}); break;
    case 0x5C: act_({ActionKind::Next, selected_}); break;
    case 0x5D: act_({ActionKind::Stop, selected_}); break;
    case 0x5E: act_({ActionKind::PlayPause, selected_}); break;
    case 0x56: act_({ActionKind::Repeat, selected_}); break;
    default: break;
    }
}
void Surface::Cached(int id, const Bytes& bytes, bool force)
{
    if (bytes.empty()) return;
    if (force || cache_[id] != bytes) { cache_[id] = bytes; send_(bytes); }
}
void Surface::Feedback(std::uint64_t now, bool force)
{
    force = force || dirty_;
    dirty_ = false;
    // Traditional DIN MIDI is bandwidth-limited. Keep slow display traffic
    // below motor/touch traffic; do not assume a fast USB-only controller.
    const bool ringsDue = force || now >= ringDue_;
    const bool lcdDue = force || now >= lcdDue_;
    if (ringsDue) ringDue_ = now + 50;
    if (lcdDue) lcdDue_ = now + 200;
    for (int i = 0; i <= Master; ++i)
    {
        const auto track = At(i);
        if (!touched_[i])
        {
            const bool pan = flip_ && i != Master;
            const auto v = track ? Value(*track, pan ? ActionKind::Pan : ActionKind::Volume, now) : 0.F;
            Cached(i, Fader(i, pan ? (track && track->canPan ? (v + 1.F) * .5F : .5F) : v), force);
        }
        if (i == Master) break;
        Cached(20 + i, Led(i, track && track->defaultDevice), force);
        Cached(30 + i, Led(8 + i, track && track->solo), force);
        Cached(40 + i, Led(16 + i, track && track->muted), force);
        Cached(50 + i, Led(24 + i, track && track->key == selected_), force);
        const bool volume = volumeKnobs_ || flip_;
        if (ringsDue) Cached(60 + i, Ring(i, track ? Value(*track, volume ? ActionKind::Volume : ActionKind::Pan, now) : 0,
            track && (volume || track->canPan), volume), force);
        if (LcdEnabled && lcdDue)
        {
            auto label = track ? Label(track->name) : std::string(7, ' ');
            char value[16]{};
            if (track) std::snprintf(value, sizeof(value), "%3d%%", int(std::lround(Value(*track, ActionKind::Volume, now) * 100)));
            std::string lower(value); lower.resize(7, ' ');
            Cached(80 + i, Lcd(i * 7, label), force);
            Cached(90 + i, Lcd(56 + i * 7, lower), force);
        }
    }
    Cached(101, Led(0x32, flip_), force);
    Cached(102, Led(0x28, volumeKnobs_), force);
    Cached(103, Led(0x2A, !volumeKnobs_), force);
    Cached(104, Led(0x73, AnySolo), force);
    Cached(105, Led(0x5E, MediaAvailable && MediaPlaying), force);
    Cached(106, Led(0x5D, MediaAvailable && !MediaPlaying), force);
    Cached(107, Led(0x56, MediaAvailable && MediaRepeat), force);
    if (MetersEnabled && (force || now >= meterDue_))
    {
        meterDue_ = now + 50;
        for (int i = 0; i < Strips; ++i)
        {
            const auto track = At(i);
            const auto db = track ? track->peakDb : -120.F;
            Cached(120 + i, Meter(i, db >= 0 ? 14 : 15), force);
            send_(Meter(i, MeterLevel(db))); // Refresh: device meters decay locally.
        }
    }
}
}
