#include <Windows.h>
#include <appmodel.h>
#include <dwmapi.h>

#include "EuconHost.h"

#include "DiagnosticLog.h"
#include "EuBatchedMeterWriter.h"
#include "EuCon.h"
#include "EuConManager.h"
#include "EuDefinitions.h"
#include "EuconChannel.h"
#include "WindowsCommandProcessor.h"
#include "WindowsMediaController.h"
#include "WindowsSystemProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace
{
constexpr float kVolumeDifference = 0.001F;
constexpr float kPendingMatch = 0.005F;
constexpr float kPanDifference = 0.005F;
constexpr float kPanPendingMatch = 0.015F;
constexpr int kUnityFaderIndex = 728;
constexpr UINT kMotorBurstMergeMs = 10U;
constexpr auto kVolumeHold = std::chrono::milliseconds(180);
constexpr auto kPanHold = std::chrono::milliseconds(120);
constexpr auto kMuteHold = std::chrono::milliseconds(120);

unsigned long long ApplicationIdentityHash(const std::wstring& key)
{
    unsigned long long hash = 14695981039346656037ULL;
    for (const auto value : key)
    {
        hash ^= static_cast<unsigned long long>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::wstring TrackPersistenceId(const std::wstring& key)
{
    // Stable FNV-1a hash: EuControl layouts/assignments can recognize an app
    // after process restarts even when its internal CoreAudio slot changes.
    return L"FaderBridge.WindowsTrack." + std::to_wstring(ApplicationIdentityHash(key));
}

NEuCon::int32 ApplicationChannelColor(const std::wstring& key)
{
    // Project fallback palette. Exclude white and dark greys so every active
    // application is distinct from the uncolored/default surface state.
    static constexpr NEuCon::int32 colors[] =
    {
        0x000000FF, // blue
        0x0000FF00, // green
        0x0000FFFF, // cyan
        0x00FF0000, // red
        0x00FF00FF, // magenta
        0x00FFFF00, // yellow
    };
    return colors[ApplicationIdentityHash(key) % std::size(colors)];
}

NEuCon::int32 EuconTrackType(const AudioStripRole role)
{
    switch (role)
    {
    case AudioStripRole::OutputDevice: return kTRACK_Monitor;
    case AudioStripRole::MasterOutput: return kTRACK_Master;
    case AudioStripRole::InputDevice: return kTRACK_Input;
    default: return kTRACK_Audio;
    }
}

NEuCon::uint32 EuconMeterRole(const AudioMeterRole role)
{
    switch (role)
    {
    case AudioMeterRole::Left: return kMTR_Left;
    case AudioMeterRole::Right: return kMTR_Right;
    case AudioMeterRole::Center: return kMTR_Center;
    case AudioMeterRole::Lfe: return kMTR_LFE;
    case AudioMeterRole::LeftSurround: return kMTR_LeftSurround;
    case AudioMeterRole::RightSurround: return kMTR_RightSurround;
    case AudioMeterRole::LeftBackSurround: return kMTR_LeftBackSurround;
    case AudioMeterRole::RightBackSurround: return kMTR_RightBackSurround;
    case AudioMeterRole::CenterSurround: return kMTR_CenterSurround;
    case AudioMeterRole::LeftCenter: return kMTR_LeftCenter;
    case AudioMeterRole::RightCenter: return kMTR_RightCenter;
    case AudioMeterRole::Top: return kMTR_Top;
    case AudioMeterRole::HeightLeftFront: return kMTR_HeightLeftFront;
    case AudioMeterRole::HeightCenterFront: return kMTR_HeightCenterFront;
    case AudioMeterRole::HeightRightFront: return kMTR_HeightRightFront;
    case AudioMeterRole::HeightLeftSurround: return kMTR_HeightLeftSurround;
    case AudioMeterRole::HeightCenterSurround: return kMTR_HeightCenterSurround;
    case AudioMeterRole::HeightRightSurround: return kMTR_HeightRightSurround;
    default: return kMTR_Mono;
    }
}

std::vector<NEuCon::uint32> EuconMeterRoles(const AudioStripState& strip)
{
    std::vector<NEuCon::uint32> roles;
    roles.reserve(strip.meterRoles.size());
    for (const auto role : strip.meterRoles)
    {
        roles.push_back(EuconMeterRole(role));
    }
    if (roles.empty())
    {
        roles.push_back(kMTR_Mono);
    }
    return roles;
}

bool EqualIdentity(const std::wstring& left, const std::wstring& right)
{
    return !left.empty() && !right.empty() && _wcsicmp(left.c_str(), right.c_str()) == 0;
}

struct WindowIdentity
{
    std::wstring executablePath;
    std::wstring packageFamilyName;
};

WindowIdentity GetWindowProcessIdentity(const DWORD processId)
{
    WindowIdentity result;
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
    {
        return result;
    }

    std::array<wchar_t, 32768> path{};
    DWORD pathLength = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(process, 0, path.data(), &pathLength))
    {
        result.executablePath.assign(path.data(), pathLength);
    }

    UINT32 familyLength = 0U;
    if (GetPackageFamilyName(process, &familyLength, nullptr) == ERROR_INSUFFICIENT_BUFFER &&
        familyLength > 1U)
    {
        result.packageFamilyName.resize(familyLength);
        if (GetPackageFamilyName(process, &familyLength,
                result.packageFamilyName.data()) == ERROR_SUCCESS)
        {
            result.packageFamilyName.resize(familyLength - 1U);
        }
        else
        {
            result.packageFamilyName.clear();
        }
    }
    CloseHandle(process);
    return result;
}

struct WindowSearch
{
    const std::vector<DWORD>* processIds = nullptr;
    const std::wstring* executablePath = nullptr;
    const std::wstring* packageFamilyName = nullptr;
    HWND bestWindow = nullptr;
    DWORD bestProcessId = 0U;
    int bestScore = 0;
};

struct ApplicationWindowState
{
    bool available = false;
    bool foreground = false;
    bool minimized = false;
    bool maximized = false;
    bool topmost = false;
};

BOOL CALLBACK FindApplicationWindow(const HWND window, const LPARAM parameter)
{
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0 ||
        GetWindowTextLengthW(window) == 0)
    {
        return TRUE;
    }

    DWORD cloaked = 0U;
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked,
            sizeof(cloaked))) && cloaked != 0U)
    {
        return TRUE;
    }

    DWORD processId = 0U;
    GetWindowThreadProcessId(window, &processId);
    if (processId == 0U)
    {
        return TRUE;
    }

    int score = 0;
    if (search.processIds && std::find(search.processIds->begin(), search.processIds->end(),
            processId) != search.processIds->end())
    {
        score = 300;
    }
    else
    {
        const auto identity = GetWindowProcessIdentity(processId);
        if (search.packageFamilyName &&
            EqualIdentity(identity.packageFamilyName, *search.packageFamilyName))
        {
            score = 200;
        }
        else if (search.executablePath &&
            EqualIdentity(identity.executablePath, *search.executablePath))
        {
            score = 100;
        }
    }
    if (score > search.bestScore)
    {
        search.bestWindow = window;
        search.bestProcessId = processId;
        search.bestScore = score;
    }
    return TRUE;
}

