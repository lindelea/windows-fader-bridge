#pragma once

#include <memory>
#include <string>

enum class MediaControlAction
{
    PlayPause,
    Previous,
    Next,
    Stop,
    Seek,
    Shuffle,
    Repeat,
};

struct WindowsMediaState
{
    bool available = false;
    bool playing = false;
    bool canPlayPause = false;
    bool canPrevious = false;
    bool canNext = false;
    bool canStop = false;
    bool hasPosition = false;
    bool canSeek = false;
    bool canShuffle = false;
    bool shuffle = false;
    bool canRepeat = false;
    int repeatMode = 0;
    float position = 0.0F;
    std::wstring sourceAppId;
    std::wstring title;
    std::wstring artist;
};

class WindowsMediaController final
{
public:
    WindowsMediaController();
    ~WindowsMediaController();

    WindowsMediaController(const WindowsMediaController&) = delete;
    WindowsMediaController& operator=(const WindowsMediaController&) = delete;

    bool Initialize();
    WindowsMediaState GetState(const std::wstring& executablePath,
        const std::wstring& packageFamilyName) const;
    bool Execute(const std::wstring& executablePath,
        const std::wstring& packageFamilyName, MediaControlAction action,
        float value) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
