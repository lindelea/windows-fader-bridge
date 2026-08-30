#include "MackieMedia.h"
#include "WindowsCommandExecutor.h"
#include "DiagnosticLog.h"
#include <Roapi.h>

MackieMedia::MackieMedia() : worker_(&MackieMedia::Run, this) {}
MackieMedia::~MackieMedia()
{
    { const std::scoped_lock lock(mutex_); running_ = false; }
    wake_.notify_all();
    worker_.join();
}
void MackieMedia::Target(const std::wstring& path, const std::wstring& package)
{
    const std::scoped_lock lock(mutex_);
    if (path_ == path && package_ == package) return;
    path_ = path; package_ = package; state_ = {};
    wake_.notify_one();
}
void MackieMedia::Request(mackie::ActionKind kind, float value)
{
    const std::scoped_lock lock(mutex_);
    if (requests_.size() < 128) requests_.push_back({kind, value, path_, package_});
    wake_.notify_one();
}
WindowsMediaState MackieMedia::State(std::wstring& status)
{
    const std::scoped_lock lock(mutex_);
    status = status_;
    return state_;
}
void MackieMedia::Run()
{
    const auto initialized = RoInitialize(RO_INIT_MULTITHREADED);
    {
        WindowsMediaController media;
        media.Initialize();
        for (;;)
        {
            std::deque<RequestItem> requests;
            std::wstring path, package;
            {
                std::unique_lock lock(mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(200), [&] { return !running_ || !requests_.empty(); });
                if (!running_) break;
                requests.swap(requests_); path = path_; package = package_;
            }
            std::wstring status;
            for (const auto& request : requests)
            {
                const auto current = media.GetState(request.path, request.package);
                MediaControlAction action = MediaControlAction::PlayPause;
                float value = 0;
                bool supported = false;
                using K = mackie::ActionKind;
                switch (request.kind)
                {
                case K::PlayPause: supported = current.canPlayPause; value = current.playing ? 0.F : 1.F; break;
                case K::Stop:
                    action = current.canStop ? MediaControlAction::Stop : MediaControlAction::PlayPause;
                    supported = current.canStop || (current.canPlayPause && current.playing); break;
                case K::Previous: action = MediaControlAction::Previous; supported = current.canPrevious; break;
                case K::Next: action = MediaControlAction::Next; supported = current.canNext; break;
                case K::Repeat: action = MediaControlAction::Repeat; supported = current.canRepeat; value = current.repeatMode ? 0.F : 1.F; break;
                case K::Seek: action = MediaControlAction::Seek; supported = current.canSeek && current.hasPosition; value = std::clamp(current.position + request.value, 0.F, 1.F); break;
                default: break;
                }
                bool accepted = supported && media.Execute(request.path, request.package, action, value);
                // Legacy players without GSMTC still receive standard system media
                // keys. No fake play LED/title is generated for this fallback.
                if (!current.available)
                {
                    bool fallback = true;
                    WindowsCommand command{};
                    switch (request.kind)
                    {
                    case K::PlayPause: command = WindowsCommand::MediaPlayPause; break;
                    case K::Stop: command = WindowsCommand::MediaStop; break;
                    case K::Previous: command = WindowsCommand::MediaPrevious; break;
                    case K::Next: command = WindowsCommand::MediaNext; break;
                    default: fallback = false; break;
                    }
                    if (fallback)
                    {
                        accepted = WindowsCommandExecutor::Execute(command);
                        status = L"已发送系统媒体键；目标播放器及状态无法保证";
                    }
                }
                if (status.empty()) status = accepted ? L"媒体请求已发送，LED 等待播放器实际状态" : L"此播放器未提供所请求的控制能力";
            }
            auto latest = media.GetState(path, package);
            {
                const std::scoped_lock lock(mutex_);
                if (path == path_ && package == package_) state_ = std::move(latest);
                if (!status.empty()) status_ = std::move(status);
            }
        }
    }
    if (SUCCEEDED(initialized)) RoUninitialize();
}
