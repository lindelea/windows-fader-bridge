#pragma once
#include "ChannelControl.h"
#include "Observer.h"
#include "ReadOnlyClient.h"

namespace apollo
{
class ChannelWriteClient
{
  public:
    explicit ChannelWriteClient(const std::atomic<bool> &stop, uint16_t port = 4710) : client_(stop, port)
    {
    }
    void Connect()
    {
        client_.Connect();
    }
    // Re-read identity/capabilities immediately before a typed write. A cancelled
    // request sends no set. No subscriptions share this command connection.
    std::optional<Json> Apply(const ChannelRequest &request,
                              const std::function<bool(const Channel &)> &authorize);

  private:
    ReadOnlyClient client_;
};
struct ControlStatus
{
    uint64_t epoch = 0, confirmed = 0;
    std::string key, name, error, lastOperation;
    size_t pending = 0;
};
class ChannelController
{
  public:
    explicit ChannelController(Observer &observer, uint16_t port = 4710);
    ~ChannelController();
    void Arm(const std::string &key);
    void ArmAll();
    void Disarm();
    void Disarm(const std::string &key);
    void UnlockSafety(const std::string &key);
    uint64_t Epoch(const std::string &key) const;
    void Validate();
    uint64_t Epoch() const
    {
        return epoch_.load();
    }
    bool Submit(const std::string &key, ChannelAddress field, const Json &value, uint64_t epoch);
    ControlStatus Status() const;
    Channel Feedback(Channel channel);

  private:
    struct Pending
    {
        uint64_t sequence;
        Json value;
        bool confirmed = false;
        std::chrono::steady_clock::time_point at;
    };
    struct Permission
    {
        ChannelQueue queue;
        std::map<ChannelAddress, Pending> pending;
        uint64_t callbackEpoch = 0;
        bool safety = false, writingContext = false;
        std::optional<ChannelRequest> transition;
        std::chrono::steady_clock::time_point transitionAt;
        uint64_t metadataRevision = 0;
    };
    void Fail(const std::string &key, const std::string &message);
    void UpdateEpoch();
    void Run();
    Observer &observer_;
    const uint16_t port_;
    std::atomic<bool> stop_ = false;
    std::atomic<uint64_t> epoch_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::map<std::string, Permission> permissions_;
    uint64_t nextEpoch_ = 0;
    std::string lastKey_;
    std::string error_, lastOperation_;
    uint64_t confirmed_ = 0;
    std::thread worker_;
};
} // namespace apollo
