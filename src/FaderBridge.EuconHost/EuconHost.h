#pragma once

#include "EuNode.h"
#include "AudioPipeClient.h"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class EuconChannel;

constexpr UINT kSurfaceChangeMessage = WM_APP + 1;
constexpr UINT kAudioFrameMessage = WM_APP + 2;

struct SurfaceChange
{
    int channel = 0;
    int kind = 0;
    float value = 0.0F;
};

class FaderBridgeNode final : public EuNode
{
public:
    void OnCallback(tEVT eventType, void*, void*, void*, int&) override
    {
        if (eventType == kEVT_NODE_SurfaceNodeAdded || eventType == kEVT_NODE_SurfaceNodeRemoved)
        {
            refreshRequested_.store(true);
        }
    }

    bool ConsumeRefreshRequest() noexcept { return refreshRequested_.exchange(false); }

private:
    std::atomic<bool> refreshRequested_ = true;
};

class EuconHost final
{
public:
    static constexpr int ChannelCount = 16;

    explicit EuconHost(HWND notificationWindow);
    ~EuconHost();

    bool IsReady() const noexcept { return ready_; }
    int InitializationError() const noexcept { return initializationError_; }
    int ApplyAudioFrame(const AudioFrame& frame);
    bool HandleSurfaceChange(const SurfaceChange& change);

private:
    struct ChannelCache
    {
        bool active = false;
        bool muted = false;
        bool volumePending = false;
        bool mutePending = false;
        float volume = 0.0F;
        float peakDb = -120.0F;
        float requestedVolume = 0.0F;
        bool requestedMute = false;
        std::wstring name;
        std::chrono::steady_clock::time_point volumeHoldUntil{};
        std::chrono::steady_clock::time_point muteHoldUntil{};
    };

    HWND notificationWindow_;
    int initializationError_ = 0;
    bool ready_ = false;
    std::unique_ptr<FaderBridgeNode> node_;
    std::vector<std::unique_ptr<EuconChannel>> channels_;
    std::unique_ptr<AudioPipeClient> audioPipe_;
    std::array<ChannelCache, ChannelCount> cache_{};
};
