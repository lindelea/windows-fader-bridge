#include "NativeAudioController.h"

#include <Audioclient.h>
#include <Audiopolicy.h>
#include <Endpointvolume.h>
#include <Mmdeviceapi.h>
#include <Shlwapi.h>
#include <appmodel.h>
#include <avrt.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr auto kMeterPeriod = std::chrono::milliseconds(30);
constexpr auto kDiscoveryPeriod = std::chrono::milliseconds(500);
constexpr auto kSlotRetention = std::chrono::seconds(5);

class SessionEvents final : public IAudioSessionEvents
{
public:
    SessionEvents(HANDLE wakeEvent, std::atomic_bool* changePending,
        std::atomic_bool* discoveryPending)
        : wakeEvent_(wakeEvent), changePending_(changePending),
          discoveryPending_(discoveryPending)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (!object)
        {
            return E_POINTER;
        }
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioSessionEvents))
        {
            *object = static_cast<IAudioSessionEvents*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return references_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto remaining = references_.fetch_sub(1U, std::memory_order_acq_rel) - 1U;
        if (remaining == 0U)
        {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR, LPCGUID) override
    {
        SignalDiscovery();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR, LPCGUID) override
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float, BOOL, LPCGUID) override
    {
        SignalChange();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD, float[], DWORD, LPCGUID) override
    {
        SignalChange();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID, LPCGUID) override
    {
        SignalDiscovery();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState) override
    {
        SignalDiscovery();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason) override
    {
        SignalDiscovery();
        return S_OK;
    }

private:
    void SignalChange() const
    {
        changePending_->store(true, std::memory_order_release);
        SetEvent(wakeEvent_);
    }

    void SignalDiscovery() const
    {
        discoveryPending_->store(true, std::memory_order_release);
        SignalChange();
    }

    std::atomic<ULONG> references_ = 1U;
    HANDLE wakeEvent_ = nullptr;
    std::atomic_bool* changePending_ = nullptr;
    std::atomic_bool* discoveryPending_ = nullptr;
};

float PeakToDb(const float peak)
{
    return peak <= 0.000001F ? -120.0F :
        std::clamp(20.0F * std::log10(peak), -120.0F, 0.0F);
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }
    const auto length = MultiByteToWideChar(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0)
    {
        return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), length);
    return result;
}

