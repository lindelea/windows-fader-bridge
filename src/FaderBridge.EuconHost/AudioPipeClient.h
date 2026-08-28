#pragma once

#include <Windows.h>

#include <atomic>
#include <mutex>
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
    std::wstring name;
};

struct AudioFrame
{
    std::vector<AudioStripState> strips;
};

class AudioPipeClient final
{
public:
    AudioPipeClient(HWND notificationWindow, UINT snapshotMessage);
    ~AudioPipeClient();

    AudioPipeClient(const AudioPipeClient&) = delete;
    AudioPipeClient& operator=(const AudioPipeClient&) = delete;

    void Start();
    bool SendVolume(int slot, float volume);
    bool SendMute(int slot, bool muted);

private:
    void Run();
    void StartAudioHostIfNeeded();
    bool ReadFrame(HANDLE pipe, AudioFrame& frame) const;
    bool SendPacket(const void* payload, DWORD payloadSize);
    static bool ReadExact(HANDLE pipe, void* buffer, DWORD bytes);
    static std::wstring Utf8ToWide(const char* data, int length);

    HWND notificationWindow_;
    UINT snapshotMessage_;
    std::atomic<bool> running_ = false;
    std::atomic<HANDLE> pipe_ = INVALID_HANDLE_VALUE;
    std::thread readerThread_;
    std::mutex writeMutex_;
    HANDLE childProcess_ = nullptr;
    HANDLE childJob_ = nullptr;
};
