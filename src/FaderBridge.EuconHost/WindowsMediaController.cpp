#include "WindowsMediaController.h"

#include "DiagnosticLog.h"

#include <Windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <type_traits>
#include <unordered_map>

namespace
{
using MediaSession = winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSession;

std::wstring Lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](const wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return value;
}

int MatchScore(const std::wstring& sourceAppId,
    const std::wstring& executablePath, const std::wstring& packageFamilyName)
{
    const auto source = Lower(sourceAppId);
    if (!packageFamilyName.empty())
    {
        const auto package = Lower(packageFamilyName);
        if (source.find(package) != std::wstring::npos)
        {
            return 100;
        }
    }

    if (!executablePath.empty())
    {
        const std::filesystem::path path(executablePath);
        const auto filename = Lower(path.filename().wstring());
        const auto stem = Lower(path.stem().wstring());
        if (!filename.empty() && source.find(filename) != std::wstring::npos)
        {
            return 80;
        }
        if (stem.size() >= 4U && source.find(stem) != std::wstring::npos)
        {
            return 60;
        }
    }
    return 0;
}

std::uintptr_t SessionKey(const MediaSession& session)
{
    return reinterpret_cast<std::uintptr_t>(winrt::get_abi(session));
}
}

struct WindowsMediaController::Impl
{
    struct MetadataEntry
    {
        std::wstring title;
        std::wstring artist;
        std::chrono::steady_clock::time_point lastRequest{};
        bool pending = false;
    };

    struct SharedCache
    {
        std::mutex mutex;
        std::unordered_map<std::uintptr_t, MetadataEntry> metadata;
        std::unordered_map<std::wstring, unsigned int> capabilities;
    };

    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager
        manager{ nullptr };
    std::shared_ptr<SharedCache> cache = std::make_shared<SharedCache>();

    MediaSession Find(const std::wstring& executablePath,
        const std::wstring& packageFamilyName) const
    {
        if (!manager)
        {
            return nullptr;
        }
        MediaSession best{ nullptr };
        int bestScore = 0;
        const auto current = manager.GetCurrentSession();
        for (const auto& session : manager.GetSessions())
        {
            const auto source = std::wstring(session.SourceAppUserModelId());
            auto score = MatchScore(source, executablePath, packageFamilyName) * 100;
            if (score == 0) continue;
            if (current && SessionKey(session) == SessionKey(current)) score += 50;
            try
            {
                const auto status = session.GetPlaybackInfo().PlaybackStatus();
                using Status = winrt::Windows::Media::Control::
                    GlobalSystemMediaTransportControlsSessionPlaybackStatus;
                if (status == Status::Playing) score += 30;
                else if (status == Status::Paused) score += 10;
            }
            catch (const winrt::hresult_error&)
            {
                // A session can disappear between GetSessions and inspection.
                continue;
            }
            if (score > bestScore)
            {
                best = session;
                bestScore = score;
            }
        }
        return best;
    }

    void ReadMetadata(const MediaSession& session, std::wstring& title,
        std::wstring& artist) const
    {
        const auto key = SessionKey(session);
        const auto now = std::chrono::steady_clock::now();
        bool request = false;
        {
            const std::scoped_lock lock(cache->mutex);
            auto& entry = cache->metadata[key];
            title = entry.title;
            artist = entry.artist;
            request = !entry.pending && now - entry.lastRequest >= std::chrono::seconds(1);
            if (request)
            {
                entry.pending = true;
                entry.lastRequest = now;
            }
        }
        if (!request) return;

        try
        {
            const auto sharedCache = cache;
            auto operation = session.TryGetMediaPropertiesAsync();
            operation.Completed([sharedCache, key](const auto& completed,
                const winrt::Windows::Foundation::AsyncStatus status) noexcept
            {
                std::wstring newTitle;
                std::wstring newArtist;
                try
                {
                    if (status == winrt::Windows::Foundation::AsyncStatus::Completed)
                    {
                        const auto properties = completed.GetResults();
                        if (properties)
                        {
                            newTitle = properties.Title();
                            newArtist = properties.Artist();
                        }
                    }
                }
                catch (const winrt::hresult_error& error)
                {
                    FB_TRACE("MEDIA_METADATA hr=%08X", static_cast<unsigned>(error.code()));
                }
                const std::scoped_lock lock(sharedCache->mutex);
                auto& entry = sharedCache->metadata[key];
                entry.pending = false;
                if (status == winrt::Windows::Foundation::AsyncStatus::Completed)
                {
                    entry.title = std::move(newTitle);
                    entry.artist = std::move(newArtist);
                }
            });
        }
        catch (const winrt::hresult_error& error)
        {
            const std::scoped_lock lock(cache->mutex);
            cache->metadata[key].pending = false;
            FB_TRACE("MEDIA_METADATA_START hr=%08X", static_cast<unsigned>(error.code()));
        }
    }