std::wstring PackageDisplayName(const std::wstring& packageFullName,
    const std::wstring& executablePath)
{
    static std::unordered_map<std::wstring, std::wstring> cache;
    if (packageFullName.empty())
    {
        return {};
    }
    if (const auto cached = cache.find(packageFullName); cached != cache.end())
    {
        return cached->second;
    }

    std::wstring result;
    auto lowerPath = executablePath;
    auto lowerPackage = packageFullName;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(),
        [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    std::transform(lowerPackage.begin(), lowerPackage.end(), lowerPackage.begin(),
        [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    const auto packageOffset = lowerPath.find(lowerPackage);
    if (packageOffset != std::wstring::npos)
    {
        const auto packageRoot = executablePath.substr(0,
            packageOffset + packageFullName.size());
        std::ifstream manifest(std::filesystem::path(packageRoot) / L"AppxManifest.xml",
            std::ios::in | std::ios::binary);
        std::ostringstream contents;
        contents << manifest.rdbuf();
        const auto xml = contents.str();
        auto lowerXml = xml;
        std::transform(lowerXml.begin(), lowerXml.end(), lowerXml.begin(),
            [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
        const auto properties = lowerXml.find("<properties");
        const auto displayTag = properties == std::string::npos
            ? std::string::npos : lowerXml.find("<displayname", properties);
        const auto valueStart = displayTag == std::string::npos
            ? std::string::npos : lowerXml.find('>', displayTag);
        const auto valueEnd = valueStart == std::string::npos
            ? std::string::npos : lowerXml.find("</displayname>", valueStart + 1U);
        if (valueStart != std::string::npos && valueEnd != std::string::npos)
        {
            auto displayValue = Utf8ToWide(xml.substr(valueStart + 1U,
                valueEnd - (valueStart + 1U)));
            constexpr wchar_t resourcePrefix[] = L"ms-resource:";
            if (displayValue.rfind(resourcePrefix, 0U) == 0U)
            {
                auto key = displayValue.substr(std::size(resourcePrefix) - 1U);
                while (!key.empty() && key.front() == L'/')
                {
                    key.erase(key.begin());
                }
                if (key.rfind(L"Resources/", 0U) != 0U &&
                    key.rfind(L"resources/", 0U) != 0U)
                {
                    key = L"Resources/" + key;
                }
                const auto source = L"@{" + packageFullName +
                    L"? ms-resource:///" + key + L"}";
                std::array<wchar_t, 512> resolved{};
                if (SUCCEEDED(SHLoadIndirectString(source.c_str(), resolved.data(),
                    static_cast<UINT>(resolved.size()), nullptr)))
                {
                    result = resolved.data();
                }
            }
            else
            {
                result = std::move(displayValue);
            }
        }
    }
    cache.emplace(packageFullName, result);
    return result;
}

std::wstring ProcessName(const DWORD processId)
{
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
    {
        // CoreAudio can retain an inactive session briefly after its owning
        // browser/helper process has exited. It is not a controllable app and
        // must not consume its own S3 strip.
        return {};
    }

    std::array<wchar_t, 32768> path{};
    DWORD length = static_cast<DWORD>(path.size());
    std::wstring result;
    std::wstring packageFullName;
    if (QueryFullProcessImageNameW(process, 0, path.data(), &length))
    {
        result.assign(path.data(), length);
        UINT32 packageLength = 0;
        if (GetPackageFullName(process, &packageLength, nullptr) == ERROR_INSUFFICIENT_BUFFER &&
            packageLength > 1U)
        {
            packageFullName.resize(packageLength);
            if (GetPackageFullName(process, &packageLength, packageFullName.data()) == ERROR_SUCCESS)
            {
                packageFullName.resize(packageLength > 0U ? packageLength - 1U : 0U);
            }
            else
            {
                packageFullName.clear();
            }
        }
        if (const auto packageName = PackageDisplayName(packageFullName, result);
            !packageName.empty())
        {
            CloseHandle(process);
            return packageName;
        }
        const auto separator = result.find_last_of(L"\\/");
        if (separator != std::wstring::npos)
        {
            result.erase(0, separator + 1);
        }
        const auto extension = result.find_last_of(L'.');
        if (extension != std::wstring::npos)
        {
            result.resize(extension);
        }
    }
    CloseHandle(process);
    return result;
}

std::wstring StableKey(std::wstring name)
{
    std::transform(name.begin(), name.end(), name.begin(),
        [](const wchar_t value) { return static_cast<wchar_t>(std::towupper(value)); });
    return name;
}
}

struct NativeAudioController::Impl
{
    struct Session
    {
        DWORD processId = 0;
        bool persistentAppIdentity = false;
        bool volumeObserved = false;
        float lastObservedVolume = 0.0F;
        bool muteObserved = false;
        bool lastObservedMute = false;
        std::wstring identifier;
        ComPtr<IAudioSessionControl> control;
        ComPtr<ISimpleAudioVolume> volume;
        ComPtr<IAudioMeterInformation> meter;
        ComPtr<IAudioSessionEvents> events;
    };

    struct Application
    {
        std::wstring key;
        std::wstring name;
        std::vector<Session> sessions;
    };

    struct Slot
    {
        std::wstring key;
        std::wstring name;
        std::vector<Session> sessions;
        std::wstring preferredVolumeSession;
        std::wstring preferredMuteSession;
        std::chrono::steady_clock::time_point lastSeen{};
    };

    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> endpoint;
    ComPtr<IAudioSessionManager2> manager;
    std::array<Slot, StripCount> slots;
    std::array<unsigned long long, StripCount> appliedVolumeVersions{};
    std::array<unsigned long long, StripCount> appliedMuteVersions{};
    HANDLE wakeEvent = nullptr;
    std::atomic_bool* changePending = nullptr;
    std::atomic_bool* discoveryPending = nullptr;

    ~Impl()
    {
        for (auto& slot : slots)
        {
            UnregisterSessions(slot.sessions);
        }
    }

    static void UnregisterSessions(std::vector<Session>& sessions)
    {
        for (auto& session : sessions)
        {
            if (session.control && session.events)
            {
                session.control->UnregisterAudioSessionNotification(session.events.Get());
            }
            session.events.Reset();
        }
    }

    void RegisterSession(Session& session) const
    {
        if (!session.control || !wakeEvent || !changePending || !discoveryPending)
        {
            return;
        }

        ComPtr<IAudioSessionEvents> events;
        events.Attach(new SessionEvents(wakeEvent, changePending, discoveryPending));
        if (SUCCEEDED(session.control->RegisterAudioSessionNotification(events.Get())))
        {
            session.events = std::move(events);
        }
    }

    bool Initialize()
    {
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator))))
        {
            return false;
        }
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &endpoint)))
        {
            return false;
        }
        return SUCCEEDED(endpoint->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
            nullptr, reinterpret_cast<void**>(manager.GetAddressOf())));
    }

    bool RefreshApplications()
    {
        ComPtr<IAudioSessionEnumerator> sessionEnumerator;
        if (!manager || FAILED(manager->GetSessionEnumerator(&sessionEnumerator)))
        {
            return false;
        }

        int count = 0;
        if (FAILED(sessionEnumerator->GetCount(&count)))
        {
            return false;
        }

        std::map<std::wstring, Application> applications;
        for (int index = 0; index < count; ++index)
        {
            ComPtr<IAudioSessionControl> control;
            if (FAILED(sessionEnumerator->GetSession(index, &control)))
            {
                continue;
            }
            ComPtr<IAudioSessionControl2> control2;
            if (FAILED(control.As(&control2)) || control2->IsSystemSoundsSession() == S_OK)
            {
                continue;
            }
            AudioSessionState state{};
            if (FAILED(control->GetState(&state)) || state == AudioSessionStateExpired)
            {
                continue;
            }

            DWORD processId = 0;
            if (FAILED(control2->GetProcessId(&processId)) || processId == 0)
            {
                continue;
            }
            const auto name = ProcessName(processId);
            if (name.empty())
            {
                continue;
            }
            const auto key = StableKey(name);

            ComPtr<ISimpleAudioVolume> volume;
            ComPtr<IAudioMeterInformation> meter;
            if (FAILED(control.As(&volume)))
            {
                continue;
            }
            control.As(&meter);

            LPWSTR sessionIdentifier = nullptr;
            std::wstring identifier;
            bool persistentAppIdentity = false;
            if (SUCCEEDED(control2->GetSessionIdentifier(&sessionIdentifier)) && sessionIdentifier)
            {
                // Windows Store/packaged applications expose a persistent
                // application-identity session ("|#%b{..."). Windows 11's
                // application volume mixer presents this session. The same
                // process may also expose a stale executable/PID fallback
                // session with a different volume.
                identifier = sessionIdentifier;
                persistentAppIdentity = identifier.find(L"|#%b{") != std::wstring::npos;
                CoTaskMemFree(sessionIdentifier);
            }

            auto& app = applications[key];
            app.key = key;
            app.name = name;
            app.sessions.push_back({ processId, persistentAppIdentity,
                false, 0.0F, false, false,
                std::move(identifier),
                std::move(control), std::move(volume), std::move(meter), nullptr });
        }

        const auto now = std::chrono::steady_clock::now();
        for (auto& slot : slots)
        {
            const auto found = applications.find(slot.key);
            if (found != applications.end())
            {
                slot.name = found->second.name;
                auto refreshedSessions = std::move(found->second.sessions);
                for (auto& refreshed : refreshedSessions)
                {
                    const auto previous = std::find_if(slot.sessions.begin(), slot.sessions.end(),
                        [&refreshed](const Session& session)
                        {
                            return !refreshed.identifier.empty() &&
                                session.identifier == refreshed.identifier;
                        });
                    if (previous != slot.sessions.end())
                    {
                        // Keep the original COM interfaces and callback alive.
                        // Re-registering every discovery pass creates a small
                        // notification gap that is visible during fast moves.
                        const auto processId = refreshed.processId;
                        const auto persistentAppIdentity = refreshed.persistentAppIdentity;
                        refreshed = std::move(*previous);
                        refreshed.processId = processId;
                        refreshed.persistentAppIdentity = persistentAppIdentity;
                    }
                    else
                    {
                        RegisterSession(refreshed);
                    }
                }
                // Moved-from entries are empty; only genuinely removed
                // sessions are unregistered here.
                UnregisterSessions(slot.sessions);
                slot.sessions = std::move(refreshedSessions);
                slot.lastSeen = now;
                applications.erase(found);
            }
            else
            {
                UnregisterSessions(slot.sessions);
                slot.sessions.clear();
                if (!slot.key.empty() && now - slot.lastSeen >= kSlotRetention)
                {
                    slot = Slot{};
                }
            }
        }

        for (auto& [key, application] : applications)
        {
            const auto empty = std::find_if(slots.begin(), slots.end(),
                [](const Slot& slot) { return slot.key.empty(); });
            if (empty == slots.end())
            {
                break;
            }
            empty->key = key;
            empty->name = application.name;
            empty->sessions = std::move(application.sessions);
            for (auto& session : empty->sessions)
            {
                RegisterSession(session);
            }
            empty->lastSeen = now;
        }
        return true;
    }

    bool ApplyVolume(const int slotIndex, const float value)
    {
        if (slotIndex < 0 || slotIndex >= StripCount || slots[slotIndex].sessions.empty())
        {
            return false;
        }
        bool changed = false;
        for (const auto& session : slots[slotIndex].sessions)
        {
            changed = SUCCEEDED(session.volume->SetMasterVolume(
                std::clamp(value, 0.0F, 1.0F), nullptr)) || changed;
        }
        return changed;
    }

    bool ApplyMute(const int slotIndex, const bool muted)
    {
        if (slotIndex < 0 || slotIndex >= StripCount || slots[slotIndex].sessions.empty())
        {
            return false;
        }
        bool changed = false;
        for (const auto& session : slots[slotIndex].sessions)
        {
            changed = SUCCEEDED(session.volume->SetMute(muted, nullptr)) || changed;
        }
        return changed;
    }

    std::unique_ptr<AudioFrame> MakeFrame()
    {
        auto frame = std::make_unique<AudioFrame>();
        frame->strips.reserve(StripCount);
        for (int index = 0; index < StripCount; ++index)
        {
            auto& slot = slots[index];
            AudioStripState strip;
            strip.slot = index;
            strip.active = !slot.sessions.empty();
            strip.key = strip.active ? slot.key : L"";
            strip.name = strip.active ? slot.name : L"";
            if (strip.active)
            {
                struct Observation
                {
                    Session* session = nullptr;
                    AudioSessionState state = AudioSessionStateInactive;
                    float volume = 0.0F;
                    bool volumeValid = false;
                    bool muted = false;
                };

                std::vector<Observation> observations;
                observations.reserve(slot.sessions.size());
                int changedSession = -1;
                int changedMuteSession = -1;
                float peak = 0.0F;

                for (auto& session : slot.sessions)
                {
                    Observation observation;
                    observation.session = &session;
                    if (session.control)
                    {
                        session.control->GetState(&observation.state);
                    }
                    if (SUCCEEDED(session.volume->GetMasterVolume(&observation.volume)))
                    {
                        observation.volumeValid = true;
                        if (session.volumeObserved &&
                            std::fabs(observation.volume - session.lastObservedVolume) > 0.0005F)
                        {
                            if (changedSession < 0 || session.persistentAppIdentity)
                            {
                                changedSession = static_cast<int>(observations.size());
                            }
                        }
                        session.lastObservedVolume = observation.volume;
                        session.volumeObserved = true;
                    }
                    BOOL muted = FALSE;
                    if (SUCCEEDED(session.volume->GetMute(&muted)))
                    {
                        observation.muted = muted != FALSE;
                        if (session.muteObserved &&
                            observation.muted != session.lastObservedMute)
                        {
                            if (changedMuteSession < 0 || session.persistentAppIdentity)
                            {
                                changedMuteSession = static_cast<int>(observations.size());
                            }
                        }
                        session.lastObservedMute = observation.muted;
                        session.muteObserved = true;
                    }
                    float sessionPeak = 0.0F;
                    if (session.meter)
                    {
                        session.meter->GetPeakValue(&sessionPeak);
                        peak = std::max(peak, sessionPeak);
                    }
                    observations.push_back(observation);
                }

                if (changedSession >= 0)
                {
                    slot.preferredVolumeSession =
                        observations[changedSession].session->identifier;
                }
                if (changedMuteSession >= 0)
                {
                    slot.preferredMuteSession =
                        observations[changedMuteSession].session->identifier;
                }

                auto selected = observations.end();
                if (!slot.preferredVolumeSession.empty())
                {
                    selected = std::find_if(observations.begin(), observations.end(),
                        [&slot](const Observation& observation)
                        {
                            return observation.session->identifier ==
                                slot.preferredVolumeSession;
                        });
                }
                if (selected == observations.end())
                {
                    selected = std::find_if(observations.begin(), observations.end(),
                        [](const Observation& observation)
                        {
                            return observation.session->persistentAppIdentity;
                        });
                }
                if (selected == observations.end())
                {
                    selected = std::find_if(observations.begin(), observations.end(),
                        [](const Observation& observation)
                        {
                            return observation.state == AudioSessionStateActive;
                        });
                }
                if (selected == observations.end() && !observations.empty())
                {
                    selected = observations.begin();
                }

                if (selected != observations.end())
                {
                    if (selected->volumeValid)
                    {
                        strip.volume = selected->volume;
                    }
                }

                auto selectedMute = observations.end();
                if (!slot.preferredMuteSession.empty())
                {
                    selectedMute = std::find_if(observations.begin(), observations.end(),
                        [&slot](const Observation& observation)
                        {
                            return observation.session->identifier ==
                                slot.preferredMuteSession;
                        });
                }
                if (selectedMute == observations.end())
                {
                    selectedMute = std::find_if(observations.begin(), observations.end(),
                        [](const Observation& observation)
                        {
                            return observation.session->persistentAppIdentity;
                        });
                }
                if (selectedMute == observations.end())
                {
                    selectedMute = std::find_if(observations.begin(), observations.end(),
                        [](const Observation& observation)
                        {
                            return observation.state == AudioSessionStateActive;
                        });
                }
                if (selectedMute == observations.end() && !observations.empty())
                {
                    selectedMute = observations.begin();
                }
                if (selectedMute != observations.end())
                {
                    strip.muted = selectedMute->muted;
                }
                strip.peakDb = PeakToDb(peak);
            }
            frame->strips.push_back(std::move(strip));
        }
        return frame;
    }
};

