#pragma once
#include "Model.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

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
    Snapshot Latest() const;
    void Refresh()
    {
        refresh_ = true;
    }

  private:
    void Run();
    void Publish(Snapshot snapshot);
    const uint16_t port_;
    std::atomic<bool> stop_ = false;
    std::atomic<bool> refresh_ = false;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Snapshot latest_;
};
} // namespace apollo
