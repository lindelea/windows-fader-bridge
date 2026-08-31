#include "ChannelFeatureFixture.h"
#include "ChannelWriter.h"
#include "LiveControlProbe.h"
#include "MonitorWriter.h"
#include "Observer.h"
#include "ReadOnlyClient.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <winsock2.h>

using namespace apollo;
using Clock = std::chrono::steady_clock;
namespace
{
void Check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void Until(F predicate, int milliseconds, const char *message)
{
    const auto end = Clock::now() + std::chrono::milliseconds(milliseconds);
    while (!predicate())
    {
        Check(Clock::now() < end, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
// Synthetic, same-process loopback fixture on an ephemeral port. Never connects
// to 4710 or loads EUCON, and never contains captured hardware data.
class Engine
{
  public:
    uint16_t port = 0;
    std::atomic<unsigned> connections = 0, commands = 0;
    std::atomic<bool> badCommand = false, drop = false, malformed = false, partial = false,
                      partialSent = false;
    std::atomic<bool> allowWrites = false, rejectWrites = false, wrongReadback = false, dropAfterSet = false;
    std::atomic<unsigned> writes = 0;
    std::atomic<int> readDelayMs = 0;
    Engine()
    {
        WSADATA data{};
        Check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Fixture Winsock");
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Check(listener_ != INVALID_SOCKET, "Fixture listener");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Check(bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0, "Fixture bind");
        int size = sizeof(address);
        Check(getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) == 0, "Fixture address");
        port = ntohs(address.sin_port);
        Check(port != 4710 && listen(listener_, 1) == 0, "Ephemeral fixture only");
        nodes_["/"] = R"({"properties":{}})";
        nodes_["/devices"] = R"({"children":{"0":{}}})";
        nodes_["/devices/0"] =
            R"({"properties":{"DeviceOnline":{"type":"bool","value":true},"DeviceHwID":{"type":"int64","value":1234567890123456789},"DeviceName":{"type":"string","value":"Synthetic"}}})";
        nodes_["/devices/0/inputs"] = R"({"children":{"0":{}}})";
        nodes_["/devices/0/auxs"] = nodes_["/devices/0/outputs"] = R"({"children":{}})";
        nodes_["/devices/0/inputs/0"] =
            R"({"properties":{"Active":{"type":"bool","value":true},"IOType":{"value":"Virtual"},"Stereo":{"type":"bool","value":true},"Name":{"type":"string","value":"Fixture input"},"FaderLevel":{"type":"float","min":-144,"max":12,"value":-20},"Pan":{"type":"float","min":-1,"max":1,"value":-1},"Pan2":{"type":"float","min":-1,"max":1,"value":1},"Mute":{"type":"bool","value":false},"Solo":{"type":"bool","value":false}},"children":{"meters":{}}})";
        nodes_["/devices/0/inputs/0/meters"] = R"({"children":{"0":{}}})";
        nodes_["/devices/0/inputs/0/meters/0"] =
            R"({"properties":{"MeterLevel":{"type":"float","min":-77,"max":0,"value":-18}}})";
        worker_ = std::thread([this] { Run(); });
    }
    ~Engine()
    {
        stop_ = true;
        if (worker_.joinable())
            worker_.join();
        for (auto &peer : peers_)
            peer.join();
        closesocket(listener_);
        WSACleanup();
    }
    void AddFeatures()
    {
        std::lock_guard<std::mutex> lock(nodesMutex_);
        for (const auto &entry : ChannelFeatureFixture("/devices/0/inputs/0"))
            nodes_[entry.first] = Encode(entry.second);
    }
    void InstallConsole()
    {
        std::lock_guard<std::mutex> lock(nodesMutex_);
        nodes_.clear();
        for (const auto &entry : ConsoleFixture("/devices/0"))
            nodes_[entry.first] = Encode(entry.second);
    }
    void AddMonitor()
    {
        std::lock_guard<std::mutex> lock(nodesMutex_);
        auto device = Json::Parse(nodes_.at("/devices/0"));
        auto &properties = device.object["properties"].object;
        properties["SurroundMonitorMode"] = Json::Parse(R"({"type":"string","value":"STEREO"})");
        properties["AltMonSelection"] = Json::Parse(R"({"type":"int","min":0,"max":2,"value":0})");
        properties["DimAttenuation"] = Json::Parse(R"({"type":"int","min":0,"max":60,"value":17})");
        properties["Enable24dBMode"] = Json::Parse(R"({"type":"bool","value":false})");
        properties["TalkbackMaster"] = Json::Parse(R"({"type":"int","value":0})");
        nodes_["/"] =
            R"({"properties":{"TalkbackOn":{"type":"bool","value":false},"TalkbackMaster":{"type":"int","value":0},"TalkbackInPhysicalCR":{"type":"bool","value":false},"TalkbackMicSelect":{"type":"int","value":0}}})";
        nodes_["/devices/0/inputs"] = R"({"children":{"0":{},"9":{}}})";
        nodes_["/devices/0/inputs/9"] =
            R"({"properties":{"IOType":{"type":"string","value":"TalkbackMic"}}})";
        nodes_["/devices/0"] = Encode(device);
        nodes_["/devices/0/outputs"] = R"({"children":{"42":{}}})";
        nodes_["/devices/0/outputs/42"] =
            R"({"properties":{"IOType":{"type":"string","value":"Monitor"},"Stereo":{"type":"bool","value":true},"Name":{"type":"string","value":"Fixture monitor"},"MixInSource":{"type":"string","value":"mon","values":["mon","cue1","cue2","cue3","cue4"]},"CRMonitorLevel":{"type":"float","min":-96,"max":0,"value":-30},"Mute":{"type":"bool","value":false},"DimOn":{"type":"bool","value":false},"MixToMono":{"type":"bool","value":false}}})";
    }
    void AddUnison()
    {
        std::lock_guard<std::mutex> lock(nodesMutex_);
        NodeMap data;
        AddUnisonFixture(data, "/devices/0/inputs/0");
        for (const auto &entry : data)
            nodes_[entry.first] = Encode(entry.second);
    }
    void SetMetadata(const std::string &path, const std::string &property, const std::string &key,
                     const Json &value)
    {
        std::lock_guard<std::mutex> lock(nodesMutex_);
        auto node = Json::Parse(nodes_.at(path));
        node.object["properties"].object[property].object[key] = value;
        nodes_[path] = Encode(node);
    }
    void SetProperty(const std::string &path, const std::string &property, const Json &value)
    {
        std::lock_guard<std::mutex> lock(nodesMutex_);
        auto node = Json::Parse(nodes_.at(path));
        node.object["properties"].object[property].object["value"] = value;
        nodes_[path] = Encode(node);
    }

  private:
    bool Ready(SOCKET socket)
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(socket, &set);
        timeval time{0, 40000};
        return select(0, &set, nullptr, nullptr, &time) > 0;
    }
    bool Send(SOCKET socket, const std::string &message)
    {
        size_t sent = 0;
        while (!stop_ && sent < message.size())
        {
            const auto count =
                send(socket, message.data() + sent, static_cast<int>(message.size() - sent), 0);
            if (count <= 0)
                return false;
            sent += static_cast<size_t>(count);
        }
        return sent == message.size();
    }
    std::string Value(const std::string &path)
    {
        const auto tail = path.rfind("/value");
        if (tail == path.npos || tail + 6 != path.size())
            return "null";
        const auto split = path.rfind('/', tail - 1);
        const auto it = nodes_.find(split == 0 ? "/" : path.substr(0, split));
        if (it == nodes_.end())
            return "null";
        const auto value =
            Json::Parse(it->second).At("properties").At(path.substr(split + 1, tail - split - 1)).At("value");
        return value.kind == Json::Kind::String    ? "\"" + value.String() + "\""
               : value.kind == Json::Kind::Boolean ? value.Bool() ? "true" : "false"
                                                   : value.scalar;
    }
    void Run() noexcept
    {
        try
        {
            while (!stop_)
            {
                if (!Ready(listener_))
                    continue;
                const SOCKET peer = accept(listener_, nullptr, nullptr);
                if (peer == INVALID_SOCKET)
                    continue;
                ++connections;
                const BOOL noDelay = TRUE;
                setsockopt(peer, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay),
                           sizeof(noDelay));
                const DWORD timeout = 200;
                setsockopt(peer, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout),
                           sizeof(timeout));
                peers_.emplace_back([this, peer] {
                    try
                    {
                        Serve(peer);
                    }
                    catch (...)
                    {
                        badCommand = true;
                    }
                    closesocket(peer);
                });
            }
        }
        catch (...)
        {
            badCommand = true;
        }
    }
    void Serve(SOCKET peer)
    {
        FrameDecoder decoder;
        std::set<std::string> subscribed;
        int meter = -30;
        while (!stop_)
        {
            if (drop.exchange(false))
                return;
            if (Ready(peer))
            {
                char buffer[16384];
                const int count = recv(peer, buffer, sizeof(buffer), 0);
                if (count <= 0)
                    return;
                for (const auto &command : decoder.Feed(std::string_view(buffer, count)))
                {
                    ++commands;
                    const auto space = command.find(' ');
                    const auto verb = command.substr(0, space);
                    auto path = command.substr(space + 1);
                    if (verb == "set" && allowWrites)
                    {
                        const auto split = path.find(' ');
                        const auto scalar = path.substr(split + 1);
                        path.resize(split);
                        const bool monitor = path.rfind("/devices/0/outputs/42/", 0) == 0;
                        const bool dimDepth = path == "/devices/0/DimAttenuation/value";
                        const bool talk =
                            path == "/TalkbackOn/value" || path == "/TalkbackInPhysicalCR/value";
                        const bool feature = path.find("/sends/") != path.npos ||
                                             path.find("/preamps/") != path.npos ||
                                             path.find("/effects/") != path.npos;
                        const std::string nodePath = talk       ? "/"
                                                     : dimDepth ? "/devices/0"
                                                     : monitor
                                                         ? "/devices/0/outputs/42"
                                                         : path.substr(0, path.rfind('/', path.size() - 7));
                        const std::string prefix = nodePath == "/" ? "/" : nodePath + "/";
                        Check(path.rfind(prefix, 0) == 0 && path.substr(path.size() - 6) == "/value",
                              "Fixture scoped set");
                        const auto property = path.substr(prefix.size(), path.size() - prefix.size() - 6);
                        Check(talk || dimDepth ||
                                  (feature
                                       ? (property == "Gain" || property == "Bypass" || property == "Pan" ||
                                          property == "LowCut" || property == "Phase" || property == "Pad" ||
                                          property == "Power" || property == "NormalizedValue" ||
                                          property == "StepValue" || property == "48V")
                                   : monitor ? (property == "CRMonitorLevel" || property == "Mute" ||
                                                property == "DimOn" || property == "MixToMono" ||
                                                property == "MixInSource")
                                             : (property == "FaderLevel" || property == "Pan" ||
                                                property == "Pan2" || property == "Mute" ||
                                                property == "Solo" || property == "OutputDestination" ||
                                                property == "IOType" || property == "RecordPreEffects" ||
                                                property == "Pad" || property == "SRConvert" ||
                                                property == "SendPostFader" || property == "MixToMono")),
                              "Fixture write field");
                        ++writes;
                        if (rejectWrites)
                        {
                            Send(peer, "{\"path\":\"" + path + "\",\"error\":\"denied\"}" + '\0');
                            continue;
                        }
                        if (!wrongReadback)
                        {
                            std::lock_guard<std::mutex> lock(nodesMutex_);
                            auto node = Json::Parse(nodes_.at(nodePath));
                            node.object["properties"].object[property].object["value"] = Json::Parse(scalar);
                            nodes_[nodePath] = Encode(node);
                        }
                        if (dropAfterSet)
                            return;
                        // Echo the desired value even when state didn't change:
                        // tests must not mistake this echo for confirmation.
                        Send(peer, "{\"path\":\"" + path + "\",\"data\":" + scalar + "}" + '\0');
                        continue;
                    }
                    if ((verb != "get" && verb != "subscribe") || !IsPath(path))
                    {
                        badCommand = true;
                        return;
                    }
                    if (malformed.exchange(false))
                    {
                        Send(peer, std::string("{\"path\":\"bad\",\"data\":0}") + '\0');
                        continue;
                    }
                    if (partial.exchange(false))
                    {
                        Send(peer, "{\"path\":");
                        partialSent = true;
                        continue;
                    }
                    std::string value;
                    if (verb == "get" && readDelayMs > 0)
                        std::this_thread::sleep_for(std::chrono::milliseconds(readDelayMs.load()));
                    {
                        std::lock_guard<std::mutex> lock(nodesMutex_);
                        const auto it = nodes_.find(path);
                        value = it == nodes_.end() ? Value(path) : it->second;
                    }
                    const auto response = "{\"path\":\"" + path +
                                          "\",\"data\":" + (value.empty() ? "null" : value) + "}" + '\0';
                    if (!Send(peer, response.substr(0, 5)) || !Send(peer, response.substr(5)))
                        return;
                    if (verb == "subscribe")
                        subscribed.insert(path);
                }
            }
            for (const auto &path : subscribed)
                if (path.find("MeterLevel/value") != path.npos)
                    if (!Send(peer,
                              "{\"path\":\"" + path + "\",\"data\":" + std::to_string(meter) + "}" + '\0'))
                        return;
            if (++meter > -12)
                meter = -30;
        }
    }
    SOCKET listener_ = INVALID_SOCKET;
    std::atomic<bool> stop_ = false;
    std::thread worker_;
    std::vector<std::thread> peers_;
    std::mutex nodesMutex_;
    static std::string Encode(const Json &j)
    {
        if (j.kind == Json::Kind::Array)
        {
            std::string result = "[";
            for (const auto &v : j.array)
            {
                if (result.size() > 1)
                    result += ',';
                result += Encode(v);
            }
            return result + ']';
        }
        if (j.kind == Json::Kind::Object)
        {
            std::string result = "{";
            for (const auto &p : j.object)
            {
                if (result.size() > 1)
                    result += ',';
                result += "\"" + p.first + "\":" + Encode(p.second);
            }
            return result + '}';
        }
        if (j.kind == Json::Kind::String)
            return "\"" + j.scalar + "\"";
        if (j.kind == Json::Kind::Boolean)
            return j.Bool() ? "true" : "false";
        return j.kind == Json::Kind::Number ? j.scalar : "null";
    }
    std::map<std::string, std::string> nodes_;
};
void Transport()
{
    Engine engine;
    std::atomic<bool> stop = false;
    {
        ReadOnlyClient client(stop, engine.port);
        client.Connect();
        Check(client.Get("/devices").At("children").Has("0"), "Native get/framing");
        client.Subscribe("/devices/0/inputs/0/Mute/value");
        std::optional<Json> reply;
        Until(
            [&] {
                reply = client.Poll(50);
                return reply.has_value();
            },
            1000, "Subscribe initial response");
        Check(reply->At("data").kind == Json::Kind::Boolean && !reply->At("data").Bool(),
              "Native subscription type");
        engine.malformed = true;
        bool rejected = false;
        try
        {
            client.Get("/devices");
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        Check(rejected, "Malformed envelope rejected");
    }
    {
        ReadOnlyClient client(stop, engine.port);
        client.Connect();
        engine.partial = true;
        engine.partialSent = false;
        std::atomic<bool> exited = false;
        std::thread blocked([&] {
            try
            {
                client.Get("/devices");
            }
            catch (const std::exception &)
            {
            }
            exited = true;
        });
        Until([&] { return engine.partialSent.load(); }, 1000, "Partial fixture started");
        const auto start = Clock::now();
        stop = true;
        blocked.join();
        Check(exited && Clock::now() - start < std::chrono::milliseconds(500),
              "Cancellation interrupts partial read");
    }
    stop = false;
    Observer observer(engine.port);
    observer.Start();
    Until([&] { return observer.Latest().connected; }, 4000, "Observer connects to synthetic tree");
    const auto first = observer.Latest();
    Check(first.channels.size() == 1 && first.generation == 1, "Observer discovery");
    Until([&] { return observer.Latest().receivedFrames > first.receivedFrames + 10; }, 2000,
          "Live subscription frames");
    engine.drop = true;
    Until([&] { return !observer.Latest().connected; }, 2000, "Disconnect observed");
    Check(observer.Latest().channels.empty() && observer.Latest().monitors.empty(),
          "Disconnect clears stale model");
    Until([&] { return observer.Latest().connected && observer.Latest().generation == 2; }, 6000,
          "Reconnect with fresh generation");
    Check(observer.Latest().channels[0].key == first.channels[0].key, "Reconnect preserves logical identity");
    const auto start = Clock::now();
    observer.Stop();
    Check(Clock::now() - start < std::chrono::milliseconds(500), "Observer bounded shutdown");
    Check(engine.commands > 30 && !engine.badCommand, "Only valid read commands sent");
}
void Writes()
{
    Engine engine;
    engine.allowWrites = true;
    std::atomic<bool> stop = false;
    Observer observer(engine.port);
    observer.Start();
    Until([&] { return observer.Latest().connected; }, 4000, "Write fixture observed");
    ChannelController controller(observer, engine.port);
    const auto key = observer.Latest().channels[0].key;
    Check(!controller.Epoch() && !controller.Submit(key, ChannelField::Mute, Json::Parse("true"), 1),
          "Unarmed has no writes");
    controller.Arm(key);
    Check(engine.writes == 0, "Arming changes no audio");
    uint64_t expected = 0;
    for (auto field : {ChannelField::Level, ChannelField::PanLeft, ChannelField::PanRight, ChannelField::Mute,
                       ChannelField::Solo})
    {
        auto value = (field == ChannelField::Mute || field == ChannelField::Solo)
                         ? Json::Parse("true")
                         : ControlNumber(field == ChannelField::Level ? -22 : .25);
        Check(controller.Submit(key, field, value, controller.Epoch()), "Typed control queued");
        ++expected;
        Until([&] { return controller.Status().confirmed == expected || !controller.Epoch(); }, 4000,
              "Write completion");
        Check(controller.Status().confirmed == expected && controller.Status().error.empty(),
              "Explicit readback confirmed");
    }
    const auto previous = controller.Epoch();
    controller.Disarm();
    Check(!controller.Submit(key, ChannelField::Mute, Json::Parse("false"), previous), "Old epoch discarded");
    controller.Arm(key);
    engine.wrongReadback = true;
    controller.Submit(key, ChannelField::PanLeft, ControlNumber(-.5), controller.Epoch());
    Until([&] { return !controller.Epoch(); }, 4000, "Wrong readback disarms");
    Check(!controller.Status().error.empty() && controller.Status().confirmed == expected,
          "Set echo not accepted as readback");
    engine.wrongReadback = false;
    {
        ChannelWriteClient client(stop, engine.port);
        client.Connect();
        const auto state = observer.Latest();
        ChannelRequest request{
            state.channels[0], ChannelField::Level, ControlNumber(-21), 1, state.generation, 1, Clock::now()};
        const auto before = engine.writes.load();
        Check(!client.Apply(request, [](const Channel &) { return false; }), "Final permission cancellation");
        Check(engine.writes == before, "Cancelled transaction sent no write");
        request.created -= std::chrono::seconds(2);
        bool expired = false;
        try
        {
            client.Apply(request, [](const Channel &) { return true; });
        }
        catch (const std::exception &)
        {
            expired = true;
        }
        Check(expired && engine.writes == before, "Expired request rejected before set");
        request.created = Clock::now();
        request.target.stereo = false;
        bool changed = false;
        try
        {
            client.Apply(request, [](const Channel &) { return true; });
        }
        catch (const std::exception &)
        {
            changed = true;
        }
        Check(changed && engine.writes == before, "Changed channel format rejected before set");
    }
    controller.Arm(key);
    engine.rejectWrites = true;
    controller.Submit(key, ChannelField::Mute, Json::Parse("false"), controller.Epoch());
    Until([&] { return !controller.Epoch(); }, 4000, "Engine rejection disarms");
    engine.rejectWrites = false;
    controller.Arm(key);
    engine.dropAfterSet = true;
    controller.Submit(key, ChannelField::Level, ControlNumber(-25), controller.Epoch());
    Until([&] { return !controller.Epoch(); }, 4000, "Ambiguous disconnect disarms");
    const auto count = engine.writes.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    Check(engine.writes == count && !engine.badCommand, "No retry after uncertain write");
    observer.Stop();
}
void FeatureWrites()
{
    Engine engine;
    engine.AddFeatures();
    engine.allowWrites = true;
    std::atomic<bool> stop = false;
    Observer observer(engine.port);
    observer.Start();
    Until([&] { return observer.Latest().connected; }, 5000, "Feature discovery completes");
    const auto initial = observer.Latest();
    const auto target = initial.channels.front();
    Check(target.sends.size() == 2 && target.inserts.size() == 1 && target.preamps.size() == 1,
          "Observer discovers complete child controls");
    ChannelWriteClient writer(stop, engine.port);
    writer.Connect();
    const std::vector<std::pair<ChannelAddress, Json>> operations{
        {{ChannelField::SendLevel, "5"}, ControlNumber(-15)},
        {{ChannelField::SendLevel, "9"}, ControlNumber(-5)},
        {{ChannelField::SendBypass, "5"}, Json::Parse("true")},
        {{ChannelField::SendPan, "5"}, ControlNumber(-.75)},
        {{ChannelField::PreampGain, "0"}, ControlNumber(25)},
        {{ChannelField::LowCut, "0"}, Json::Parse("true")},
        {{ChannelField::Phase, "0"}, Json::Parse("true")},
        {{ChannelField::Pad, "0"}, Json::Parse("true")},
        {{ChannelField::InsertPower, "2"}, Json::Parse("false")},
        {{ChannelField::InsertValue, "2", "0"}, ControlNumber(.625)},
        {{ChannelField::InsertStep, "2", "1"}, Json::Parse("\"Fast\"")}};
    uint64_t sequence = 0;
    for (const auto &operation : operations)
    {
        ChannelRequest request{target,     operation.first, operation.second, 1, initial.generation,
                               ++sequence, Clock::now()};
        const auto result = writer.Apply(request, [](const Channel &) { return true; });
        Check(result && SameControlValue(operation.first, *result, operation.second),
              "Feature full-node readback confirmed");
    }
    auto rejected = [&](ChannelRequest request, bool expectWrite) {
        const auto before = engine.writes.load();
        request.created = Clock::now();
        bool failed = false;
        try
        {
            writer.Apply(request, [](const Channel &) { return true; });
        }
        catch (const std::exception &)
        {
            failed = true;
        }
        Check(failed && engine.writes == before + (expectWrite ? 1 : 0),
              "Feature failure respects pre-write guards and no retry");
    };
    ChannelRequest send{
        target,      {ChannelField::SendLevel, "5"}, ControlNumber(-12), 1, initial.generation, ++sequence,
        Clock::now()};
    const auto beforeCancel = engine.writes.load();
    Check(!writer.Apply(send, [](const Channel &) { return false; }) && engine.writes == beforeCancel,
          "Cancelled feature sends no set");
    engine.SetProperty(target.path + "/sends/5", "ID", Json::Parse("\"rerouted\""));
    rejected(send, false);
    engine.SetProperty(target.path + "/sends/5", "ID", Json::Parse("\"test-destination5\""));
    engine.SetMetadata(target.path + "/sends/5", "Gain", "enabled", Json::Parse("false"));
    rejected(send, false);
    engine.SetMetadata(target.path + "/sends/5", "Gain", "enabled", Json::Parse("true"));
    ChannelRequest plugin{target,
                          {ChannelField::InsertValue, "2", "0"},
                          ControlNumber(.5),
                          1,
                          initial.generation,
                          ++sequence,
                          Clock::now()};
    engine.SetProperty(target.path + "/effects/2", "EffectInstance", ControlNumber(123456));
    rejected(plugin, false);
    engine.SetProperty(target.path + "/effects/2", "EffectInstance", ControlNumber(987654321));
    engine.SetProperty(target.path + "/effects/2/parameters/0", "Name",
                       Json::Parse("\"Different parameter\""));
    rejected(plugin, false);
    engine.SetProperty(target.path + "/effects/2/parameters/0", "Name", Json::Parse("\"Time\""));
    engine.SetMetadata(target.path + "/effects/2/parameters/0", "NormalizedValue", "readonly",
                       Json::Parse("true"));
    rejected(plugin, false);
    engine.SetMetadata(target.path + "/effects/2/parameters/0", "NormalizedValue", "readonly",
                       Json::Parse("false"));
    ChannelRequest preamp{
        target,      {ChannelField::PreampGain, "0"}, ControlNumber(30), 1, initial.generation, ++sequence,
        Clock::now()};
    engine.SetProperty(target.path + "/preamps/0", "HiZ", Json::Parse("true"));
    rejected(preamp, false);
    engine.SetProperty(target.path + "/preamps/0", "HiZ", Json::Parse("false"));
    engine.wrongReadback = true;
    rejected(send, true);
    rejected(plugin, true);
    engine.wrongReadback = false;
    ChannelRequest output{target,      ChannelField::Output, Json::Parse("\"Line 1-2\""),
                          1,           initial.generation,   ++sequence,
                          Clock::now()};
    Check(writer.Apply(output, [](const Channel &) { return true; })->String() == "Line 1-2",
          "Output string route explicitly confirmed");
    rejected(send, false); // Ordinary old gestures cannot follow a route change.
    engine.SetProperty(target.path, "OutputDestination", Json::Parse("\"Monitor\""));
    ChannelRequest input{target,     ChannelField::Input, Json::Parse("\"Line\""), 1, initial.generation,
                         ++sequence, Clock::now()};
    Check(writer.Apply(input, [](const Channel &) { return true; })->String() == "Line",
          "Input source explicitly confirmed");
    engine.SetProperty(target.path, "IOType", Json::Parse("\"Mic\""));
    observer.Stop();
    observer.Start();
    Until([&] { return observer.Latest().connected; }, 5000, "Fresh feature observer");
    ChannelController controller(observer, engine.port);
    controller.Arm(target.key);
    const auto beforeRouteEpoch = controller.Epoch(target.key);
    Check(
        controller.Submit(target.key, ChannelField::Output, Json::Parse("\"Line 1-2\""), controller.Epoch()),
        "Routing queued after explicit arm");
    Until(
        [&] { return controller.Status().confirmed == 1 && controller.Epoch(target.key) > beforeRouteEpoch; },
        6000, "Confirmed route resumes same channel with a fresh gesture epoch");
    Check(controller.Status().error.empty(), "Normal routing is not an error");
    Check(!controller.Submit(target.key, ChannelField::Mute, Json::Parse("true"), beforeRouteEpoch),
          "Pre-route callback cannot act on the new route");
    Check(
        controller.Submit(target.key, ChannelField::Mute, Json::Parse("true"), controller.Epoch(target.key)),
        "Post-route channel remains controllable");
    Until([&] { return controller.Status().confirmed == 2; }, 4000, "Post-route mute confirmed");
    Check(!engine.badCommand, "No unallowlisted feature commands");
    observer.Stop();
}
void MonitorWrites()
{
    Engine engine;
    engine.AddMonitor();
    engine.allowWrites = true;
    Observer observer(engine.port);
    observer.Start();
    Until([&] { return observer.Latest().connected && observer.Latest().monitors.size() == 1; }, 4000,
          "Monitor fixture observed");
    MonitorController controller(observer, engine.port);
    ChannelController channels(observer, engine.port);
    const auto state = observer.Latest();
    const auto m = state.monitors.front();
    channels.Arm(state.channels.front().key);
    Check(!controller.Epoch() &&
              !controller.Submit(m.key, MonitorField::Level, ControlNumber(-40), channels.Epoch()),
          "Channel authorization cannot unlock monitor");
    Check(engine.writes == 0, "Locked monitor sends zero writes");
    controller.Arm(m.key);
    Check(engine.writes == 0 && controller.Status().ceiling == -30,
          "Unlock captures ceiling without writing");
    uint64_t expected = 0;
    for (auto field : {MonitorField::Level, MonitorField::Mute, MonitorField::Dim, MonitorField::Mono})
    {
        const auto value = field == MonitorField::Level ? ControlNumber(-40) : Json::Parse("true");
        Check(controller.Submit(m.key, field, value, controller.Epoch()), "Monitor gesture queued");
        ++expected;
        Until([&] { return controller.Status().confirmed == expected || !controller.Epoch(); }, 4000,
              "Monitor completion");
        Check(controller.Status().confirmed == expected && controller.Status().error.empty(),
              "Monitor node readback verified");
    }
    for (const auto &operation :
         std::vector<std::pair<MonitorField, Json>>{{MonitorField::DimAmount, ControlNumber(26)},
                                                    {MonitorField::Source, Json::Parse("\"cue1\"")},
                                                    {MonitorField::Source, Json::Parse("\"cue2\"")},
                                                    {MonitorField::Source, Json::Parse("\"cue3\"")},
                                                    {MonitorField::Source, Json::Parse("\"cue4\"")},
                                                    {MonitorField::Source, Json::Parse("\"mon\"")},
                                                    {MonitorField::Talk, Json::Parse("true")},
                                                    {MonitorField::Talk, Json::Parse("false")}})
    {
        Check(controller.Submit(m.key, operation.first, operation.second, controller.Epoch()),
              "Extended monitor gesture queued");
        ++expected;
        Until([&] { return controller.Status().confirmed == expected || !controller.Epoch(); }, 4000,
              "Extended monitor completion");
        Check(controller.Status().confirmed == expected && controller.Status().error.empty(),
              "Root/device/output writes independently confirmed");
    }
    Check(controller.Submit(m.key, MonitorField::Level, ControlNumber(-2), controller.Epoch()),
          "Ceiling gesture accepted");
    ++expected;
    Until([&] { return controller.Status().confirmed == expected || !controller.Epoch(); }, 4000,
          "Ceiling completion");
    Check(controller.Status().confirmed == expected, "Clamped monitor value confirmed");
    const auto oldEpoch = controller.Epoch();
    controller.Disarm();
    Check(!controller.Submit(m.key, MonitorField::Mute, Json::Parse("false"), oldEpoch),
          "Locked rejects prior gesture");
    std::atomic<bool> stop = false;
    {
        ReadOnlyClient reader(stop, engine.port);
        reader.Connect();
        Check(Property(reader.Get(m.path), "CRMonitorLevel").At("value").Number() == -30,
              "No command above captured ceiling reaches engine");
        MonitorWriteClient writer(stop, engine.port);
        writer.Connect();
        auto request = MonitorRequest{m, MonitorField::Level, ControlNumber(-40), -30, 1, state.generation,
                                      1, Clock::now()};
        const auto before = engine.writes.load();
        Check(!writer.Apply(request, [](const Monitor &) { return false; }) && engine.writes == before,
              "Last-moment cancellation sends no monitor write");
        auto rejects = [&](const MonitorRequest &r) {
            bool rejected = false;
            try
            {
                writer.Apply(r, [](const Monitor &) { return true; });
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            Check(rejected && engine.writes == before, "Unsafe monitor transaction rejected before set");
        };
        request.created -= std::chrono::seconds(2);
        rejects(request);
        request.created = Clock::now();
        engine.SetProperty(m.path, "CRMonitorLevel", ControlNumber(-20));
        rejects(request);
        engine.SetProperty(m.path, "CRMonitorLevel", ControlNumber(-30));
        for (const auto &change : {std::pair<const char *, const char *>{"AltMonSelection", "1"},
                                   {"Enable24dBMode", "true"},
                                   {"DimAttenuation", "27"},
                                   {"SurroundMonitorMode", "\"5.1\""}})
        {
            const auto original = Property(reader.Get("/devices/0"), change.first).At("value");
            engine.SetProperty("/devices/0", change.first, Json::Parse(change.second));
            request.created = Clock::now();
            rejects(request);
            engine.SetProperty("/devices/0", change.first, original);
        }
        engine.SetProperty(m.path, "MixInSource", Json::Parse("\"unknown\""));
        request.created = Clock::now();
        rejects(request);
        engine.SetProperty(m.path, "MixInSource", Json::Parse("\"mon\""));
    }
    for (const auto &operation :
         std::vector<std::pair<MonitorField, Json>>{{MonitorField::DimAmount, ControlNumber(34)},
                                                    {MonitorField::Source, Json::Parse("\"cue2\"")},
                                                    {MonitorField::Talk, Json::Parse("true")}})
    {
        controller.Arm(m.key);
        engine.wrongReadback = true;
        const auto writes = engine.writes.load();
        controller.Submit(m.key, operation.first, operation.second, controller.Epoch());
        Until([&] { return !controller.Epoch(); }, 4000, "New monitor field wrong readback locks");
        Check(engine.writes == writes + 1 && controller.Status().confirmed == expected &&
                  !controller.Status().error.empty(),
              "Global/device/source set echo is not proof; never retried");
        engine.wrongReadback = false;
    }
    controller.Arm(m.key);
    engine.wrongReadback = true;
    controller.Submit(m.key, MonitorField::Level, ControlNumber(-45), controller.Epoch());
    Until([&] { return !controller.Epoch(); }, 4000, "Wrong monitor readback locks");
    Check(controller.Status().confirmed == expected && !controller.Status().error.empty(),
          "Echo not confirmation");
    engine.wrongReadback = false;
    controller.Arm(m.key);
    engine.rejectWrites = true;
    controller.Submit(m.key, MonitorField::Mute, Json::Parse("false"), controller.Epoch());
    Until([&] { return !controller.Epoch(); }, 4000, "Monitor denial locks");
    engine.rejectWrites = false;
    controller.Arm(m.key);
    engine.dropAfterSet = true;
    controller.Submit(m.key, MonitorField::Level, ControlNumber(-45), controller.Epoch());
    Until([&] { return !controller.Epoch(); }, 4000, "Ambiguous monitor disconnect locks");
    const auto count = engine.writes.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    Check(engine.writes == count && !engine.badCommand, "No monitor retry after uncertain write");
    observer.Stop();
}
void ConsoleWorkflow()
{
    Engine engine;
    engine.InstallConsole();
    engine.allowWrites = true;
    Observer observer(engine.port);
    observer.Start();
    Until([&] { return observer.Latest().connected; }, 6000, "Console observer ready");
    ChannelController controller(observer, engine.port);
    const auto initial = observer.Latest();
    const auto key = [&](const std::string &path) {
        for (const auto &c : initial.channels)
            if (c.path == path)
                return c.key;
        throw std::runtime_error("Missing Console channel");
    };
    const auto mic = key("/devices/0/inputs/0"), line = key("/devices/0/inputs/4"),
               digital = key("/devices/0/inputs/8"), talk = key("/devices/0/inputs/12"),
               aux = key("/devices/0/auxs/0");
    controller.ArmAll();
    Check(engine.writes == 0, "Enabling all channels never writes audio state");
    for (const auto &c : initial.channels)
        Check(controller.Epoch(c.key) != 0, "Each channel independently armed");
    Check(!controller.Submit(line, ChannelField::Mute, Json::Parse("true"), controller.Epoch(mic)),
          "Cross-channel callback rejected");
    unsigned confirmed = 0;
    const auto change = [&](const std::string &k, ChannelAddress field, const Json &value) {
        Check(controller.Submit(k, field, value, controller.Epoch(k)), "Console operation queued");
        ++confirmed;
        Until([&] { return controller.Status().confirmed == confirmed || !controller.Epoch(k); }, 6000,
              "Console write finishes");
        Check(controller.Status().confirmed == confirmed, "Console write has authoritative readback");
    };
    change(line, ChannelField::Reference, Json::Parse("true"));
    change(digital, ChannelField::SampleRateConvert, Json::Parse("true"));
    change(aux, ChannelField::SendPostFader, Json::Parse("false"));
    change(aux, ChannelField::Mono, Json::Parse("true"));
    change(mic, ChannelField::RecordPreEffects, Json::Parse("false"));
    const auto writes = engine.writes.load();
    Check(!controller.Submit(mic, {ChannelField::Phantom, "0"}, Json::Parse("true"), controller.Epoch(mic)),
          "Phantom activation blocked by default");
    Check(!controller.Submit(talk, ChannelField::TalkToMonitor, Json::Parse("true"), controller.Epoch(talk)),
          "TB monitor activation blocked by default");
    Check(engine.writes == writes, "Protected operations sent no sets");
    controller.UnlockSafety(talk);
    change(talk, ChannelField::TalkToMonitor, Json::Parse("true"));
    change(talk, ChannelField::Talk, Json::Parse("true"));
    change(talk, ChannelField::Talk, Json::Parse("false"));
    change(talk, ChannelField::TalkToMonitor, Json::Parse("false"));
    const auto oldMic = controller.Epoch(mic), oldLine = controller.Epoch(line);
    controller.UnlockSafety(mic);
    change(mic, {ChannelField::Phantom, "0"}, Json::Parse("true"));
    Until([&] { return controller.Epoch(mic) > oldMic; }, 6000, "Phantom context refresh resumes mic");
    Check(controller.Epoch(line) == oldLine, "Mic refresh preserves other channel authorization");
    Check(!controller.Submit(mic, ChannelField::Mute, Json::Parse("true"), oldMic),
          "Old preamp epoch cannot reappear");
    change(mic, {ChannelField::Phantom, "0"}, Json::Parse("false"));
    Until([&] { return controller.Epoch(mic) != 0; }, 6000, "Phantom off refresh finishes");
    change(mic, ChannelField::Input, Json::Parse("\"Line\""));
    Until([&] { return controller.Epoch(mic) != 0; }, 6000, "Input source refresh finishes");
    const auto beforeRoute = controller.Epoch(line);
    change(line, ChannelField::Output, Json::Parse("\"Line 1-2\""));
    Until([&] { return controller.Epoch(line) > beforeRoute; }, 6000, "Route continues with fresh epoch");
    for (const auto &k : {mic, line, aux})
        Check(controller.Submit(k, ChannelField::Mute, Json::Parse("true"), controller.Epoch(k)),
              "Independent gestures queued together");
    confirmed += 3;
    Until([&] { return controller.Status().confirmed == confirmed; }, 5000,
          "Fair writer completes simultaneous channels");
    const auto revision = observer.Latest().metadataRevision;
    engine.readDelayMs = 65;
    observer.Refresh();
    const auto finish = Clock::now() + std::chrono::seconds(12);
    while (observer.Latest().metadataRevision == revision)
    {
        const auto state = observer.Latest();
        Check(Clock::now() < finish && state.connected, "Delayed refresh finishes while staying online");
        Check(Clock::now() - state.receivedAt < std::chrono::milliseconds(800),
              "Discovery never starves published feedback");
        Check(controller.Epoch(line) && controller.Epoch(mic), "Long refresh preserves channel permissions");
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    engine.readDelayMs = 0;
    engine.SetProperty("/devices/0/inputs/4", "OutputDestination", Json::Parse("\"Monitor\""));
    observer.Refresh();
    Until([&] { return !controller.Epoch(line); }, 6000, "External routing revokes only affected channel");
    Check(controller.Epoch(mic) && controller.Epoch(aux),
          "Other channels remain available after external reroute");
    controller.Disarm();
    Check(!controller.Epoch(), "Global lock clears all channels");
    Check(!engine.badCommand, "Console workflow stays within synthetic allowlist");
    observer.Stop();
}
} // namespace
void UpperWorkflow()
{
    Engine engine;
    engine.InstallConsole();
    engine.AddUnison();
    engine.AddMonitor();
    engine.allowWrites = true;
    Observer observer(engine.port);
    observer.Start();
    Until([&] { return observer.Latest().connected; }, 6000, "Upper fixture observer");
    const auto state = observer.Latest();
    const auto &c = state.channels.front();
    Check(c.preamps[0].unison[0].parameters.size() == 3, "Observer discovers nested UNISON parameters");
    const ChannelAddress gain{ChannelField::UnisonValue, "0", "0", "0"};
    const ChannelAddress mode{ChannelField::UnisonStep, "0", "1", "0"};
    const ChannelAddress power{ChannelField::UnisonPower, "0", "", "0"};
    ChannelController channels(observer, engine.port);
    MonitorController monitor(observer, engine.port);
    channels.Arm(c.key);
    Check(!monitor.Epoch(), "Arming upper channel never unlocks monitor");
    Check(!channels.Submit(c.key, gain, ControlNumber(.5), channels.Epoch(c.key)) && engine.writes == 0,
          "UNISON remains protected before explicit safety confirmation");
    channels.UnlockSafety(c.key);
    size_t confirmed = 0;
    for (const auto &operation : std::vector<std::pair<ChannelAddress, Json>>{
             {gain, ControlNumber(.625)}, {mode, Json::Parse("\"Fast\"")}, {power, Json::Parse("false")}})
    {
        Check(channels.Submit(c.key, operation.first, operation.second, channels.Epoch(c.key)),
              "Queue upper UNISON");
        ++confirmed;
        Until([&] { return channels.Status().confirmed == confirmed || !channels.Epoch(c.key); }, 6000,
              "Upper UNISON writer completes");
        Check(channels.Status().confirmed == confirmed, "Upper UNISON native readback confirmed");
    }
    Check(!monitor.Submit(state.monitors.front().key, MonitorField::Level, ControlNumber(-50), 0),
          "Channel safety confirmation never authorizes monitoring");
    monitor.Arm(state.monitors.front().key);
    Check(monitor.Submit(state.monitors.front().key, MonitorField::DimAmount, ControlNumber(26),
                         monitor.Epoch()),
          "Monitor alias uses independent monitor authorization");
    Until([&] { return monitor.Status().confirmed == 1 || !monitor.Epoch(); }, 6000,
          "Upper monitor readback");
    Check(monitor.Status().confirmed == 1, "Monitor alias shares confirmed native state");
    channels.Disarm();
    Check(monitor.Epoch() != 0, "Channel lock does not lock independent monitor permission");
    monitor.Disarm();
    std::atomic<bool> stop{false};
    ChannelWriteClient writer(stop, engine.port);
    writer.Connect();
    auto target = observer.Latest().channels.front();
    ChannelRequest request{target, gain, ControlNumber(.4), 1, state.generation, 1, Clock::now()};
    const auto before = engine.writes.load();
    engine.SetProperty(target.path + "/preamps/0/effects/0", "EffectInstance", ControlNumber(11111));
    bool rejected = false;
    try
    {
        writer.Apply(request, [](const Channel &) { return true; });
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    Check(rejected && engine.writes == before, "UNISON instance replacement rejects stale write before send");
    observer.Stop();
    Check(!engine.badCommand, "Upper workflow uses only known fixture paths");
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 1)
        {
            Check(argc == 3 && (std::string(argv[1]) == "--live-muted-channel" ||
                                std::string(argv[1]) == "--live-readiness"),
                  "Unknown arguments; live probe is explicit opt-in");
            LiveMutedChannelProbe(argv[2], std::string(argv[1]) == "--live-readiness");
            return 0;
        }
        Transport();
        Writes();
        FeatureWrites();
        ConsoleWorkflow();
        MonitorWrites();
        UpperWorkflow();
        std::cout << "Apollo transport: framing, subscription, invalid envelope, "
                     "cancellation, disconnect, reconnect "
                     "and shutdown; typed channel/control-room writes, ceiling, context "
                     "guards, readback, rejection, "
                     "independent permissions and no-retry safety passed. Synthetic "
                     "ephemeral loopback only.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