NativeAudioController::NativeAudioController(const HWND notificationWindow,
    const UINT snapshotMessage)
    : notificationWindow_(notificationWindow), snapshotMessage_(snapshotMessage),
      wakeEvent_(CreateEventW(nullptr, FALSE, FALSE, nullptr)), impl_(std::make_unique<Impl>())
{
}

NativeAudioController::~NativeAudioController()
{
    running_ = false;
    if (wakeEvent_)
    {
        SetEvent(wakeEvent_);
    }
    if (worker_.joinable())
    {
        worker_.join();
    }
    if (wakeEvent_)
    {
        CloseHandle(wakeEvent_);
    }
}

void NativeAudioController::Start()
{
    if (!wakeEvent_ || running_.exchange(true))
    {
        return;
    }
    worker_ = std::thread(&NativeAudioController::Run, this);
}

bool NativeAudioController::QueueVolume(const int slot, const float volume) noexcept
{
    if (!running_ || slot < 0 || slot >= StripCount)
    {
        return false;
    }
    pendingVolumes_[slot].store(std::clamp(volume, 0.0F, 1.0F), std::memory_order_release);
    volumeVersions_[slot].fetch_add(1, std::memory_order_acq_rel);
    SetEvent(wakeEvent_);
    return true;
}

bool NativeAudioController::QueueMute(const int slot, const bool muted) noexcept
{
    if (!running_ || slot < 0 || slot >= StripCount)
    {
        return false;
    }
    pendingMutes_[slot].store(muted ? 1 : 0, std::memory_order_release);
    muteVersions_[slot].fetch_add(1, std::memory_order_acq_rel);
    SetEvent(wakeEvent_);
    return true;
}

