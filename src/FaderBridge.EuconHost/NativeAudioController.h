#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct AudioStripState
{
    int slot = 0;
    bool active = false;
    bool muted = false;
    float volume = 0.0F;
    float peakDb = -120.0F;
    std::wstring key;
    std::wstring name;
};

struct AudioFrame
{
    std::vector<AudioStripState> strips;
};

class NativeAudioController final
{
public:
    // EUCON surfaces bank over a virtual channel list. This is an internal
    // safety ceiling, not the number of faders on an attached EUCON surface.
    static constexpr int StripCount = 128;

    NativeAudioController(HWND notificationWindow, UINT snapshotMessage);
    ~NativeAudioController();

    NativeAudioController(const NativeAudioController&) = delete;
    NativeAudioController& operator=(const NativeAudioController&) = delete;

    void Start();
    bool QueueVolume(int slot, float volume) noexcept;
    bool QueueMute(int slot, bool muted) noexcept;
    bool IsReady() const noexcept { return ready_.load(); }

private:
    struct Impl;

    void Run();

    HWND notificationWindow_;
    UINT snapshotMessage_;
    std::atomic_bool running_ = false;
    std::atomic_bool ready_ = false;
    std::atomic_bool sessionChangePending_ = false;
    std::atomic_bool sessionDiscoveryPending_ = false;
    std::array<std::atomic<float>, StripCount> pendingVolumes_{};
    std::array<std::atomic<unsigned long long>, StripCount> volumeVersions_{};
    std::array<std::atomic<int>, StripCount> pendingMutes_{};
    std::array<std::atomic<unsigned long long>, StripCount> muteVersions_{};
    HANDLE wakeEvent_ = nullptr;
    std::thread worker_;
    std::unique_ptr<Impl> impl_;
};
