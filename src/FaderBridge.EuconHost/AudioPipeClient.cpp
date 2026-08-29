#include "AudioPipeClient.h"

#include <array>
#include <cstring>
#include <memory>

namespace
{
constexpr wchar_t kPipePath[] = L"\\\\.\\pipe\\FaderBridge.Eucon.2026";
constexpr unsigned char kSnapshotMessage = 1;
constexpr unsigned char kSetVolumeMessage = 2;
constexpr unsigned char kSetMuteMessage = 3;
constexpr DWORD kMaximumPacketSize = 16U * 1024U;

template <typename T>
bool ReadValue(const std::vector<unsigned char>& payload, size_t& offset, T& value)
{
    if (offset + sizeof(T) > payload.size())
    {
        return false;
    }
    std::memcpy(&value, payload.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}
}

AudioPipeClient::AudioPipeClient(const HWND notificationWindow, const UINT snapshotMessage)
    : notificationWindow_(notificationWindow), snapshotMessage_(snapshotMessage)
{
}

AudioPipeClient::~AudioPipeClient()
{
    running_ = false;
    const auto pipe = pipe_.load();
    if (pipe != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(pipe, nullptr);
    }
    if (readerThread_.joinable())
    {
        readerThread_.join();
    }
    if (childJob_)
    {
        CloseHandle(childJob_);
    }
    if (childProcess_)
    {
        CloseHandle(childProcess_);
    }
}

void AudioPipeClient::Start()
{
    if (running_.exchange(true))
    {
        return;
    }
    StartAudioHostIfNeeded();
    readerThread_ = std::thread(&AudioPipeClient::Run, this);
}

void AudioPipeClient::StartAudioHostIfNeeded()
{
    if (WaitNamedPipeW(kPipePath, 100U))
    {
        return;
    }

    std::array<wchar_t, MAX_PATH> modulePath{};
    const auto length = GetModuleFileNameW(nullptr, modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length == modulePath.size())
    {
        return;
    }
    std::wstring audioHostPath(modulePath.data(), length);
    const auto separator = audioHostPath.find_last_of(L"\\/");
    audioHostPath.resize(separator == std::wstring::npos ? 0 : separator + 1);
    audioHostPath += L"FaderBridge.AudioHost.exe";
    if (GetFileAttributesW(audioHostPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + audioHostPath + L"\"";
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');
    if (!CreateProcessW(audioHostPath.c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
    {
        return;
    }

    CloseHandle(process.hThread);
    childProcess_ = process.hProcess;
    childJob_ = CreateJobObjectW(nullptr, nullptr);
    if (childJob_)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(childJob_, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(childJob_, childProcess_);
    }
}

void AudioPipeClient::Run()
{
    while (running_)
    {
        if (!WaitNamedPipeW(kPipePath, 1000U))
        {
            continue;
        }

        const auto pipe = CreateFileW(kPipePath, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        pipe_ = pipe;
        while (running_)
        {
            auto frame = std::make_unique<AudioFrame>();
            if (!ReadFrame(pipe, *frame))
            {
                break;
            }
            if (!PostMessageW(notificationWindow_, snapshotMessage_, 0,
                reinterpret_cast<LPARAM>(frame.get())))
            {
                break;
            }
            frame.release();
        }

        {
            std::scoped_lock lock(writeMutex_);
            pipe_ = INVALID_HANDLE_VALUE;
            CloseHandle(pipe);
        }
    }
}

bool AudioPipeClient::ReadFrame(const HANDLE pipe, AudioFrame& frame) const
{
    DWORD payloadSize = 0;
    if (!ReadExact(pipe, &payloadSize, sizeof(payloadSize)) ||
        payloadSize < 2U || payloadSize > kMaximumPacketSize)
    {
        return false;
    }

    std::vector<unsigned char> payload(payloadSize);
    if (!ReadExact(pipe, payload.data(), payloadSize))
    {
        return false;
    }

    size_t offset = 0;
    unsigned char messageType = 0;
    unsigned char count = 0;
    if (!ReadValue(payload, offset, messageType) || messageType != kSnapshotMessage ||
        !ReadValue(payload, offset, count) || count > 16U)
    {
        return false;
    }

    frame.strips.reserve(count);
    for (unsigned index = 0; index < count; ++index)
    {
        unsigned char slot = 0;
        unsigned char flags = 0;
        float volume = 0.0F;
        float peakDb = -120.0F;
        unsigned short nameLength = 0;
        if (!ReadValue(payload, offset, slot) || !ReadValue(payload, offset, flags) ||
            !ReadValue(payload, offset, volume) || !ReadValue(payload, offset, peakDb) ||
            !ReadValue(payload, offset, nameLength) || offset + nameLength > payload.size())
        {
            return false;
        }

        AudioStripState strip;
        strip.slot = slot;
        strip.active = (flags & 1U) != 0;
        strip.muted = (flags & 2U) != 0;
        strip.volume = volume;
        strip.peakDb = peakDb;
        strip.name = Utf8ToWide(reinterpret_cast<const char*>(payload.data() + offset), nameLength);
        offset += nameLength;
        frame.strips.push_back(std::move(strip));
    }
    return offset == payload.size();
}

bool AudioPipeClient::SendVolume(const int slot, const float volume)
{
    std::array<unsigned char, 6> payload{};
    payload[0] = kSetVolumeMessage;
    payload[1] = static_cast<unsigned char>(slot);
    std::memcpy(payload.data() + 2, &volume, sizeof(volume));
    return SendPacket(payload.data(), static_cast<DWORD>(payload.size()));
}

bool AudioPipeClient::SendMute(const int slot, const bool muted)
{
    const std::array<unsigned char, 3> payload{
        kSetMuteMessage,
        static_cast<unsigned char>(slot),
        static_cast<unsigned char>(muted ? 1 : 0)};
    return SendPacket(payload.data(), static_cast<DWORD>(payload.size()));
}

bool AudioPipeClient::SendPacket(const void* payload, const DWORD payloadSize)
{
    std::scoped_lock lock(writeMutex_);
    const auto pipe = pipe_.load();
    if (pipe == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD written = 0;
    return WriteFile(pipe, &payloadSize, sizeof(payloadSize), &written, nullptr) &&
        written == sizeof(payloadSize) &&
        WriteFile(pipe, payload, payloadSize, &written, nullptr) && written == payloadSize;
}

bool AudioPipeClient::ReadExact(const HANDLE pipe, void* buffer, const DWORD bytes)
{
    auto* destination = static_cast<unsigned char*>(buffer);
    DWORD offset = 0;
    while (offset < bytes)
    {
        DWORD read = 0;
        if (!ReadFile(pipe, destination + offset, bytes - offset, &read, nullptr) || read == 0)
        {
            return false;
        }
        offset += read;
    }
    return true;
}

std::wstring AudioPipeClient::Utf8ToWide(const char* data, const int length)
{
    if (length <= 0)
    {
        return {};
    }
    const auto wideLength = MultiByteToWideChar(CP_UTF8, 0, data, length, nullptr, 0);
    std::wstring value(wideLength, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, data, length, value.data(), wideLength);
    return value;
}
