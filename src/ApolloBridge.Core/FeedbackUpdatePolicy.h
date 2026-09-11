#pragma once
#include <array>
#include <chrono>
#include <cstdint>

namespace apollo
{
// Only skip unchanged non-meter work. The settling period requests MORE
// updates to preserve optimistic-feedback expiry; it never delays an update.
class FeedbackUpdatePolicy
{
  public:
    using Stamp = std::array<uint64_t, 11>;
    bool FullUpdate(const Stamp &stamp, bool event, bool pending,
                    std::chrono::steady_clock::time_point now)
    {
        const bool changed = !initialized_ || stamp != last_;
        if (changed || event || pending)
            settleUntil_ = now + std::chrono::milliseconds(300);
        last_ = stamp;
        initialized_ = true;
        return changed || event || pending || now < settleUntil_;
    }
  private:
    bool initialized_ = false;
    Stamp last_{};
    std::chrono::steady_clock::time_point settleUntil_{};
};
}
