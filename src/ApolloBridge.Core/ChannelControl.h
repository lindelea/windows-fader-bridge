#pragma once
#include "Model.h"
#include <deque>
#include <functional>
#include <tuple>

namespace apollo
{
enum class ChannelField
{
    Level,
    PanLeft,
    PanRight,
    Mute,
    Solo,
    Output,
    Input,
    SendLevel,
    SendBypass,
    SendPan,
    PreampGain,
    LowCut,
    Phase,
    Pad,
    InsertPower,
    InsertValue,
    InsertStep,
    Phantom,
    Reference,
    SampleRateConvert,
    RecordPreEffects,
    SendPostFader,
    Mono,
    Talk,
    TalkToMonitor,
    UnisonPower,
    UnisonValue,
    UnisonStep
};
struct ChannelAddress
{
    ChannelField kind = ChannelField::Level;
    std::string slot, parameter, preamp;
    ChannelAddress() = default;
    ChannelAddress(ChannelField field, std::string item = {}, std::string param = {}, std::string pre = {})
        : kind(field), slot(std::move(item)), parameter(std::move(param)), preamp(std::move(pre))
    {
    }
    friend bool operator<(const ChannelAddress &a, const ChannelAddress &b)
    {
        return std::tie(a.kind, a.slot, a.parameter, a.preamp) <
               std::tie(b.kind, b.slot, b.parameter, b.preamp);
    }
    friend bool operator==(const ChannelAddress &a, const ChannelAddress &b)
    {
        return std::tie(a.kind, a.slot, a.parameter, a.preamp) ==
               std::tie(b.kind, b.slot, b.parameter, b.preamp);
    }
    friend bool operator!=(const ChannelAddress &a, const ChannelAddress &b)
    {
        return !(a == b);
    }
};
inline bool UnisonField(ChannelAddress a)
{
    return a.kind == ChannelField::UnisonPower || a.kind == ChannelField::UnisonValue ||
           a.kind == ChannelField::UnisonStep;
}
const char *FieldName(ChannelAddress field);
const std::optional<Parameter> &FieldParameter(const Channel &channel, ChannelAddress field);
void SetFieldFeedback(Channel &channel, ChannelAddress field, const Json &value);
std::string FieldNodePath(const Channel &channel, ChannelAddress field);
bool BooleanField(ChannelAddress field);
bool ChoiceField(ChannelAddress field);
bool SameFieldTarget(const Channel &a, const Channel &b, ChannelAddress field);
bool SameFieldShape(const Channel &a, const Channel &b, ChannelAddress field);
bool SameExtensionShape(const Channel &a, const Channel &b);
std::vector<ChannelAddress> ExtensionAddresses(const Channel &channel);
bool ControlEligible(const Channel &channel);
bool SameControlTarget(const Channel &a, const Channel &b);
// Permission follows a stable logical channel, while callback epochs and field
// validation follow the current control shape.
bool SamePermissionTarget(const Channel &a, const Channel &b);
// Strictly typed, range checked ordinary-channel writes; no arbitrary paths or
// strings.
std::string ChannelCommand(const Channel &channel, ChannelAddress field, const Json &value);
bool SameControlValue(ChannelAddress field, const Json &a, const Json &b);
bool NeedsSafetyUnlock(const Channel &channel, ChannelAddress field, const Json &value);
bool ChangesChannelContext(ChannelAddress field);
Json ControlNumber(double value);

struct ChannelRequest
{
    Channel target;
    ChannelAddress field = ChannelField::Level;
    Json value;
    uint64_t epoch = 0, generation = 0, sequence = 0;
    std::chrono::steady_clock::time_point created{};
};
// Owner supplies synchronization. Coalescing is by typed address, not knob
// index.
class ChannelQueue
{
  public:
    uint64_t Arm(const Snapshot &snapshot, const std::string &key);
    void Disarm();
    bool Valid(const Snapshot &snapshot) const;
    uint64_t Submit(ChannelAddress field, const Json &value, uint64_t epoch);
    std::optional<ChannelRequest> Take();
    // Remove the oldest request accepted by ready. This lets the transport
    // rate-limit one continuous parameter without delaying a different fader,
    // pan or switch queued behind it.
    std::optional<ChannelRequest> TakeReady(const std::function<bool(const ChannelRequest &)> &ready);
    uint64_t Epoch() const
    {
        return armed_ ? epoch_ : 0;
    }
    uint64_t Generation() const
    {
        return generation_;
    }
    const Channel &Target() const
    {
        return target_;
    }
    size_t Size() const
    {
        return pending_.size();
    }

  private:
    Channel target_;
    bool armed_ = false;
    uint64_t epoch_ = 0, generation_ = 0, sequence_ = 0;
    std::deque<ChannelRequest> pending_;
};
} // namespace apollo
