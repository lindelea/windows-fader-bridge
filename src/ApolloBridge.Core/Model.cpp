#include "Model.h"
#include "ConfigLayout.h"
#include "Configuration.h"
#include "Protocol.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace apollo
{
namespace
{
const Json &Node(const NodeMap &nodes, const std::string &path)
{
    static const Json empty;
    const auto it = nodes.find(path);
    return it == nodes.end() ? empty : it->second;
}
std::optional<double> Numeric(const Json &value)
{
    const double n = value.Number(std::numeric_limits<double>::quiet_NaN());
    return std::isfinite(n) ? std::optional<double>(n) : std::nullopt;
}
bool VisibleInput(const Json &node)
{
    // Active=false also represents the subordinate side of a linked pair.
    return Property(node, "Active").At("value").Bool() &&
           Property(node, "EnabledByUser").At("value").Bool(true) &&
           !Property(node, "ChannelHidden").At("value").Bool();
}
std::string EffectInstance(const Json &node)
{
    const auto &p = Property(node, "EffectInstance");
    const auto &v = p.At("value");
    if (p.At("type").String() != "pointer" || v.kind != Json::Kind::Number || v.scalar.empty() ||
        v.scalar.size() > 20 || v.scalar.find_first_not_of("0123456789") != v.scalar.npos)
        return {};
    return v.scalar;
}
std::vector<Meter> Meters(const NodeMap &nodes, const std::string &path, size_t count)
{
    std::vector<Meter> result;
    for (size_t index = 0; index < count; ++index)
    {
        // Preserve leg identity even during partial discovery. A missing left
        // feed must never shift the right feed into the left speaker's slot.
        const auto meterPath = path + "/meters/" + std::to_string(index);
        Meter m;
        if (const auto level = ReadParameter(nodes, meterPath, "MeterLevel"))
            m.levelDb = Numeric(level->value);
        if (const auto peak = ReadParameter(nodes, meterPath, "MeterPeakLevel"))
            m.peakDb = Numeric(peak->value);
        if (const auto clip = ReadParameter(nodes, meterPath, "MeterClip");
            clip && clip->value.kind == Json::Kind::Boolean)
            m.clip = clip->value.Bool();
        result.push_back(m);
    }
    return result;
}
} // namespace
bool NumericSlot(const std::string &slot)
{
    return !slot.empty() && slot.size() <= 8 && slot.find_first_not_of("0123456789") == slot.npos;
}
const Json &Property(const Json &node, std::string_view key)
{
    return node.At("properties").At(key);
}
bool DeviceOnline(const Json &node)
{
    return Property(node, "DeviceOnline").At("value").Bool();
}
std::vector<std::string> Children(const Json &node)
{
    std::vector<std::string> result;
    for (const auto &entry : node.At("children").object)
    {
        // Do not turn an engine-supplied child name into an injected command.
        if (entry.first.find('/') == std::string::npos && IsPath("/" + entry.first))
            result.push_back(entry.first);
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        const auto digits = [](const auto &s) {
            return !s.empty() && s.find_first_not_of("0123456789") == s.npos;
        };
        if (digits(a) && digits(b) && a.size() != b.size())
            return a.size() < b.size();
        return a < b;
    });
    return result;
}
std::string StableToken(std::string_view identity)
{
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : identity)
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return std::to_string(hash);
}
std::optional<Parameter> ReadParameter(const NodeMap &nodes, const std::string &path,
                                       const std::string &property)
{
    const auto &p = Property(Node(nodes, path), property);
    if (p.kind != Json::Kind::Object || !p.Has("value"))
        return {};
    const auto &value = p.At("value");
    const auto type = p.At("type").String();
    if (type == "bool"                       ? value.kind != Json::Kind::Boolean
        : (type == "float" || type == "int") ? !Numeric(value).has_value()
        : type == "string"                   ? value.kind != Json::Kind::String
                                             : true)
        return {};
    Parameter parameter{(path == "/" ? "" : path) + "/" + property + "/value",
                        value,
                        Numeric(p.At("min")),
                        Numeric(p.At("max")),
                        p.At("enabled").Bool(true),
                        p.At("readonly").Bool()};
    if (value.kind == Json::Kind::String)
    {
        for (const auto &choice : p.At("values").array)
        {
            const auto &v = choice.kind == Json::Kind::Object ? choice.At("value") : choice;
            if (v.kind == Json::Kind::String &&
                (choice.kind != Json::Kind::Object || choice.At("enabled").Bool(true)))
            {
                const auto text = v.String();
                if (std::find(parameter.choices.begin(), parameter.choices.end(), text) ==
                    parameter.choices.end())
                    parameter.choices.push_back(text);
            }
        }
    }
    if (parameter.minimum && parameter.maximum && *parameter.minimum > *parameter.maximum)
        return {};
    if (value.kind == Json::Kind::Number && ((parameter.minimum && value.Number() < *parameter.minimum) ||
                                             (parameter.maximum && value.Number() > *parameter.maximum)))
        return {};
    return parameter;
}
uint32_t ChannelColor(const Channel &c)
{
    if (c.auxiliary)
        return 0x8000FF;
    if (c.ioType == "TalkbackMic")
        return 0xFFFF00;
    if (!c.preamps.empty())
        return 0x00FF00;
    if (c.ioType == "Line")
        return 0xFF8000;
    if (c.ioType == "S/PDIF")
        return 0x0000FF;
    if (c.ioType == "ADAT")
        return 0x00FFFF;
    if (c.ioType == "Virtual")
        return 0xFF0000;
    return 0x9AA5B5;
}
namespace
{
AudioFormat DestinationFormat(const NodeMap &nodes, const std::string &device, const std::string &route)
{
    std::map<std::string, size_t> legs;
    size_t monitorCount = 0;
    AudioFormat monitor = AudioFormat::Unknown;
    for (const auto &slot : Children(Node(nodes, device + "/outputs")))
    {
        const auto &output = Node(nodes, device + "/outputs/" + slot);
        const auto type = Property(output, "IOType").At("value").String();
        const auto &stereo = Property(output, "Stereo").At("value");
        if (route == "Monitor" && type == "Monitor" && stereo.kind == Json::Kind::Boolean)
        {
            ++monitorCount;
            monitor = stereo.Bool() ? AudioFormat::Stereo : AudioFormat::Unknown;
        }
        if (!Property(output, "Active").At("value").Bool() || stereo.kind != Json::Kind::Boolean ||
            stereo.Bool())
            continue;
        const auto name = Property(output, "Name").At("value").String();
        if ((type == "Line" && name.rfind("ANALOG ", 0) == 0) ||
            (type == "ADAT" && name.rfind("ADAT ", 0) == 0) ||
            (type == "S/PDIF" && (name == "S/PDIF L" || name == "S/PDIF R")))
            ++legs[name];
    }
    if (route == "Monitor")
        return monitorCount == 1 ? monitor : AudioFormat::Unknown;
    if (route == "S/PDIF" && legs["S/PDIF L"] == 1 && legs["S/PDIF R"] == 1)
        return AudioFormat::Stereo;
    for (const std::string family : {"Line", "ADAT"})
    {
        const std::string physical = family == "Line" ? "ANALOG " : "ADAT ";
        for (int n = 1; n <= 32; ++n)
        {
            if (legs[physical + std::to_string(n)] != 1)
                continue;
            if (route == family + " " + std::to_string(n))
                return AudioFormat::Mono;
            if (n % 2 && route == family + " " + std::to_string(n) + "-" + std::to_string(n + 1) &&
                legs[physical + std::to_string(n + 1)] == 1)
                return AudioFormat::Stereo;
        }
    }
    return AudioFormat::Unknown;
}
} // namespace
Snapshot BuildSnapshot(const NodeMap &nodes)
{
    Snapshot result;
    result.globalConfig = ReadGlobalConfig(Node(nodes, "/"));
    std::set<std::string> identities;
    for (const auto &device : Children(Node(nodes, "/devices")))
    {
        const auto path = "/devices/" + device;
        const auto &data = Node(nodes, path);
        if (!DeviceOnline(data))
        {
            ++result.offlineDevices;
            continue;
        }
        const auto &hardwareId = Property(data, "DeviceHwID").At("value");
        if ((hardwareId.kind != Json::Kind::Number && hardwareId.kind != Json::Kind::String) ||
            hardwareId.scalar.empty() || hardwareId.scalar == "0")
            throw std::runtime_error("Online Apollo has no stable hardware identity");
        const auto identity = StableToken(hardwareId.scalar);
        if (!identities.insert(identity).second)
            throw std::runtime_error("Duplicate Apollo hardware identity");
        ++result.onlineDevices;
        const auto name = Property(data, "DeviceName").At("value").String("Apollo");
        for (const std::string group : {"inputs", "auxs"})
        {
            const auto groupPath = path + "/" + group;
            for (const auto &child : Children(Node(nodes, groupPath)))
            {
                const auto channelPath = groupPath + "/" + child;
                const auto &channel = Node(nodes, channelPath);
                if (!VisibleInput(channel))
                {
                    ++result.skippedChannels;
                    continue;
                }
                Channel c;
                c.path = channelPath;
                c.key = "apollo." + identity + "." + group + "." + child;
                c.deviceName = name;
                c.ioType = Property(channel, "IOType").At("value").String();
                c.destination = Property(channel, "OutputDestination").At("value").String();
                c.auxiliary = group == "auxs";
                c.stereo = c.auxiliary || Property(channel, "Stereo").At("value").Bool();
                c.outputFormat = DestinationFormat(nodes, path, c.auxiliary ? "Monitor" : c.destination);
                c.name =
                    Property(channel, c.stereo && !c.auxiliary ? "StereoName" : "Name").At("value").String();
                if (c.name.empty())
                    c.name = Property(channel, "Name").At("value").String("Channel " + child);
                c.level = ReadParameter(nodes, channelPath, "FaderLevel");
                c.mute = ReadParameter(nodes, channelPath, "Mute");
                c.solo = ReadParameter(nodes, channelPath, "Solo");
                c.pan = ReadParameter(nodes, channelPath, "Pan");
                c.output = ReadParameter(nodes, channelPath, "OutputDestination");
                c.input = ReadParameter(nodes, channelPath, "IOType");
                if (c.auxiliary)
                {
                    c.sendPostFader = ReadParameter(nodes, channelPath, "SendPostFader");
                    c.mono = ReadParameter(nodes, channelPath, "MixToMono");
                }
                else
                    c.recordPreEffects = ReadParameter(nodes, channelPath, "RecordPreEffects");
                if (c.ioType == "S/PDIF")
                    c.sampleRateConvert = ReadParameter(nodes, channelPath, "SRConvert");
                const auto globalMaster = ReadParameter(nodes, "/", "TalkbackMaster");
                const auto deviceMaster = ReadParameter(nodes, path, "TalkbackMaster");
                if (c.ioType == "TalkbackMic" && globalMaster && deviceMaster &&
                    globalMaster->value.kind == Json::Kind::Number && globalMaster->value.scalar == device &&
                    deviceMaster->value.kind == Json::Kind::Number && deviceMaster->value.scalar == device &&
                    Property(channel, "TalkbackEnabled").At("value").Bool())
                {
                    size_t mics = 0;
                    for (const auto &candidate : Children(Node(nodes, groupPath)))
                        if (Property(Node(nodes, groupPath + "/" + candidate), "IOType")
                                .At("value")
                                .String() == "TalkbackMic")
                            ++mics;
                    const auto select = ReadParameter(nodes, "/", "TalkbackMicSelect");
                    if (mics == 1 && select && select->value.kind == Json::Kind::Number &&
                        select->value.Number(NAN) == 0)
                    {
                        c.talkContext = device + ":" + select->value.scalar;
                        c.talk = ReadParameter(nodes, "/", "TalkbackOn");
                        c.talkToMonitor = ReadParameter(nodes, "/", "TalkbackInPhysicalCR");
                    }
                }
                for (const auto &slot : Children(Node(nodes, channelPath + "/sends")))
                {
                    if (!NumericSlot(slot) || c.sends.size() >= 16)
                        continue;
                    Send send;
                    send.slot = slot;
                    send.path = channelPath + "/sends/" + slot;
                    const auto &sendData = Node(nodes, send.path);
                    if (Property(sendData, "Type").At("value").String() != "send")
                        continue;
                    send.id = Property(sendData, "ID").At("value").String();
                    send.name = Property(sendData, "Name").At("value").String();
                    if (send.id.empty() || send.name.empty())
                        continue;
                    send.level = ReadParameter(nodes, send.path, "Gain");
                    send.bypass = ReadParameter(nodes, send.path, "Bypass");
                    // UA explicitly hides send pan on linked stereo inputs.
                    if (!c.stereo)
                        send.pan = ReadParameter(nodes, send.path, "Pan");
                    c.sends.push_back(std::move(send));
                }
                for (const auto &slot : Children(Node(nodes, channelPath + "/preamps")))
                {
                    if (!NumericSlot(slot) || c.preamps.size() >= 2)
                        continue;
                    Preamp preamp;
                    preamp.slot = slot;
                    preamp.path = channelPath + "/preamps/" + slot;
                    const auto &preampData = Node(nodes, preamp.path);
                    if (Property(preampData, "Type").At("value").String() != "preamp")
                        continue;
                    bool unison = false;
                    bool unisonKnown = !preampData.At("children").Has("effects") ||
                                       nodes.count(preamp.path + "/effects") != 0;
                    for (const auto &effect : Children(Node(nodes, preamp.path + "/effects")))
                    {
                        const auto &fx = Node(nodes, preamp.path + "/effects/" + effect);
                        const auto instance = EffectInstance(fx);
                        unisonKnown &= !instance.empty();
                        preamp.context += effect + ":" + instance + ":" +
                                          Property(fx, "EffectName").At("value").String() + ";";
                        unison |= !instance.empty() && instance != "0";
                        if (NumericSlot(effect) && Property(fx, "Type").At("value").String() == "effect" &&
                            !instance.empty() && instance != "0")
                        {
                            Insert insert;
                            insert.slot = effect;
                            insert.path = preamp.path + "/effects/" + effect;
                            insert.name = Property(fx, "EffectName").At("value").String();
                            insert.identity = StableToken(instance + ":" + insert.name);
                            insert.power = ReadParameter(nodes, insert.path, "Power");
                            for (const auto &parameterSlot :
                                 Children(Node(nodes, insert.path + "/parameters")))
                            {
                                if (!NumericSlot(parameterSlot) || insert.parameters.size() >= 256)
                                    continue;
                                const auto parameterPath = insert.path + "/parameters/" + parameterSlot;
                                const auto &p = Node(nodes, parameterPath);
                                if (Property(p, "Type").At("value").String() != "parameter")
                                    continue;
                                InsertParameter parameter;
                                parameter.slot = parameterSlot;
                                parameter.name = Property(p, "Name").At("value").String();
                                parameter.display = Property(p, "StringValue").At("value").String();
                                parameter.normalized = ReadParameter(nodes, parameterPath, "NormalizedValue");
                                parameter.step = ReadParameter(nodes, parameterPath, "StepValue");
                                if (!parameter.name.empty())
                                    insert.parameters.push_back(std::move(parameter));
                            }
                            if (!insert.name.empty())
                                preamp.unison.push_back(std::move(insert));
                        }
                    }
                    const bool bypass =
                        c.ioType == "Line" && Property(channel, "PGADisabledInLineMode").At("value").Bool();
                    if (bypass)
                        for (auto &insert : preamp.unison)
                        {
                            if (insert.power)
                                insert.power->enabled = false;
                            for (auto &parameter : insert.parameters)
                            {
                                if (parameter.normalized)
                                    parameter.normalized->enabled = false;
                                if (parameter.step)
                                    parameter.step->enabled = false;
                            }
                        }
                    const bool hiZ = Property(preampData, "HiZ").At("value").Bool();
                    const bool phantom = Property(preampData, "48V").At("value").Bool();
                    preamp.context += (bypass ? "bypass" : "active") + std::string(hiZ ? ":hiZ" : ":rear");
                    preamp.context += phantom ? ":phantom" : ":no-phantom";
                    if (hiZ || phantom)
                        c.input.reset();
                    // Native gain has device-specific steps and Unison coupling.
                    // Use only the plain native gain case; never invent a Unison scale.
                    if (unisonKnown && !unison && !bypass && (c.ioType == "Mic" || c.ioType == "Line"))
                    {
                        preamp.gain = ReadParameter(nodes, preamp.path, "Gain");
                        if (preamp.gain && preamp.gain->minimum && preamp.gain->maximum)
                        {
                            preamp.gain->minimum = std::ceil(std::max(10.0, *preamp.gain->minimum));
                            preamp.gain->maximum = std::floor(std::min(65.0, *preamp.gain->maximum));
                            if (*preamp.gain->minimum >= *preamp.gain->maximum ||
                                preamp.gain->value.Number() < *preamp.gain->minimum ||
                                preamp.gain->value.Number() > *preamp.gain->maximum)
                                preamp.gain.reset();
                        }
                    }
                    preamp.lowCut = ReadParameter(nodes, preamp.path, "LowCut");
                    preamp.phase = ReadParameter(nodes, preamp.path, "Phase");
                    if (c.ioType == "Mic" && !hiZ)
                    {
                        preamp.pad = ReadParameter(nodes, preamp.path, "Pad");
                        preamp.phantom = ReadParameter(nodes, preamp.path, "48V");
                    }
                    c.preamps.push_back(std::move(preamp));
                }
                if (c.preamps.empty())
                {
                    c.input.reset();
                    if (c.ioType == "Line")
                        c.reference = ReadParameter(nodes, channelPath, "Pad");
                }
                for (const auto &slot : Children(Node(nodes, channelPath + "/effects")))
                {
                    if (!NumericSlot(slot) || c.inserts.size() >= 16)
                        continue;
                    c.insertSlots.push_back(slot);
                    Insert insert;
                    insert.slot = slot;
                    insert.path = channelPath + "/effects/" + slot;
                    const auto &insertData = Node(nodes, insert.path);
                    const auto instance = EffectInstance(insertData);
                    insert.name = Property(insertData, "EffectName").At("value").String();
                    if (Property(insertData, "Type").At("value").String() != "effect" || instance.empty() ||
                        instance == "0" || insert.name.empty())
                        continue;
                    insert.identity = StableToken(instance + ":" + insert.name);
                    insert.power = ReadParameter(nodes, insert.path, "Power");
                    for (const auto &parameterSlot : Children(Node(nodes, insert.path + "/parameters")))
                    {
                        if (!NumericSlot(parameterSlot) || insert.parameters.size() >= 256)
                            continue;
                        const auto parameterPath = insert.path + "/parameters/" + parameterSlot;
                        const auto &p = Node(nodes, parameterPath);
                        if (Property(p, "Type").At("value").String() != "parameter")
                            continue;
                        InsertParameter parameter;
                        parameter.slot = parameterSlot;
                        parameter.name = Property(p, "Name").At("value").String();
                        parameter.display = Property(p, "StringValue").At("value").String();
                        parameter.normalized = ReadParameter(nodes, parameterPath, "NormalizedValue");
                        parameter.step = ReadParameter(nodes, parameterPath, "StepValue");
                        if (!parameter.name.empty())
                            insert.parameters.push_back(std::move(parameter));
                    }
                    c.inserts.push_back(std::move(insert));
                }
                if (c.stereo)
                    c.panRight = ReadParameter(nodes, channelPath, "Pan2");
                c.meters = Meters(nodes, channelPath, c.stereo ? 2 : 1);
                result.channels.push_back(std::move(c));
            }
        }
        for (const auto &child : Children(Node(nodes, path + "/outputs")))
        {
            const auto outputPath = path + "/outputs/" + child;
            const auto &output = Node(nodes, outputPath);
            // The monitor output can report Active=false in a working stereo
            // system. Input Active and monitor availability are distinct concepts.
            if (Property(output, "IOType").At("value").String() != "Monitor")
                continue;
            Monitor m;
            m.key = "apollo." + identity + ".monitor." + child;
            m.path = outputPath;
            m.deviceName = name;
            m.name = Property(output, "Name").At("value").String("MONITOR");
            m.source = Property(output, "MixInSource").At("value").String();
            m.sourceSelect = ReadParameter(nodes, outputPath, "MixInSource");
            m.stereo = Property(output, "Stereo").At("value").Bool();
            m.mode = Property(data, "SurroundMonitorMode").At("value").String();
            m.speakerSelection = ReadParameter(nodes, path, "AltMonSelection");
            m.dimAttenuation = ReadParameter(nodes, path, "DimAttenuation");
            m.highHeadroom = ReadParameter(nodes, path, "Enable24dBMode");
            m.talkbackMaster = ReadParameter(nodes, "/", "TalkbackMaster");
            m.talkbackToMonitor = ReadParameter(nodes, "/", "TalkbackInPhysicalCR");
            m.talkbackMicSelect = ReadParameter(nodes, "/", "TalkbackMicSelect");
            if (m.talkbackMaster && m.talkbackMaster->value.kind == Json::Kind::Number &&
                m.talkbackMaster->value.scalar == device &&
                Property(data, "TalkbackMaster").At("value").scalar == device)
            {
                for (const auto &input : Children(Node(nodes, path + "/inputs")))
                {
                    const auto mic = path + "/inputs/" + input;
                    if (Property(Node(nodes, mic), "IOType").At("value").String() == "TalkbackMic")
                    {
                        if (!m.talkbackMicPath.empty())
                        {
                            m.talkbackMicPath.clear();
                            break;
                        }
                        m.talkbackMicPath = mic;
                    }
                }
                if (!m.talkbackMicPath.empty())
                    m.talk = ReadParameter(nodes, "/", "TalkbackOn");
            }
            m.level = ReadParameter(nodes, outputPath, "CRMonitorLevel");
            if (!m.level)
                continue;
            m.mute = ReadParameter(nodes, outputPath, "Mute");
            m.dim = ReadParameter(nodes, outputPath, "DimOn");
            m.mono = ReadParameter(nodes, outputPath, "MixToMono");
            m.meters = Meters(nodes, outputPath, 16);
            result.monitors.push_back(std::move(m));
        }
    }
    result.configuration = BuildConfiguration(nodes);
    for (auto &c : result.channels) c.configuration = result.configuration;
    for (auto &m : result.monitors) m.configuration = result.configuration;
    return result;
}
std::vector<Channel> SurfaceChannels(const Snapshot &snapshot)
{
    auto channels = snapshot.channels;
    channels.erase(std::remove_if(channels.begin(), channels.end(), [](const auto &c) { return c.monitor; }),
                   channels.end());
    return channels;
}
std::optional<double> MeterMaximum(const std::vector<Meter> &meters, bool peakHold)
{
    std::optional<double> maximum;
    for (const auto &meter : meters)
    {
        const auto value = peakHold ? meter.peakDb : meter.levelDb;
        if (value && std::isfinite(*value))
            maximum = maximum ? std::max(*maximum, *value) : value;
    }
    return maximum;
}
bool ApplyValue(NodeMap &nodes, const Json &response)
{
    const auto path = response.At("path").String();
    if (!IsPath(path) || path.size() < 7 || path.substr(path.size() - 6) != "/value" ||
        !response.Has("data") || response.Has("error"))
        return false;
    const auto propertyEnd = path.size() - 6;
    const auto slash = path.rfind('/', propertyEnd - 1);
    if (slash == path.npos)
        return false;
    const auto it = nodes.find(slash == 0 ? "/" : path.substr(0, slash));
    if (it == nodes.end())
        return false;
    auto &properties = it->second.object["properties"].object;
    const auto p = properties.find(path.substr(slash + 1, propertyEnd - slash - 1));
    if (p == properties.end() || !p->second.Has("value"))
        return false;
    if (p->second.At("value").kind != response.At("data").kind)
        return false;
    const bool changed = p->second.At("value").scalar != response.At("data").scalar;
    const bool replaced = changed && (p->first == "EffectInstance" || p->first == "EffectName");
    p->second.object["value"] = response.At("data");
    if (changed && p->first == "NormalizedValue")
    {
        const auto display = properties.find("StringValue");
        if (display != properties.end() && display->second.At("value").kind == Json::Kind::String)
            display->second.object["value"].scalar.clear();
    }
    if (replaced)
    {
        // A new plug-in must never inherit the old instance's parameter map,
        // even during the short gap before the next complete discovery.
        const auto prefix = it->first + "/parameters";
        for (auto descendant = nodes.lower_bound(prefix);
             descendant != nodes.end() &&
             (descendant->first == prefix || descendant->first.rfind(prefix + "/", 0) == 0);)
            descendant = nodes.erase(descendant);
    }
    return true;
}
std::vector<std::string> SubscriptionPaths(const NodeMap &nodes)
{
    static const std::set<std::string> allowed = {"DeviceOnline",
                                                  "DeviceName",
                                                  "Name",
                                                  "StereoName",
                                                  "Active",
                                                  "EnabledByUser",
                                                  "ChannelHidden",
                                                  "Stereo",
                                                  "FaderLevel",
                                                  "Pan",
                                                  "Pan2",
                                                  "Mute",
                                                  "Solo",
                                                  "CRMonitorLevel",
                                                  "DimOn",
                                                  "MixToMono",
                                                  "MeterLevel",
                                                  "MeterPeakLevel",
                                                  "MeterClip",
                                                  "OutputDestination",
                                                  "MixInSource",
                                                  "IOType",
                                                  "SurroundMonitorMode",
                                                  "AltMonSelection",
                                                  "DimAttenuation",
                                                  "Enable24dBMode",
                                                  "TalkbackOn",
                                                  "TalkbackMaster",
                                                  "TalkbackInPhysicalCR",
                                                  "TalkbackMicSelect",
                                                  "Gain",
                                                  "Bypass",
                                                  "ID",
                                                  "LowCut",
                                                  "48V",
                                                  "Phase",
                                                  "Pad",
                                                  "PGADisabledInLineMode",
                                                  "EffectInstance",
                                                  "EffectName",
                                                  "Preset",
                                                  "FuncSwitchMode",
                                                  "MirrorsToDigital",
                                                  "Power",
                                                  "NormalizedValue",
                                                  "StringValue",
                                                  "StepValue",
                                                  "RecordPreEffects",
                                                  "SendPostFader",
                                                  "SRConvert",
                                                  "HiZ",
                                                  "TalkbackEnabled"};
    std::vector<std::string> paths;
    for (const auto &node : nodes)
        for (const auto &property : node.second.At("properties").object)
            if ((allowed.count(property.first) ||
                 (node.first == "/" && std::any_of(GlobalConfigEntries.begin(), GlobalConfigEntries.end(),
                     [&](const auto &entry) { return property.first == entry.property; }))) &&
                property.second.Has("value"))
                paths.push_back((node.first == "/" ? "" : node.first) + "/" + property.first + "/value");
    if (paths.size() > 32768)
        throw std::runtime_error("Apollo subscription limit exceeded");
    return paths;
}
} // namespace apollo
