#pragma once

#include "MackieProtocol.h"
#include <functional>
#include <map>
#include <set>

namespace mackie
{
enum class ActionKind { Volume, Pan, Mute, Solo, DefaultDevice, Focus,
    ClearSolo, PlayPause, Stop, Previous, Next, Repeat, Seek };
struct Action { ActionKind kind; std::wstring key; float value = 0; };
struct Track
{
    std::wstring key, name;
    bool active = false, muted = false, solo = false, defaultDevice = false;
    bool defaultSelectable = false, master = false, application = false, canPan = false;
    float volume = 0, pan = 0, peakDb = -120;
};
struct Pending
{
    float target = 0;
    std::uint64_t until = 0;
    float Resolve(float actual, std::uint64_t now, float tolerance)
    {
        if (now >= until || std::abs(actual - target) <= tolerance) { until = 0; return actual; }
        return target;
    }
};
class Surface
{
public:
    using Sender = std::function<void(const Bytes&)>;
    using Handler = std::function<void(const Action&)>;
    Surface(Sender sender, Handler handler) : send_(std::move(sender)), act_(std::move(handler)) {}
    void Update(const std::vector<Track>& tracks, std::uint64_t now);
    void Input(std::uint32_t raw, std::uint64_t now);
    void Feedback(std::uint64_t now, bool force = false);
    void ResetConnection();
    void InvalidateFeedback() { cache_.clear(); dirty_ = true; }
    bool Bank(int delta);
    bool Select(const std::wstring& key, bool focus = false);
    void SetEncoderVolume(bool enabled) { if (!Touched()) { volumeKnobs_ = enabled; flip_ = false; dirty_ = true; } }
    bool VolumeKnobs() const { return volumeKnobs_; }
    bool Flipped() const { return flip_; }
    bool Touched() const;
    int BankStart() const { return bank_; }
    const std::vector<std::wstring>& Order() const { return order_; }
    void RestoreOrder(const std::vector<std::wstring>& order);
    const Track* At(int physical) const;
    const Track* Selected() const;
    std::wstring Status() const { return status_; }
    bool RequireTouch = true;
    bool LcdEnabled = true;
    bool MetersEnabled = true;
    bool MediaPlaying = false, MediaAvailable = false, MediaRepeat = false;
    bool AnySolo = false;
private:
    const Track* Find(const std::wstring& key) const;
    float Value(const Track& track, ActionKind kind, std::uint64_t now);
    void Request(const Track& track, ActionKind kind, float value, std::uint64_t now);
    void Cached(int id, const Bytes& bytes, bool force);
    Sender send_;
    Handler act_;
    std::map<std::wstring, Track> tracks_;
    std::vector<std::wstring> order_;
    std::wstring selected_, status_;
    std::map<std::pair<std::wstring, ActionKind>, Pending> pending_;
    std::array<bool, 9> touched_{};
    std::array<std::wstring, 9> touchKey_{};
    std::array<bool, 128> pressed_{};
    std::map<int, Bytes> cache_;
    int bank_ = 0;
    bool flip_ = false, volumeKnobs_ = false, dirty_ = true;
    std::uint64_t meterDue_ = 0;
    std::uint64_t lcdDue_ = 0, ringDue_ = 0;
};
}
