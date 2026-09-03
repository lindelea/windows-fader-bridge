#include "ChannelControl.h"
#include "Protocol.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace apollo
{
namespace
{
bool ChannelPath(const std::string &path, bool auxiliary)
{
    if (!IsPath(path) || path.rfind("/devices/", 0) != 0)
        return false;
    const auto slash = path.find('/', 9);
    if (slash == path.npos || slash == 9 ||
        path.substr(9, slash - 9).find_first_not_of("0123456789") != path.npos)
        return false;
    const std::string group = auxiliary ? "/auxs/" : "/inputs/";
    if (path.compare(slash, group.size(), group) != 0)
        return false;
    const auto id = path.substr(slash + group.size());
    return !id.empty() && id.find_first_not_of("0123456789") == id.npos;
}
bool Fresh(const Snapshot &snapshot)
{
    const auto age = std::chrono::steady_clock::now() - snapshot.receivedAt;
    return snapshot.connected && snapshot.generation != 0 && age >= decltype(age)::zero() &&
           age < std::chrono::seconds(2);
}
} // namespace
const char *FieldName(ChannelAddress field)
{
    switch (field.kind)
    {
    case ChannelField::Level:
        return "FaderLevel";
    case ChannelField::PanLeft:
        return "Pan";
    case ChannelField::PanRight:
        return "Pan2";
    case ChannelField::Mute:
        return "Mute";
    case ChannelField::Solo:
        return "Solo";
    case ChannelField::Output:
        return "OutputDestination";
    case ChannelField::Input:
        return "IOType";
    case ChannelField::SendLevel:
    case ChannelField::PreampGain:
        return "Gain";
    case ChannelField::SendBypass:
        return "Bypass";
    case ChannelField::SendPan:
        return "Pan";
    case ChannelField::LowCut:
        return "LowCut";
    case ChannelField::Phase:
        return "Phase";
    case ChannelField::Pad:
        return "Pad";
    case ChannelField::InsertPower:
    case ChannelField::UnisonPower:
        return "Power";
    case ChannelField::InsertValue:
    case ChannelField::UnisonValue:
        return "NormalizedValue";
    case ChannelField::InsertStep:
    case ChannelField::UnisonStep:
        return "StepValue";
    case ChannelField::Phantom:
        return "48V";
    case ChannelField::Reference:
        return "Pad";
    case ChannelField::SampleRateConvert:
        return "SRConvert";
    case ChannelField::RecordPreEffects:
        return "RecordPreEffects";
    case ChannelField::SendPostFader:
        return "SendPostFader";
    case ChannelField::Mono:
        return "MixToMono";
    case ChannelField::Talk:
        return "TalkbackOn";
    case ChannelField::TalkToMonitor:
        return "TalkbackInPhysicalCR";
    }
    throw std::invalid_argument("Unknown channel parameter");
}
const std::optional<Parameter> &FieldParameter(const Channel &c, ChannelAddress field)
{
    static const std::optional<Parameter> absent;
    switch (field.kind)
    {
    case ChannelField::UnisonPower:
    case ChannelField::UnisonValue:
    case ChannelField::UnisonStep:
        for (const auto &preamp : c.preamps)
            if (preamp.slot == field.preamp)
                for (const auto &insert : preamp.unison)
                    if (insert.slot == field.slot)
                    {
                        if (field.kind == ChannelField::UnisonPower)
                            return insert.power;
                        for (const auto &parameter : insert.parameters)
                            if (parameter.slot == field.parameter)
                                return field.kind == ChannelField::UnisonValue ? parameter.normalized
                                                                               : parameter.step;
                    }
        return absent;
    case ChannelField::Level:
        return c.level;
    case ChannelField::PanLeft:
        return c.pan;
    case ChannelField::PanRight:
        return c.panRight;
    case ChannelField::Mute:
        return c.mute;
    case ChannelField::Solo:
        return c.solo;
    case ChannelField::Output:
        return c.output;
    case ChannelField::Input:
        return c.input;
    case ChannelField::Reference:
        return c.reference;
    case ChannelField::SampleRateConvert:
        return c.sampleRateConvert;
    case ChannelField::RecordPreEffects:
        return c.recordPreEffects;
    case ChannelField::SendPostFader:
        return c.sendPostFader;
    case ChannelField::Mono:
        return c.mono;
    case ChannelField::Talk:
        return c.talk;
    case ChannelField::TalkToMonitor:
        return c.talkToMonitor;
    case ChannelField::SendLevel:
    case ChannelField::SendBypass:
    case ChannelField::SendPan:
        for (const auto &s : c.sends)
            if (s.slot == field.slot)
                return field.kind == ChannelField::SendLevel ? s.level
                       : field.kind == ChannelField::SendPan ? s.pan
                                                             : s.bypass;
        return absent;
    case ChannelField::PreampGain:
    case ChannelField::LowCut:
    case ChannelField::Phase:
    case ChannelField::Pad:
    case ChannelField::Phantom:
        for (const auto &p : c.preamps)
            if (p.slot == field.slot)
                return field.kind == ChannelField::PreampGain ? p.gain
                       : field.kind == ChannelField::LowCut   ? p.lowCut
                       : field.kind == ChannelField::Phase    ? p.phase
                       : field.kind == ChannelField::Phantom  ? p.phantom
                                                              : p.pad;
        return absent;
    case ChannelField::InsertPower:
    case ChannelField::InsertValue:
    case ChannelField::InsertStep:
        for (const auto &i : c.inserts)
            if (i.slot == field.slot)
            {
                if (field.kind == ChannelField::InsertPower)
                    return i.power;
                for (const auto &p : i.parameters)
                    if (p.slot == field.parameter)
                        return field.kind == ChannelField::InsertValue ? p.normalized : p.step;
            }
        return absent;
    }
    throw std::invalid_argument("Unknown channel parameter");
}
void SetFieldFeedback(Channel &c, ChannelAddress field, const Json &value)
{
    const auto &p = FieldParameter(c, field);
    if (p)
        const_cast<Parameter &>(*p).value = value; // c is mutable; absent sentinel is never modified.
}
bool BooleanField(ChannelAddress field)
{
    return field.kind == ChannelField::Mute || field.kind == ChannelField::Solo ||
           field.kind == ChannelField::SendBypass || field.kind == ChannelField::LowCut ||
           field.kind == ChannelField::Phase || field.kind == ChannelField::Pad ||
           field.kind == ChannelField::InsertPower || field.kind == ChannelField::Phantom ||
           field.kind == ChannelField::Reference || field.kind == ChannelField::SampleRateConvert ||
           field.kind == ChannelField::RecordPreEffects || field.kind == ChannelField::SendPostFader ||
           field.kind == ChannelField::Mono || field.kind == ChannelField::Talk ||
           field.kind == ChannelField::TalkToMonitor || field.kind == ChannelField::UnisonPower;
}
bool ChoiceField(ChannelAddress field)
{
    return field.kind == ChannelField::Output || field.kind == ChannelField::Input ||
           field.kind == ChannelField::InsertStep || field.kind == ChannelField::UnisonStep;
}
std::string FieldNodePath(const Channel &c, ChannelAddress a)
{
    if (UnisonField(a))
    {
        if (!NumericSlot(a.preamp) || !NumericSlot(a.slot))
            throw std::invalid_argument("Invalid UNISON address");
        auto path = c.path + "/preamps/" + a.preamp + "/effects/" + a.slot;
        if (a.kind != ChannelField::UnisonPower)
        {
            if (!NumericSlot(a.parameter))
                throw std::invalid_argument("Invalid UNISON parameter");
            path += "/parameters/" + a.parameter;
        }
        else if (!a.parameter.empty())
            throw std::invalid_argument("Unexpected UNISON parameter");
        return path;
    }
    if (!a.preamp.empty())
        throw std::invalid_argument("Unexpected preamp address");
    std::string group;
    switch (a.kind)
    {
    case ChannelField::Talk:
    case ChannelField::TalkToMonitor:
        if (!a.slot.empty() || !a.parameter.empty() || c.talkContext.empty())
            throw std::invalid_argument("Invalid talkback master");
        return ""; // exact global property path, never arbitrary routing
    case ChannelField::SendLevel:
    case ChannelField::SendBypass:
    case ChannelField::SendPan:
        group = "sends";
        break;
    case ChannelField::PreampGain:
    case ChannelField::LowCut:
    case ChannelField::Phase:
    case ChannelField::Pad:
    case ChannelField::Phantom:
        group = "preamps";
        break;
    case ChannelField::InsertPower:
    case ChannelField::InsertValue:
    case ChannelField::InsertStep:
        group = "effects";
        break;
    default:
        FieldName(a); // Reject unknown enum values, including otherwise plausible paths.
        if (!a.slot.empty() || !a.parameter.empty())
            throw std::invalid_argument("Unexpected channel address components");
        return c.path;
    }
    if (!NumericSlot(a.slot))
        throw std::invalid_argument("Invalid channel feature slot");
    const auto path = c.path + "/" + group + "/" + a.slot;
    if (a.kind == ChannelField::InsertValue || a.kind == ChannelField::InsertStep)
    {
        if (!NumericSlot(a.parameter))
            throw std::invalid_argument("Invalid plug-in parameter slot");
        return path + "/parameters/" + a.parameter;
    }
    if (!a.parameter.empty())
        throw std::invalid_argument("Unexpected parameter slot");
    return path;
}
namespace
{
bool SameParameter(const std::optional<Parameter> &a, const std::optional<Parameter> &b)
{
    return a.has_value() == b.has_value() &&
           (!a || (a->path == b->path && a->value.kind == b->value.kind && a->minimum == b->minimum &&
                   a->maximum == b->maximum && a->enabled == b->enabled &&
                   a->reportedReadOnly == b->reportedReadOnly && a->choices == b->choices));
}
std::string ItemIdentity(const Channel &c, ChannelAddress a)
{
    if (UnisonField(a))
        for (const auto &preamp : c.preamps)
            if (preamp.slot == a.preamp)
                for (const auto &insert : preamp.unison)
                    if (insert.slot == a.slot)
                    {
                        auto id = preamp.path + ":" + preamp.context + ":" + insert.identity;
                        if (a.kind == ChannelField::UnisonPower)
                            return id;
                        for (const auto &parameter : insert.parameters)
                            if (parameter.slot == a.parameter)
                                return id + ":" + parameter.slot + ":" + parameter.name;
                    }
    for (const auto &s : c.sends)
        if (s.slot == a.slot && (a.kind == ChannelField::SendLevel || a.kind == ChannelField::SendBypass ||
                                 a.kind == ChannelField::SendPan))
            return s.path + ":" + s.id + ":" + s.name;
    for (const auto &p : c.preamps)
        if (p.slot == a.slot &&
            (a.kind == ChannelField::PreampGain || a.kind == ChannelField::LowCut ||
             a.kind == ChannelField::Phase || a.kind == ChannelField::Pad || a.kind == ChannelField::Phantom))
            return p.path + ":" + p.context;
    for (const auto &i : c.inserts)
        if (i.slot == a.slot && (a.kind == ChannelField::InsertPower || a.kind == ChannelField::InsertValue ||
                                 a.kind == ChannelField::InsertStep))
        {
            if (a.kind == ChannelField::InsertPower)
                return i.path + ":" + i.identity;
            for (const auto &p : i.parameters)
                if (p.slot == a.parameter)
                    return i.path + ":" + i.identity + ":" + p.slot + ":" + p.name;
        }
    return {};
}
std::string QuotedChoice(const std::string &s)
{
    if (s.empty() || s.size() > 256)
        throw std::invalid_argument("Invalid choice length");
    std::string quoted = "\"";
    for (const unsigned char c : s)
    {
        if (c < 32 || c == 127)
            throw std::invalid_argument("Control characters are not permitted in choices");
        if (c == '"' || c == '\\')
            quoted += '\\';
        quoted += static_cast<char>(c);
    }
    return quoted + '"';
}
} // namespace
bool SameFieldTarget(const Channel &a, const Channel &b, ChannelAddress field)
{
    return SameControlTarget(a, b) && SameFieldShape(a, b, field);
}
bool SameFieldShape(const Channel &a, const Channel &b, ChannelAddress field)
{
    return SameParameter(FieldParameter(a, field), FieldParameter(b, field)) &&
           ItemIdentity(a, field) == ItemIdentity(b, field);
}
std::vector<ChannelAddress> ExtensionAddresses(const Channel &c)
{
    std::vector<ChannelAddress> addresses{ChannelField::Output,
                                          ChannelField::Input,
                                          ChannelField::Reference,
                                          ChannelField::SampleRateConvert,
                                          ChannelField::RecordPreEffects,
                                          ChannelField::SendPostFader,
                                          ChannelField::Mono,
                                          ChannelField::Talk,
                                          ChannelField::TalkToMonitor};
    for (const auto &s : c.sends)
        for (auto kind : {ChannelField::SendLevel, ChannelField::SendBypass, ChannelField::SendPan})
            addresses.emplace_back(kind, s.slot);
    for (const auto &p : c.preamps)
    {
        for (auto kind : {ChannelField::PreampGain, ChannelField::LowCut, ChannelField::Phase,
                          ChannelField::Pad, ChannelField::Phantom})
            addresses.emplace_back(kind, p.slot);
        for (const auto &insert : p.unison)
        {
            addresses.emplace_back(ChannelField::UnisonPower, insert.slot, "", p.slot);
            for (const auto &parameter : insert.parameters)
            {
                addresses.emplace_back(ChannelField::UnisonValue, insert.slot, parameter.slot, p.slot);
                addresses.emplace_back(ChannelField::UnisonStep, insert.slot, parameter.slot, p.slot);
            }
        }
    }
    for (const auto &i : c.inserts)
    {
        addresses.emplace_back(ChannelField::InsertPower, i.slot);
        for (const auto &p : i.parameters)
        {
            addresses.emplace_back(ChannelField::InsertValue, i.slot, p.slot);
            addresses.emplace_back(ChannelField::InsertStep, i.slot, p.slot);
        }
    }
    return addresses;
}
bool SameExtensionShape(const Channel &a, const Channel &b)
{
    const auto addresses = ExtensionAddresses(a);
    if (addresses != ExtensionAddresses(b) || a.insertSlots != b.insertSlots)
        return false;
    for (const auto &field : addresses)
        if (!SameParameter(FieldParameter(a, field), FieldParameter(b, field)) ||
            ItemIdentity(a, field) != ItemIdentity(b, field))
            return false;
    return true;
}
bool ControlEligible(const Channel &c)
{
    // Further input families require an independently verified type; never
    // include TalkbackMic just because its node contains familiar properties.
    return !c.monitor && !c.key.empty() && ChannelPath(c.path, c.auxiliary) &&
           (c.auxiliary || c.ioType == "Mic" || c.ioType == "Line" || c.ioType == "Virtual" ||
            c.ioType == "ADAT" || c.ioType == "S/PDIF" ||
            (c.ioType == "TalkbackMic" && !c.talkContext.empty() && c.talk && c.talkToMonitor));
}
bool SameControlTarget(const Channel &a, const Channel &b)
{
    if (!ControlEligible(a) || !ControlEligible(b) || a.key != b.key || a.path != b.path ||
        a.stereo != b.stereo || a.auxiliary != b.auxiliary || a.ioType != b.ioType ||
        a.destination != b.destination || a.talkContext != b.talkContext)
        return false;
    for (auto field : {ChannelField::Level, ChannelField::PanLeft, ChannelField::PanRight, ChannelField::Mute,
                       ChannelField::Solo})
    {
        const auto &x = FieldParameter(a, field), &y = FieldParameter(b, field);
        if (x.has_value() != y.has_value())
            return false;
        if (x && (x->path != y->path || x->minimum != y->minimum || x->maximum != y->maximum ||
                  x->enabled != y->enabled || x->reportedReadOnly != y->reportedReadOnly ||
                  x->value.kind != y->value.kind))
            return false;
    }
    return true;
}
Json ControlNumber(double value)
{
    if (!std::isfinite(value))
        throw std::invalid_argument("Non-finite control value");
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(12) << value;
    return Json::Parse(text.str());
}
bool SamePermissionTarget(const Channel &a, const Channel &b)
{
    if (!ControlEligible(a) || !ControlEligible(b) || a.key != b.key || a.path != b.path ||
        a.auxiliary != b.auxiliary || a.monitor != b.monitor)
        return false;
    // Talkback routing is device-global and may be rebound to another physical
    // source. Do not carry authority across that identity change.
    return (a.ioType != "TalkbackMic" && b.ioType != "TalkbackMic") || a.talkContext == b.talkContext;
}
bool ChangesChannelContext(ChannelAddress field)
{
    return field.kind == ChannelField::Output || field.kind == ChannelField::Input ||
           field.kind == ChannelField::Phantom;
}
bool NeedsSafetyUnlock(const Channel &c, ChannelAddress field, const Json &value)
{
    // UNISON parameters can be coupled to analog preamp state, including phantom
    // and impedance. Never infer safety from a plug-in's free-form parameter name.
    if (UnisonField(field))
        return true;
    return value.Bool() &&
           (field.kind == ChannelField::Phantom || field.kind == ChannelField::TalkToMonitor ||
            (field.kind == ChannelField::Talk && (!c.talkToMonitor || c.talkToMonitor->value.Bool())));
}
std::string ChannelCommand(const Channel &c, ChannelAddress field, const Json &value)
{
    const auto &p = FieldParameter(c, field);
    if (!ControlEligible(c) || !p || !p->enabled || p->reportedReadOnly ||
        p->path != FieldNodePath(c, field) + "/" + FieldName(field) + "/value" ||
        (field == ChannelField::PanRight && !c.stereo) || (field.kind == ChannelField::SendPan && c.stereo))
        throw std::invalid_argument("Channel control is unavailable");
    std::string scalar;
    if (BooleanField(field))
    {
        if (value.kind != Json::Kind::Boolean || p->value.kind != Json::Kind::Boolean)
            throw std::invalid_argument("Expected Boolean control");
        scalar = value.Bool() ? "true" : "false";
    }
    else if (ChoiceField(field))
    {
        if (value.kind != Json::Kind::String || p->value.kind != Json::Kind::String ||
            p->choices.size() > 256 ||
            std::find(p->choices.begin(), p->choices.end(), value.String()) == p->choices.end() ||
            (field == ChannelField::Input &&
             (c.preamps.empty() || (value.String() != "Mic" && value.String() != "Line"))))
            throw std::invalid_argument("Unavailable channel choice");
        scalar = QuotedChoice(value.String());
    }
    else
    {
        const double n = value.Number(NAN);
        if (value.kind != Json::Kind::Number || p->value.kind != Json::Kind::Number || !std::isfinite(n) ||
            !p->minimum || !p->maximum || !std::isfinite(*p->minimum) || !std::isfinite(*p->maximum) ||
            *p->minimum >= *p->maximum || n < *p->minimum || n > *p->maximum ||
            ((field.kind == ChannelField::PanLeft || field.kind == ChannelField::PanRight ||
              field.kind == ChannelField::SendPan) &&
             (n < -1 || n > 1)) ||
            ((field.kind == ChannelField::Level || field.kind == ChannelField::SendLevel) &&
             (n < -144 || n > 12)) ||
            (field.kind == ChannelField::PreampGain && (n < 10 || n > 65 || std::floor(n) != n)) ||
            ((field.kind == ChannelField::InsertValue || field.kind == ChannelField::UnisonValue) &&
             (n < 0 || n > 1)))
            throw std::invalid_argument("Invalid or out-of-range channel value");
        scalar = ControlNumber(n).scalar;
    }
    return "set " + p->path + " " + scalar + '\0';
}
bool SameControlValue(ChannelAddress field, const Json &a, const Json &b)
{
    if (a.kind != b.kind)
        return false;
    if (BooleanField(field))
        return a.kind == Json::Kind::Boolean && a.Bool() == b.Bool();
    if (ChoiceField(field))
        return a.kind == Json::Kind::String && a.scalar == b.scalar;
    return a.kind == Json::Kind::Number && std::isfinite(a.Number(NAN)) && std::isfinite(b.Number(NAN)) &&
           std::abs(a.Number() - b.Number()) <=
               ((field.kind == ChannelField::Level || field.kind == ChannelField::SendLevel ||
                 field.kind == ChannelField::PreampGain)
                    ? 0.01
                    : 0.0001);
}
uint64_t ChannelQueue::Arm(const Snapshot &snapshot, const std::string &key)
{
    Disarm();
    if (!Fresh(snapshot))
        throw std::runtime_error("Fresh Apollo state required");
    const auto it = std::find_if(snapshot.channels.begin(), snapshot.channels.end(),
                                 [&](const auto &c) { return c.key == key; });
    if (it == snapshot.channels.end() || !ControlEligible(*it))
        throw std::runtime_error("Select a supported input or AUX return");
    target_ = *it;
    generation_ = snapshot.generation;
    armed_ = true;
    return epoch_;
}
void ChannelQueue::Disarm()
{
    armed_ = false;
    ++epoch_;
    pending_.clear();
}
bool ChannelQueue::Valid(const Snapshot &snapshot) const
{
    if (!armed_ || !Fresh(snapshot) || snapshot.generation != generation_)
        return false;
    const auto it = std::find_if(snapshot.channels.begin(), snapshot.channels.end(),
                                 [&](const auto &c) { return c.key == target_.key; });
    return it != snapshot.channels.end() && SameControlTarget(target_, *it) &&
           SameExtensionShape(target_, *it);
}
uint64_t ChannelQueue::Submit(ChannelAddress field, const Json &value, uint64_t epoch)
{
    if (!armed_ || epoch != epoch_ || !epoch)
        return 0;
    ChannelCommand(target_, field, value); // validate before anything is queued
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(), [&](const auto &r) { return r.field == field; }),
        pending_.end());
    if (pending_.size() >= 128)
        throw std::runtime_error("Too many pending channel gestures");
    pending_.push_back(
        {target_, field, value, epoch_, generation_, ++sequence_, std::chrono::steady_clock::now()});
    return sequence_;
}
std::optional<ChannelRequest> ChannelQueue::Take()
{
    return TakeReady([](const ChannelRequest &) { return true; });
}
std::optional<ChannelRequest> ChannelQueue::TakeReady(
    const std::function<bool(const ChannelRequest &)> &ready)
{
    if (!armed_ || pending_.empty())
        return {};
    const auto found = std::find_if(pending_.begin(), pending_.end(), ready);
    if (found == pending_.end())
        return {};
    auto request = std::move(*found);
    pending_.erase(found);
    // The writer owns the final freshness check so it can discard one stale
    // gesture without revoking the channel's explicitly granted permission.
    return request;
}
} // namespace apollo
