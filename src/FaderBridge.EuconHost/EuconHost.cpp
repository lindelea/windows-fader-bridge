#include <Windows.h>

#include "EuconHost.h"

#include "DiagnosticLog.h"
#include "EuBatchedMeterWriter.h"
#include "EuCon.h"
#include "EuConManager.h"
#include "EuDefinitions.h"
#include "EuconChannel.h"
#include "ExProcessorCommand.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
constexpr float kVolumeDifference = 0.001F;
constexpr float kPendingMatch = 0.005F;
constexpr int kUnityFaderIndex = 728;
constexpr UINT kMotorBurstMergeMs = 10U;
constexpr auto kVolumeHold = std::chrono::milliseconds(180);
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

std::wstring ApplicationPersistenceId(const std::wstring& key)
{
    // Stable FNV-1a hash: EuControl layouts/assignments can recognize an app
    // after process restarts even when its internal CoreAudio slot changes.
    return L"FaderBridge.WindowsApp." + std::to_wstring(ApplicationIdentityHash(key));
}

NEuCon::int32 ApplicationChannelColor(const std::wstring& key)
{
    // Saturated colors from the official EuConApp channel color example.
    // Exclude white and dark greys so every active application is distinct
    // from the uncolored/default surface state.
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

    track.channel->SetFaderNormalized(volume);
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
    const int audioSlot, const std::wstring& key, const std::wstring& name)
{
    auto track = std::make_unique<TrackState>();
    track->key = key;
    track->route = std::make_shared<TrackRoute>();
    track->route->audioSlot.store(audioSlot);
    track->route->channelOrder.store(channelOrder);

    const auto route = track->route;
    const auto report = [this, route, key](const float value, const int kind,
        const NEuCon::uint16 rawIndex, const float rawTableValue)
    {
        const auto audioSlot = route->audioSlot.load(std::memory_order_acquire);
        const auto channelOrder = route->channelOrder.load(std::memory_order_acquire);
        bool commandQueued = false;
        if (audioController_ && audioSlot >= 0)
        {
            commandQueued = kind == 2
                ? audioController_->QueueMute(audioSlot, value != 0.0F)
                : audioController_->QueueVolume(audioSlot, value);
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

    track->channel = std::make_unique<EuconChannel>(channelOrder,
        ApplicationChannelColor(key),
        ApplicationPersistenceId(key), name,
        [report](const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(value, 0, rawIndex, rawValue); },
        [report](const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(value, 1, rawIndex, rawValue); },
        [report](const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(value, 2, rawIndex, rawValue); });
    track->channel->SetMuted(false);
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
            auto added = CreateTrack(order, strip.slot, strip.key, strip.name);
            node_->RegisterProcessor(*added->channel);
            added->channel->PostRegisterMeterInitialization();
            FB_TRACE("EUCON_TRACK_ADD order=%d slot=%d", order, strip.slot);
            tracks_.push_back(std::move(added));
            continue;
        }

        track->route->audioSlot.store(strip.slot, std::memory_order_release);
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
    node_->Thaw();
}

void FaderBridgeNode::OnCallback(const tEVT eventType, void* hidden, void* shown, void*, int&)
{
    if (eventType == kEVT_NODE_VisibilityChangedV2)
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

    node_ = std::make_unique<FaderBridgeNode>();
    node_->SetAttribute2(kATRIBID_ProcessorMeterAPIVersion, kMeterAPIVersion_3_1, false);
    node_->Freeze();
    node_->SetPersistenceID(L"FaderBridge.WindowsAudio.2026");
    node_->SetAttribute(kATRIBID_SimpleFriendlyName, L"FaderBridge Windows Audio");

    // Match the official EuConApp processor ordering: global command processor
    // before dynamic channel strips.
    commandProcessor_ = std::make_unique<ExProcessorCommand>();
    node_->RegisterProcessor(*commandProcessor_);

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
        if (commandProcessor_)
        {
            node_->UnregisterProcessor(*commandProcessor_);
            commandProcessor_.reset();
        }
        EuCon::GetInstance().UnregisterNode(*node_);
        node_.reset();
    }
    audioController_.reset();
    EuConManager::Destroy();
}

int EuconHost::ApplyAudioFrame(const AudioFrame& frame)
{
    if (!ready_)
    {
        return 0;
    }

    ReconcileChannelTopology(frame);
    const auto now = std::chrono::steady_clock::now();
    const auto fullRefresh = node_->ConsumeRefreshRequest();
    EuBatchedMeterWriter meterWriter(*node_);
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
        if (!cache.active || fullRefresh || cache.name != strip.name)
        {
            channel.SetName(strip.name);
            cache.name = strip.name;
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

        channel.WriteMeterDb(meterWriter, strip.peakDb, strip.peakDb >= -0.01F);
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
    return activeCount;
}

bool EuconHost::HandleSurfaceChange(const SurfaceChange& change)
{
    const auto kind = change.kind;
    const auto value = change.value;
    auto* track = FindTrack(change.trackKey);
    if (!track)
    {
        return false;
    }
    const auto order = track->route->channelOrder.load();

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
    return false;
}