    void TraceCapabilities(const WindowsMediaState& state) const
    {
        unsigned int bits = 0U;
        bits |= state.canPlayPause ? 1U << 0U : 0U;
        bits |= state.canPrevious ? 1U << 1U : 0U;
        bits |= state.canNext ? 1U << 2U : 0U;
        bits |= state.canStop ? 1U << 3U : 0U;
        bits |= state.canSeek ? 1U << 4U : 0U;
        bits |= state.canShuffle ? 1U << 5U : 0U;
        bits |= state.canRepeat ? 1U << 6U : 0U;
        bits |= state.hasPosition ? 1U << 7U : 0U;
        bool changed = false;
        {
            const std::scoped_lock lock(cache->mutex);
            auto [iterator, inserted] = cache->capabilities.emplace(state.sourceAppId, bits);
            changed = inserted || iterator->second != bits;
            iterator->second = bits;
        }
        if (changed)
        {
            FB_TRACE("MEDIA_CAPS source=%ls bits=%02X play=%d prev=%d next=%d stop=%d position=%d seek=%d shuffle=%d repeat=%d",
                state.sourceAppId.c_str(), bits, state.canPlayPause ? 1 : 0,
                state.canPrevious ? 1 : 0, state.canNext ? 1 : 0,
                state.canStop ? 1 : 0, state.hasPosition ? 1 : 0,
                state.canSeek ? 1 : 0,
                state.canShuffle ? 1 : 0, state.canRepeat ? 1 : 0);
        }
    }
};

WindowsMediaController::WindowsMediaController() : impl_(std::make_unique<Impl>())
{
}

WindowsMediaController::~WindowsMediaController() = default;

bool WindowsMediaController::Initialize()
{
    try
    {
        impl_->manager = winrt::Windows::Media::Control::
            GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        FB_TRACE("MEDIA_MANAGER ready=1");
        return static_cast<bool>(impl_->manager);
    }
    catch (const winrt::hresult_error& error)
    {
        FB_TRACE("MEDIA_MANAGER ready=0 hr=%08X", static_cast<unsigned>(error.code()));
        return false;
    }
}

WindowsMediaState WindowsMediaController::GetState(
    const std::wstring& executablePath, const std::wstring& packageFamilyName) const
{
    WindowsMediaState state;
    try
    {
        const auto session = impl_->Find(executablePath, packageFamilyName);
        if (!session)
        {
            return state;
        }
        state.available = true;
        state.sourceAppId = session.SourceAppUserModelId();
        const auto playback = session.GetPlaybackInfo();
        const auto controls = playback.Controls();
        state.playing = playback.PlaybackStatus() == winrt::Windows::Media::Control::
            GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        state.canPlayPause = controls.IsPlayPauseToggleEnabled() ||
            controls.IsPlayEnabled() || controls.IsPauseEnabled();
        state.canPrevious = controls.IsPreviousEnabled();
        state.canNext = controls.IsNextEnabled();
        state.canStop = controls.IsStopEnabled();
        state.canSeek = controls.IsPlaybackPositionEnabled();
        state.canShuffle = controls.IsShuffleEnabled();
        if (const auto shuffle = playback.IsShuffleActive())
        {
            state.shuffle = shuffle.Value();
        }
        state.canRepeat = controls.IsRepeatEnabled();
        if (const auto repeat = playback.AutoRepeatMode())
        {
            state.repeatMode = std::clamp(static_cast<int>(repeat.Value()), 0, 2);
        }
        const auto timeline = session.GetTimelineProperties();
        const auto start = timeline.MinSeekTime();
        const auto end = timeline.MaxSeekTime();
        if (end > start)
        {
            state.hasPosition = true;
            const auto position = std::clamp(timeline.Position(), start, end);
            state.position = static_cast<float>(
                static_cast<double>((position - start).count()) /
                static_cast<double>((end - start).count()));
        }
        impl_->ReadMetadata(session, state.title, state.artist);
        impl_->TraceCapabilities(state);
    }
    catch (const winrt::hresult_error& error)
    {
        FB_TRACE("MEDIA_STATE hr=%08X", static_cast<unsigned>(error.code()));
    }
    return state;
}

