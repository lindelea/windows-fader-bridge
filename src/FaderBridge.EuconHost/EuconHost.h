#pragma once

#include "NativeAudioController.h"
#include "EuNode.h"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class EuconChannel;
class ExProcessorCommand;

constexpr UINT kSurfaceChangeMessage = WM_APP + 1;
constexpr UINT kAudioFrameMessage = WM_APP + 2;
constexpr UINT_PTR kMotorFlushTimerId = 0x4642U;

struct SurfaceChange
{
    int channel = 0;
    int kind = 0;
    float value = 0.0F;
    NEuCon::uint16 rawIndex = 0U;
    float rawTableValue = 0.0F;
    bool commandQueued = false;
};

class FaderBridgeNode final : public EuNode
{
public:
    void OnCallback(tEVT eventType, void* hidden, void* shown, void*, int&) override;
    bool ConsumeRefreshRequest() noexcept { return refreshRequested_.exchange(false); }

private:
    std::atomic<bool> refreshRequested_ = true;
};

class EuconHost final
{
public:
    static constexpr int MaxChannelCount = NativeAudioController::StripCount;

    explicit EuconHost(HWND notificationWindow);
    ~EuconHost();

    bool IsReady() const noexcept { return ready_; }
    int InitializationError() const noexcept { return initializationError_; }
    int ApplyAudioFrame(const AudioFrame& frame);
    bool HandleSurfaceChange(const SurfaceChange& change);
    void FlushPendingMotors();

private:
    struct ChannelCache
    {
        bool active = false;
        bool muted = false;
        bool volumePending = false;
        int pendingVolumeKind = 0;
        bool mutePending = false;
        float volume = 0.0F;
        float peakDb = -120.0F;
        float requestedVolume = 0.0F;
        bool requestedMute = false;
        int lastMotorIndex = -1;
        bool motorDispatchPending = false;
        float pendingMotorVolume = 0.0F;
        std::wstring name;
        std::chrono::steady_clock::time_point volumeHoldUntil{};
        std::chrono::steady_clock::time_point muteHoldUntil{};
    };

    HWND notificationWindow_;
    int initializationError_ = 0;
    bool ready_ = false;
    std::unique_ptr<FaderBridgeNode> node_;
    std::unique_ptr<ExProcessorCommand> commandProcessor_;
    std::vector<std::unique_ptr<EuconChannel>> channels_;
    std::array<ChannelCache, MaxChannelCount> cache_{};
    std::unique_ptr<NativeAudioController> audioController_;
    bool motorFlushTimerActive_ = false;

    void SetFaderFromWindows(int channel, float volume);
    void ScheduleFaderFromWindows(int channel, float volume);
    std::unique_ptr<EuconChannel> CreateChannel(int channel, const std::wstring& key,
        const std::wstring& name);
    void ReconcileChannelTopology(const AudioFrame& frame);
};
