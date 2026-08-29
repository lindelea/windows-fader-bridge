#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class AudioStripRole
{
    Application,
    OutputDevice,
    MasterOutput,
    InputDevice,
};

enum class AudioMeterRole
{
    Mono,
    Left,
    Right,
    Center,
    Lfe,
    LeftSurround,
    RightSurround,
    LeftBackSurround,
    RightBackSurround,
    CenterSurround,
    LeftCenter,
    RightCenter,
    Top,
    HeightLeftFront,
    HeightCenterFront,
    HeightRightFront,
    HeightLeftSurround,
    HeightCenterSurround,
    HeightRightSurround,
};

struct AudioStripState
{
    static constexpr std::uint32_t NoChannelColor = 0xFFFFFFFFU;
    static constexpr std::size_t MaxMeterChannels = 16U;

    int slot = 0;
    int sortGroup = 2;
    AudioStripRole role = AudioStripRole::Application;
    bool active = false;
    bool muted = false;
    bool defaultSelectable = false;
    bool isDefault = false;
    bool soloed = false;
    float volume = 0.0F;
    float peakDb = -120.0F;
    std::vector<float> meterDb;
    std::vector<AudioMeterRole> meterRoles;
    std::uint32_t channelColor = NoChannelColor;
    std::wstring key;
    std::wstring name;
    // Window activation identity is separate from audio-session identity.
    // Packaged apps and browsers commonly render their top-level window from
    // a process other than the one that owns the Core Audio session.
    std::vector<DWORD> focusProcessIds;
    std::wstring focusExecutablePath;
    std::wstring focusPackageFamilyName;
};

struct AudioFrame
{
    std::vector<AudioStripState> strips;
    bool monoAudioEnabled = false;
    bool anySolo = false;
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
    bool QueueSetDefault(int slot) noexcept;
    bool QueueToggleMonoAudio() noexcept;
    bool QueueToggleSolo(const std::wstring& trackKey);
    bool QueueClearSolo();
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
    std::atomic<int> pendingDefaultSlot_ = -1;
    std::atomic<unsigned long long> defaultVersion_ = 0;
    std::atomic<unsigned long long> monoToggleVersion_ = 0;
    struct SoloCommand
    {
        bool clear = false;
        std::wstring trackKey;
    };
    std::mutex soloCommandMutex_;
    std::vector<SoloCommand> pendingSoloCommands_;
    HANDLE wakeEvent_ = nullptr;
    std::thread worker_;
    std::unique_ptr<Impl> impl_;
};
