#pragma once
#include "Configuration.h"
#include "Observer.h"
#include "ReadOnlyClient.h"

namespace apollo
{
struct ConfigRequest
{
    ConfigSetting setting;
    ConfigOption option;
    uint64_t epoch = 0, generation = 0;
    std::chrono::steady_clock::time_point created;
};
inline bool ConfigOptionAvailable(const ConfigSetting &setting, const ConfigOption &option)
{
    return std::any_of(setting.options.begin(), setting.options.end(), [&](const auto &o) {
        return ConfigValueEqual(o.value, option.value) && o.catalog == option.catalog;
    });
}
inline bool ConfigRequestMatches(const ConfigRequest &request, const ConfigSetting &fresh)
{
    return request.setting.key == fresh.key && request.setting.kind == fresh.kind &&
           request.setting.node == fresh.node && request.setting.property == fresh.property &&
           request.setting.owner == fresh.owner && request.setting.context == fresh.context &&
           ConfigValueEqual(request.setting.value, fresh.value) &&
           ConfigOptionAvailable(fresh, request.option);
}
class ConfigWriteClient
{
  public:
    ConfigWriteClient(const std::atomic<bool> &stop, uint16_t port = 4710) : client_(stop, port)
    {
    }
    bool Apply(const ConfigRequest &request, const std::function<bool()> &authorize)
    {
        if (request.setting.reads.size() > 64 || !IsPath(request.setting.node))
            throw std::invalid_argument("Invalid configuration request");
        client_.Connect();
        NodeMap nodes;
        for (const auto &path : request.setting.reads)
            nodes[path] = client_.Get(path);
        if (!request.option.catalog.empty())
            nodes[request.option.catalog] = client_.Get(request.option.catalog);
        const auto state = BuildConfiguration(nodes);
        const auto *fresh = FindConfig(state, request.setting.key);
        if (!fresh || !ConfigRequestMatches(request, *fresh))
            throw std::runtime_error("Config changed or choice unavailable; select again");
        // The path comes only from the rebuilt screenshot allowlist, never from display text.
        const auto command = "set " + fresh->key + "/value " + ConfigScalar(request.option.value) + '\0';
        if (std::chrono::steady_clock::now() - request.created > std::chrono::seconds(2))
            throw std::runtime_error("Config confirmation expired; nothing sent");
        if (!authorize || !authorize())
            return false;
        client_.SendBytes(command);
        const auto after = client_.Get(fresh->node, [](const Json &message) {
            if (message.Has("error"))
                throw std::runtime_error("Apollo rejected Config write; not retried");
        });
        if (!ConfigValueEqual(Property(after, fresh->property).At("value"), request.option.value))
            throw std::runtime_error("Config readback unconfirmed; inspect Console before retrying");
        if (fresh->kind == ConfigKind::Plugin && !request.option.value.scalar.empty() &&
            (Property(after, "EffectInstance").At("value").scalar.empty() ||
             Property(after, "EffectInstance").At("value").scalar == "0"))
            throw std::runtime_error("Plug-in did not instantiate; inspect Console/DSP status");
        if (fresh->kind == ConfigKind::Plugin && request.option.value.scalar.empty() &&
            Property(after, "EffectInstance").At("value").scalar != "0")
            throw std::runtime_error("Plug-in removal unconfirmed; not retried");
        const auto deviceList = client_.Get("/devices");
        if (Children(deviceList) != Children(ConfigNode(nodes, "/devices")))
            throw std::runtime_error("Device topology changed during Config write; inspect Console");
        for (const auto &entry : nodes)
            if (entry.first.rfind("/devices/", 0) == 0 && NumericSlot(entry.first.substr(9)))
            {
                const auto device = client_.Get(entry.first);
                if (DeviceOnline(device) != DeviceOnline(entry.second) ||
                    !ConfigValueEqual(Property(device, "DeviceHwID").At("value"),
                                      Property(entry.second, "DeviceHwID").At("value")))
                    throw std::runtime_error("Device identity changed during Config write; inspect Console");
            }
        return true;
    }

