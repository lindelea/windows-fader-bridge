#pragma once
#include "Model.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <functional>

namespace apollo
{
// Owns all UA sockets on one worker. Published snapshots never expose raw trees,
// serial numbers, plugin state blobs or mutable transport objects to a surface.
class Observer
{
  public:
    explicit Observer(uint16_t loopbackPort = 4710) : port_(loopbackPort)
    {
    }
    ~Observer();
    void Start();
    void Stop();
    void EnableConfiguration(); // Before Start only; opt-in catalog discovery.
    void SetNotification(std::function<void()> notify); // Before Start; called outside snapshot lock.
    Snapshot Latest() const;
    void Refresh()
    {
        refresh_ = true;
    }
    // Recreate the observation socket and subscriptions after device recovery.
    void RenewSubscriptions() { renew_ = true; }

  private:
    void Run();
    void Publish(Snapshot snapshot);
    const uint16_t port_;
    bool configuration_ = false;
    std::atomic<bool> stop_ = false;
    std::atomic<bool> refresh_ = false;
    std::atomic<bool> renew_ = false;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Snapshot latest_;
    std::function<void()> notify_;
};
} // namespace apollo