bool WindowsMediaController::Execute(const std::wstring& executablePath,
    const std::wstring& packageFamilyName, const MediaControlAction action,
    const float value) const
{
    try
    {
        const auto session = impl_->Find(executablePath, packageFamilyName);
        if (!session)
        {
            FB_TRACE("MEDIA_COMMAND action=%d accepted=0 reason=no_session",
                static_cast<int>(action));
            return false;
        }
        const auto playback = session.GetPlaybackInfo();
        const auto controls = playback.Controls();
        bool supported = false;
        switch (action)
        {
        case MediaControlAction::PlayPause:
            supported = controls.IsPlayPauseToggleEnabled() || controls.IsPlayEnabled() ||
                controls.IsPauseEnabled();
            if (supported)
            {
                if (value != 0.0F && controls.IsPlayEnabled())
                    (void)session.TryPlayAsync();
                else if (value == 0.0F && controls.IsPauseEnabled())
                    (void)session.TryPauseAsync();
                else
                    (void)session.TryTogglePlayPauseAsync();
            }
            break;
        case MediaControlAction::Previous:
            supported = controls.IsPreviousEnabled();
            if (supported) (void)session.TrySkipPreviousAsync();
            break;
        case MediaControlAction::Next:
            supported = controls.IsNextEnabled();
            if (supported) (void)session.TrySkipNextAsync();
            break;
        case MediaControlAction::Stop:
            supported = controls.IsStopEnabled();
            if (supported) (void)session.TryStopAsync();
            break;
        case MediaControlAction::Seek:
        {
            supported = controls.IsPlaybackPositionEnabled();
            if (supported)
            {
                const auto timeline = session.GetTimelineProperties();
                const auto start = timeline.MinSeekTime();
                const auto end = timeline.MaxSeekTime();
                if (end > start)
                {
                    const auto span = end - start;
                    using Span = std::remove_cv_t<decltype(span)>;
                    const auto requested = start + Span(static_cast<Span::rep>(
                        static_cast<double>(span.count()) *
                        std::clamp(value, 0.0F, 1.0F)));
                    (void)session.TryChangePlaybackPositionAsync(requested.count());
                }
            }
            break;
        }
        case MediaControlAction::Shuffle:
            supported = controls.IsShuffleEnabled();
            if (supported) (void)session.TryChangeShuffleActiveAsync(value != 0.0F);
            break;
        case MediaControlAction::Repeat:
            supported = controls.IsRepeatEnabled();
            if (supported)
            {
                const auto mode = static_cast<winrt::Windows::Media::
                    MediaPlaybackAutoRepeatMode>(std::clamp(static_cast<int>(value), 0, 2));
                (void)session.TryChangeAutoRepeatModeAsync(mode);
            }
            break;
        }
        FB_TRACE("MEDIA_COMMAND action=%d value=%.3f supported=%d source=%ls",
            static_cast<int>(action), value, supported ? 1 : 0,
            session.SourceAppUserModelId().c_str());
        return supported;
    }
    catch (const winrt::hresult_error& error)
    {
        FB_TRACE("MEDIA_COMMAND action=%d hr=%08X", static_cast<int>(action),
            static_cast<unsigned>(error.code()));
        return false;
    }
}