void NativeAudioController::Run()
{
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        running_ = false;
        return;
    }

    DWORD mmcssTaskIndex = 0;
    const auto mmcssHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcssTaskIndex);
    impl_->wakeEvent = wakeEvent_;
    impl_->changePending = &sessionChangePending_;
    impl_->discoveryPending = &sessionDiscoveryPending_;
    if (!impl_->Initialize())
    {
        if (mmcssHandle)
        {
            AvRevertMmThreadCharacteristics(mmcssHandle);
        }
        CoUninitialize();
        running_ = false;
        return;
    }

    impl_->RefreshApplications();
    ready_ = true;
    auto nextMeter = std::chrono::steady_clock::now();
    auto nextDiscovery = nextMeter + kDiscoveryPeriod;

    while (running_)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto waitUntil = std::min(nextMeter, nextDiscovery);
        const auto waitDuration = waitUntil > now
            ? std::chrono::duration_cast<std::chrono::milliseconds>(waitUntil - now)
            : std::chrono::milliseconds(0);
        WaitForSingleObject(wakeEvent_, static_cast<DWORD>(std::clamp<long long>(
            waitDuration.count(), 0, 30)));

        for (int slot = 0; slot < StripCount; ++slot)
        {
            const auto volumeVersion = volumeVersions_[slot].load(std::memory_order_acquire);
            if (volumeVersion != impl_->appliedVolumeVersions[slot] &&
                impl_->ApplyVolume(slot, pendingVolumes_[slot].load(std::memory_order_acquire)))
            {
                impl_->appliedVolumeVersions[slot] = volumeVersion;
            }

            const auto muteVersion = muteVersions_[slot].load(std::memory_order_acquire);
            if (muteVersion != impl_->appliedMuteVersions[slot] &&
                impl_->ApplyMute(slot, pendingMutes_[slot].load(std::memory_order_acquire) != 0))
            {
                impl_->appliedMuteVersions[slot] = muteVersion;
            }
        }

        const auto afterCommands = std::chrono::steady_clock::now();
        const auto discoveryEvent = sessionDiscoveryPending_.exchange(false,
            std::memory_order_acq_rel);
        if (discoveryEvent || afterCommands >= nextDiscovery)
        {
            impl_->RefreshApplications();
            nextDiscovery = afterCommands + kDiscoveryPeriod;
        }
        const auto volumeEvent = sessionChangePending_.exchange(false,
            std::memory_order_acq_rel);
        if (volumeEvent || afterCommands >= nextMeter)
        {
            auto frame = impl_->MakeFrame();
            if (PostMessageW(notificationWindow_, snapshotMessage_, 0,
                reinterpret_cast<LPARAM>(frame.get())))
            {
                frame.release();
            }
            if (afterCommands >= nextMeter)
            {
                nextMeter = afterCommands + kMeterPeriod;
            }
        }
    }

    ready_ = false;
    if (mmcssHandle)
    {
        AvRevertMmThreadCharacteristics(mmcssHandle);
    }
    impl_.reset();
    CoUninitialize();
}
