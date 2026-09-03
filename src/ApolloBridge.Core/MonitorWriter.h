#pragma once
#include "MonitorControl.h"
#include "ReadOnlyClient.h"
#include "Observer.h"

namespace apollo
{
class MonitorWriteClient
{
  public:
    explicit MonitorWriteClient(const std::atomic<bool> &stop, uint16_t port = 4710) : client_(stop, port)
    {
    }
    void Connect()
    {
        client_.Connect();
    }
    // Re-read identity/capabilities immediately before a typed write. A cancelled
    // request sends no set. No subscriptions share this command connection.
    std::optional<Json> Apply(const MonitorRequest &request, const std::function<bool(const Monitor &)> &authorize);
    // Control-room mixing is a live control surface path, not a configuration
    // transaction. Final state is reconciled by the observer subscription.
    std::optional<Json> ApplyRealtime(const MonitorRequest &request,
                                      const std::function<bool(const Monitor &)> &authorize);
    void CheckRealtimeReplies()
    {
        DrainReplies(0);
    }

  private:
    void DrainReplies(int milliseconds);
    ReadOnlyClient client_;
};
struct MonitorStatus
{
    uint64_t epoch = 0, confirmed = 0;
    std::string key, name, error, lastOperation;
    double ceiling = -144;
    size_t pending = 0;
};
class MonitorController
{
  public:
    explicit MonitorController(Observer &observer, uint16_t port = 4710);
    ~MonitorController();
    void Arm(const std::string &key, std::optional<double> ceiling = std::nullopt);
    void Disarm();
    void Validate();
    uint64_t Epoch() const
    {
        return epoch_.load();
    }
    bool Submit(const std::string &key, MonitorField field, const Json &value, uint64_t epoch);
    MonitorStatus Status() const;
    Monitor Feedback(Monitor monitor);

  private:
    struct Pending
    {
        uint64_t sequence;
        Json value;
        bool confirmed = false;
        std::chrono::steady_clock::time_point at;
    };
    void Fail(const std::string &message);
    void Run();
    Observer &observer_;
    const uint16_t port_;
    std::atomic<bool> stop_ = false;
    std::atomic<uint64_t> epoch_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    MonitorQueue queue_;
    std::map<MonitorField, Pending> pending_;
    std::map<MonitorField, std::chrono::steady_clock::time_point> lastDispatch_;
    std::string error_, lastOperation_;
    uint64_t confirmed_ = 0;
    std::thread worker_;
};
} // namespace apollo
