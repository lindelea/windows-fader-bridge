#include <Windows.h>

#include "EuconHost.h"

#include "EuCon.h"
#include "EuBatchedMeterWriter.h"
#include "EuConManager.h"
#include "EuconChannel.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
constexpr float kVolumeDifference = 0.001F;
constexpr float kPendingMatch = 0.005F;
constexpr auto kVolumeHold = std::chrono::milliseconds(650);
constexpr auto kMuteHold = std::chrono::milliseconds(350);

float VolumeToFaderDb(const float volume)
{
    if (volume <= 0.0F)
    {
        return -144.5F;
    }
    return std::clamp(20.0F * std::log10(volume), -100.0F, 0.0F);
}

float FaderDbToVolume(const float valueDb)
{
    if (valueDb <= -100.0F)
    {
        return 0.0F;
    }
    return std::clamp(std::pow(10.0F, valueDb / 20.0F), 0.0F, 1.0F);
}

}

EuconHost::EuconHost(const HWND notificationWindow) : notificationWindow_(notificationWindow)
{
    initializationError_ = static_cast<int>(EuConManager::Initialize());
    if (initializationError_ != kERR_OK)
    {
        return;
    }

    node_ = std::make_unique<FaderBridgeNode>();
    node_->Freeze();
    node_->SetPersistenceID(L"FaderBridge.WindowsAudio.2026");
    node_->SetAttribute(kATRIBID_SimpleFriendlyName, L"FaderBridge Windows Audio");
    node_->SetAttribute(kATRIBID_ProcessorMeterAPIVersion, kMeterAPIVersion_3_0);

    channels_.reserve(ChannelCount);
    for (int index = 0; index < ChannelCount; ++index)
    {
        const auto report = [this](const int channel, const float value, const int kind,
            const NEuCon::uint16 rawIndex, const float rawTableValue)
        {
            auto change = std::make_unique<SurfaceChange>();
            change->channel = channel;
            change->kind = kind;
            change->value = value;
            change->rawIndex = rawIndex;
            change->rawTableValue = rawTableValue;
            if (PostMessageW(notificationWindow_, kSurfaceChangeMessage, 0,
                reinterpret_cast<LPARAM>(change.get())))
            {
                change.release();
            }
        };
        auto channel = std::make_unique<EuconChannel>(index,
            L"",
            [report](const int ch, const float valueDb, const NEuCon::uint16 rawIndex,
                const float rawValue) { report(ch, FaderDbToVolume(valueDb), 0, rawIndex, rawValue); },
            [report](const int ch, const float value, const NEuCon::uint16 rawIndex,
                const float rawValue) { report(ch, value, 1, rawIndex, rawValue); },
            [report](const int ch, const float value, const NEuCon::uint16 rawIndex,
                const float rawValue) { report(ch, value, 2, rawIndex, rawValue); });
        channel->SetFaderDb(-144.5F);
        channel->SetKnobPosition(0.0F);
        channel->SetMeterDb(-120.0F);
        node_->RegisterProcessor(*channel);
        channels_.push_back(std::move(channel));
    }

    node_->Thaw();
    EuCon::GetInstance().RegisterNode(node_.get());
    ready_ = true;
    audioPipe_ = std::make_unique<AudioPipeClient>(notificationWindow_, kAudioFrameMessage);
    audioPipe_->Start();
}

EuconHost::~EuconHost()
{
    audioPipe_.reset();
    if (node_)
    {
        node_->Freeze();
        for (auto& channel : channels_)
        {
            node_->UnregisterProcessor(*channel);
        }
        channels_.clear();
        EuCon::GetInstance().UnregisterNode(*node_);
        node_.reset();
    }
    EuConManager::Destroy();
}

int EuconHost::ApplyAudioFrame(const AudioFrame& frame)
{
    if (!ready_)
    {
        return 0;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto fullRefresh = node_->ConsumeRefreshRequest();
    EuBatchedMeterWriter meterWriter(*node_);
    int activeCount = 0;
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
            ++activeCount;
            if (!cache.active || fullRefresh || cache.name != strip.name)
            {
                channel->SetName(strip.name);
                cache.name = strip.name;
            }

            const auto volumeMatchesPending = cache.volumePending &&
                std::fabs(strip.volume - cache.requestedVolume) <= kPendingMatch;
            if (volumeMatchesPending || (cache.volumePending && now >= cache.volumeHoldUntil))
            {
                cache.volumePending = false;
            }
            if (!cache.volumePending &&
                (!cache.active || fullRefresh || std::fabs(strip.volume - cache.volume) > kVolumeDifference))
            {
                channel->SetFaderDb(VolumeToFaderDb(strip.volume));
                channel->SetKnobPosition(strip.volume);
                cache.volume = strip.volume;
            }

            channel->WriteMeterDb(meterWriter, strip.peakDb);
            cache.peakDb = strip.peakDb;

            const auto muteMatchesPending = cache.mutePending && strip.muted == cache.requestedMute;
            if (muteMatchesPending || (cache.mutePending && now >= cache.muteHoldUntil))
            {
                cache.mutePending = false;
            }
            if (!cache.mutePending && (!cache.active || fullRefresh || strip.muted != cache.muted))
            {
                channel->SetMute(strip.muted);
                cache.muted = strip.muted;
            }
            cache.active = true;
        }
        else if (cache.active)
        {
            channel->SetName(L"");
            channel->SetFaderDb(-144.5F);
            channel->SetKnobPosition(0.0F);
            channel->WriteMeterDb(meterWriter, -120.0F);
            channel->SetMute(false);
            cache = ChannelCache{};
        }
    }
    return activeCount;
}

bool EuconHost::HandleSurfaceChange(const SurfaceChange& change)
{
    if (!audioPipe_ || change.channel < 0 || change.channel >= ChannelCount)
    {
        return false;
    }
    if (change.kind == 0 || change.kind == 1)
    {
        const auto volume = std::clamp(change.value, 0.0F, 1.0F);
        auto& cache = cache_[change.channel];
        cache.volumePending = true;
        cache.requestedVolume = volume;
        cache.volume = volume;
        cache.volumeHoldUntil = std::chrono::steady_clock::now() + kVolumeHold;
        if (change.kind == 1)
        {
            channels_[change.channel]->SetFaderDb(VolumeToFaderDb(volume));
        }
        const auto sent = audioPipe_->SendVolume(change.channel, volume);
        if (!sent)
        {
            cache.volumePending = false;
        }
        return sent;
    }
    if (change.kind == 2)
    {
        auto& cache = cache_[change.channel];
        cache.mutePending = true;
        cache.requestedMute = change.value != 0.0F;
        cache.muted = cache.requestedMute;
        cache.muteHoldUntil = std::chrono::steady_clock::now() + kMuteHold;
        channels_[change.channel]->SetMute(cache.requestedMute);
        const auto sent = audioPipe_->SendMute(change.channel, cache.requestedMute);
        if (!sent)
        {
            cache.mutePending = false;
        }
        return sent;
    }
    return false;
}
