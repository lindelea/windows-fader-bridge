#pragma once
#include "Json.h"
#include "Protocol.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>

namespace apollo
{
class ReadOnlyClient
{
  public:
    // Alternate loopback port is for isolated transport fixtures; the product
    // always uses 4710 and exposes neither a host nor port setting.
    explicit ReadOnlyClient(const std::atomic<bool> &stop, uint16_t loopbackPort = 4710);
    ~ReadOnlyClient();
    ReadOnlyClient(const ReadOnlyClient &) = delete;
    ReadOnlyClient &operator=(const ReadOnlyClient &) = delete;
    void Connect();
    void Close() noexcept;
    Json Get(const std::string &path, const std::function<void(const Json &)> &update = {});
    void Subscribe(const std::string &path);
    std::optional<Json> Poll(int milliseconds);
    uint64_t Frames() const
    {
        return frames_;
    }
    uint64_t ReadsSent() const
    {
        return reads_;
    }

  private:
    friend class ChannelWriteClient;
    friend class MonitorWriteClient;
    friend class ConfigWriteClient;
    void SendBytes(const std::string &command);
    void SendRead(std::string_view verb, const std::string &path);
    bool Wait(bool writing, int milliseconds);
    void CheckStop() const;
    const std::atomic<bool> &stop_;
    const uint16_t port_;
    uintptr_t socket_ = ~uintptr_t{0};
    bool winsock_ = false;
    FrameDecoder decoder_;
    std::deque<Json> inbox_;
    uint64_t frames_ = 0, reads_ = 0;
    std::chrono::steady_clock::time_point partialSince_{};
};
} // namespace apollo
