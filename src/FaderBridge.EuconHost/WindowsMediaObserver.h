#pragma once

#include "WindowsMediaController.h"

#include <condition_variable>
#include <mutex>
#include <thread>

// Presentation-only system media snapshot. Media discovery stays off the
// EUCON/UI thread so the overview can never add latency to surface callbacks.
class WindowsMediaObserver final
{
public:
    WindowsMediaObserver();
    ~WindowsMediaObserver();

    WindowsMediaObserver(const WindowsMediaObserver&) = delete;
    WindowsMediaObserver& operator=(const WindowsMediaObserver&) = delete;

    WindowsMediaState Snapshot() const;

private:
    void Run();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool running_ = true;
    WindowsMediaState state_;
    std::thread worker_;
};
