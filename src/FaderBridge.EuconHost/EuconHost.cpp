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

std::wstring ApplicationPersistenceId(const std::wstring& key)
{
    // Stable FNV-1a hash: EuControl layouts/assignments can recognize an app
    // after process restarts even when its internal CoreAudio slot changes.
    unsigned long long hash = 14695981039346656037ULL;
    for (const auto value : key)
    {
        hash ^= static_cast<unsigned long long>(value);
        hash *= 1099511628211ULL;
    }
    return L"FaderBridge.WindowsApp." + std::to_wstring(hash);
}

template<typename TDataVector>
void ApplyMeterVisibility(void* data, const bool visible)
{
    const auto* entries = reinterpret_cast<const TDataVector*>(data);
    if (!entries)
    {
        return;
    }

    for (const auto& entry : *entries)
    {
        if (auto* channel = static_cast<EuconChannel*>(entry.mUserPointer))
        {
            channel->SetMeterVisibility(visible, entry.mVisibilityHandle, entry.mMeterFormat);
        }
    }
}

}

void EuconHost::SetFaderFromWindows(const int channel, const float volume)
{
    if (channel < 0 || channel >= MaxChannelCount || !channels_[channel])
    {
        return;
    }
    auto& cache = cache_[channel];
    const auto index = static_cast<int>(std::lround(
        std::clamp(volume, 0.0F, 1.0F) * static_cast<float>(kUnityFaderIndex)));
    if (cache.lastMotorIndex == index)
    {
        return;
    }

    channels_[channel]->SetFaderNormalized(volume);
    cache.lastMotorIndex = index;
}

