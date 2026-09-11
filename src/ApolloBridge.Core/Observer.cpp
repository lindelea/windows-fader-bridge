#include "Observer.h"
#include "ReadOnlyClient.h"
#include "Configuration.h"
#include <set>
#include <stdexcept>

namespace apollo
{
namespace
{
using Clock = std::chrono::steady_clock;
NodeMap Discover(ReadOnlyClient &client, const std::function<void(const Json &)> &update,
                 const std::function<void()> &progress = {}, bool configuration = false)
{
    NodeMap nodes;
    const auto get = [&](const std::string &path) -> const Json & {
        if (nodes.size() >= 16384)
            throw std::runtime_error("Apollo node limit exceeded");
        auto value = client.Get(path, [&](const Json &message) {
            ApplyValue(nodes, message);
            if (update)
                update(message);
        });
        if (value.kind != Json::Kind::Object || (!value.Has("properties") && !value.Has("children")))
            throw std::runtime_error("Unknown Apollo node schema");
        const auto &saved = nodes.emplace(path, std::move(value)).first->second;
        if (progress)
            progress();
        return saved;
    };
    get("/"); // Global talkback state; observation only, never routing changes.
    if (configuration)
    {
        const auto catalog = Children(get("/plugins"));
        if (catalog.size() > 512) throw std::runtime_error("Plugin catalog limit exceeded");
        for (const auto &slot : catalog)
            if (NumericSlot(slot)) get("/plugins/" + slot);
    }
    const auto devices = Children(get("/devices"));
    if (devices.size() > 16)
        throw std::runtime_error("Apollo device limit exceeded");
    for (const auto &device : devices)
    {
        const auto devicePath = "/devices/" + device;
        if (!DeviceOnline(get(devicePath)))
            continue;
        for (const std::string group : {"inputs", "auxs", "outputs"})
        {
            const auto groupPath = devicePath + "/" + group;
            const auto children = Children(get(groupPath));
            if (children.size() > 512)
                throw std::runtime_error("Apollo channel limit exceeded");
            for (const auto &child : children)
            {
                const auto path = groupPath + "/" + child;
                const auto &node = get(path);
                const bool monitor = Property(node, "IOType").At("value").String() == "Monitor";
                const bool input = group != "outputs" && Property(node, "Active").At("value").Bool() &&
                                   !Property(node, "ChannelHidden").At("value").Bool();
                if (input)
                {
                    const auto featureChildren = node.At("children");
                    for (const std::string feature : {"sends", "preamps", "effects"})
                    {
                        if (!featureChildren.Has(feature))
                            continue;
                        const auto featurePath = path + "/" + feature;
                        const auto slots = Children(get(featurePath));
                        if (slots.size() > (feature == "preamps" ? 2U : 16U))
                            throw std::runtime_error("Unknown Apollo channel feature layout");
                        for (const auto &slot : slots)
                        {
                            if (!NumericSlot(slot))
                                throw std::runtime_error("Invalid Apollo feature slot");
                            const auto itemPath = featurePath + "/" + slot;
                            const auto &item = get(itemPath);
                            if (feature == "preamps" && item.At("children").Has("effects"))
                            {
                                const auto unison = Children(get(itemPath + "/effects"));
                                if (unison.size() > 2)
                                    throw std::runtime_error("Unknown preamp effect layout");
                                for (const auto &fx : unison)
                                    if (NumericSlot(fx))
                                    {
                                        const auto effectPath = itemPath + "/effects/" + fx;
                                        const auto &effect = get(effectPath);
                                        if (effect.At("children").Has("parameters") &&
                                            Property(effect, "EffectInstance").At("value").scalar != "0")
                                        {
                                            const auto parameters = Children(get(effectPath + "/parameters"));
                                            if (parameters.size() > 256)
                                                throw std::runtime_error("UNISON parameter limit exceeded");
                                            for (const auto &parameter : parameters)
                                                if (NumericSlot(parameter))
                                                    get(effectPath + "/parameters/" + parameter);
                                        }
                                    }
                            }
                            if (feature != "effects" || !item.At("children").Has("parameters") ||
                                Property(item, "EffectInstance").At("value").scalar == "0")
                                continue;
                            const auto parameters = Children(get(itemPath + "/parameters"));
                            if (parameters.size() > 256)
                                throw std::runtime_error("Apollo plug-in parameter limit exceeded");
                            for (const auto &parameter : parameters)
                                if (NumericSlot(parameter))
                                    get(itemPath + "/parameters/" + parameter);
                        }
                    }
                }
                if ((monitor || input) && node.At("children").Has("meters"))
                {
                    const auto meters = Children(get(path + "/meters"));
                    if (meters.size() > 16)
                        throw std::runtime_error("Unknown Apollo meter layout");
                    for (const auto &meter : meters)
                        get(path + "/meters/" + meter);
                }
            }
        }
    }
    return nodes;
}
} // namespace
Observer::~Observer()
{
    Stop();
}
void Observer::EnableConfiguration()
{
    if (thread_.joinable()) throw std::logic_error("Configure observation before Start");
    configuration_ = true;
}
void Observer::Start()
{
    if (thread_.joinable())
        return;
    stop_ = false;
    thread_ = std::thread([this] { Run(); });
}
void Observer::SetNotification(std::function<void()> notify)
{
    if (thread_.joinable()) throw std::logic_error("Set notification before Start");
    notify_ = std::move(notify);
}
void Observer::Stop()
{
    stop_ = true;
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}
Snapshot Observer::Latest() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}
void Observer::Publish(Snapshot snapshot)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_ = std::move(snapshot);
    }
    if (notify_) notify_();
}
void Observer::Run()
{
    uint64_t generation = 0;
    while (!stop_)
    {
        try
        {
            ReadOnlyClient client(stop_, port_);
            client.Connect();
            NodeMap nodes = Discover(client, {}, {}, configuration_);
            const auto identity = BuildConfiguration(nodes)->systemIdentity;
            ++generation;
            std::set<std::string> subscribed;
            bool refreshRequested = false;
            uint64_t revision = 1;
            uint64_t controlRevision = 0;
            Snapshot state;
            bool modelDirty = true, pending = true;
            auto receivedAt = Clock::now();
            const auto publish = [&] {
                if (!pending)
                    return;
                if (modelDirty)
                {
                    state = BuildSnapshot(nodes);
                    ++controlRevision;
                }
                modelDirty = false;
                state.connected = true;
                state.generation = generation;
                state.metadataRevision = revision;
                state.controlRevision = controlRevision;
                state.receivedFrames = client.Frames();
                state.receivedAt = receivedAt;
                state.status = state.onlineDevices ? "Read-only connection" : "No online Apollo";
                Publish(state);
                pending = false;
            };
            const auto update = [&](const Json &message) {
                receivedAt = Clock::now();
                const auto path = message.At("path").String();
                for (const std::string property :
                     {"EffectInstance", "EffectName", "IOType", "Stereo", "Active", "DeviceOnline", "48V",
                      "HiZ", "PGADisabledInLineMode"})
                {
                    const auto suffix = "/" + property + "/value";
                    if (path.size() <= suffix.size() ||
                        path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0)
                        continue;
                    const auto node = nodes.find(path.substr(0, path.size() - suffix.size()));
                    if (node != nodes.end() &&
                        Property(node->second, property).At("value").scalar != message.At("data").scalar)
                        refreshRequested = true;
                }
                if (ApplyValue(nodes, message))
                {
                    if (!modelDirty && !ApplyMeterFeedback(state, message)) modelDirty = true;
                    pending = true;
                }
            };
            const auto subscribe = [&] {
                for (const auto &path : SubscriptionPaths(nodes))
                    if (subscribed.insert(path).second)
                    {
                        client.Subscribe(path);
                        // Drain while subscribing so a fast meter stream cannot
                        // fill the engine's send buffer during initialization.
                        for (int i = 0; i < 4; ++i)
                        {
                            auto value = client.Poll(0);
                            if (!value)
                                break;
                            update(*value);
                        }
                    }
                if (subscribed.size() > 65536)
                    throw std::runtime_error("Apollo subscription lifetime limit exceeded");
            };
            subscribe();
            publish();
            auto refreshAt = Clock::now() + std::chrono::seconds(10);
            auto heartbeatAt = Clock::now() + std::chrono::seconds(1);
            while (!stop_)
            {
                if (renew_.exchange(false))
                    break;
                if (auto message = client.Poll(20))
                {
                    update(*message);
                    // Consume already available frames, never wait to fill a
                    // batch. Bound draining so a continuous stream cannot
                    // starve publication or lifecycle checks.
                    const auto drainUntil = Clock::now() + std::chrono::milliseconds(2);
                    for (int n = 0; n < 255 && Clock::now() < drainUntil; ++n)
                    {
                        auto next = client.Poll(0);
                        if (!next) break;
                        update(*next);
                    }
                    publish();
                }
                const auto now = Clock::now();
                if (now >= heartbeatAt)
                {
                    const auto root = client.Get("/devices", update);
                    receivedAt = Clock::now();
                    pending = true;
                    if (Children(root) != Children(nodes.at("/devices")))
                        refreshAt = now;
                    heartbeatAt = Clock::now() + std::chrono::seconds(1);
                }
                const bool explicitRefresh = refresh_.exchange(false);
                if (now >= refreshAt || refreshRequested || explicitRefresh)
                {
                    refreshRequested = false;
                    // Fresh discovery invalidates stale per-property metadata.
                    // No model is rebuilt from a partial/failed discovery.
                    auto fresh = Discover(client, update, [&] {
                        receivedAt = Clock::now();
                        pending = true;
                        publish();
                    }, configuration_);
                    // Paths can survive a driver/device reconstruction while
                    // the engine's subscription objects do not. A new socket
                    // gives every path a fresh subscription exactly once.
                    if (BuildConfiguration(fresh)->systemIdentity != identity)
                        break;
                    nodes = std::move(fresh);
                    modelDirty = pending = true;
                    ++revision;
                    subscribe();
                    refreshAt = Clock::now() + std::chrono::seconds(10);
                }
                publish();
            }
            // Planned subscription renewal is immediate, not an error retry.
            // The next snapshot carries a fresh connection generation.
            if (!stop_)
                continue;
        }
        catch (const std::exception &error)
        {
            Snapshot disconnected;
            disconnected.generation = generation;
            disconnected.status = stop_ ? "Stopped" : error.what();
            // Never retain believable faders/meters after loss of the engine.
            Publish(std::move(disconnected));
        }
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::seconds(3), [this] { return stop_.load(); });
    }
}
} // namespace apollo
