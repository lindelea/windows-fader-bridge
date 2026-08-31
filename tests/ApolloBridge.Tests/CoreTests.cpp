#include "../../src/ApolloBridge.EuconHost/UpperDirectoryLayout.h"
#include "ChannelControl.h"
#include "ChannelFeatureFixture.h"
#include "ChannelLayout.h"
#include "FaderScale.h"
#include "Json.h"
#include "Model.h"
#include "MonitorControl.h"
#include "MonitorLayout.h"
#include "Protocol.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <set>
#include <stdexcept>

using namespace apollo;
namespace
{
size_t checks = 0;
void Check(bool result, const char *message)
{
    ++checks;
    if (!result)
        throw std::runtime_error(message);
}
template <class F> void Reject(F f)
{
    bool rejected = false;
    try
    {
        f();
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    Check(rejected, "Expected rejection");
}
NodeMap Fixture()
{
    // Entirely synthetic fixtures. Never use a captured device tree in tests.
    NodeMap n;
    n["/devices"] = Json::Parse(R"({"children":{"7":{},"3":{}}})");
    n["/devices/3"] = Json::Parse(
        R"({"properties":{"DeviceOnline":{"type":"bool","value":true},"DeviceHwID":{"type":"int64","value":9223372036854775806},"DeviceName":{"type":"string","value":"Test interface"}}})");
    n["/devices/7"] = Json::Parse(R"({"properties":{"DeviceOnline":{"type":"bool","value":false}}})");
    n["/devices/3/inputs"] = Json::Parse(R"({"children":{"0":{},"1":{},"2":{},"3":{},"10":{}}})");
    n["/devices/3/inputs/0"] = Json::Parse(
        R"({"properties":{"Active":{"type":"bool","value":true},"Name":{"type":"string","value":"Input A"},"FaderLevel":{"type":"float","min":-144,"max":12,"value":6},"Pan":{"type":"float","min":-1,"max":1,"value":0.25},"Mute":{"type":"bool","value":false},"Solo":{"type":"bool","value":true}}})");
    n["/devices/3/inputs/1"] = Json::Parse(R"({"properties":{"Active":{"type":"bool","value":false}}})");
    n["/devices/3/inputs/2"] = Json::Parse(
        R"({"properties":{"Active":{"type":"bool","value":true},"ChannelHidden":{"type":"bool","value":true}}})");
    n["/devices/3/inputs/3"] = Json::Parse(
        R"({"properties":{"Active":{"type":"bool","value":true},"EnabledByUser":{"type":"bool","value":false}}})");
    n["/devices/3/inputs/10"] = Json::Parse(
        R"({"properties":{"Active":{"type":"bool","value":true},"Stereo":{"type":"bool","value":true},"StereoName":{"type":"string","value":"Stereo pair"},"Pan":{"type":"float","min":-1,"max":1,"value":-1},"Pan2":{"type":"float","min":-1,"max":1,"value":1}}})");
    n["/devices/3/outputs"] = Json::Parse(R"({"children":{"22":{},"4":{}}})");
    n["/devices/3/outputs/4"] = Json::Parse(R"({"properties":{"IOType":{"type":"string","value":"Line"}}})");
    n["/devices/3/outputs/22"] = Json::Parse(
        R"({"properties":{"IOType":{"type":"string","value":"Monitor"},"Active":{"type":"bool","value":false},"CRMonitorLevel":{"type":"float","min":-96,"max":0,"value":-30},"DimOn":{"type":"bool","value":true}}})");
    n["/devices/3/auxs"] = Json::Parse(R"({"children":{"0":{}}})");
    n["/devices/3/auxs/0"] = Json::Parse(
        R"({"properties":{"Active":{"type":"bool","value":true},"Name":{"type":"string","value":"Aux test"}}})");
    return n;
}
void JsonTests()
{
    Check(Json::Parse("9223372036854775807").scalar == "9223372036854775807", "int64 identity preserved");
    Check(Json::Parse(R"("\u4e2d\u6587 \ud83c\udfb5")").String() == u8"中文 🎵", "Unicode escapes");
    Check(Json::Parse(u8"\"中文 🎵\"").String() == u8"中文 🎵", "UTF8 strings");
    Check(Json::Parse(" -2.3e-5 \r\n").Number() == -2.3e-5, "Exponent");
    for (const auto *invalid :
         {"", "{}{}", "[1,]", "{\"a\":1,\"a\":2}", "01", "1.", "--1", "1e", "NaN", "1e309"})
        Reject([&] { Json::Parse(invalid); });
    Reject([] { Json::Parse(R"("\ud800")"); });
    Reject([] { Json::Parse(R"("\udc00")"); });
    Reject([] { Json::Parse("\"\xC0\x80\""); });
    Reject([] { Json::Parse("\"\xED\xA0\x80\""); });
    Reject([] { Json::Parse(std::string(70, '[') + "0" + std::string(70, ']')); });
    std::mt19937 random(104);
    for (int i = 0; i < 10000; ++i)
    {
        const auto value = random();
        Check(Json::Parse(std::to_string(value)).Number() == value, "Numeric roundtrip");
    }
}
void ProtocolTests()
{
    Check(ReadCommand("get", "/devices").back() == '\0', "NUL framing");
    for (const auto *verb : {"set", "Set", "get\nset", "unsubscribe", ""})
        Reject([&] { ReadCommand(verb, "/devices"); });
    for (const auto &path :
         {std::string("/devices/../0"), std::string("/devices 0"), std::string("//devices"),
          std::string("/x\nset /Sleep false"), std::string("/x\0/y", 5), std::string("relative")})
        Reject([&] { ReadCommand("get", path); });
    const std::string raw = std::string(u8"{\"name\":\"中文 🎵\"}") + '\0' + "{\"data\":true}" + '\0';
    for (size_t split = 0; split <= raw.size(); ++split)
    {
        FrameDecoder decoder;
        auto messages = decoder.Feed(std::string_view(raw).substr(0, split));
        const auto tail = decoder.Feed(std::string_view(raw).substr(split));
        messages.insert(messages.end(), tail.begin(), tail.end());
        Check(messages.size() == 2 && !decoder.Partial(), "Split/coalesced frame");
        Check(Json::Parse(messages[0]).At("name").String() == u8"中文 🎵", "Split UTF8");
    }
    FrameDecoder decoder;
    Reject([&] { decoder.Feed(std::string(FrameDecoder::MaxBytes + 1, 'x')); });
    decoder.Reset();
    Reject([&] { decoder.Feed(std::string(1, '\0')); });
    decoder.Reset();
    Check(decoder.Feed("{}").empty() && decoder.Partial(), "Partial EOF tracked");
}
void ModelTests()
{
    auto n = Fixture();
    auto s = BuildSnapshot(n);
    Check(s.onlineDevices == 1 && s.offlineDevices == 1, "Only online devices");
    Check(s.channels.size() == 3 && s.skippedChannels == 3, "Dense eligible channels");
    Check(s.channels[0].level->value.Number() == 6, "Native dB not Windows 0-1 cap");
    Check(s.channels[1].name == "Stereo pair" && s.channels[1].stereo, "Linked stereo name");
    Check(s.channels[1].pan->value.Number() == -1 && s.channels[1].panRight->value.Number() == 1,
          "Dual pan independent");
    Check(s.channels[2].auxiliary && !s.channels[2].solo, "Missing controls remain missing");
    Check(s.monitors.size() == 1 && s.monitors[0].path == "/devices/3/outputs/22",
          "Monitor discovered, not fixed index");
    Check(s.monitors[0].dim->value.Bool() && !s.monitors[0].mute, "Read-only capabilities");
    auto paths = SubscriptionPaths(n);
    for (const auto &path : paths)
        Check(IsPath(path) && path.find("DeviceHwID") == path.npos, "Allowlisted subscriptions");
    const auto stable = s.channels[0].key;
    Check(ApplyValue(n, Json::Parse(R"({"path":"/devices/3/inputs/0/Name/value","data":"New display"})")),
          "Known update accepted");
    Check(BuildSnapshot(n).channels[0].key == stable, "Rename keeps identity");
    Check(!ApplyValue(n, Json::Parse(R"({"path":"/devices/3/inputs/0/Solo/value","data":1})")),
          "Type change rejected");
    Check(!ApplyValue(n, Json::Parse(R"({"path":"/devices/3/inputs/0/Unknown/value","data":true})")),
          "Unknown update rejected");
    ApplyValue(n, Json::Parse(R"({"path":"/devices/3/inputs/0/FaderLevel/value","data":100})"));
    Check(!BuildSnapshot(n).channels[0].level, "Out-of-range telemetry not clipped to plausible value");
    ApplyValue(n, Json::Parse(R"({"path":"/devices/3/DeviceOnline/value","data":false})"));
    Check(BuildSnapshot(n).channels.empty() && BuildSnapshot(n).monitors.empty(), "Offline removes state");
    n = Fixture();
    n["/devices/7"] = n.at("/devices/3");
    Reject([&] { BuildSnapshot(n); });
    n = Fixture();
    n["/devices/3"].object["properties"].object.erase("DeviceHwID");
    Reject([&] { BuildSnapshot(n); });
    auto reordered = Fixture();
    NodeMap moved;
    for (const auto &entry : reordered)
    {
        std::string path = entry.first;
        if (path.find("/devices/3") == 0)
            path.replace(0, 10, "/devices/5");
        moved.emplace(path, entry.second);
    }
    moved["/devices"] = Json::Parse(R"({"children":{"5":{},"7":{}}})");
    Check(BuildSnapshot(moved).channels[0].key == stable, "Device reorder keeps identity");
    n = Fixture();
    n["/devices/3/inputs/0/meters"] = Json::Parse(R"({"children":{"0":{}}})");
    n["/devices/3/inputs/0/meters/0"] = Json::Parse(
        R"({"properties":{"MeterLevel":{"type":"float","min":-77,"max":0,"value":-18},"MeterPeakLevel":{"type":"float","min":-77,"max":0,"value":-12},"MeterClip":{"type":"bool","value":false}}})");
    Check(BuildSnapshot(n).channels[0].meters[0].levelDb == -18, "Meter dB metadata accepted");
    ApplyValue(n, Json::Parse(R"({"path":"/devices/3/inputs/0/meters/0/MeterLevel/value","data":50})"));
    Check(!BuildSnapshot(n).channels[0].meters[0].levelDb, "Out-of-range meter is unavailable");
    Check(
        !ApplyValue(n, Json::Parse(R"({"path":"/devices/3/inputs/0/Mute/value","error":true,"data":true})")),
        "Failed subscription is not a value");
    Check(!BuildSnapshot(n).channels[0].mute->value.Bool(), "Error retains no false update");
    ApplyValue(n, Json::Parse(R"({"path":"/devices/3/inputs/10/Stereo/value","data":false})"));
    Check(!BuildSnapshot(n).channels[1].panRight, "Unlink removes second pan");
}
void FaderTests()
{
    const auto table = FaderDbTable(-144, 12);
    Check(table.size() == 1025 && table.front() == -144 && table.back() == 12, "Fader endpoints");
    Check(table[832] == 0, "Exact unity point");
    for (size_t i = 1; i < table.size(); ++i)
        Check(table[i] > table[i - 1], "Strict float monotonicity");
    const auto monitor = FaderDbTable(-96, 0);
    Check(monitor.front() == -96 && monitor.back() == 0, "Monitor range retains endpoints");
    for (size_t i = 1; i < monitor.size(); ++i)
        Check(monitor[i] > monitor[i - 1], "Monitor strict monotonicity");
    Reject([] { FaderDbTable(0, 0); });
    Reject([] { FaderDbTable(-144, 100); });
    Reject([] { FaderDbTable(12, -96); });
}
void FeedbackTests()
{
    auto n = Fixture();
    auto &output = n["/devices/3/outputs/22"].object["properties"].object;
    output["Stereo"] = Json::Parse(R"({"type":"bool","value":true})");
    output["Name"] = Json::Parse(R"({"type":"string","value":"Main monitors"})");
    output["MixToMono"] = Json::Parse(R"({"type":"bool","value":true})");
    output["MixInSource"] = Json::Parse(R"({"type":"string","value":"mon"})");
    auto &mono = n["/devices/3/inputs/0"].object["properties"].object;
    mono["OutputDestination"] = Json::Parse(R"({"type":"string","value":"Monitor"})");
    n["/devices/3/outputs/22/meters/0"] = Json::Parse(
        R"({"properties":{"MeterLevel":{"type":"float","min":-77,"max":0,"value":-20},"MeterPeakLevel":{"type":"float","min":-77,"max":0,"value":-6},"MeterClip":{"type":"bool","value":false}}})");
    n["/devices/3/outputs/22/meters/1"] = Json::Parse(
        R"({"properties":{"MeterLevel":{"type":"float","min":-77,"max":0,"value":-18},"MeterPeakLevel":{"type":"float","min":-77,"max":0,"value":-12},"MeterClip":{"type":"bool","value":true}}})");
    n["/devices/3/outputs/22/meters/2"] =
        Json::Parse(R"({"properties":{"MeterLevel":{"type":"float","min":-77,"max":0,"value":0}}})");
    auto s = BuildSnapshot(n);
    auto c = SurfaceChannels(s);
    Check(c.size() == s.channels.size() &&
              std::none_of(c.begin(), c.end(), [](const auto &item) { return item.monitor; }),
          "Monitor never occupies a channel/fader slot");
    Check(c.front().key == s.channels.front().key && c.back().key == s.channels.back().key,
          "Channel identities retained");
    Check(s.monitors[0].name == "Main monitors" && s.monitors[0].path == "/devices/3/outputs/22",
          "Monitor is capability discovered");
    Check(!c.front().stereo && c.front().meters.size() == 1 && c.front().outputFormat == AudioFormat::Stereo,
          "Mono channel and stereo panner destination are independent");
    Check(c[1].outputFormat == AudioFormat::Unknown, "Unreported output format stays unknown");
    Check(s.monitors[0].stereo, "Mono summing retains stereo output format");
    Check(s.monitors[0].level->value.Number() == -30 && !s.monitors[0].mute,
          "Native monitor attenuation and missing mute");
    const std::vector<Meter> mainMeters(s.monitors[0].meters.begin(), s.monitors[0].meters.begin() + 2);
    Check(MeterMaximum(mainMeters, false) == -18, "Monitor meter stays pre-attenuation");
    Check(MeterMaximum(mainMeters, true) == -6, "Native peak remains -6 dB, no S3 compensation");
    Check(mainMeters[1].clip == true && mainMeters[0].clip == false, "Clip preserves leg identity");
    Check(s.monitors[0].source == "mon" && s.monitors[0].mono->value.Bool(),
          "Monitor source and mono are separate state");
    n.erase("/devices/3/outputs/22/meters/0");
    s = BuildSnapshot(n);
    Check(!s.monitors[0].meters[0].levelDb && s.monitors[0].meters[1].levelDb == -18,
          "Missing left never reassigns right meter");
    output["Stereo"].object["value"] = Json::Parse("false");
    s = BuildSnapshot(n);
    Check(s.monitors.size() == 1 && SurfaceChannels(s).size() == s.channels.size(),
          "Unknown monitor layout retained only as raw state");
    Check(s.channels.front().outputFormat == AudioFormat::Unknown,
          "Unknown monitor destination stays unknown");
    output.erase("Stereo");
    Check(SurfaceChannels(BuildSnapshot(n)).size() == s.channels.size(),
          "Missing stereo declaration not inferred from sixteen meters");
    auto &stereo = n["/devices/3/inputs/10"].object["properties"].object;
    stereo["Pan"].object["value"] = Json::Parse("-0.25");
    stereo["Pan2"].object["value"] = Json::Parse("0.75");
    c = SurfaceChannels(BuildSnapshot(n));
    Check(c[1].pan->value.Number() == -0.25 && c[1].panRight->value.Number() == 0.75,
          "Asymmetric dual pan not averaged");
    Check(!MeterMaximum({}, false) && !MeterMaximum({}, true), "Missing meters stay unavailable");
    const std::vector<Meter> liveOnly = {{-20, std::nullopt, false}};
    Check(MeterMaximum(liveOnly, false) == -20 && !MeterMaximum(liveOnly, true),
          "No fabricated peak from live signal");
    const auto subscriptions = SubscriptionPaths(n);
    Check(std::find(subscriptions.begin(), subscriptions.end(),
                    "/devices/3/inputs/0/OutputDestination/value") != subscriptions.end(),
          "Output format routing updates subscribed");
}
void ControlTests()
{
    auto nodes = Fixture();
    nodes["/devices/3/inputs/0"].object["properties"].object["IOType"] =
        Json::Parse(R"({"value":"Virtual"})");
    auto state = BuildSnapshot(nodes);
    state.connected = true;
    state.generation = 1;
    state.receivedAt = std::chrono::steady_clock::now();
    auto c = state.channels[0];
    Check(ControlEligible(c), "Virtual eligible");
    Check(ChannelCommand(c, ChannelField::Level, ControlNumber(-20)) ==
              std::string("set /devices/3/inputs/0/FaderLevel/value -20") + '\0',
          "Exact typed wire command");
    Check(ChannelCommand(c, ChannelField::Mute, Json::Parse("true")).find(" true") != std::string::npos,
          "Boolean command");
    for (double value : {-145., 13., 1e9})
        Reject([&] { ChannelCommand(c, ChannelField::Level, ControlNumber(value)); });
    Reject([&] { ControlNumber(NAN); });
    Reject([&] { ChannelCommand(c, ChannelField::Mute, ControlNumber(1)); });
    Reject([&] { ChannelCommand(c, ChannelField::Level, Json::Parse(R"("0\nset /bad 1")")); });
    Reject([&] { ChannelCommand(c, ChannelField::PanRight, ControlNumber(0)); });
    auto changed = c;
    changed.monitor = true;
    Check(!ControlEligible(changed), "Monitor blocked");
    changed = c;
    changed.ioType = "TalkbackMic";
    Check(!ControlEligible(changed), "Talkback blocked");
    for (const auto *path : {"/devices/3/outputs/0", "/devices/3/inputs/0/../1", "/devices/x/inputs/0",
                             "/devices/3/inputs/0/Pan/value", "/devices/3/inputs/"})
    {
        changed = c;
        changed.path = path;
        Check(!ControlEligible(changed), "Only exact channel routes");
    }
    changed = c;
    changed.level->path = "/devices/3/inputs/1/FaderLevel/value";
    Reject([&] { ChannelCommand(changed, ChannelField::Level, ControlNumber(-20)); });
    changed = c;
    changed.level->reportedReadOnly = true;
    Reject([&] { ChannelCommand(changed, ChannelField::Level, ControlNumber(-20)); });
    changed = c;
    changed.level->enabled = false;
    Reject([&] { ChannelCommand(changed, ChannelField::Level, ControlNumber(-20)); });
    changed = c;
    changed.level->minimum.reset();
    Reject([&] { ChannelCommand(changed, ChannelField::Level, ControlNumber(-20)); });
    changed = c;
    changed.name = "Renamed";
    changed.level->value = ControlNumber(-24);
    Check(SameControlTarget(c, changed), "Name/value changes do not retarget");
    changed.stereo = !c.stereo;
    Check(!SameControlTarget(c, changed), "Link change revokes control");
    changed = c;
    changed.destination = "Different output";
    Check(!SameControlTarget(c, changed), "Route change revokes control");
    changed = c;
    changed.key += "x";
    Check(!SameControlTarget(c, changed), "Identity change revokes control");
    Check(!SameControlValue(ChannelField::Level, ControlNumber(-20), ControlNumber(-19)),
          "Wrong readback rejected");
    Check(SameControlValue(ChannelField::PanLeft, ControlNumber(.5), ControlNumber(.5000001)),
          "Float readback tolerance");
    ChannelQueue q;
    Check(!q.Epoch() && !q.Submit(ChannelField::Mute, Json::Parse("true"), 1), "Default is unarmed");
    const auto epoch = q.Arm(state, c.key);
    Check(q.Valid(state) && q.Size() == 0 && epoch, "Arming sends nothing");
    for (int i = 0; i < 1000; ++i)
        Check(q.Submit(ChannelField::Level, ControlNumber(-50 + i * .05), epoch) != 0, "Burst accepted");
    Check(q.Size() == 1, "Absolute gestures coalesced");
    const auto request = q.Take();
    Check(request && SameControlValue(ChannelField::Level, request->value, ControlNumber(-.05)),
          "Latest absolute target retained");
    q.Submit(ChannelField::Mute, Json::Parse("true"), epoch);
    q.Disarm();
    Check(!q.Take() && !q.Submit(ChannelField::Mute, Json::Parse("false"), epoch),
          "Disarm drops queue and stale callbacks");
    q.Arm(state, c.key);
    Check(!q.Submit(ChannelField::Mute, Json::Parse("true"), epoch), "New arm rejects old epoch");
    ++state.generation;
    Check(!q.Valid(state), "Reconnect invalidates scope");
    state.receivedAt -= std::chrono::seconds(2);
    Reject([&] { q.Arm(state, c.key); });
    Check(!q.Epoch(), "Failed arm leaves control disabled");
}
void MonitorTests()
{
    auto nodes = Fixture();
    auto &device = nodes["/devices/3"].object["properties"].object;
    device["SurroundMonitorMode"] = Json::Parse(R"({"type":"string","value":"STEREO"})");
    device["AltMonSelection"] = Json::Parse(R"({"type":"int","min":0,"max":2,"value":0})");
    device["DimAttenuation"] = Json::Parse(R"({"type":"int","min":0,"max":60,"value":17})");
    device["Enable24dBMode"] = Json::Parse(R"({"type":"bool","value":false})");
    auto &output = nodes["/devices/3/outputs/22"].object["properties"].object;
    output["Stereo"] = Json::Parse(R"({"type":"bool","value":true})");
    output["MixInSource"] = Json::Parse(R"({"type":"string","value":"mon"})");
    output["Mute"] = output["MixToMono"] = Json::Parse(R"({"type":"bool","value":false})");
    auto state = BuildSnapshot(nodes);
    state.connected = true;
    state.generation = 1;
    state.receivedAt = std::chrono::steady_clock::now();
    const auto m = state.monitors.front();
    Check(MonitorEligible(m), "Stereo main monitor eligible");
    Check(MonitorKnobValue(MonitorField::DimAmount, 17) == -17 &&
              MonitorKnobValue(MonitorField::DimAmount, -26) == 26 &&
              MonitorKnobValue(MonitorField::Level, -40) == -40,
          "Upper monitor signed DIM round trip, unchanged volume dB");
    const auto upperMonitor = DescribeMonitor(m);
    Check(!upperMonitor.empty() && upperMonitor.front().field == MonitorField::Level &&
              upperMonitor.front().knob,
          "Upper control room describes actual monitoring level");
    auto monitorFeedback = m;
    monitorFeedback.level->value = ControlNumber(-40);
    Check(SameMonitorPage(m, monitorFeedback), "Monitor feedback preserves upper page");
    monitorFeedback.level->reportedReadOnly = true;
    Check(!SameMonitorPage(m, monitorFeedback), "Changed monitor permissions invalidate upper page");
    Check(SurfaceChannels(state).size() == state.channels.size(), "Eligible monitor still has no fader slot");
    MonitorQueue q;
    Check(!q.Epoch() && !q.Submit(MonitorField::Level, ControlNumber(-40), 0), "Monitor defaults locked");
    const auto epoch = q.Arm(state, m.key);
    Check(epoch && q.Ceiling() == -30 && q.Size() == 0,
          "Unlock captures current level without sending a write");
    Check(MonitorCommand(m, MonitorField::Level, ControlNumber(-40), -30) ==
              std::string("set /devices/3/outputs/22/CRMonitorLevel/value -40") + '\0',
          "Exact monitor wire command");
    Check(ConstrainMonitorValue(m, MonitorField::Level, ControlNumber(-5), -30).Number() == -30,
          "Increase clamped to explicit ceiling");
    for (auto field : {MonitorField::Mute, MonitorField::Dim, MonitorField::Mono})
    {
        Check(MonitorCommand(m, field, Json::Parse("true"), -30).find(" true") != std::string::npos,
              "Typed Boolean monitor write");
        Reject([&] { MonitorCommand(m, field, ControlNumber(1), -30); });
    }
    Reject([&] { MonitorCommand(m, MonitorField::Level, ControlNumber(-29), -30); });
    for (double value : {-97., 1., 1000.})
        Reject([&] { ConstrainMonitorValue(m, MonitorField::Level, ControlNumber(value), -30); });
    Reject([&] { MonitorCommand(m, MonitorField::Level, ControlNumber(-40), NAN); });
    Reject([&] { MonitorCommand(m, MonitorField::Level, Json::Parse(R"("0\nset /bad 1")"), -30); });
    auto changed = m;
    changed.level->path = "/devices/3/inputs/0/FaderLevel/value";
    Check(!MonitorEligible(changed), "No monitor-to-channel path substitution");
    for (const auto *path : {"/devices/3/inputs/22", "/devices/3/outputs/22/../23", "/devices/x/outputs/22",
                             "/devices/3/outputs/"})
    {
        changed = m;
        changed.path = path;
        Check(!MonitorEligible(changed), "Only exact discovered output paths");
    }
    changed = m;
    changed.level->reportedReadOnly = true;
    Check(!MonitorEligible(changed), "Read-only main gain cannot be unlocked");
    changed = m;
    changed.mute->enabled = false;
    Reject([&] { MonitorCommand(changed, MonitorField::Mute, Json::Parse("true"), -30); });
    changed = m;
    changed.source = "cue1";
    Check(!SameMonitorTarget(m, changed), "Source change invalidates authorization");
    changed = m;
    changed.mode = "5.1";
    Check(!MonitorEligible(changed), "Surround cannot be treated as stereo control room");
    changed = m;
    changed.speakerSelection->value = ControlNumber(1);
    Check(!MonitorEligible(changed), "ALT speaker context not silently inherited");
    changed = m;
    changed.highHeadroom->value = Json::Parse("true");
    Check(!MonitorEligible(changed), "Gain-mode change blocks initial write scope");
    changed = m;
    changed.dimAttenuation->value = ControlNumber(26);
    Check(SameMonitorTarget(m, changed), "Supported dim-depth changes are state, not topology");
    changed.dimAttenuation->value = ControlNumber(27);
    Check(!SameMonitorTarget(m, changed), "Unknown dim-depth capability revokes permission");
    changed = m;
    changed.level->value = ControlNumber(-20);
    Reject([&] { MonitorCommand(changed, MonitorField::Mute, Json::Parse("false"), -30); });
    for (const auto depth : MonitorDimTable())
        Check(MonitorCommand(m, MonitorField::DimAmount, ControlNumber(-depth), -30) ==
                  std::string("set /devices/3/DimAttenuation/value ") + ControlNumber(-depth).scalar + '\0',
              "Discrete signed EUCON dim depth maps to native positive attenuation");
    for (double depth : {-17., 0., 10., 17.5, 61.})
        Reject([&] { MonitorCommand(m, MonitorField::DimAmount, ControlNumber(depth), -30); });
    Reject([&] { MonitorCommand(m, MonitorField::Source, Json::Parse("\"cue1\""), -30); });
    auto advancedNodes = nodes;
    advancedNodes[m.path].object["properties"].object["MixInSource"] = Json::Parse(
        R"({"type":"string","value":"mon","values":["mon","cue1",{"value":"cue2","enabled":false},"cue3","cue4","bad\nset /Sleep false"]})");
    advancedNodes["/"] = Json::Parse(
        R"({"properties":{"TalkbackOn":{"type":"bool","value":false},"TalkbackMaster":{"type":"int","value":3},"TalkbackInPhysicalCR":{"type":"bool","value":false},"TalkbackMicSelect":{"type":"int","value":0}}})");
    advancedNodes["/devices/3"].object["properties"].object["TalkbackMaster"] =
        Json::Parse(R"({"type":"int","value":3})");
    advancedNodes["/devices/3/inputs"].object["children"].object["90"] = Json::Parse("{}");
    advancedNodes["/devices/3/inputs/90"] =
        Json::Parse(R"({"properties":{"IOType":{"type":"string","value":"TalkbackMic"}}})");
    auto advanced = BuildSnapshot(advancedNodes).monitors.front();
    Check(MonitorSources(advanced) == std::vector<std::string>({"mon", "cue1", "cue3", "cue4"}),
          "Only known enabled source tokens published");
    Check(MonitorFieldAvailable(advanced, MonitorField::Talk),
          "Talkback requires discovered online master mic");
    Check(MonitorCommand(advanced, MonitorField::Talk, Json::Parse("true"), -30) ==
              std::string("set /TalkbackOn/value true") + '\0',
          "Exact global talkback command");
    Check(MonitorCommand(advanced, MonitorField::Source, Json::Parse("\"cue3\""), -30) ==
              std::string("set /devices/3/outputs/22/MixInSource/value \"cue3\"") + '\0',
          "Exact source command");
    for (const auto *source : {"\"cue2\"", "\"cue5\"", "\"bad\\nset /Sleep false\"", "1"})
        Reject([&] { MonitorCommand(advanced, MonitorField::Source, Json::Parse(source), -30); });
    auto cue = advanced;
    cue.source = "cue1";
    cue.sourceSelect->value = Json::Parse("\"cue1\"");
    Check(SameMonitorTarget(advanced, cue), "Source change retains the independent monitor processor");
    cue = advanced;
    cue.talkbackToMonitor->value = Json::Parse("true");
    Reject([&] { MonitorCommand(cue, MonitorField::Talk, Json::Parse("true"), -30); });
    Check(!MonitorCommand(cue, MonitorField::Talk, Json::Parse("false"), -30).empty(),
          "Physical CR routing never prevents explicit talk-off");
    Check(!SameMonitorTarget(advanced, cue), "Talkback routing change revokes authorization");
    cue = advanced;
    cue.talkbackMaster->value = ControlNumber(4);
    Check(!MonitorFieldAvailable(cue, MonitorField::Talk), "Do not open another device microphone");
    cue = advanced;
    cue.talkbackMicSelect->value = ControlNumber(1);
    Check(!MonitorFieldAvailable(cue, MonitorField::Talk), "Unknown mic selector not silently accepted");
    cue = advanced;
    cue.talk->reportedReadOnly = true;
    Reject([&] { MonitorCommand(cue, MonitorField::Talk, Json::Parse("true"), -30); });
    Check(ApplyValue(advancedNodes, Json::Parse(R"({"path":"/TalkbackOn/value","data":true})")),
          "Root subscription update accepted");
    Check(BuildSnapshot(advancedNodes).monitors.front().talk->value.Bool(), "Root talkback feedback applied");
    const auto extendedPaths = SubscriptionPaths(advancedNodes);
    Check(std::find(extendedPaths.begin(), extendedPaths.end(), "/TalkbackOn/value") != extendedPaths.end(),
          "Root path has one slash");
    auto other = state;
    other.monitors.front() = changed;
    Check(!q.Valid(other), "External increase above ceiling locks without writing correction");
    other = state;
    other.monitors.front().level->value = ControlNumber(-40);
    Check(q.Valid(other), "External decrease preserves original ceiling");
    for (int i = 0; i < 1000; ++i)
        Check(q.Submit(MonitorField::Level, ControlNumber(-96 + i * .09), epoch) != 0,
              "Coalesced monitor gesture");
    Check(q.Size() == 1 && q.Take()->value.Number() == -30, "One latest clamped level only");
    q.Submit(MonitorField::Dim, Json::Parse("false"), epoch);
    q.Disarm();
    Check(!q.Take() && !q.Submit(MonitorField::Mute, Json::Parse("false"), epoch),
          "Lock discards requests and old callback epoch");
    q.Arm(state, m.key);
    Check(!q.Submit(MonitorField::Level, ControlNumber(-40), epoch),
          "Unlock cannot replay previous gestures");
    other = state;
    other.monitors.push_back(m);
    Reject([&] { q.Arm(other, m.key); });
    Check(!q.Epoch(), "Ambiguous monitor never silently selected");
    q.Arm(state, m.key);
    other = state;
    ++other.generation;
    Check(!q.Valid(other), "Reconnect invalidates monitor authorization");
    other = state;
    other.receivedAt -= std::chrono::seconds(3);
    Reject([&] { q.Arm(other, m.key); });
    other = state;
    other.monitors.clear();
    Check(!q.Valid(other), "Removed monitor cannot retain authority");
    ChannelQueue channel;
    Reject([&] { channel.Arm(state, m.key); });
    state.channels.front().monitor = true;
    Check(SurfaceChannels(state).size() == state.channels.size() - 1,
          "Defensive projection excludes injected monitor channel");
    const auto subscriptions = SubscriptionPaths(nodes);
    for (const auto *property :
         {"SurroundMonitorMode", "AltMonSelection", "DimAttenuation", "Enable24dBMode"})
        Check(std::find(subscriptions.begin(), subscriptions.end(),
                        std::string("/devices/3/") + property + "/value") != subscriptions.end(),
              "Monitor context guard is subscribed");
}
#include "ChannelFeatureTests.h"
#include "UpperControlTests.h"
} // namespace
int main()
{
    try
    {
        JsonTests();
        ProtocolTests();
        ModelTests();
        FaderTests();
        FeedbackTests();
        ControlTests();
        MonitorTests();
        ChannelFeatureTests();
        ConsoleFeatureTests();
        UpperControlTests();
        std::cout << "Apollo core: " << checks
                  << " checks passed. No sockets, audio, MIDI or EUCON initialized.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