WindowSearch FindBestApplicationWindow(const std::vector<DWORD>& processIds,
    const std::wstring& executablePath, const std::wstring& packageFamilyName)
{
    WindowSearch search{ &processIds, &executablePath, &packageFamilyName };
    EnumWindows(FindApplicationWindow, reinterpret_cast<LPARAM>(&search));
    return search;
}

ApplicationWindowState GetApplicationWindowState(const std::vector<DWORD>& processIds,
    const std::wstring& executablePath, const std::wstring& packageFamilyName)
{
    ApplicationWindowState state;
    const auto search = FindBestApplicationWindow(processIds, executablePath,
        packageFamilyName);
    if (!search.bestWindow)
    {
        return state;
    }
    state.available = true;
    state.foreground = GetForegroundWindow() == search.bestWindow;
    state.minimized = IsIconic(search.bestWindow) != FALSE;
    state.maximized = IsZoomed(search.bestWindow) != FALSE;
    state.topmost = (GetWindowLongPtrW(search.bestWindow, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    return state;
}

bool SetApplicationWindowMaximized(const std::vector<DWORD>& processIds,
    const std::wstring& executablePath, const std::wstring& packageFamilyName,
    const bool maximized)
{
    const auto search = FindBestApplicationWindow(processIds, executablePath,
        packageFamilyName);
    if (!search.bestWindow) return false;
    const auto accepted = ShowWindowAsync(search.bestWindow,
        maximized ? SW_MAXIMIZE : SW_RESTORE) != FALSE;
    FB_TRACE("WINDOW_MAXIMIZE hwnd=%p state=%d accepted=%d", search.bestWindow,
        maximized ? 1 : 0, accepted ? 1 : 0);
    return accepted;
}

bool SetApplicationWindowTopmost(const std::vector<DWORD>& processIds,
    const std::wstring& executablePath, const std::wstring& packageFamilyName,
    const bool topmost)
{
    const auto search = FindBestApplicationWindow(processIds, executablePath,
        packageFamilyName);
    if (!search.bestWindow) return false;
    const auto accepted = SetWindowPos(search.bestWindow,
        topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
    FB_TRACE("WINDOW_TOPMOST hwnd=%p state=%d accepted=%d", search.bestWindow,
        topmost ? 1 : 0, accepted ? 1 : 0);
    return accepted;
}

bool BringApplicationWindowToFront(const std::vector<DWORD>& processIds,
    const std::wstring& executablePath, const std::wstring& packageFamilyName)
{
    const auto search = FindBestApplicationWindow(
        processIds, executablePath, packageFamilyName);
    if (!search.bestWindow)
    {
        FB_TRACE("WINDOW_FOCUS no_match=1 pids=%u package=%d executable=%d",
            static_cast<unsigned>(processIds.size()), packageFamilyName.empty() ? 0 : 1,
            executablePath.empty() ? 0 : 1);
        return false;
    }

    const auto wasIconic = IsIconic(search.bestWindow) != FALSE;
    bool restoreMessageHandled = false;
    if (wasIconic)
    {
        // ShowWindowAsync only queues the restore. Activating immediately can
        // report success while the target is still iconic, which made every
        // other Select press appear to fail. Give the target a bounded chance
        // to process SC_RESTORE, retain the async call as a fallback, and do
        // not proceed to activation until the state transition is observable.
        DWORD_PTR restoreResult = 0U;
        restoreMessageHandled = SendMessageTimeoutW(search.bestWindow,
            WM_SYSCOMMAND, SC_RESTORE, 0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 120U, &restoreResult) != 0;
        ShowWindowAsync(search.bestWindow, SW_RESTORE);
        for (int attempt = 0; attempt < 20 && IsIconic(search.bestWindow); ++attempt)
        {
            Sleep(5U);
        }
    }
    const auto iconicAfterRestore = IsIconic(search.bestWindow) != FALSE;
    const auto positioned = SetWindowPos(search.bestWindow, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    auto foreground = SetForegroundWindow(search.bestWindow);
    bool attachedForeground = false;
    bool attachedTarget = false;
    if (!foreground)
    {
        // SetForegroundWindow is intentionally restricted by Windows. A
        // surface press is not a Win32 input event, so temporarily share the
        // current foreground input queue, retry the documented top/activate
        // calls, and immediately detach. No keyboard event is synthesized.
        const auto currentThread = GetCurrentThreadId();
        const auto foregroundWindow = GetForegroundWindow();
        const auto foregroundThread = foregroundWindow
            ? GetWindowThreadProcessId(foregroundWindow, nullptr) : 0U;
        const auto targetThread = GetWindowThreadProcessId(search.bestWindow, nullptr);
        if (foregroundThread != 0U && foregroundThread != currentThread)
        {
            attachedForeground =
                AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;
        }
        if (targetThread != 0U && targetThread != currentThread &&
            targetThread != foregroundThread)
        {
            attachedTarget = AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;
        }
        BringWindowToTop(search.bestWindow);
        foreground = SetForegroundWindow(search.bestWindow);
        if (attachedTarget)
        {
            AttachThreadInput(currentThread, targetThread, FALSE);
        }
        if (attachedForeground)
        {
            AttachThreadInput(currentThread, foregroundThread, FALSE);
        }
    }
    if (!foreground)
    {
        FLASHWINFO flash{ sizeof(flash), search.bestWindow,
            FLASHW_TRAY | FLASHW_TIMERNOFG, 3U, 0U };
        FlashWindowEx(&flash);
    }
    FB_TRACE("WINDOW_FOCUS hwnd=%p pid=%lu score=%d wasIconic=%d restoreSync=%d iconicAfter=%d positioned=%d foreground=%d attachFg=%d attachTarget=%d",
        search.bestWindow, static_cast<unsigned long>(search.bestProcessId), search.bestScore,
        wasIconic ? 1 : 0, restoreMessageHandled ? 1 : 0,
        iconicAfterRestore ? 1 : 0, positioned ? 1 : 0, foreground ? 1 : 0,
        attachedForeground ? 1 : 0, attachedTarget ? 1 : 0);
    return !iconicAfterRestore && foreground != FALSE;
}

bool MinimizeApplicationWindow(const std::vector<DWORD>& processIds,
    const std::wstring& executablePath, const std::wstring& packageFamilyName)
{
    const auto search = FindBestApplicationWindow(
        processIds, executablePath, packageFamilyName);
    if (!search.bestWindow)
    {
        FB_TRACE("WINDOW_MINIMIZE no_match=1 pids=%u",
            static_cast<unsigned>(processIds.size()));
        return false;
    }
    const auto started = ShowWindowAsync(search.bestWindow, SW_MINIMIZE);
    FB_TRACE("WINDOW_MINIMIZE hwnd=%p pid=%lu score=%d started=%d",
        search.bestWindow, static_cast<unsigned long>(search.bestProcessId), search.bestScore,
        started ? 1 : 0);
    return started != FALSE;
}

template<typename TDataVector>
void ApplyMeterVisibility(void* data, const bool visible)
{
    const auto* entries = reinterpret_cast<const TDataVector*>(data);
    if (!entries)
    {
        return;
    }

    FB_TRACE("METER_VIS_CALLBACK visible=%d count=%u", visible ? 1 : 0,
        static_cast<unsigned>(entries->size()));

    for (const auto& entry : *entries)
    {
        if (auto* channel = static_cast<EuconChannel*>(entry.mUserPointer))
        {
            channel->SetMeterVisibility(visible, entry.mVisibilityHandle, entry.mMeterFormat);
        }
    }
}

}

EuconHost::TrackState* EuconHost::FindTrack(const std::wstring& key) noexcept
{
    const auto found = std::find_if(tracks_.begin(), tracks_.end(),
        [&key](const auto& track) { return track->key == key; });
    return found == tracks_.end() ? nullptr : found->get();
}

void EuconHost::SetFaderFromWindows(TrackState& track, const float volume)
{
    if (!track.channel)
    {
        return;
    }
    auto& cache = track.cache;
    const auto index = static_cast<int>(std::lround(
        std::clamp(volume, 0.0F, 1.0F) * static_cast<float>(kUnityFaderIndex)));
    if (cache.lastMotorIndex == index)
    {
        return;
    }

    if (track.channel->SetFaderNormalized(volume))
        cache.lastMotorIndex = index;
}

void EuconHost::ScheduleFaderFromWindows(TrackState& track, const float volume)
{
    auto& cache = track.cache;
    cache.pendingMotorVolume = volume;
    cache.motorDispatchPending = true;
    if (!motorFlushTimerActive_)
    {
        motorFlushTimerActive_ =
            SetTimer(notificationWindow_, kMotorFlushTimerId, kMotorBurstMergeMs, nullptr) != 0;
        if (!motorFlushTimerActive_)
        {
            cache.motorDispatchPending = false;
            SetFaderFromWindows(track, volume);
        }
    }
}

void EuconHost::FlushPendingMotors()
{
    if (motorFlushTimerActive_)
    {
        KillTimer(notificationWindow_, kMotorFlushTimerId);
        motorFlushTimerActive_ = false;
    }
    for (auto& track : tracks_)
    {
        auto& cache = track->cache;
        if (!cache.motorDispatchPending)
        {
            continue;
        }
        cache.motorDispatchPending = false;
        FB_TRACE("MOTOR_BURST_FLUSH track=%d value=%.4f",
            track->route->channelOrder.load(),
            cache.pendingMotorVolume);
        SetFaderFromWindows(*track, cache.pendingMotorVolume);
    }
}

std::unique_ptr<EuconHost::TrackState> EuconHost::CreateTrack(const int channelOrder,
    const AudioStripState& strip)
{
    auto track = std::make_unique<TrackState>();
    track->key = strip.key;
    track->route = std::make_shared<TrackRoute>();
    track->route->audioSlot.store(strip.slot);
    track->route->channelOrder.store(channelOrder);

    const auto route = track->route;
    const auto key = strip.key;
    const auto report = [this, route, key](const float value, const int kind,
        const NEuCon::uint16 rawIndex, const float rawTableValue)
    {
        const auto audioSlot = route->audioSlot.load(std::memory_order_acquire);
        const auto channelOrder = route->channelOrder.load(std::memory_order_acquire);
        bool commandQueued = false;
        if (audioController_ && audioSlot >= 0)
        {
            switch (kind)
            {
            case 0:
            case 1:
                commandQueued = audioController_->QueueVolume(audioSlot, value);
                break;
            case 2:
                commandQueued = audioController_->QueueMute(audioSlot, value != 0.0F);
                break;
            case 3:
                commandQueued = audioController_->QueueSetDefault(audioSlot);
                break;
            case 7:
            case 8:
                commandQueued = audioController_->QueuePan(audioSlot, value);
                break;
            default:
                break;
            }
        }
        FB_TRACE("SURFACE_QUEUE track=%d slot=%d kind=%d value=%.4f queued=%d",
            channelOrder, audioSlot, kind, value, commandQueued ? 1 : 0);

        auto change = std::make_unique<SurfaceChange>();
        change->channel = channelOrder - 1;
        change->trackKey = key;
        change->kind = kind;
        change->value = value;
        change->rawIndex = rawIndex;
        change->rawTableValue = rawTableValue;
        change->commandQueued = commandQueued;
        if (PostMessageW(notificationWindow_, kSurfaceChangeMessage, 0,
            reinterpret_cast<LPARAM>(change.get())))
        {
            change.release();
        }
    };

    const auto resolvedColor = strip.channelColor == AudioStripState::NoChannelColor
        ? ApplicationChannelColor(key)
        : static_cast<NEuCon::int32>(strip.channelColor & 0x00FFFFFFU);
    EuconChannel::ChangeHandler recordArmHandler;
    EuconChannel::ChangeHandler soloHandler;
    EuconChannel::ChangeHandler selectHandler;
    EuconChannel::ChangeHandler panHandler;
    EuconChannel::ChangeHandler panResetHandler;
    EuconChannel::RouteHandler outputRouteHandler;
    EuconChannel::RouteHandler inputRouteHandler;
    EuconChannel::AppActionHandler appActionHandler;
    if (strip.role == AudioStripRole::Application)
    {
        soloHandler = [report](const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(value, 4, rawIndex, rawValue); };
        selectHandler = [report](const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(value, 5, rawIndex, rawValue); };
        outputRouteHandler = [this, key](const std::wstring& endpointId)
        {
            if (audioController_)
            {
                audioController_->QueueApplicationRoute(key, false, endpointId);
            }
        };
        inputRouteHandler = [this, key](const std::wstring& endpointId)
        {
            if (audioController_)
            {
                audioController_->QueueApplicationRoute(key, true, endpointId);
            }
        };
        appActionHandler = [report](const EuconChannel::AppAction action,
            const float value, const NEuCon::uint16 rawIndex, const float rawValue)
        {
            report(value, 100 + static_cast<int>(action), rawIndex, rawValue);
        };
    }
    if (strip.panAvailable)
    {
        panHandler = [report](const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(value, 7, rawIndex, rawValue); };
        panResetHandler = [report](const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(value, 8, rawIndex, rawValue); };
    }
    if (strip.defaultSelectable)
    {
        recordArmHandler = [report](const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(value, 3, rawIndex, rawValue); };
    }
    track->channel = std::make_unique<EuconChannel>(channelOrder, resolvedColor,
        TrackPersistenceId(key), strip.name,
        [report](const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(value, 0, rawIndex, rawValue); },
        [report](const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(value, 1, rawIndex, rawValue); },
        std::move(panHandler),
        std::move(panResetHandler),
        [report](const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(value, 2, rawIndex, rawValue); },
        std::move(soloHandler), std::move(selectHandler), std::move(recordArmHandler),
        std::move(outputRouteHandler), std::move(inputRouteHandler),
        std::move(appActionHandler),
        EuconTrackType(strip.role),
        strip.sortGroup == 0 ? L"Output" : strip.sortGroup == 1 ? L"Input" : L"Audio");
    track->channel->SetMuted(false);
    track->channel->SetSoloed(strip.soloed);
    track->channel->SetSelected(strip.key == selectedTrackKey_);
    track->channel->SetRecordArmed(strip.isDefault);
    track->cache.isDefault = strip.isDefault;
    track->cache.trackType = EuconTrackType(strip.role);
    track->cache.selected = strip.key == selectedTrackKey_;
    track->cache.focusable = strip.role == AudioStripRole::Application;
    track->cache.focusProcessIds = strip.focusProcessIds;
    track->cache.focusExecutablePath = strip.focusExecutablePath;
    track->cache.focusPackageFamilyName = strip.focusPackageFamilyName;
    return track;
}

void EuconHost::ReconcileChannelTopology(const AudioFrame& frame)
{
    std::vector<const AudioStripState*> desired;
    desired.reserve(MaxChannelCount);
    for (const auto& strip : frame.strips)
    {
        if (strip.slot >= 0 && strip.slot < MaxChannelCount && strip.active &&
            !strip.key.empty())
        {
            desired.push_back(&strip);
        }
    }
    std::stable_sort(desired.begin(), desired.end(), [](const auto* left, const auto* right)
    {
        if (left->sortGroup != right->sortGroup)
        {
            return left->sortGroup < right->sortGroup;
        }
        // Default-device state must never affect channel identity or order.
        // EuControl owns physical assignment/banking, and the endpoint's
        // persistent Processor follows that assignment exactly like an app.
        return left->name < right->name;
    });

    bool changed = tracks_.size() != desired.size();
    for (size_t index = 0; !changed && index < desired.size(); ++index)
    {
        const auto* track = FindTrack(desired[index]->key);
        if (!track || track->route->channelOrder.load() != static_cast<int>(index + 1U))
        {
            changed = true;
        }
    }
    if (!changed)
    {
        // Core Audio routing is deliberately separate from EUCON identity.
        // A Windows session may move to another internal slot without changing
        // the Channel Processor or its persistent surface assignment.
        for (const auto* strip : desired)
        {
            FindTrack(strip->key)->route->audioSlot.store(strip->slot,
                std::memory_order_release);
        }
        return;
    }

    // The processors are the application's virtual tracks. Only real Windows
    // applications are registered; EuControl owns physical-strip assignment,
    // banking and layouts across the resulting list.
    node_->Freeze();

    for (auto iterator = tracks_.begin(); iterator != tracks_.end();)
    {
        const auto stillPresent = std::any_of(desired.begin(), desired.end(),
            [&iterator](const auto* strip) { return strip->key == (*iterator)->key; });
        if (!stillPresent)
        {
            if ((*iterator)->key == selectedTrackKey_)
            {
                selectedTrackKey_.clear();
            }
            (*iterator)->route->audioSlot.store(-1, std::memory_order_release);
            FB_TRACE("EUCON_TRACK_REMOVE order=%d",
                (*iterator)->route->channelOrder.load());
            node_->UnregisterProcessor(*(*iterator)->channel);
            iterator = tracks_.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }

    for (size_t index = 0; index < desired.size(); ++index)
    {
        const auto order = static_cast<int>(index + 1U);
        const auto& strip = *desired[index];
        auto* track = FindTrack(strip.key);
        if (!track)
        {
            auto added = CreateTrack(order, strip);
            node_->RegisterProcessor(*added->channel);
            added->channel->PostRegisterMeterInitialization(
                frame.monoAudioEnabled, EuconMeterRoles(strip));
            FB_TRACE("EUCON_TRACK_ADD order=%d slot=%d color=%06X", order, strip.slot,
                static_cast<unsigned>(strip.channelColor & 0x00FFFFFFU));
            tracks_.push_back(std::move(added));
            continue;
        }

        track->route->audioSlot.store(strip.slot, std::memory_order_release);
        const auto trackType = EuconTrackType(strip.role);
        if (track->cache.trackType != trackType)
        {
            track->channel->SetTrackMetadata(trackType,
                strip.sortGroup == 0 ? L"Output" : strip.sortGroup == 1 ? L"Input" : L"Audio");
            track->cache.trackType = trackType;
        }
        if (track->route->channelOrder.exchange(order) != order)
        {
            track->channel->SetOrder(order);
            FB_TRACE("EUCON_TRACK_REORDER order=%d slot=%d", order, strip.slot);
        }
    }

    std::sort(tracks_.begin(), tracks_.end(), [](const auto& left, const auto& right)
    {
        return left->route->channelOrder.load() < right->route->channelOrder.load();
    });
    const auto thawResult = node_->Thaw();
    FB_TRACE("EUCON_TOPOLOGY_THAW tracks=%u result=%d",
        static_cast<unsigned>(tracks_.size()), static_cast<int>(thawResult));
    // A retained track can remain visible while its surface assignment changes.
    // Recover unchanged motor targets without depending on a visibility edge.
    // ApplyAudioFrame keeps this request pending until touches/writes settle.
    RequestSurfaceRefresh("channel-topology-changed");
}

void FaderBridgeNode::OnCallback(const tEVT eventType, void* hidden, void* shown, void*, int&)
{
    if (eventType == kEVT_AttributeChange)
    {
        const auto* change = reinterpret_cast<const AttributeChangeData*>(hidden);
        if (change && change->mEventType == kEVT_AttributeChange &&
            change->mAttributeKeyType == kATRIB_KEYTYPE_Int &&
            change->mIntAttributeKey == kATRIBID_AttentionedTrackPID &&
            change->mAttributeValueType == kATRIB_VALUETYPE_String &&
            !change->mStringAttributeValue.empty())
        {
            if (GetTickCount64() < attentionSuppressedUntil_.load())
            {
                FB_TRACE("ATTENTION_EVT suppressed=surface_attach");
                return;
            }
            // The installed header contract says the surface owns this node
            // attribute. Copy the persistent ID and leave the EUCON callback
            // immediately; window activation happens on the Win32 host thread.
            auto surfaceChange = std::make_unique<SurfaceChange>();
            surfaceChange->kind = 6;
            surfaceChange->trackKey = change->mStringAttributeValue;
            if (PostMessageW(notificationWindow_, kSurfaceChangeMessage, 0,
                    reinterpret_cast<LPARAM>(surfaceChange.get())))
            {
                surfaceChange.release();
            }
        }
    }
    else if (eventType == kEVT_NODE_VisibilityChangedV2)
    {
        ApplyMeterVisibility<NEuCon::tVisChangeDataVectorV2>(hidden, false);
        ApplyMeterVisibility<NEuCon::tVisChangeDataVectorV2>(shown, true);
    }
    else if (eventType == kEVT_NODE_VisibilityChanged)
    {
        ApplyMeterVisibility<NEuCon::tVisChangeDataVector>(hidden, false);
        ApplyMeterVisibility<NEuCon::tVisChangeDataVector>(shown, true);
    }
    else if (eventType == kEVT_NODE_SurfaceNodeAdded || eventType == kEVT_NODE_SurfaceNodeRemoved)
    {
        refreshRequested_.store(true);
        if (eventType == kEVT_NODE_SurfaceNodeAdded)
        {
            // A surface announces its existing attention as part of attach.
            // That is model synchronization, not a user request to activate a
            // Windows application.
            attentionSuppressedUntil_.store(GetTickCount64() + 1500ULL);
        }
    }
}

EuconHost::EuconHost(const HWND notificationWindow) : notificationWindow_(notificationWindow)
{
    initializationError_ = static_cast<int>(EuConManager::Initialize());
    if (initializationError_ != kERR_OK)
    {
        return;
    }

    // CoreAudio owns a dedicated MTA/MMCSS worker. Surface callbacks enqueue
    // directly to it; the Win32 message loop is only used for diagnostics.
    audioController_ = std::make_unique<NativeAudioController>(
        notificationWindow_, kAudioFrameMessage);
    audioController_->Start();
    mediaController_ = std::make_unique<WindowsMediaController>();
    mediaController_->Initialize();

    node_ = std::make_unique<FaderBridgeNode>(notificationWindow_);
    node_->SetAttribute2(kATRIBID_ProcessorMeterAPIVersion, kMeterAPIVersion_3_1, false);
    node_->SetAttribute2(kATRIBID_SupportedProcessorFeatures,
        kSupportsNumberOfChildrenAttribute, false);
    node_->Freeze();
    node_->SetPersistenceID(L"FaderBridge.WindowsAudio.2026");
    node_->SetAttribute(kATRIBID_SimpleFriendlyName, L"Windows Fader Bridge");

    // Register global processors before the dynamic channel strips so the
    // application model has stable top-level ordering.
    const auto clearSolo = [this]
    {
        if (audioController_)
        {
            audioController_->QueueClearSolo();
        }
    };
    commandProcessor_ = std::make_unique<WindowsCommandProcessor>([this]
        {
            if (audioController_)
            {
                audioController_->QueueToggleMonoAudio();
            }
        }, clearSolo, [notificationWindow](const WindowsCommand command)
        {
            if (!PostMessageW(notificationWindow, kWindowsCommandMessage,
                static_cast<WPARAM>(command), 0))
            {
                FB_TRACE("WINDOWS_COMMAND_QUEUE_FAILED command=%u error=%lu",
                    static_cast<unsigned>(command), GetLastError());
            }
        });
    node_->RegisterProcessor(*commandProcessor_);
    systemProcessor_ = std::make_unique<WindowsSystemProcessor>(clearSolo);
    node_->RegisterProcessor(*systemProcessor_);

    node_->SetMeterInfo(kMeterType__Standard, -12.0F, -3.0F, 0.0F);
    node_->SetMeterInfo(kMeterType__SamplePeak, -12.0F, -3.0F, 0.0F);
    node_->Thaw();
    EuCon::GetInstance().RegisterNode(node_.get());

    ready_ = true;
}

EuconHost::~EuconHost()
{
    if (motorFlushTimerActive_)
    {
        KillTimer(notificationWindow_, kMotorFlushTimerId);
        motorFlushTimerActive_ = false;
    }
    if (node_)
    {
        node_->Freeze();
        for (auto& track : tracks_)
        {
            if (track->channel)
            {
                track->route->audioSlot.store(-1, std::memory_order_release);
                node_->UnregisterProcessor(*track->channel);
            }
        }
        tracks_.clear();
        if (systemProcessor_)
        {
            node_->UnregisterProcessor(*systemProcessor_);
            systemProcessor_.reset();
        }
        if (commandProcessor_)
        {
            node_->UnregisterProcessor(*commandProcessor_);
            commandProcessor_.reset();
        }
        EuCon::GetInstance().UnregisterNode(*node_);
        node_.reset();
    }
    audioController_.reset();
    mediaController_.reset();
    EuConManager::Destroy();
}

void EuconHost::RequestSurfaceRefresh(const char* reason)
{
    if (!node_) return;
    node_->RequestRefresh();
    FB_TRACE("SURFACE_SYNC_REQUEST reason=%s", reason);
}

int EuconHost::ApplyAudioFrame(const AudioFrame& frame)
{
    if (!ready_)
    {
        return 0;
    }

    ReconcileChannelTopology(frame);
    commandProcessor_->SetMonoAudioEnabled(frame.monoAudioEnabled);
    commandProcessor_->SetSoloActive(frame.anySolo);
    systemProcessor_->SetSoloActive(frame.anySolo);
    const auto now = std::chrono::steady_clock::now();
    for (const auto& track : tracks_)
        if (track->channel->ConsumeFeedbackRefresh())
            RequestSurfaceRefresh("channel-visible");
    // Preserve recovery across pending writes and touches. The current frame
    // still reconciles them normally; recovery uses the next settled frame.
    const bool busy = std::any_of(tracks_.begin(), tracks_.end(), [](const auto& track) {
        return track->channel->FaderTouched() || track->cache.volumePending ||
            track->cache.panPending || track->cache.mutePending || track->cache.motorDispatchPending;
    });
    const auto fullRefresh = !busy && node_->ConsumeRefreshRequest();
    if (fullRefresh)
        for (const auto& track : tracks_) track->cache.lastMotorIndex = -1;
    EuBatchedMeterWriter meterWriter(*node_);
    std::vector<EuconChannel::RouteOption> outputRoutes;
    std::vector<EuconChannel::RouteOption> inputRoutes;
    outputRoutes.reserve(frame.outputRoutes.size());
    inputRoutes.reserve(frame.inputRoutes.size());
    for (const auto& route : frame.outputRoutes)
    {
        outputRoutes.push_back({ route.id, route.name,
            static_cast<NEuCon::int32>(route.color & 0x00FFFFFFU) });
    }
    for (const auto& route : frame.inputRoutes)
    {
        inputRoutes.push_back({ route.id, route.name,
            static_cast<NEuCon::int32>(route.color & 0x00FFFFFFU) });
    }
    int activeCount = 0;

    for (auto& track : tracks_)
    {
        track->channel->ApplyPendingFaderRebound();
    }

    for (const auto& strip : frame.strips)
    {
        if (!strip.active || strip.key.empty())
        {
            continue;
        }

        auto* track = FindTrack(strip.key);
        if (!track)
        {
            continue;
        }
        track->route->audioSlot.store(strip.slot, std::memory_order_release);
        auto& channel = *track->channel;
        auto& cache = track->cache;
        const auto order = track->route->channelOrder.load();
        ++activeCount;
        cache.focusProcessIds = strip.focusProcessIds;
        cache.focusExecutablePath = strip.focusExecutablePath;
        cache.focusPackageFamilyName = strip.focusPackageFamilyName;
        cache.focusable = strip.role == AudioStripRole::Application;
        if (strip.role == AudioStripRole::Application)
        {
            channel.SetRouteOptions(outputRoutes, strip.outputRouteId,
                inputRoutes, strip.inputRouteId);
        }
        channel.ConfigureMeter(frame.monoAudioEnabled, EuconMeterRoles(strip));
        const auto trackType = EuconTrackType(strip.role);
        if (!cache.active || cache.trackType != trackType)
        {
            channel.SetTrackMetadata(trackType,
                strip.sortGroup == 0 ? L"Output" : strip.sortGroup == 1 ? L"Input" : L"Audio");
            cache.trackType = trackType;
        }
        if (!cache.active || fullRefresh || cache.name != strip.name)
        {
            channel.SetName(strip.name);
            cache.name = strip.name;
        }
        if (!cache.active || fullRefresh || cache.isDefault != strip.isDefault)
        {
            channel.SetRecordArmed(strip.isDefault);
            cache.isDefault = strip.isDefault;
        }
        if (!cache.active || fullRefresh || cache.soloed != strip.soloed)
        {
            channel.SetSoloed(strip.soloed);
            cache.soloed = strip.soloed;
        }
        const auto selected = strip.key == selectedTrackKey_;
        if (!cache.active || fullRefresh || cache.selected != selected)
        {
            channel.SetSelected(selected);
            cache.selected = selected;
        }
        if ((selected || fullRefresh) && strip.role == AudioStripRole::Application)
        {
            RefreshApplicationControls(*track, fullRefresh);
        }

        const auto volumeMatchesPending = cache.volumePending &&
            std::fabs(strip.volume - cache.requestedVolume) <= kPendingMatch;
        if (!cache.active || volumeMatchesPending ||
            std::fabs(strip.volume - cache.volume) > kVolumeDifference)
        {
            FB_TRACE("AUDIO_FRAME track=%d slot=%d value=%.4f old=%.4f pending=%d match=%d",
                order, strip.slot, strip.volume, cache.volume,
                cache.volumePending ? 1 : 0, volumeMatchesPending ? 1 : 0);
        }
        const auto windowsVolumeChanged =
            std::fabs(strip.volume - cache.volume) > kVolumeDifference;
        if (cache.volumePending && cache.pendingVolumeKind == 1 && windowsVolumeChanged)
        {
            // Encoder writes can be superseded faster than CoreAudio reports
            // them. Forward confirmed Windows values immediately.
            FB_TRACE("MOTOR_FROM_WINDOWS_EVENT track=%d slot=%d value=%.4f match=%d",
                order, strip.slot, strip.volume, volumeMatchesPending ? 1 : 0);
            if (volumeMatchesPending)
            {
                cache.motorDispatchPending = false;
                SetFaderFromWindows(*track, strip.volume);
            }
            else
            {
                ScheduleFaderFromWindows(*track, strip.volume);
            }
            cache.volume = strip.volume;
        }
        else if (volumeMatchesPending && cache.pendingVolumeKind == 1)
        {
            FB_TRACE("MOTOR_FROM_WINDOWS_ACK_UNCHANGED track=%d slot=%d value=%.4f",
                order, strip.slot, strip.volume);
        }
        if (volumeMatchesPending || (cache.volumePending && now >= cache.volumeHoldUntil))
        {
            cache.volumePending = false;
        }
        if (!cache.volumePending &&
            (!cache.active || fullRefresh || std::fabs(strip.volume - cache.volume) > kVolumeDifference))
        {
            // A matching pending value acknowledges this surface's own write.
            // Only initial state and external Windows changes drive surfaces.
            if (!volumeMatchesPending)
            {
                SetFaderFromWindows(*track, strip.volume);
                channel.SetKnobNormalized(strip.volume);
            }
            cache.volume = strip.volume;
        }
        else if (volumeMatchesPending)
        {
            cache.volume = strip.volume;
        }

        const auto panMatchesPending = cache.panPending &&
            std::fabs(strip.pan - cache.requestedPan) <= kPanPendingMatch;
        if (panMatchesPending || (cache.panPending && now >= cache.panHoldUntil))
        {
            cache.panPending = false;
        }
        if (strip.panAvailable && !cache.panPending &&
            (!cache.active || fullRefresh ||
                std::fabs(strip.pan - cache.pan) > kPanDifference))
        {
            FB_TRACE("PAN_FROM_WINDOWS track=%d slot=%d value=%.4f", order,
                strip.slot, strip.pan);
            channel.SetPan(strip.pan);
            cache.pan = strip.pan;
        }
        else if (panMatchesPending)
        {
            cache.pan = strip.pan;
        }

        channel.WriteMeterDb(meterWriter, strip.meterDb);
        cache.peakDb = strip.peakDb;

        const auto muteMatchesPending = cache.mutePending && strip.muted == cache.requestedMute;
        if (muteMatchesPending || (cache.mutePending && now >= cache.muteHoldUntil))
        {
            cache.mutePending = false;
        }
        if (!cache.mutePending && (!cache.active || fullRefresh || strip.muted != cache.muted))
        {
            channel.SetMuted(strip.muted);
            cache.muted = strip.muted;
        }
        cache.active = true;
    }
    if (fullRefresh)
    {
        const auto result = node_->SyncNode();
        FB_TRACE("SURFACE_SYNC_COMPLETE tracks=%d result=%d", activeCount, static_cast<int>(result));
    }
    return activeCount;
}

bool EuconHost::HandleSurfaceChange(const SurfaceChange& change)
{
    const auto kind = change.kind;
    const auto value = change.value;
    if (kind == 6)
    {
        // AttentionedTrackPID is a surface-owned node attribute containing a
        // channel Processor persistence ID, not the Windows application key.
        const auto attentioned = std::find_if(tracks_.begin(), tracks_.end(),
            [&change](const auto& candidate)
            {
                const auto persistenceId = TrackPersistenceId(candidate->key);
                return persistenceId == change.trackKey ||
                    (change.trackKey.size() > persistenceId.size() &&
                        change.trackKey.compare(change.trackKey.size() - persistenceId.size(),
                            persistenceId.size(), persistenceId) == 0);
            });
        FB_TRACE("ATTENTION_EVT matched=%d", attentioned != tracks_.end() ? 1 : 0);
        return attentioned != tracks_.end() && SelectAndFocusTrack(**attentioned);
    }
    auto* track = FindTrack(change.trackKey);
    if (!track)
    {
        return false;
    }
    const auto order = track->route->channelOrder.load();

    if (kind >= 100)
    {
        const auto action = static_cast<EuconChannel::AppAction>(kind - 100);
        const auto audioSlot = track->route->audioSlot.load(std::memory_order_acquire);
        bool accepted = false;
        switch (action)
        {
        case EuconChannel::AppAction::ResetVolume:
            accepted = audioController_ && audioController_->QueueVolume(audioSlot, 1.0F);
            break;
        case EuconChannel::AppAction::ResetPan:
            accepted = audioController_ && audioController_->QueuePan(audioSlot, 0.0F);
            break;
        case EuconChannel::AppAction::DefaultOutput:
            accepted = audioController_ &&
                audioController_->QueueApplicationRoute(track->key, false, L"");
            break;
        case EuconChannel::AppAction::DefaultInput:
            accepted = audioController_ &&
                audioController_->QueueApplicationRoute(track->key, true, L"");
            break;
        case EuconChannel::AppAction::Unmute:
            accepted = audioController_ && audioController_->QueueMute(audioSlot, false);
            break;
        case EuconChannel::AppAction::ClearSolo:
            accepted = audioController_ && audioController_->QueueClearSolo();
            break;
        case EuconChannel::AppAction::WindowFocus:
            accepted = BringApplicationWindowToFront(track->cache.focusProcessIds,
                track->cache.focusExecutablePath, track->cache.focusPackageFamilyName);
            break;
        case EuconChannel::AppAction::WindowMinimize:
            accepted = MinimizeApplicationWindow(track->cache.focusProcessIds,
                track->cache.focusExecutablePath, track->cache.focusPackageFamilyName);
            break;
        case EuconChannel::AppAction::WindowMaximize:
            accepted = SetApplicationWindowMaximized(track->cache.focusProcessIds,
                track->cache.focusExecutablePath, track->cache.focusPackageFamilyName,
                value != 0.0F);
            break;
        case EuconChannel::AppAction::WindowTopmost:
            accepted = SetApplicationWindowTopmost(track->cache.focusProcessIds,
                track->cache.focusExecutablePath, track->cache.focusPackageFamilyName,
                value != 0.0F);
            break;
        default:
        {
            if (!mediaController_)
            {
                break;
            }
            MediaControlAction mediaAction{};
            switch (action)
            {
            case EuconChannel::AppAction::MediaPlayPause:
                mediaAction = MediaControlAction::PlayPause; break;
            case EuconChannel::AppAction::MediaPrevious:
                mediaAction = MediaControlAction::Previous; break;
            case EuconChannel::AppAction::MediaNext:
                mediaAction = MediaControlAction::Next; break;
            case EuconChannel::AppAction::MediaStop:
                mediaAction = MediaControlAction::Stop; break;
            case EuconChannel::AppAction::MediaSeek:
                mediaAction = MediaControlAction::Seek; break;
            case EuconChannel::AppAction::MediaShuffle:
                mediaAction = MediaControlAction::Shuffle; break;
            case EuconChannel::AppAction::MediaRepeat:
                mediaAction = MediaControlAction::Repeat; break;
            default:
                return false;
            }
            accepted = mediaController_->Execute(track->cache.focusExecutablePath,
                track->cache.focusPackageFamilyName, mediaAction, value);
            break;
        }
        }
        track->cache.appControlRefreshAt = {};
        FB_TRACE("APP_CONTROL track=%d action=%d value=%.3f accepted=%d", order,
            static_cast<int>(action), value, accepted ? 1 : 0);
        return accepted;
    }

    if (kind == 0 || kind == 1)
    {
        const auto volume = std::clamp(value, 0.0F, 1.0F);
        auto& cache = track->cache;

        // Keep the two physical volume controls coherent without waiting for
        // the Windows CoreAudio round trip. Do this on the host/UI thread, not
        // inside EuCon's callback thread where calling back into the SDK can
        // deadlock.
        if (kind == 0)
        {
            FB_TRACE("CROSS_SYNC fader_to_knob track=%d value=%.4f", order, volume);
            track->channel->SetKnobNormalized(volume);
            cache.volume = volume;
            cache.lastMotorIndex = static_cast<int>(std::lround(
                volume * static_cast<float>(kUnityFaderIndex)));
        }
        else
        {
            FB_TRACE("KNOB_WAIT_WINDOWS_ACK track=%d value=%.4f", order, volume);
        }

        cache.volumePending = true;
        cache.pendingVolumeKind = kind;
        cache.requestedVolume = volume;
        cache.volumeHoldUntil = std::chrono::steady_clock::now() + kVolumeHold;
        if (!change.commandQueued)
        {
            cache.volumePending = false;
        }
        return change.commandQueued;
    }

    if (kind == 2)
    {
        auto& cache = track->cache;
        cache.mutePending = true;
        cache.requestedMute = value != 0.0F;
        cache.muted = cache.requestedMute;
        cache.muteHoldUntil = std::chrono::steady_clock::now() + kMuteHold;
        track->channel->SetMuted(cache.requestedMute);
        if (!change.commandQueued)
        {
            cache.mutePending = false;
        }
        return change.commandQueued;
    }
    if (kind == 7 || kind == 8)
    {
        auto& cache = track->cache;
        cache.panPending = true;
        cache.requestedPan = std::clamp(value, -1.0F, 1.0F);
        cache.pan = cache.requestedPan;
        cache.panHoldUntil = std::chrono::steady_clock::now() + kPanHold;
        if (kind == 8)
        {
            // The callback only queued the request. EUCON feedback is applied
            // here on the host thread, then confirmed by the Core Audio frame.
            track->channel->SetPan(0.0F);
        }
        if (!change.commandQueued)
        {
            cache.panPending = false;
        }
        return change.commandQueued;
    }
    if (kind == 3)
    {
        // Record Arm is used as a one-of-N default-endpoint selector. A press
        // on the already-selected device is not an "unset default" command;
        // the next Core Audio frame restores the authoritative switch/LED.
        return change.commandQueued;
    }
    if (kind == 4)
    {
        // The callback only posted this stable track identity. Resolve and
        // enqueue the intercancel operation here, outside EUCON's callback.
        return audioController_ && audioController_->QueueToggleSolo(track->key);
    }
    if (kind == 5)
    {
        // Windows application visibility maps cleanly to the standard two
        // Select states: on activates/unminimizes; off minimizes.
        return value != 0.0F
            ? SelectAndFocusTrack(*track)
            : DeselectAndMinimizeTrack(*track);
    }
    return false;
}

bool EuconHost::SelectTrack(const std::wstring& trackKey)
{
    auto* track = FindTrack(trackKey);
    if (!track || !track->cache.active)
    {
        return false;
    }
    selectedTrackKey_ = track->key;
    for (auto& candidate : tracks_)
    {
        const auto selected = candidate.get() == track;
        if (candidate->cache.selected != selected)
        {
            candidate->channel->SetSelected(selected);
            candidate->cache.selected = selected;
        }
    }
    FB_TRACE("DESKTOP_SELECT track=%d", track->route->channelOrder.load());
    return true;
}

void EuconHost::RefreshApplicationControls(TrackState& track, const bool force)
{
    if (!track.channel || !track.cache.focusable)
    {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!force && now < track.cache.appControlRefreshAt)
    {
        return;
    }
    track.cache.appControlRefreshAt = now + std::chrono::milliseconds(200);

    const auto window = GetApplicationWindowState(track.cache.focusProcessIds,
        track.cache.focusExecutablePath, track.cache.focusPackageFamilyName);
    auto& cache = track.cache;
    if (force || cache.windowAvailable != window.available ||
        cache.windowForeground != window.foreground ||
        cache.windowMinimized != window.minimized ||
        cache.windowMaximized != window.maximized ||
        cache.windowTopmost != window.topmost)
    {
        track.channel->SetWindowState(window.available, window.foreground,
            window.minimized, window.maximized, window.topmost);
        cache.windowAvailable = window.available;
        cache.windowForeground = window.foreground;
        cache.windowMinimized = window.minimized;
        cache.windowMaximized = window.maximized;
        cache.windowTopmost = window.topmost;
    }

    const auto media = mediaController_
        ? mediaController_->GetState(cache.focusExecutablePath,
            cache.focusPackageFamilyName)
        : WindowsMediaState{};
    const auto mediaCapabilities =
        (media.canPlayPause ? 1U << 0U : 0U) |
        (media.canPrevious ? 1U << 1U : 0U) |
        (media.canNext ? 1U << 2U : 0U) |
        (media.canStop ? 1U << 3U : 0U) |
        (media.canSeek ? 1U << 4U : 0U) |
        (media.canShuffle ? 1U << 5U : 0U) |
        (media.canRepeat ? 1U << 6U : 0U) |
        (media.hasPosition ? 1U << 7U : 0U);
    if (force || cache.mediaAvailable != media.available ||
        cache.mediaPlaying != media.playing ||
        cache.mediaCapabilities != mediaCapabilities ||
        cache.mediaShuffle != media.shuffle ||
        cache.mediaRepeatMode != media.repeatMode ||
        std::fabs(cache.mediaPosition - media.position) > 0.002F ||
        cache.mediaTitle != media.title || cache.mediaArtist != media.artist)
    {
        track.channel->SetMediaState(media.available, media.playing,
            media.canPlayPause, media.canPrevious, media.canNext, media.canStop,
            media.hasPosition, media.canSeek, media.canShuffle, media.shuffle, media.canRepeat,
            media.repeatMode, media.position, media.title, media.artist);
        cache.mediaAvailable = media.available;
        cache.mediaPlaying = media.playing;
        cache.mediaCapabilities = mediaCapabilities;
        cache.mediaShuffle = media.shuffle;
        cache.mediaRepeatMode = media.repeatMode;
        cache.mediaPosition = media.position;
        cache.mediaTitle = media.title;
        cache.mediaArtist = media.artist;
    }
}

bool EuconHost::SelectAndFocusTrack(TrackState& track)
{
    if (!track.cache.focusable)
    {
        FB_TRACE("WINDOW_SELECT rejected=non_application track=%d",
            track.route->channelOrder.load());
        return false;
    }
    selectedTrackKey_ = track.key;
    for (auto& candidate : tracks_)
    {
        const auto selected = candidate.get() == &track;
        if (candidate->cache.selected != selected)
        {
            candidate->channel->SetSelected(selected);
            candidate->cache.selected = selected;
        }
    }
    FB_TRACE("WINDOW_SELECT track=%d pids=%u package=%d executable=%d",
        track.route->channelOrder.load(),
        static_cast<unsigned>(track.cache.focusProcessIds.size()),
        track.cache.focusPackageFamilyName.empty() ? 0 : 1,
        track.cache.focusExecutablePath.empty() ? 0 : 1);
    const auto focused = BringApplicationWindowToFront(track.cache.focusProcessIds,
        track.cache.focusExecutablePath, track.cache.focusPackageFamilyName);
    track.cache.appControlRefreshAt = {};
    RefreshApplicationControls(track, true);
    return focused;
}

bool EuconHost::DeselectAndMinimizeTrack(TrackState& track)
{
    if (!track.cache.focusable)
    {
        return false;
    }
    if (selectedTrackKey_ == track.key)
    {
        selectedTrackKey_.clear();
    }
    if (track.cache.selected)
    {
        track.channel->SetSelected(false);
        track.cache.selected = false;
    }
    FB_TRACE("WINDOW_DESELECT track=%d", track.route->channelOrder.load());
    return MinimizeApplicationWindow(track.cache.focusProcessIds,
        track.cache.focusExecutablePath, track.cache.focusPackageFamilyName);
}
