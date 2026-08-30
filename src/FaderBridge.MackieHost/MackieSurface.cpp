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
    haveSnapshot_ = true;
    onlineOrder_.clear();
    for (auto& [key, track] : tracks_) track.active = false;
    for (const auto& track : tracks)
    {
        if (track.key.empty() || !track.active) continue;
        onlineOrder_.push_back(track.key);
        tracks_[track.key] = track;
    }
    ReconcileOrder();
}
void Surface::ReconcileOrder()
{
    // A strip can move only after every touched fader is released. Until then
    // an offline target is inert, and touchKey_ still identifies the old target.
    if (!haveSnapshot_ || Touched() || DeferTopology) return;
    std::vector<std::wstring> next;
    std::set<std::wstring> seen;
    for (const auto& key : order_) if (Find(key) && seen.insert(key).second) next.push_back(key);
    for (const auto& key : onlineOrder_) if (Find(key) && seen.insert(key).second) next.push_back(key);
    if (next != order_)
    {
        order_ = std::move(next);
        const int last = order_.empty() ? 0 : ((static_cast<int>(order_.size()) - 1) / Strips) * Strips;
        bank_ = std::min(bank_, last);
        dirty_ = true;
    }
    if (!Find(selected_) && (!selected_.empty() || !order_.empty()))
    {
        selected_ = order_.empty() ? L"" : order_[bank_];
        dirty_ = true;
    }
    for (auto it = tracks_.begin(); it != tracks_.end();)
        if (!it->second.active) it = tracks_.erase(it); else ++it;
    for (auto it = pending_.begin(); it != pending_.end();)
        if (!Find(it->first.first)) it = pending_.erase(it); else ++it;
}
bool Surface::Bank(int delta)
{
    selectPressKey_.clear(); selectPressNote_ = -1;
    if (Touched()) { status_ = L"Release the fader before banking"; return false; }
    const auto last = order_.empty() ? 0 : ((static_cast<int>(order_.size()) - 1) / Strips) * Strips;
    const auto it = std::find(order_.begin(), order_.end(), selected_);
    const int offset = std::clamp(static_cast<int>(it - order_.begin()) - bank_, 0, Strips - 1);
    const int next = std::clamp(bank_ + delta, 0, last);
    if (next != bank_ && !order_.empty())
        selected_ = order_[std::min(next + offset, static_cast<int>(order_.size()) - 1)];
    bank_ = next;
    dirty_ = true;
    status_ = L"Bank " + std::to_wstring(bank_ + 1);
    return true;
}
bool Surface::Select(const std::wstring& key, bool focus)
{
    selectPressKey_.clear(); selectPressNote_ = -1;
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
    CursorGestures.ResetGesture(); cursorZoom_ = false;
    selectPressKey_.clear(); selectPressNote_ = -1;
    touched_.fill(false);
    touchKey_.fill({});
    pressed_.fill(false);
    encoderDue_.fill(0);
    ReconcileOrder();
    pending_.clear();
    cache_.clear();
    meterDue_ = 0;
    lcdDue_ = ringDue_ = timeDue_ = 0;
    dirty_ = true;
}
void Surface::JogRotate(int delta, std::uint64_t now)
{
    const int step = Jog.Rotate(delta, now);
    if (!step) return;
    const int seek = JogSeekDirections[delta > 0 ? 1 : 0];
    if (seek == -1 || seek == 1)
    {
        CursorGestures.ResetGesture();
        act_({ActionKind::SeekSeconds, selected_, static_cast<float>(std::abs(step) * seek)});
    }
    else if (const int command = CursorGestures.Rotate(4, delta > 0 ? 1 : -1, now))
        act_({ActionKind::CursorCommand, selected_, static_cast<float>(command), 4});
}
void Surface::Cursor(int note, std::uint64_t now)
{
    if (note < 0x60 || note > 0x63) return;
    const bool vertical = note < 0x62;
    const int axis = (cursorZoom_ ? 2 : 0) + (vertical ? 0 : 1);
    const int direction = vertical ? (note == 0x60 ? -1 : 1) : (note == 0x62 ? -1 : 1);
    const int step = CursorGestures.Rotate(axis, direction, now);
    if (step) act_({ActionKind::CursorCommand, selected_, static_cast<float>(step), axis});
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
bool Surface::ChannelCommand(ActionKind kind, float value, std::uint64_t now)
{
    const auto track = Selected();
    if (!track || !std::isfinite(value)) return false;
    switch (kind)
    {
    case ActionKind::Volume:
        Request(*track, kind, std::clamp(Value(*track, kind, now) + value, 0.F, 1.F), now); break;
    case ActionKind::Pan:
        if (!track->canPan) return false;
        Request(*track, kind, value == 0 ? 0 : std::clamp(Value(*track, kind, now) + value, -1.F, 1.F), now); break;
    case ActionKind::Mute: Request(*track, kind, 1.F - Value(*track, kind, now), now); break;
    case ActionKind::DefaultDevice:
        if (!track->defaultSelectable) return false;
        act_({kind, track->key}); break;
    case ActionKind::Solo: case ActionKind::Focus: case ActionKind::PlayPause:
    case ActionKind::Stop: case ActionKind::Previous: case ActionKind::Next:
    case ActionKind::Repeat: case ActionKind::Seek:
        if (!track->application) return false;
        act_({kind, track->key, value, -1, true}); break;
    default: return false;
    }
    return true;
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
        if (!Touched() && selected_ != track->key) { selected_ = track->key; dirty_ = true; }
        const auto v = FaderScalar(a | (b << 7));
        Request(*track, ActionKind::Volume, v, now);
        return;
    }
    if (channel != 0) return; // Custom note bindings are a separate host layer.
    if (type == 0xB0)
    {
        const int delta = Delta(b);
        if (!delta) return;
        if (a >= 0x10 && a <= 0x17)
        {
            const int encoder = a - 0x10;
            if (encoder == 0) ChannelCommand(ActionKind::Volume, delta * .01F, now);
            else if (encoder == 1) ChannelCommand(ActionKind::Pan, delta * .02F, now);
            // Discrete commands execute once per packet, not accelerated delta.
            // Bound long spinning to 10 commands/sec per encoder; never backlog.
            else if (now >= encoderDue_[encoder - 2])
            {
                encoderDue_[encoder - 2] = now + 100;
                act_({ActionKind::Encoder, selected_, delta > 0 ? 1.F : -1.F, encoder});
            }
        }
        else if (a == 0x3C) JogRotate(delta, now);
        return;
    }
    if (type != 0x90 && type != 0x80) return;
    const bool down = type == 0x90 && b != 0;
    if (a >= 0x68 && a <= 0x70)
    {
        const int fader = a - 0x68;
        if (down && !touched_[fader])
        {
            selectPressKey_.clear(); selectPressNote_ = -1;
            touchKey_[fader] = At(fader) ? At(fader)->key : L"";
            // First touch chooses the target; additional simultaneous touches
            // cannot move the encoder target during the existing gesture.
            if (!Touched() && !touchKey_[fader].empty())
            { selected_ = touchKey_[fader]; dirty_ = true; }
        }
        touched_[fader] = down;
        if (!down) { touchKey_[fader].clear(); cache_.erase(fader); ReconcileOrder(); }
        return;
    }
    const bool wasDown = pressed_[a];
    pressed_[a] = down;
    if (a >= 0x18 && a <= 0x1F)
    {
        // SELECT is an absolute identity, not a toggle. Some MCU navigation
        // streams send successive SELECT downs without releases. Revisit every
        // down, but reserve window activation for a matching physical release.
        const auto track = At(a - 0x18);
        if (down)
        {
            selectPressKey_.clear(); selectPressNote_ = -1;
            if (track && Select(track->key)) { selectPressKey_ = track->key; selectPressNote_ = a; }
        }
        else if (wasDown && selectPressNote_ == a)
        {
            const bool focus = !Touched() && track && track->key == selectPressKey_ && selected_ == selectPressKey_;
            const auto key = selectPressKey_;
            selectPressKey_.clear(); selectPressNote_ = -1;
            if (focus) act_({ActionKind::Focus, key});
        }
        return;
    }
    if (!down || wasDown) return;
    if (a >= 0x20 && a < 0x28)
    {
        if (a == 0x21) ChannelCommand(ActionKind::Pan, 0, now);
        else if (a >= 0x22) act_({ActionKind::Encoder, selected_, 0, a - 0x20});
        return; // Volume push deliberately has no reset-to-full action.
    }
    if (a < 0x20)
    {
        const auto track = At(a % Strips);
        if (!track) return;
        if (a < 8 && track->defaultSelectable) act_({ActionKind::DefaultDevice, track->key});
        else if (a >= 8 && a < 16 && track->application) act_({ActionKind::Solo, track->key});
        else if (a >= 16 && a < 24) Request(*track, ActionKind::Mute, 1.F - Value(*track, ActionKind::Mute, now), now);
        return;
    }
    switch (a)
    {
    case 0x2E: Bank(-Strips); break;
    case 0x2F: Bank(Strips); break;
    case 0x30: Bank(-1); break;
    case 0x31: Bank(1); break;
    // Assignment and Flip cannot change the current-channel encoder layout.
    case 0x5A: act_({ActionKind::ClearSolo, {}}); break;
    case 0x5B: act_({ActionKind::Previous, selected_}); break;
    case 0x5C: act_({ActionKind::Next, selected_}); break;
    case 0x5D: act_({ActionKind::Stop, selected_}); break;
    case 0x5E: act_({ActionKind::PlayPause, selected_}); break;
    case 0x56: act_({ActionKind::Repeat, selected_}); break;
    case 0x60: case 0x61: case 0x62: case 0x63: Cursor(a, now); break;
    case 0x64: SetCursorZoom(!cursorZoom_); break;
    // Do not invent a software action for native Focus/Scrub/Jog push.
    case 0x65: break;
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
            const auto v = track ? Value(*track, ActionKind::Volume, now) : 0.F;
            Cached(i, Fader(i, v), force);
        }
        if (i == Master) break;
        Cached(20 + i, Led(i, track && track->defaultDevice), force);
        Cached(30 + i, Led(8 + i, track && track->solo), force);
        Cached(40 + i, Led(16 + i, track && track->muted), force);
        Cached(50 + i, Led(24 + i, track && track->key == selected_), force);
        if (ringsDue)
        {
            const auto current = Selected();
            const bool available = current && (i == 0 || (i == 1 && current->canPan));
            const auto kind = i == 0 ? ActionKind::Volume : ActionKind::Pan;
            Cached(60 + i, Ring(i, available ? Value(*current, kind, now) : 0, available, i == 0), force);
        }
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
    Cached(101, Led(0x32, false), force);
    Cached(102, Led(0x28, false), force);
    Cached(103, Led(0x2A, true), force);
    Cached(104, Led(0x73, AnySolo), force);
    Cached(105, Led(0x5E, MediaAvailable && MediaPlaying), force);
    Cached(106, Led(0x5D, MediaAvailable && !MediaPlaying), force);
    Cached(107, Led(0x56, MediaAvailable && MediaRepeat), force);
    Cached(108, Led(0x64, cursorZoom_), force);
    if (force || now >= timeDue_)
    {
        timeDue_ = now + 200;
        const auto digits = TimeDigits(TimeDisplaySeconds);
        for (int i = 0; i < 10; ++i) Cached(140 + i, TimeDigit(i, digits[i]), force);
    }
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