  private:
    ReadOnlyClient client_;
};
struct ConfigStatus
{
    uint64_t epoch = 0, confirmed = 0;
    bool busy = false;
    std::string message;
};
class ConfigController
{
  public:
    explicit ConfigController(Observer &observer, uint16_t port = 4710)
        : observer_(observer), port_(port), worker_([this] { Run(); })
    {
    }
    ~ConfigController()
    {
        Disarm();
        stop_ = true;
        wake_.notify_all();
        if (worker_.joinable())
            worker_.join();
    }
    uint64_t Epoch() const
    {
        return epoch_.load();
    }
    void Arm()
    {
        const auto s = observer_.Latest();
        if (!Fresh(s) || !s.configuration || s.configuration->settings.empty())
            throw std::runtime_error("No fresh configuration available");
        std::lock_guard<std::mutex> lock(mutex_);
        if (busy_)
            throw std::runtime_error("Wait for the current Config operation");
        generation_ = s.generation;
        system_ = s.configuration->systemIdentity;
        epoch_ = ++serial_;
        message_.clear();
    }
    void Disarm()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        epoch_ = 0;
        if (pending_)
            busy_ = false;
        pending_.reset();
    }
    ConfigStatus Status() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {epoch_.load(), confirmed_, busy_, message_};
    }
    bool Submit(const ConfigSetting &setting, size_t choice, uint64_t epoch)
    {
        const auto snapshot = observer_.Latest();
        std::lock_guard<std::mutex> lock(mutex_);
        if (!epoch || epoch != epoch_ || !Authorized(snapshot) || busy_ || pending_ ||
            choice >= setting.options.size())
            return false;
        ConfigRequest request{setting, setting.options[choice], epoch, generation_,
                              std::chrono::steady_clock::now()};
        const auto *fresh = FindConfig(snapshot.configuration, setting.key);
        if (!fresh || !ConfigRequestMatches(request, *fresh))
        {
            message_ = "Config changed; select again";
            return false;
        }
        if (ConfigValueEqual(setting.value, request.option.value) && setting.kind != ConfigKind::Preset)
            return false;
        pending_ = std::move(request);
        busy_ = true;
        message_ = "CONFIG: APPLYING";
        wake_.notify_all();
        return true;
    }

  private:
    static bool Fresh(const Snapshot &s)
    {
        return s.connected && std::chrono::steady_clock::now() - s.receivedAt < std::chrono::seconds(3);
    }
    bool Authorized(const Snapshot &s) const
    {
        return Fresh(s) && s.generation == generation_ && s.configuration &&
               s.configuration->systemIdentity == system_;
    }
    void Run()
    {
        while (!stop_)
        {
            std::optional<ConfigRequest> request;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(50),
                               [&] { return stop_ || pending_.has_value(); });
                if (stop_)
                    break;
                const auto s = observer_.Latest();
                if (!Authorized(s))
                {
                    epoch_ = 0;
                    pending_.reset();
                    busy_ = false;
                }
                request = std::move(pending_);
                pending_.reset();
            }
            if (!request)
                continue;
            try
            {
                ConfigWriteClient client(stop_, port_);
                const bool applied = client.Apply(*request, [&] {
                    const auto snapshot = observer_.Latest();
                    std::lock_guard<std::mutex> lock(mutex_);
                    return !stop_ && epoch_ == request->epoch && Authorized(snapshot);
                });
                if (applied)
                    observer_.Refresh();
                std::lock_guard<std::mutex> lock(mutex_);
                busy_ = false;
                if (applied)
                {
                    ++confirmed_;
                    message_ =
                        "CONFIG: " + request->setting.label + " = " + request->option.label + " CONFIRMED";
                }
            }
            catch (const std::exception &e)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                busy_ = false;
                epoch_ = 0;
                message_ = e.what(); // Never retry uncertain writes.
            }
        }
    }
    Observer &observer_;
    uint16_t port_;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> epoch_{0};
    uint64_t generation_ = 0, serial_ = 0, confirmed_ = 0;
    bool busy_ = false;
    std::string message_, system_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<ConfigRequest> pending_;
    std::thread worker_;
};
} // namespace apollo