void EuconHost::ScheduleFaderFromWindows(const int channel, const float volume)
{
    auto& cache = cache_[channel];
    cache.pendingMotorVolume = volume;
    cache.motorDispatchPending = true;
    if (!motorFlushTimerActive_)
    {
        motorFlushTimerActive_ =
            SetTimer(notificationWindow_, kMotorFlushTimerId, kMotorBurstMergeMs, nullptr) != 0;
        if (!motorFlushTimerActive_)
        {
            cache.motorDispatchPending = false;
            SetFaderFromWindows(channel, volume);
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
    for (int channel = 0; channel < MaxChannelCount; ++channel)
    {
        auto& cache = cache_[channel];
        if (!cache.motorDispatchPending)
        {
            continue;
        }
        cache.motorDispatchPending = false;
        FB_TRACE("MOTOR_BURST_FLUSH ch=%d value=%.4f", channel + 1,
            cache.pendingMotorVolume);
        SetFaderFromWindows(channel, cache.pendingMotorVolume);
    }
}

std::unique_ptr<EuconChannel> EuconHost::CreateChannel(
    const int channelIndex, const std::wstring& key, const std::wstring& name)
{
    const auto report = [this](const int channel, const float value, const int kind,
        const NEuCon::uint16 rawIndex, const float rawTableValue)
    {
        bool commandQueued = false;
        if (audioController_)
        {
            commandQueued = kind == 2
                ? audioController_->QueueMute(channel, value != 0.0F)
                : audioController_->QueueVolume(channel, value);
        }
        FB_TRACE("SURFACE_QUEUE ch=%d kind=%d value=%.4f queued=%d", channel + 1,
            kind, value, commandQueued ? 1 : 0);

        auto change = std::make_unique<SurfaceChange>();
        change->channel = channel;
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

    auto channel = std::make_unique<EuconChannel>(channelIndex,
        ApplicationPersistenceId(key), name,
        [report](const int ch, const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(ch, value, 0, rawIndex, rawValue); },
        [report](const int ch, const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(ch, value, 1, rawIndex, rawValue); },
        [report](const int ch, const float value, const NEuCon::uint16 rawIndex,
            const float rawValue) { report(ch, value, 2, rawIndex, rawValue); });
    channel->SetMuted(false);
    return channel;
}

void EuconHost::ReconcileChannelTopology(const AudioFrame& frame)
{
    std::array<bool, MaxChannelCount> desired{};
    std::array<std::wstring, MaxChannelCount> keys{};
    std::array<std::wstring, MaxChannelCount> names{};
    for (const auto& strip : frame.strips)
    {
        if (strip.slot >= 0 && strip.slot < MaxChannelCount && strip.active)
        {
            desired[strip.slot] = true;
            keys[strip.slot] = strip.key;
            names[strip.slot] = strip.name;
        }
    }

    bool changed = false;
    for (int index = 0; index < MaxChannelCount; ++index)
    {
        if (desired[index] != static_cast<bool>(channels_[index]))
        {
            changed = true;
            break;
        }
    }
    if (!changed)
    {
        return;
    }

    // The processors are the application's virtual tracks. Only real Windows
    // applications are registered; EuControl owns physical-strip assignment,
    // banking and layouts across the resulting list.
    node_->Freeze();
    for (int index = 0; index < MaxChannelCount; ++index)
    {
        if (desired[index] && !channels_[index])
        {
            auto channel = CreateChannel(index, keys[index], names[index]);
            node_->RegisterProcessor(*channel);
            channel->PostRegisterMeterInitialization();
            channels_[index] = std::move(channel);
            cache_[index] = ChannelCache{};
            FB_TRACE("EUCON_CHANNEL_ADD ch=%d", index + 1);
        }
        else if (!desired[index] && channels_[index])
        {
            FB_TRACE("EUCON_CHANNEL_REMOVE ch=%d", index + 1);
            node_->UnregisterProcessor(*channels_[index]);
            channels_[index].reset();
            cache_[index] = ChannelCache{};
        }
    }
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

    // Matching EuConApp's topology keeps CH1's meter visible on the S3.
    commandProcessor_ = std::make_unique<ExProcessorCommand>();
    node_->RegisterProcessor(*commandProcessor_);

    // This is a virtual EUCON channel list, not a fixed model of the S3's 16
    // physical faders. Real application processors are added on first sight.
    channels_.resize(MaxChannelCount);

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
        for (auto& channel : channels_)
        {
            if (channel)
            {
                node_->UnregisterProcessor(*channel);
            }
        }
        channels_.clear();
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

    for (auto& channel : channels_)
    {
        if (channel)
        {
            channel->ApplyPendingFaderRebound();
        }
    }

    for (const auto& strip : frame.strips)
    {
        if (strip.slot < 0 || strip.slot >= static_cast<int>(channels_.size()))
        {
            continue;
        }

        auto& channel = channels_[strip.slot];
        auto& cache = cache_[strip.slot];
        if (strip.active)
        {
            if (!channel)
            {
                continue;
            }
            ++activeCount;
            if (!cache.active || fullRefresh || cache.name != strip.name)
            {
                channel->SetName(strip.name);
                cache.name = strip.name;
            }

            const auto volumeMatchesPending = cache.volumePending &&
                std::fabs(strip.volume - cache.requestedVolume) <= kPendingMatch;
            if (!cache.active || volumeMatchesPending ||
                std::fabs(strip.volume - cache.volume) > kVolumeDifference)
            {
                FB_TRACE("AUDIO_FRAME ch=%d value=%.4f old=%.4f pending=%d match=%d",
                    strip.slot + 1, strip.volume, cache.volume,
                    cache.volumePending ? 1 : 0, volumeMatchesPending ? 1 : 0);
            }
            const auto windowsVolumeChanged =
                std::fabs(strip.volume - cache.volume) > kVolumeDifference;
            if (cache.volumePending && cache.pendingVolumeKind == 1 && windowsVolumeChanged)
            {
                // Encoder writes can be superseded faster than CoreAudio
                // reports them. Every changed value here is nevertheless a
                // confirmed Windows value, so forward it immediately rather
                // than waiting for an exact match with only the newest target.
                FB_TRACE("MOTOR_FROM_WINDOWS_EVENT ch=%d value=%.4f match=%d",
                    strip.slot + 1,
                    strip.volume, volumeMatchesPending ? 1 : 0);
                if (volumeMatchesPending)
                {
                    // The final Windows acknowledgement wins immediately.
                    cache.motorDispatchPending = false;
                    SetFaderFromWindows(strip.slot, strip.volume);
                }
                else
                {
                    // Multiple CoreAudio sessions can report an intermediate
                    // value followed 1-2 ms later by the final value. Merge
                    // only that short burst so the motor receives one target.
                    ScheduleFaderFromWindows(strip.slot, strip.volume);
                }
                cache.volume = strip.volume;
            }
            else if (volumeMatchesPending && cache.pendingVolumeKind == 1)
            {
                FB_TRACE("MOTOR_FROM_WINDOWS_ACK_UNCHANGED ch=%d value=%.4f", strip.slot + 1,
                    strip.volume);
            }
            if (volumeMatchesPending || (cache.volumePending && now >= cache.volumeHoldUntil))
            {
                cache.volumePending = false;
            }
            if (!cache.volumePending &&
                (!cache.active || fullRefresh || std::fabs(strip.volume - cache.volume) > kVolumeDifference))
            {
                // A matching pending value is the acknowledgement of this
                // surface's own write. Do not make the motor chase its own
                // movement. Only initial state and externally-originated
                // Windows changes drive the S3 fader and encoder ring.
                if (!volumeMatchesPending)
                {
                    SetFaderFromWindows(strip.slot, strip.volume);
                    channel->SetKnobNormalized(strip.volume);
                }
                cache.volume = strip.volume;
            }
            else if (volumeMatchesPending)
            {
                cache.volume = strip.volume;
            }

            channel->WriteMeterDb(meterWriter, strip.peakDb, strip.peakDb >= -0.01F);
            cache.peakDb = strip.peakDb;

            const auto muteMatchesPending = cache.mutePending && strip.muted == cache.requestedMute;
            if (muteMatchesPending || (cache.mutePending && now >= cache.muteHoldUntil))
            {
                cache.mutePending = false;
            }
            if (!cache.mutePending && (!cache.active || fullRefresh || strip.muted != cache.muted))
            {
                channel->SetMuted(strip.muted);
                cache.muted = strip.muted;
            }
            cache.active = true;
        }
        else if (channel && (cache.active || fullRefresh))
        {
            channel->SetName(L"");
            channel->WriteMeterDb(meterWriter, -120.0F, false);
            channel->SetMuted(false);
            cache = ChannelCache{};
        }
    }
    return activeCount;
}

bool EuconHost::HandleSurfaceChange(const SurfaceChange& change)
{
    const auto channel = change.channel;
    const auto kind = change.kind;
    const auto value = change.value;
    if (channel < 0 || channel >= MaxChannelCount || !channels_[channel])
    {
        return false;
    }

    if (kind == 0 || kind == 1)
    {
        const auto volume = std::clamp(value, 0.0F, 1.0F);
        auto& cache = cache_[channel];

        // Keep the two physical volume controls coherent without waiting for
        // the Windows CoreAudio round trip. Do this on the host/UI thread, not
        // inside EuCon's callback thread where calling back into the SDK can
        // deadlock.
        if (kind == 0)
        {
            FB_TRACE("CROSS_SYNC fader_to_knob ch=%d value=%.4f", channel + 1, volume);
            channels_[channel]->SetKnobNormalized(volume);
            cache.volume = volume;
            cache.lastMotorIndex = static_cast<int>(std::lround(
                volume * static_cast<float>(kUnityFaderIndex)));
        }
        else
        {
            FB_TRACE("KNOB_WAIT_WINDOWS_ACK ch=%d value=%.4f", channel + 1, volume);
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
        auto& cache = cache_[channel];
        cache.mutePending = true;
        cache.requestedMute = value != 0.0F;
        cache.muted = cache.requestedMute;
        cache.muteHoldUntil = std::chrono::steady_clock::now() + kMuteHold;
        channels_[channel]->SetMuted(cache.requestedMute);
        if (!change.commandQueued)
        {
            cache.mutePending = false;
        }
        return change.commandQueued;
    }
    return false;
}
