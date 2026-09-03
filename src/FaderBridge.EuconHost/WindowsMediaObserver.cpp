#include "WindowsMediaObserver.h"

#include <Roapi.h>

WindowsMediaObserver::WindowsMediaObserver() : worker_(&WindowsMediaObserver::Run, this)
{
}

WindowsMediaObserver::~WindowsMediaObserver()
{
    {
        const std::scoped_lock lock(mutex_);
        running_ = false;
    }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
}

WindowsMediaState WindowsMediaObserver::Snapshot() const
{
    const std::scoped_lock lock(mutex_);
    return state_;
}

void WindowsMediaObserver::Run()
{
    const auto initialized = RoInitialize(RO_INIT_MULTITHREADED);
    WindowsMediaController controller;
    const bool ready = controller.Initialize();
    for (;;)
    {
        auto state = ready
            ? controller.GetState(L"", L"", true, true)
            : WindowsMediaState{};
        std::unique_lock lock(mutex_);
        if (!running_) break;
        state_ = std::move(state);
        wake_.wait_for(lock, std::chrono::milliseconds(200), [this] { return !running_; });
        if (!running_) break;
    }
    if (SUCCEEDED(initialized)) RoUninitialize();
}
