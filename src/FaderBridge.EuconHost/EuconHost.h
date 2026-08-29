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
class WindowsCommandProcessor;
class WindowsSystemProcessor;

constexpr UINT kSurfaceChangeMessage = WM_APP + 1;
constexpr UINT kAudioFrameMessage = WM_APP + 2;
constexpr UINT_PTR kMotorFlushTimerId = 0x4642U;

struct SurfaceChange
{
    int channel = 0;
    std::wstring trackKey;
    int kind = 0;
    float value = 0.0F;
    NEuCon::uint16 rawIndex = 0U;
    float rawTableValue = 0.0F;
    bool commandQueued = false;
};

class FaderBridgeNode final : public EuNode
{
public:
    explicit FaderBridgeNode(HWND notificationWindow) : notificationWindow_(notificationWindow) {}
    void OnCallback(tEVT eventType, void* hidden, void* shown, void*, int&) override;
    bool ConsumeRefreshRequest() noexcept { return refreshRequested_.exchange(false); }

private:
    HWND notificationWindow_ = nullptr;
    std::atomic<bool> refreshRequested_ = true;
    std::atomic<unsigned long long> attentionSuppressedUntil_ = 0ULL;
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
        bool isDefault = false;
        bool soloed = false;
        bool selected = false;
        bool focusable = false;
        int trackType = 0;
        float volume = 0.0F;
        float peakDb = -120.0F;
        float requestedVolume = 0.0F;
        bool requestedMute = false;
        int lastMotorIndex = -1;
        bool motorDispatchPending = false;
        float pendingMotorVolume = 0.0F;
        std::wstring name;
        std::vector<DWORD> focusProcessIds;
        std::wstring focusExecutablePath;
        std::wstring focusPackageFamilyName;
        std::chrono::steady_clock::time_point volumeHoldUntil{};
        std::chrono::steady_clock::time_point muteHoldUntil{};
    };

    struct TrackRoute
    {
        std::atomic<int> audioSlot = -1;
        std::atomic<int> channelOrder = 0;
    };

    struct TrackState
    {
        std::wstring key;
        std::shared_ptr<TrackRoute> route;
        std::unique_ptr<EuconChannel> channel;
        ChannelCache cache;
    };

    HWND notificationWindow_;
    int initializationError_ = 0;
    bool ready_ = false;
    std::unique_ptr<FaderBridgeNode> node_;
    std::unique_ptr<WindowsCommandProcessor> commandProcessor_;
    std::unique_ptr<WindowsSystemProcessor> systemProcessor_;
    std::vector<std::unique_ptr<TrackState>> tracks_;
    std::unique_ptr<NativeAudioController> audioController_;
    bool motorFlushTimerActive_ = false;
    std::wstring selectedTrackKey_;

    TrackState* FindTrack(const std::wstring& key) noexcept;
    void SetFaderFromWindows(TrackState& track, float volume);
    void ScheduleFaderFromWindows(TrackState& track, float volume);
    std::unique_ptr<TrackState> CreateTrack(int channelOrder,
        const AudioStripState& strip);
    void ReconcileChannelTopology(const AudioFrame& frame);
    bool SelectAndFocusTrack(TrackState& track);
    bool DeselectAndMinimizeTrack(TrackState& track);
};
