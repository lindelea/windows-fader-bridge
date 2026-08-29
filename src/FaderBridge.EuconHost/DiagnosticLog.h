#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

class DiagnosticLog final
{
public:
    static DiagnosticLog& Instance();

    void Start();
    void Stop();
    void Write(const char* format, ...);
    const std::wstring& Path() const noexcept { return path_; }

private:
    DiagnosticLog() = default;
    ~DiagnosticLog();
    void Run();

    std::atomic_bool running_ = false;
    std::chrono::steady_clock::time_point start_{};
    std::wstring path_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::string> queue_;
    std::thread worker_;
};

#define FB_TRACE(...) DiagnosticLog::Instance().Write(__VA_ARGS__)
