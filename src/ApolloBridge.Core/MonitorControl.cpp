#include "MonitorControl.h"
#include "ChannelControl.h"
#include "Protocol.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace apollo
{
namespace
{
bool OutputPath(const std::string &path)
{
    if (!IsPath(path) || path.rfind("/devices/", 0) != 0)
        return false;
    const auto slash = path.find('/', 9);
    if (slash == path.npos || slash == 9 || path.substr(9, slash - 9).find_first_not_of("0123456789") != path.npos ||
        path.compare(slash, 9, "/outputs/") != 0)
        return false;
    const auto id = path.substr(slash + 9);
    return !id.empty() && id.find_first_not_of("0123456789") == id.npos;
}
bool FreshMonitorState(const Snapshot &s)
{
    const auto age = std::chrono::steady_clock::now() - s.receivedAt;
    return s.connected && s.generation && age >= decltype(age)::zero() && age < std::chrono::seconds(2);
}
bool SameParameter(const std::optional<Parameter> &a, const std::optional<Parameter> &b, bool compareValue)
{
    if (a.has_value() != b.has_value())
        return false;
    return !a || (a->path == b->path && a->minimum == b->minimum && a->maximum == b->maximum &&
                  a->enabled == b->enabled && a->reportedReadOnly == b->reportedReadOnly &&
                  a->choices == b->choices && a->value.kind == b->value.kind &&
                  (!compareValue || a->value.scalar == b->value.scalar));
}
std::string FieldPath(const Monitor &m, MonitorField field)
{
    if (field == MonitorField::Talk)
        return "/TalkbackOn/value";
    if (field == MonitorField::TalkLevel)
        return m.talkbackMicPath + "/FaderLevel/value";
    const auto parent = field == MonitorField::DimAmount || field == MonitorField::Speakers
                            ? m.path.substr(0, m.path.find('/', 9)) : m.path;
    return parent + "/" + FieldName(field) + "/value";
}
} // namespace
const char *FieldName(MonitorField field)
{
    switch (field)
    {
    case MonitorField::Level:
        return "CRMonitorLevel";
    case MonitorField::Mute:
        return "Mute";
    case MonitorField::Dim:
        return "DimOn";
    case MonitorField::Mono:
        return "MixToMono";
    case MonitorField::DimAmount:
        return "DimAttenuation";
    case MonitorField::Source:
        return "MixInSource";
    case MonitorField::Talk:
        return "TalkbackOn";
    case MonitorField::TalkLevel:
        return "FaderLevel";
    case MonitorField::Speakers:
        return "AltMonSelection";
    }
    throw std::invalid_argument("Unknown monitor field");
}
const std::optional<Parameter> &FieldParameter(const Monitor &m, MonitorField field)
{
    switch (field)
    {
    case MonitorField::Level:
        return m.level;
    case MonitorField::Mute:
        return m.mute;
    case MonitorField::Dim:
        return m.dim;
    case MonitorField::Mono:
        return m.mono;
    case MonitorField::DimAmount:
        return m.dimAttenuation;
    case MonitorField::Source:
        return m.sourceSelect;
    case MonitorField::Talk:
        return m.talk;
    case MonitorField::TalkLevel:
        return m.talkLevel;
    case MonitorField::Speakers:
        return m.speakerSelection;
    }
    throw std::invalid_argument("Unknown monitor field");
}
const std::vector<float> &MonitorDimTable()
{
    static const std::vector<float> table{-60, -51, -43, -34, -26, -17, -9};
    return table;
}
std::vector<std::string> MonitorSources(const Monitor &m)
{
    std::vector<std::string> sources;
    if (!m.sourceSelect || !m.sourceSelect->enabled || m.sourceSelect->reportedReadOnly ||
        m.sourceSelect->path != m.path + "/MixInSource/value" || m.sourceSelect->value.kind != Json::Kind::String)
        return sources;
    for (const std::string s : {"mon", "cue1", "cue2", "cue3", "cue4"})
        if (std::find(m.sourceSelect->choices.begin(), m.sourceSelect->choices.end(), s) != m.sourceSelect->choices.end())
            sources.push_back(s);
    return sources;
}
bool MonitorFieldAvailable(const Monitor &m, MonitorField field)
{
    if (!OutputPath(m.path))
        return false;
    const auto &p = FieldParameter(m, field);
    if (!p || !p->enabled || p->reportedReadOnly || p->path != FieldPath(m, field))
        return false;
    if (field == MonitorField::Speakers)
        return p->value.kind == Json::Kind::Number && p->minimum && p->maximum &&
               *p->minimum == 0 && *p->maximum >= 0 && *p->maximum <= 2 &&
               std::floor(*p->maximum) == *p->maximum &&
               std::isfinite(p->value.Number(NAN)) &&
               std::floor(p->value.Number()) == p->value.Number() &&
               p->value.Number() >= 0 && p->value.Number() <= *p->maximum;
    if (field == MonitorField::TalkLevel)
        return MonitorFieldAvailable(m, MonitorField::Talk) &&
               p->value.kind == Json::Kind::Number && p->minimum && p->maximum &&
               std::isfinite(*p->minimum) && std::isfinite(*p->maximum) &&
               *p->minimum >= -144 && *p->maximum <= 12 && *p->minimum < *p->maximum &&
               std::isfinite(p->value.Number(NAN)) && p->value.Number() >= *p->minimum &&
               p->value.Number() <= *p->maximum;
    if (field == MonitorField::Source)
    {
        const auto choices = MonitorSources(m);
        return p->value.String() == m.source && std::find(choices.begin(), choices.end(), m.source) != choices.end();
    }
    if (field == MonitorField::Level || field == MonitorField::DimAmount)
    {
        if (p->value.kind != Json::Kind::Number || !p->minimum || !p->maximum || !std::isfinite(p->value.Number(NAN)))
            return false;
        if (field == MonitorField::Level)
            return true;
        const auto &table = MonitorDimTable();
        return *p->minimum == 0 && *p->maximum == 60 &&
               std::find(table.begin(), table.end(), -p->value.Number()) != table.end();
    }
    if (field == MonitorField::Talk)
    {
        const auto device = m.path.substr(0, m.path.find('/', 9));
        if (m.talkbackMicPath.rfind(device + "/inputs/", 0) != 0 || !IsPath(m.talkbackMicPath) ||
            !m.talkbackMaster || m.talkbackMaster->path != "/TalkbackMaster/value" ||
            m.talkbackMaster->value.kind != Json::Kind::Number || m.talkbackMaster->value.scalar != device.substr(9) ||
            !m.talkbackToMonitor || m.talkbackToMonitor->path != "/TalkbackInPhysicalCR/value" ||
            m.talkbackToMonitor->value.kind != Json::Kind::Boolean ||
            !m.talkbackMicSelect || m.talkbackMicSelect->path != "/TalkbackMicSelect/value" ||
            m.talkbackMicSelect->value.kind != Json::Kind::Number || m.talkbackMicSelect->value.Number(NAN) != 0)
            return false;
    }
    return p->value.kind == Json::Kind::Boolean;
}
bool MonitorEligible(const Monitor &m)
{
    const auto sources = MonitorSources(m);
    const bool sourceKnown = m.source == "mon" || std::find(sources.begin(), sources.end(), m.source) != sources.end();
    if (m.key.empty() || !OutputPath(m.path) || !m.stereo || m.mode != "STEREO" || !sourceKnown || !m.level ||
        !m.level->enabled || m.level->reportedReadOnly || m.level->value.kind != Json::Kind::Number ||
        m.level->path != m.path + "/CRMonitorLevel/value" || !m.level->minimum || !m.level->maximum ||
        !std::isfinite(*m.level->minimum) || !std::isfinite(*m.level->maximum) || *m.level->minimum < -144 ||
        *m.level->maximum > 0 || *m.level->minimum >= *m.level->maximum || !std::isfinite(m.level->value.Number(NAN)) ||
        m.level->value.Number() < *m.level->minimum || m.level->value.Number() > *m.level->maximum)
        return false;
    // The write scope is stereo monitoring, with no calibrated
    // high-headroom mode. Other configurations retain read-only telemetry.
    const auto device = m.path.substr(0, m.path.find('/', 9));
    return m.speakerSelection && m.speakerSelection->path == device + "/AltMonSelection/value" &&
           MonitorFieldAvailable(m, MonitorField::Speakers) &&
           m.highHeadroom && m.highHeadroom->path == device + "/Enable24dBMode/value" &&
           m.highHeadroom->value.kind == Json::Kind::Boolean && !m.highHeadroom->value.Bool() && m.dimAttenuation &&
           m.dimAttenuation->path == device + "/DimAttenuation/value" &&
           m.dimAttenuation->value.kind == Json::Kind::Number && std::isfinite(m.dimAttenuation->value.Number(NAN)) &&
           m.dimAttenuation->value.Number() >= 0 && m.dimAttenuation->value.Number() <= 60;
}
bool SameMonitorTarget(const Monitor &a, const Monitor &b)
{
    if (!MonitorEligible(a) || !MonitorEligible(b) || a.key != b.key || a.path != b.path || a.stereo != b.stereo ||
        a.mode != b.mode || !SameParameter(a.speakerSelection, b.speakerSelection, false) ||
        !SameParameter(a.highHeadroom, b.highHeadroom, true) ||
        !SameParameter(a.dimAttenuation, b.dimAttenuation, false) ||
        !SameParameter(a.sourceSelect, b.sourceSelect, false) ||
        a.talkbackMicPath != b.talkbackMicPath ||
        !SameParameter(a.talkbackMaster, b.talkbackMaster, true) ||
        !SameParameter(a.talkbackToMonitor, b.talkbackToMonitor, true) ||
        !SameParameter(a.talkbackMicSelect, b.talkbackMicSelect, true))
        return false;
    for (auto field : {MonitorField::Level, MonitorField::Mute, MonitorField::Dim, MonitorField::Mono,
                      MonitorField::DimAmount, MonitorField::Source, MonitorField::Talk,
                      MonitorField::TalkLevel, MonitorField::Speakers})
        if (!SameParameter(FieldParameter(a, field), FieldParameter(b, field), false) ||
            MonitorFieldAvailable(a, field) != MonitorFieldAvailable(b, field))
            return false;
    return true;
}
std::string MonitorCommand(const Monitor &m, MonitorField field, const Json &value, double ceiling)
{
    const auto &p = FieldParameter(m, field);
    if (!MonitorEligible(m) || !std::isfinite(ceiling) || ceiling < *m.level->minimum || ceiling > *m.level->maximum ||
        !MonitorFieldAvailable(m, field))
        throw std::invalid_argument("Monitor context or permission ceiling is unavailable");
    std::string scalar;
    if (field == MonitorField::Level)
    {
        const double n = value.Number(NAN);
        if (value.kind != Json::Kind::Number || !std::isfinite(n) || n < *p->minimum || n > ceiling)
            throw std::invalid_argument("Monitor level exceeds permitted range");
        scalar = ControlNumber(n).scalar;
    }
    else if (field == MonitorField::TalkLevel || field == MonitorField::Speakers)
    {
        const auto n = value.Number(NAN);
        if (value.kind != Json::Kind::Number || !std::isfinite(n) || n < *p->minimum || n > *p->maximum ||
            (field == MonitorField::Speakers && std::floor(n) != n))
            throw std::invalid_argument("Unsupported monitor parameter value");
        scalar = ControlNumber(n).scalar;
    }
    else if (field == MonitorField::DimAmount)
    {
        const double n = value.Number(NAN);
        const auto &table = MonitorDimTable();
        if (value.kind != Json::Kind::Number || std::find(table.begin(), table.end(), -n) == table.end())
            throw std::invalid_argument("Unsupported discrete dim attenuation");
        scalar = ControlNumber(n).scalar;
    }
    else if (field == MonitorField::Source)
    {
        const auto sources = MonitorSources(m);
        if (value.kind != Json::Kind::String || std::find(sources.begin(), sources.end(), value.String()) == sources.end())
            throw std::invalid_argument("Unsupported monitor source");
        // The fixed token vocabulary above excludes quotes, spaces and control bytes.
        scalar = "\"" + value.String() + "\"";
    }
    else
    {
        if (value.kind != Json::Kind::Boolean || p->value.kind != Json::Kind::Boolean)
            throw std::invalid_argument("Expected Boolean monitor value");
        scalar = value.Bool() ? "true" : "false";
    }
    return "set " + p->path + " " + scalar + '\0';
}
Json ConstrainMonitorValue(const Monitor &m, MonitorField field, const Json &value, double ceiling)
{
    Json constrained = value;
    if (field == MonitorField::Level)
    {
        if (!MonitorEligible(m) || value.kind != Json::Kind::Number || !std::isfinite(value.Number(NAN)) ||
            value.Number() < *m.level->minimum || value.Number() > *m.level->maximum)
            throw std::invalid_argument("Invalid monitor level");
        constrained = ControlNumber(std::min(value.Number(), ceiling));
    }
    MonitorCommand(m, field, constrained, ceiling);
    return constrained;
}
bool SameMonitorValue(MonitorField field, const Json &a, const Json &b)
{
    if (field == MonitorField::Source)
        return a.kind == Json::Kind::String && b.kind == Json::Kind::String && a.scalar == b.scalar;
    return SameControlValue(field == MonitorField::Level || field == MonitorField::DimAmount ||
                            field == MonitorField::TalkLevel || field == MonitorField::Speakers
                                ? ChannelField::Level : ChannelField::Mute, a, b);
}
uint64_t MonitorQueue::Arm(const Snapshot &s, const std::string &key, std::optional<double> ceiling)
{
    Disarm();
    if (!FreshMonitorState(s) || s.monitors.size() != 1 || s.monitors.front().key != key ||
        !MonitorEligible(s.monitors.front()))
        throw std::runtime_error("One fresh, supported stereo main monitor is required");
    target_ = s.monitors.front();
    // The diagnostic entry point retains its original arm-time ceiling. The
    // desktop may explicitly choose a ceiling, never above native unity (0 dB).
    const double chosen = ceiling.value_or(target_.level->value.Number());
    if (!std::isfinite(chosen) || chosen < *target_.level->minimum ||
        chosen > std::min(0.0, *target_.level->maximum))
        throw std::invalid_argument("Monitor ceiling is outside the supported range");
    ceiling_ = chosen;
    generation_ = s.generation;
    armed_ = true;
    return epoch_;
}
void MonitorQueue::Disarm()
{
    armed_ = false;
    ++epoch_;
    pending_.clear();
}
bool MonitorQueue::Valid(const Snapshot &s) const
{
    return armed_ && FreshMonitorState(s) && s.generation == generation_ && s.monitors.size() == 1 &&
           SameMonitorTarget(target_, s.monitors.front());
}
uint64_t MonitorQueue::Submit(MonitorField field, const Json &value, uint64_t epoch)
{
    if (!armed_ || !epoch || epoch != epoch_)
        return 0;
    const auto constrained = ConstrainMonitorValue(target_, field, value, ceiling_);
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const auto &r) { return r.field == field; }),
                   pending_.end());
    pending_.push_back(
        {target_, field, constrained, ceiling_, epoch_, generation_, ++sequence_, std::chrono::steady_clock::now()});
    return sequence_;
}
std::optional<MonitorRequest> MonitorQueue::Take()
{
    return TakeReady([](const MonitorRequest &) { return true; });
}
std::optional<MonitorRequest> MonitorQueue::TakeReady(
    const std::function<bool(const MonitorRequest &)> &ready)
{
    if (!armed_ || pending_.empty())
        return {};
    const auto found = std::find_if(pending_.begin(), pending_.end(), ready);
    if (found == pending_.end())
        return {};
    auto r = std::move(*found);
    pending_.erase(found);
    // The writer owns the final freshness check so it can discard one stale
    // gesture without revoking the Control Room permission.
    return r;
}
} // namespace apollo
