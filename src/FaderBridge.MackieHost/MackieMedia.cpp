#include "MackieMedia.h"
#include "WindowsCommandExecutor.h"
#include "DiagnosticLog.h"
#include "MackieMediaSeek.h"
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
    path_ = path; package_ = package; state_ = {}; status_.clear();
    wake_.notify_one();
}
void MackieMedia::Request(mackie::ActionKind kind, float value, bool allowGlobalFallback)
{
    const std::scoped_lock lock(mutex_);
    if (kind == mackie::ActionKind::SeekSeconds && !requests_.empty() &&
        requests_.back().kind == kind && requests_.back().path == path_ && requests_.back().package == package_ &&
        requests_.back().allowGlobalFallback == allowGlobalFallback)
        requests_.back().value = std::clamp(requests_.back().value + value, -60.F, 60.F);
    else if (requests_.size() < 128) requests_.push_back({kind, value, path_, package_, allowGlobalFallback});
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
                status.clear();
                const auto current = media.GetState(request.path, request.package,
                    request.kind == mackie::ActionKind::SeekSeconds, request.allowGlobalFallback);
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
                case K::SeekSeconds:
                    action = MediaControlAction::Seek;
                    if (const auto target = MackieSeekSeconds(current, request.value)) { supported = true; value = *target; }
                    break;
                default: break;
                }
                bool accepted = supported && media.Execute(current, action, value);
                // Legacy players without GSMTC still receive standard system media
                // keys. No fake play LED/title is generated for this fallback.
                if (!current.available && request.allowGlobalFallback)
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
                if (status.empty())
                {
                    if (accepted) status = L"媒体请求已发送，LED 等待播放器实际状态";
                    else if (!current.available) status = L"没有可控制的播放器；普通 Jog 用于调整播放进度";
                    else if (request.kind == K::SeekSeconds || request.kind == K::Seek)
                        status = current.hasPosition && !current.canSeek ? L"此播放器进度只读，不允许 Jog 调整" :
                            (!supported ? L"此播放器没有提供可调整的播放进度" : L"进度请求未发出；播放器可能已退出");
                    else status = L"此播放器未提供所请求的控制能力";
                }
                FB_TRACE("MACKIE_MEDIA_REQUEST kind=%d value=%.3f available=%d seek=%d position=%d dispatched=%d source=%ls",
                    static_cast<int>(request.kind), request.value, current.available, current.canSeek,
                    current.hasPosition, accepted, current.sourceAppId.c_str());
                // A request belongs to its captured target, not whichever strip
                // the user selected while the worker was processing it.
                const std::scoped_lock lock(mutex_);
                if (request.path == path_ && request.package == package_) status_ = status;
            }
            auto latest = media.GetState(path, package, true, true);
            {
                const std::scoped_lock lock(mutex_);
                if (path == path_ && package == package_) state_ = std::move(latest);
            }
        }
    }
    if (SUCCEEDED(initialized)) RoUninitialize();
}
