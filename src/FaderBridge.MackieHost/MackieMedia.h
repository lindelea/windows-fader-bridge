#pragma once
#include "MackieSurface.h"
#include "WindowsMediaController.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

class MackieMedia final
{
public:
    MackieMedia();
    ~MackieMedia();
    void Target(const std::wstring& path, const std::wstring& package);
    void Request(mackie::ActionKind kind, float value = 0, bool allowGlobalFallback = true);
    WindowsMediaState State(std::wstring& status);
private:
    struct RequestItem { mackie::ActionKind kind; float value; std::wstring path, package; bool allowGlobalFallback; };
    void Run();
    std::mutex mutex_;
    std::condition_variable wake_;
    bool running_ = true;
    std::wstring path_, package_, status_;
    WindowsMediaState state_;
    std::deque<RequestItem> requests_;
    std::thread worker_;
};
