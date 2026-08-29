#include "DiagnosticLog.h"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <filesystem>

DiagnosticLog& DiagnosticLog::Instance()
{
    static DiagnosticLog instance;
    return instance;
}

DiagnosticLog::~DiagnosticLog()
{
    Stop();
}

void DiagnosticLog::Start()
{
    if (running_.exchange(true))
    {
        return;
    }

    wchar_t modulePath[32768]{};
    const auto length = GetModuleFileNameW(nullptr, modulePath,
        static_cast<DWORD>(std::size(modulePath)));
    const auto directory = length > 0
        ? std::filesystem::path(std::wstring(modulePath, length)).parent_path()
        : std::filesystem::current_path();
    path_ = (directory / L"FaderBridge.trace.log").wstring();
    start_ = std::chrono::steady_clock::now();
    worker_ = std::thread(&DiagnosticLog::Run, this);
    Write("TRACE_START");
}

void DiagnosticLog::Stop()
{
    if (!running_.exchange(false))
    {
        return;
    }
    condition_.notify_all();
    if (worker_.joinable())
    {
        worker_.join();
    }
}

void DiagnosticLog::Write(const char* format, ...)
{
    if (!running_.load(std::memory_order_acquire) || !format)
    {
        return;
    }

    char message[384]{};
    va_list arguments;
    va_start(arguments, format);
    vsnprintf_s(message, std::size(message), _TRUNCATE, format, arguments);
    va_end(arguments);

    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_).count();
    char line[512]{};
    sprintf_s(line, std::size(line), "%10lld us T%5lu  %s",
        static_cast<long long>(elapsed), GetCurrentThreadId(), message);

    {
        const std::scoped_lock lock(mutex_);
        if (queue_.size() >= 8192U)
        {
            queue_.pop_front();
        }
        queue_.emplace_back(line);
    }
    condition_.notify_one();
}

void DiagnosticLog::Run()
{
    std::ofstream output(std::filesystem::path(path_),
        std::ios::out | std::ios::trunc | std::ios::binary);
    std::deque<std::string> pending;
    while (running_.load(std::memory_order_acquire))
    {
        {
            std::unique_lock lock(mutex_);
            condition_.wait_for(lock, std::chrono::milliseconds(100), [this]
            {
                return !queue_.empty() || !running_.load(std::memory_order_acquire);
            });
            pending.swap(queue_);
        }
        for (const auto& line : pending)
        {
            output << line << '\n';
        }
        pending.clear();
        output.flush();
    }

    {
        const std::scoped_lock lock(mutex_);
        pending.swap(queue_);
    }
    for (const auto& line : pending)
    {
        output << line << '\n';
    }
    output.flush();
}
