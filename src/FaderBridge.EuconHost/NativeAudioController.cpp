#include "NativeAudioController.h"
#include "DiagnosticLog.h"

#include <Audioclient.h>
#include <Audiopolicy.h>
#include <Endpointvolume.h>
#include <Mmdeviceapi.h>
#include <Ksmedia.h>
#include <Propsys.h>
#include <Roapi.h>
#include <ShObjIdl.h>
#include <Shlwapi.h>
#include <appmodel.h>
#include <avrt.h>
#include <wrl/client.h>
#include <winstring.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr auto kMeterPeriod = std::chrono::milliseconds(30);
constexpr auto kDiscoveryPeriod = std::chrono::milliseconds(500);
constexpr auto kSlotRetention = std::chrono::seconds(5);
const PROPERTYKEY kDeviceFriendlyName =
{ { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

// Windows exposes endpoint enumeration/volume publicly, but still provides no
// public desktop API for changing the system default endpoint. PolicyConfig is
// the compatibility boundary used by established Windows audio utilities. It
// is isolated here so the EUCON model remains independent of this Windows ABI.
struct DeviceShareMode
{
    DWORD mode;
    BOOL unknown;
};

MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(LPCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(LPCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(LPCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(LPCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(LPCWSTR, INT, INT64*, INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(LPCWSTR, INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(LPCWSTR, DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(LPCWSTR, DeviceShareMode*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(LPCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(LPCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(LPCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(LPCWSTR, INT) = 0;
};

const CLSID CLSID_PolicyConfigClient =
{ 0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 } };

// Windows 11's Volume Mixer uses this internal WinRT policy interface for
// per-application input/output routing. Microsoft changed the IID in Windows
// 10 21H2; keep both known contracts behind one isolated compatibility class.
// The ABI was verified read-only on the current host before integration.
MIDL_INTERFACE("ab3d4648-e242-459f-b02f-541c70306324")
IAudioPolicyConfigFactoryCurrent : public IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE Reserved01() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved02() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved03() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved04() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved05() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved06() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved07() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved08() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved09() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved10() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved11() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved12() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved13() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved14() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved15() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved16() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved17() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved18() = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved19() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPersistedDefaultAudioEndpoint(
        UINT32 processId, EDataFlow flow, ERole role, HSTRING deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPersistedDefaultAudioEndpoint(
        UINT32 processId, EDataFlow flow, ERole role, HSTRING* deviceId) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAllPersistedApplicationDefaultEndpoints() = 0;
};

class PerApplicationAudioPolicy final
{
public:
    bool Initialize()
    {
        constexpr wchar_t runtimeClass[] = L"Windows.Media.Internal.AudioPolicyConfig";
        HSTRING className = nullptr;
        auto result = WindowsCreateString(runtimeClass,
            static_cast<UINT32>(std::size(runtimeClass) - 1U), &className);
        void* raw = nullptr;
        if (SUCCEEDED(result))
        {
            result = RoGetActivationFactory(className,
                __uuidof(IAudioPolicyConfigFactoryCurrent), &raw);
        }
        if (FAILED(result))
        {
            static const IID downlevelIid =
            { 0x2a59116d, 0x6c4f, 0x45e0,
              { 0xa7, 0x4f, 0x70, 0x7e, 0x3f, 0xef, 0x92, 0x58 } };
            result = RoGetActivationFactory(className, downlevelIid, &raw);
        }
        if (className) WindowsDeleteString(className);
        if (SUCCEEDED(result) && raw)
        {
            policy_.Attach(reinterpret_cast<IAudioPolicyConfigFactoryCurrent*>(raw));
        }
        FB_TRACE("APP_ROUTE_POLICY_INIT hr=%08X ready=%d",
            static_cast<unsigned>(result), policy_ ? 1 : 0);
        return policy_ != nullptr;
    }

    std::wstring Get(const DWORD processId, const EDataFlow flow) const
    {
        if (!policy_ || processId == 0U) return {};
        HSTRING packedId = nullptr;
        const auto result = policy_->GetPersistedDefaultAudioEndpoint(
            processId, flow, eConsole, &packedId);
        std::wstring value;
        if (SUCCEEDED(result) && packedId)
        {
            UINT32 length = 0U;
            const auto* text = WindowsGetStringRawBuffer(packedId, &length);
            value.assign(text, length);
            WindowsDeleteString(packedId);
        }
        constexpr wchar_t prefix[] = L"\\\\?\\SWD#MMDEVAPI#";
        constexpr wchar_t renderSuffix[] = L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}";
        constexpr wchar_t captureSuffix[] = L"#{2eef81be-33fa-4800-9670-1cd474972c3f}";
        if (value.rfind(prefix, 0U) == 0U) value.erase(0U, std::size(prefix) - 1U);
        const auto& suffix = flow == eRender ? renderSuffix : captureSuffix;
        const auto suffixLength = flow == eRender
            ? std::size(renderSuffix) - 1U : std::size(captureSuffix) - 1U;
        if (value.size() >= suffixLength &&
            value.compare(value.size() - suffixLength, suffixLength, suffix) == 0)
        {
            value.resize(value.size() - suffixLength);
        }
        return value;
    }

    bool Set(const DWORD processId, const EDataFlow flow,
        const std::wstring& endpointId) const
    {
        if (!policy_ || processId == 0U) return false;
        HSTRING packedId = nullptr;
        std::wstring packed;
        if (!endpointId.empty())
        {
            packed = L"\\\\?\\SWD#MMDEVAPI#" + endpointId +
                (flow == eRender ? L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}" :
                    L"#{2eef81be-33fa-4800-9670-1cd474972c3f}");
            if (FAILED(WindowsCreateString(packed.c_str(),
                static_cast<UINT32>(packed.size()), &packedId)))
            {
                return false;
            }
        }
        const auto console = policy_->SetPersistedDefaultAudioEndpoint(
            processId, flow, eConsole, packedId);
        const auto multimedia = policy_->SetPersistedDefaultAudioEndpoint(
            processId, flow, eMultimedia, packedId);
        const auto communications = policy_->SetPersistedDefaultAudioEndpoint(
            processId, flow, eCommunications, packedId);
        if (packedId) WindowsDeleteString(packedId);
        const auto changed = SUCCEEDED(console) && SUCCEEDED(multimedia) &&
            SUCCEEDED(communications);
        FB_TRACE("APP_ROUTE_POLICY_SET pid=%u flow=%s endpoint=%ls console=%08X multimedia=%08X communications=%08X changed=%d",
            static_cast<unsigned>(processId), flow == eRender ? "render" : "capture",
            endpointId.empty() ? L"default" : endpointId.c_str(),
            static_cast<unsigned>(console), static_cast<unsigned>(multimedia),
            static_cast<unsigned>(communications), changed ? 1 : 0);
        return changed;
    }

private:
    ComPtr<IAudioPolicyConfigFactoryCurrent> policy_;
};

enum class SlotKind
{
    Empty,
    Application,
    RenderEndpoint,
    CaptureEndpoint,
};

class MonoAudioSetting final
{
public:
    ~MonoAudioSetting()
    {
        item_.Reset();
        if (module_)
        {
            FreeLibrary(module_);
        }
    }

    bool Initialize()
    {
        module_ = LoadLibraryExW(L"SettingsHandlers_Accessibility.dll", nullptr,
            LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module_)
        {
            FB_TRACE("MONO_SETTING_INIT load=%08X", GetLastError());
            return false;
        }
        using GetSettingFn = HRESULT(WINAPI*)(HSTRING, IInspectable**);
        const auto getSetting = reinterpret_cast<GetSettingFn>(
            GetProcAddress(module_, "GetSetting"));
        if (!getSetting)
        {
            FB_TRACE("MONO_SETTING_INIT export=%08X", GetLastError());
            return false;
        }

        constexpr wchar_t settingName[] =
            L"SystemSettings_Accessibility_IsAudioMonoMixStateEnabled";
        HSTRING settingId = nullptr;
        auto result = WindowsCreateString(settingName,
            static_cast<UINT32>(std::size(settingName) - 1U), &settingId);
        ComPtr<IInspectable> returned;
        if (SUCCEEDED(result))
        {
            result = getSetting(settingId, returned.GetAddressOf());
        }
        if (settingId)
        {
            WindowsDeleteString(settingId);
        }
        if (FAILED(result) || !returned)
        {
            FB_TRACE("MONO_SETTING_INIT get=%08X", static_cast<unsigned>(result));
            return false;
        }

        // GetSetting returns SystemSettings.DataModel.ISettingItem. This is a
        // Windows-internal compatibility boundary, so require its stable IID
        // before using the interface's boolean Value ABI.
        static const IID iidSettingItem =
        { 0x40c037cc, 0xd8bf, 0x489e,
          { 0x86, 0x97, 0xd6, 0x6b, 0xaa, 0x32, 0x21, 0xbf } };
        result = returned->QueryInterface(iidSettingItem,
            reinterpret_cast<void**>(item_.GetAddressOf()));
        FB_TRACE("MONO_SETTING_INIT query=%08X ready=%d",
            static_cast<unsigned>(result), item_ ? 1 : 0);
        return SUCCEEDED(result) && item_;
    }

    bool Get(bool& enabled) const noexcept
    {
        if (!item_)
        {
            return false;
        }
        using GetValueFn = bool(__fastcall*)(void*);
        const auto vtable = *reinterpret_cast<void***>(item_.Get());
        const auto getValue = reinterpret_cast<GetValueFn>(vtable[33]);
        enabled = getValue(item_.Get());
        return true;
    }

    bool Set(const bool enabled) const noexcept
    {
        if (!item_)
        {
            return false;
        }
        using SetValueFn = HRESULT(__fastcall*)(void*, bool);
        const auto vtable = *reinterpret_cast<void***>(item_.Get());
        const auto setValue = reinterpret_cast<SetValueFn>(vtable[27]);
        const auto result = setValue(item_.Get(), enabled);
        bool confirmed = false;
        const auto read = Get(confirmed);
        FB_TRACE("MONO_SETTING_WRITE enabled=%d hr=%08X read=%d confirmed=%d",
            enabled ? 1 : 0, static_cast<unsigned>(result), read ? 1 : 0,
            confirmed ? 1 : 0);
        return SUCCEEDED(result) && read && confirmed == enabled;
    }

private:
    HMODULE module_ = nullptr;
    ComPtr<IInspectable> item_;
};

class EndpointNotifications final : public IMMNotificationClient
{
public:
    EndpointNotifications(HANDLE wakeEvent, std::atomic_bool* changePending,
        std::atomic_bool* discoveryPending)
        : wakeEvent_(wakeEvent), changePending_(changePending),
          discoveryPending_(discoveryPending)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient))
        {
            *object = static_cast<IMMNotificationClient*>(this);
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
        if (remaining == 0U) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { Signal(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { Signal(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { Signal(); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override
    {
        Signal(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override
    {
        Signal(); return S_OK;
    }

private:
    void Signal() const
    {
        discoveryPending_->store(true, std::memory_order_release);
        changePending_->store(true, std::memory_order_release);
        SetEvent(wakeEvent_);
    }
    std::atomic<ULONG> references_ = 1U;
    HANDLE wakeEvent_ = nullptr;
    std::atomic_bool* changePending_ = nullptr;
    std::atomic_bool* discoveryPending_ = nullptr;
};

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

AudioMeterRole SpeakerRole(const DWORD speaker)
{
    switch (speaker)
    {
    case SPEAKER_FRONT_LEFT: return AudioMeterRole::Left;
    case SPEAKER_FRONT_RIGHT: return AudioMeterRole::Right;
    case SPEAKER_FRONT_CENTER: return AudioMeterRole::Center;
    case SPEAKER_LOW_FREQUENCY: return AudioMeterRole::Lfe;
    case SPEAKER_BACK_LEFT: return AudioMeterRole::LeftBackSurround;
    case SPEAKER_BACK_RIGHT: return AudioMeterRole::RightBackSurround;
    case SPEAKER_FRONT_LEFT_OF_CENTER: return AudioMeterRole::LeftCenter;
    case SPEAKER_FRONT_RIGHT_OF_CENTER: return AudioMeterRole::RightCenter;
    case SPEAKER_BACK_CENTER: return AudioMeterRole::CenterSurround;
    case SPEAKER_SIDE_LEFT: return AudioMeterRole::LeftSurround;
    case SPEAKER_SIDE_RIGHT: return AudioMeterRole::RightSurround;
    case SPEAKER_TOP_CENTER: return AudioMeterRole::Top;
    case SPEAKER_TOP_FRONT_LEFT: return AudioMeterRole::HeightLeftFront;
    case SPEAKER_TOP_FRONT_CENTER: return AudioMeterRole::HeightCenterFront;
    case SPEAKER_TOP_FRONT_RIGHT: return AudioMeterRole::HeightRightFront;
    case SPEAKER_TOP_BACK_LEFT: return AudioMeterRole::HeightLeftSurround;
    case SPEAKER_TOP_BACK_CENTER: return AudioMeterRole::HeightCenterSurround;
    case SPEAKER_TOP_BACK_RIGHT: return AudioMeterRole::HeightRightSurround;
    default: return AudioMeterRole::Mono;
    }
}

std::vector<AudioMeterRole> DefaultMeterRoles(const UINT channels)
{
    if (channels <= 1U)
    {
        return { AudioMeterRole::Mono };
    }
    static constexpr AudioMeterRole order[] =
    {
        AudioMeterRole::Left, AudioMeterRole::Right, AudioMeterRole::Center,
        AudioMeterRole::Lfe, AudioMeterRole::LeftSurround, AudioMeterRole::RightSurround,
        AudioMeterRole::LeftBackSurround, AudioMeterRole::RightBackSurround,
        AudioMeterRole::Top, AudioMeterRole::HeightLeftFront,
        AudioMeterRole::HeightCenterFront, AudioMeterRole::HeightRightFront,
        AudioMeterRole::HeightLeftSurround, AudioMeterRole::HeightCenterSurround,
        AudioMeterRole::HeightRightSurround, AudioMeterRole::CenterSurround,
    };
    const auto count = std::min<std::size_t>(channels, std::size(order));
    return { std::begin(order), std::begin(order) + count };
}

std::vector<AudioMeterRole> MeterRolesForFormat(const WAVEFORMATEX* format)
{
    if (!format || format->nChannels == 0U)
    {
        return { AudioMeterRole::Mono };
    }
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
    {
        return DefaultMeterRoles(format->nChannels);
    }
    const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    std::vector<AudioMeterRole> result;
    result.reserve(format->nChannels);
    for (DWORD bit = 1U; bit != 0U && result.size() < format->nChannels; bit <<= 1U)
    {
        if ((extensible->dwChannelMask & bit) != 0U)
        {
            result.push_back(SpeakerRole(bit));
        }
    }
    return result.size() == format->nChannels
        ? result : DefaultMeterRoles(format->nChannels);
}

std::vector<AudioMeterRole> EndpointMeterRoles(IMMDevice* device)
{
    ComPtr<IAudioClient> client;
    WAVEFORMATEX* format = nullptr;
    std::vector<AudioMeterRole> result;
    if (device && SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(client.GetAddressOf()))) &&
        SUCCEEDED(client->GetMixFormat(&format)))
    {
        result = MeterRolesForFormat(format);
    }
    if (format)
    {
        CoTaskMemFree(format);
    }
    return result;
}

void MergeMeterPeaks(IAudioMeterInformation* meter, std::vector<float>& peaks)
{
    if (!meter)
    {
        return;
    }
    UINT count = 0U;
    if (FAILED(meter->GetMeteringChannelCount(&count)) || count == 0U)
    {
        return;
    }
    count = std::min<UINT>(count,
        static_cast<UINT>(AudioStripState::MaxMeterChannels));
    std::vector<float> values(count, 0.0F);
    if (FAILED(meter->GetChannelsPeakValues(count, values.data())))
    {
        return;
    }
    if (peaks.size() < values.size())
    {
        peaks.resize(values.size(), 0.0F);
    }
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        peaks[index] = std::max(peaks[index], values[index]);
    }
}

float BalanceFromChannelLevels(const float left, const float right)
{
    const auto maximum = std::max(left, right);
    if (maximum <= 0.0001F || std::fabs(left - right) <= 0.0005F)
    {
        return 0.0F;
    }
    return left > right
        ? -(1.0F - right / maximum)
        : 1.0F - left / maximum;
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

std::filesystem::path PackageRoot(const std::wstring& packageFullName,
    const std::wstring& executablePath)
{
    auto lowerPath = executablePath;
    auto lowerPackage = packageFullName;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(),
        [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    std::transform(lowerPackage.begin(), lowerPackage.end(), lowerPackage.begin(),
        [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    const auto packageOffset = lowerPath.find(lowerPackage);
    return packageOffset == std::wstring::npos ? std::filesystem::path{} :
        std::filesystem::path(executablePath.substr(0,
            packageOffset + packageFullName.size()));
}

std::string ReadPackageManifest(const std::filesystem::path& packageRoot)
{
    std::ifstream manifest(packageRoot / L"AppxManifest.xml", std::ios::in | std::ios::binary);
    std::ostringstream contents;
    contents << manifest.rdbuf();
    return contents.str();
}

std::filesystem::path PackageLogoPath(const std::wstring& packageFullName,
    const std::wstring& executablePath)
{
    const auto packageRoot = PackageRoot(packageFullName, executablePath);
    if (packageRoot.empty())
    {
        return {};
    }
    const auto xml = ReadPackageManifest(packageRoot);
    auto lowerXml = xml;
    std::transform(lowerXml.begin(), lowerXml.end(), lowerXml.begin(),
        [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    constexpr std::string_view attribute = "square44x44logo=\"";
    const auto attributeStart = lowerXml.find(attribute);
    if (attributeStart == std::string::npos)
    {
        return {};
    }
    const auto valueStart = attributeStart + attribute.size();
    const auto valueEnd = lowerXml.find('"', valueStart);
    if (valueEnd == std::string::npos)
    {
        return {};
    }
    auto relative = std::filesystem::path(Utf8ToWide(
        xml.substr(valueStart, valueEnd - valueStart)));
    auto exact = packageRoot / relative;
    if (std::filesystem::exists(exact))
    {
        return exact;
    }

    const auto directory = exact.parent_path();
    const auto stem = exact.stem().wstring();
    const std::wstring preferredNames[] =
    {
        stem + L".targetsize-256_altform-unplated.png",
        stem + L".targetsize-96_altform-unplated.png",
        stem + L".scale-400.png",
        stem + L".scale-200.png",
        stem + L".scale-100.png",
    };
    for (const auto& name : preferredNames)
    {
        const auto candidate = directory / name;
        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }
    }
    return {};
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
    const auto packageRoot = PackageRoot(packageFullName, executablePath);
    if (!packageRoot.empty())
    {
        const auto xml = ReadPackageManifest(packageRoot);
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

struct ProcessMetadata
{
    std::wstring name;
    std::wstring executablePath;
    std::wstring packageFamilyName;
    std::uint32_t channelColor = AudioStripState::NoChannelColor;
};

struct ColorSample
{
    double hue = 0.0;
    double saturation = 0.0;
    double value = 0.0;
    double weight = 0.0;
};

std::uint32_t HsvToRgb(const double hue, const double saturation, const double value)
{
    const auto chroma = value * saturation;
    const auto sector = hue / 60.0;
    const auto x = chroma * (1.0 - std::fabs(std::fmod(sector, 2.0) - 1.0));
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    if (sector < 1.0)
    {
        red = chroma; green = x;
    }
    else if (sector < 2.0)
    {
        red = x; green = chroma;
    }
    else if (sector < 3.0)
    {
        green = chroma; blue = x;
    }
    else if (sector < 4.0)
    {
        green = x; blue = chroma;
    }
    else if (sector < 5.0)
    {
        red = x; blue = chroma;
    }
    else
    {
        red = chroma; blue = x;
    }
    const auto match = value - chroma;
    const auto toByte = [match](const double component)
    {
        return static_cast<std::uint32_t>(std::lround(
            std::clamp(component + match, 0.0, 1.0) * 255.0));
    };
    return (toByte(red) << 16U) | (toByte(green) << 8U) | toByte(blue);
}

std::optional<std::uint32_t> ExtractIconColor(const std::wstring& executablePath)
{
    static std::unordered_map<std::wstring, std::optional<std::uint32_t>> cache;
    if (executablePath.empty())
    {
        return std::nullopt;
    }
    if (const auto found = cache.find(executablePath); found != cache.end())
    {
        return found->second;
    }

    std::optional<std::uint32_t> result;
    ComPtr<IShellItemImageFactory> imageFactory;
    if (SUCCEEDED(SHCreateItemFromParsingName(executablePath.c_str(), nullptr,
            IID_PPV_ARGS(imageFactory.GetAddressOf()))))
    {
        HBITMAP bitmap = nullptr;
        constexpr SIZE imageSize{ 64, 64 };
        auto lowerPath = executablePath;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(),
            [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
        const auto extension = std::filesystem::path(lowerPath).extension().wstring();
        const auto isImage = extension == L".png" || extension == L".jpg" ||
            extension == L".jpeg" || extension == L".bmp";
        const auto imageFlags = isImage
            ? static_cast<SIIGBF>(SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK | SIIGBF_SCALEUP)
            : static_cast<SIIGBF>(SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK);
        if (SUCCEEDED(imageFactory->GetImage(imageSize, imageFlags, &bitmap)) && bitmap)
        {
            BITMAP object{};
            if (GetObjectW(bitmap, sizeof(object), &object) != 0 &&
                object.bmWidth > 0 && object.bmHeight > 0)
            {
                BITMAPINFO information{};
                information.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                information.bmiHeader.biWidth = object.bmWidth;
                information.bmiHeader.biHeight = -object.bmHeight;
                information.bmiHeader.biPlanes = 1;
                information.bmiHeader.biBitCount = 32;
                information.bmiHeader.biCompression = BI_RGB;
                std::vector<std::uint32_t> pixels(
                    static_cast<size_t>(object.bmWidth) * static_cast<size_t>(object.bmHeight));
                const auto screen = GetDC(nullptr);
                const auto rows = screen ? GetDIBits(screen, bitmap, 0,
                    static_cast<UINT>(object.bmHeight), pixels.data(), &information,
                    DIB_RGB_COLORS) : 0;
                if (screen)
                {
                    ReleaseDC(nullptr, screen);
                }

                if (rows != 0)
                {
                    constexpr int binCount = 24;
                    constexpr double degreesPerBin = 360.0 / static_cast<double>(binCount);
                    std::array<double, binCount> histogram{};
                    std::vector<ColorSample> samples;
                    samples.reserve(pixels.size());
                    double chromaticCoverage = 0.0;
                    double brightNeutralCoverage = 0.0;
                    for (const auto pixel : pixels)
                    {
                        const auto blue = static_cast<double>(pixel & 0xFFU) / 255.0;
                        const auto green = static_cast<double>((pixel >> 8U) & 0xFFU) / 255.0;
                        const auto red = static_cast<double>((pixel >> 16U) & 0xFFU) / 255.0;
                        auto alpha = static_cast<double>((pixel >> 24U) & 0xFFU) / 255.0;
                        if (alpha == 0.0 && (red > 0.0 || green > 0.0 || blue > 0.0))
                        {
                            alpha = 1.0;
                        }
                        const auto maximum = std::max({ red, green, blue });
                        const auto minimum = std::min({ red, green, blue });
                        const auto delta = maximum - minimum;
                        const auto saturation = maximum <= 0.0 ? 0.0 : delta / maximum;
                        if (alpha < 0.1 || maximum < 0.18)
                        {
                            continue;
                        }
                        const auto coverage = alpha * (0.35 + (0.65 * maximum));
                        if (saturation < 0.18)
                        {
                            if (maximum >= 0.62)
                            {
                                brightNeutralCoverage += coverage;
                            }
                            continue;
                        }
                        chromaticCoverage += coverage;

                        double hue = 0.0;
                        if (delta > 0.0)
                        {
                            if (maximum == red)
                            {
                                hue = 60.0 * std::fmod((green - blue) / delta, 6.0);
                            }
                            else if (maximum == green)
                            {
                                hue = 60.0 * (((blue - red) / delta) + 2.0);
                            }
                            else
                            {
                                hue = 60.0 * (((red - green) / delta) + 4.0);
                            }
                            if (hue < 0.0)
                            {
                                hue += 360.0;
                            }
                        }
                        const auto weight = alpha * saturation * saturation *
                            (0.35 + (0.65 * maximum));
                        const auto bin = std::min(binCount - 1,
                            static_cast<int>(hue / degreesPerBin));
                        histogram[bin] += weight;
                        samples.push_back({ hue, saturation, maximum, weight });
                    }

                    // A genuinely white/grey application icon is meaningful
                    // identity, not a failed color extraction. Preserve it
                    // when neutral bright pixels form the dominant foreground.
                    if (brightNeutralCoverage > chromaticCoverage * 1.35 &&
                        brightNeutralCoverage > 1.0)
                    {
                        result = 0x00FFFFFFU;
                    }
                    else if (!samples.empty())
                    {
                        int dominantBin = 0;
                        double dominantWeight = -1.0;
                        for (int bin = 0; bin < binCount; ++bin)
                        {
                            const auto neighborhood = histogram[(bin + binCount - 1) % binCount] +
                                histogram[bin] + histogram[(bin + 1) % binCount];
                            if (neighborhood > dominantWeight)
                            {
                                dominantWeight = neighborhood;
                                dominantBin = bin;
                            }
                        }

                        constexpr double pi = 3.14159265358979323846;
                        double sine = 0.0;
                        double cosine = 0.0;
                        double saturation = 0.0;
                        double value = 0.0;
                        double totalWeight = 0.0;
                        for (const auto& sample : samples)
                        {
                            const auto sampleBin = std::min(binCount - 1,
                                static_cast<int>(sample.hue / degreesPerBin));
                            auto distance = std::abs(sampleBin - dominantBin);
                            distance = std::min(distance, binCount - distance);
                            if (distance > 1)
                            {
                                continue;
                            }
                            const auto radians = sample.hue * pi / 180.0;
                            sine += std::sin(radians) * sample.weight;
                            cosine += std::cos(radians) * sample.weight;
                            saturation += sample.saturation * sample.weight;
                            value += sample.value * sample.weight;
                            totalWeight += sample.weight;
                        }
                        if (totalWeight > 0.0)
                        {
                            auto hue = std::atan2(sine, cosine) * 180.0 / pi;
                            if (hue < 0.0)
                            {
                                hue += 360.0;
                            }
                            saturation = std::max(0.72, saturation / totalWeight);
                            value = std::clamp(value / totalWeight, 0.72, 1.0);
                            result = HsvToRgb(hue, saturation, value);
                        }
                    }
                }
            }
            DeleteObject(bitmap);
        }
    }
    cache.emplace(executablePath, result);
    return result;
}

ProcessMetadata ProcessInformation(const DWORD processId)
{
    const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
    {
        // CoreAudio can retain an inactive session briefly after its owning
        // browser/helper process has exited. It is not a controllable app and
        // must not consume its own EUCON channel strip.
        return {};
    }

    std::array<wchar_t, 32768> path{};
    DWORD length = static_cast<DWORD>(path.size());
    ProcessMetadata result;
    std::wstring executablePath;
    std::wstring packageFullName;
    if (QueryFullProcessImageNameW(process, 0, path.data(), &length))
    {
        executablePath.assign(path.data(), length);
        result.executablePath = executablePath;
        UINT32 familyLength = 0;
        if (GetPackageFamilyName(process, &familyLength, nullptr) ==
                ERROR_INSUFFICIENT_BUFFER && familyLength > 1U)
        {
            result.packageFamilyName.resize(familyLength);
            if (GetPackageFamilyName(process, &familyLength,
                    result.packageFamilyName.data()) == ERROR_SUCCESS)
            {
                result.packageFamilyName.resize(
                    familyLength > 0U ? familyLength - 1U : 0U);
            }
            else
            {
                result.packageFamilyName.clear();
            }
        }
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
        if (const auto packageName = PackageDisplayName(packageFullName, executablePath);
            !packageName.empty())
        {
            result.name = packageName;
        }
        else
        {
            result.name = executablePath;
            const auto separator = result.name.find_last_of(L"\\/");
            if (separator != std::wstring::npos)
            {
                result.name.erase(0, separator + 1);
            }
            const auto extension = result.name.find_last_of(L'.');
            if (extension != std::wstring::npos)
            {
                result.name.resize(extension);
            }
        }
        auto colorSource = executablePath;
        if (const auto packageLogo = PackageLogoPath(packageFullName, executablePath);
            !packageLogo.empty())
        {
            colorSource = packageLogo.wstring();
        }
        if (const auto color = ExtractIconColor(colorSource))
        {
            result.channelColor = *color;
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
        std::wstring endpointId;
        bool persistentAppIdentity = false;
        bool volumeObserved = false;
        float lastObservedVolume = 0.0F;
        bool muteObserved = false;
        bool lastObservedMute = false;
        bool panObserved = false;
        float lastObservedPan = 0.0F;
        std::wstring identifier;
        ComPtr<IAudioSessionControl> control;
        ComPtr<ISimpleAudioVolume> volume;
        ComPtr<IChannelAudioVolume> channelVolume;
        ComPtr<IAudioMeterInformation> meter;
        ComPtr<IAudioSessionEvents> events;
    };

    struct Application
    {
        std::wstring key;
        std::wstring name;
        std::wstring focusExecutablePath;
        std::wstring focusPackageFamilyName;
        std::uint32_t channelColor = AudioStripState::NoChannelColor;
        std::vector<Session> sessions;
    };

    struct Slot
    {
        SlotKind kind = SlotKind::Empty;
        std::wstring key;
        std::wstring name;
        std::wstring endpointId;
        std::wstring focusExecutablePath;
        std::wstring focusPackageFamilyName;
        std::uint32_t channelColor = AudioStripState::NoChannelColor;
        std::vector<AudioMeterRole> meterRoles;
        std::vector<Session> sessions;
        ComPtr<IMMDevice> endpointDevice;
        ComPtr<IAudioEndpointVolume> endpointVolume;
        ComPtr<IAudioMeterInformation> endpointMeter;
        ComPtr<IAudioClient> captureMeterClient;
        ComPtr<IAudioCaptureClient> captureMeterReader;
        bool captureMeterOpenAttempted = false;
        bool isDefault = false;
        std::wstring preferredVolumeSession;
        std::wstring preferredMuteSession;
        std::wstring outputRouteId;
        std::wstring inputRouteId;
        std::chrono::steady_clock::time_point lastSeen{};
    };

    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> endpoint;
    ComPtr<IAudioSessionManager2> manager;
    ComPtr<IMMNotificationClient> endpointNotifications;
    std::wstring sessionEndpointId;
    std::vector<AudioMeterRole> sessionMeterRoles;
    std::wstring endpointDiagnosticSignature;
    std::array<Slot, StripCount> slots;
    std::array<unsigned long long, StripCount> appliedVolumeVersions{};
    std::array<unsigned long long, StripCount> appliedPanVersions{};
    std::array<unsigned long long, StripCount> appliedMuteVersions{};
    unsigned long long appliedDefaultVersion = 0;
    unsigned long long appliedMonoToggleVersion = 0;
    bool soloActive = false;
    std::wstring soloTargetKey;
    std::unordered_map<std::wstring, bool> preSoloMutes;
    MonoAudioSetting monoAudioSetting;
    PerApplicationAudioPolicy applicationRoutePolicy;
    HANDLE wakeEvent = nullptr;
    std::atomic_bool* changePending = nullptr;
    std::atomic_bool* discoveryPending = nullptr;

    ~Impl()
    {
        for (auto& slot : slots)
        {
            UnregisterSessions(slot.sessions);
        }
        if (enumerator && endpointNotifications)
        {
            enumerator->UnregisterEndpointNotificationCallback(endpointNotifications.Get());
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

    void EnsureCaptureMeterStream(Slot& slot) const
    {
        if (slot.kind != SlotKind::CaptureEndpoint || slot.captureMeterOpenAttempted)
        {
            return;
        }
        slot.captureMeterOpenAttempted = true;

        DWORD hardwareSupport = 0;
        if (slot.endpointMeter &&
            SUCCEEDED(slot.endpointMeter->QueryHardwareSupport(&hardwareSupport)) &&
            (hardwareSupport & ENDPOINT_HARDWARE_SUPPORT_METER) != 0)
        {
            FB_TRACE("CAPTURE_METER_READY mode=hardware");
            return;
        }

        ComPtr<IAudioClient> client;
        auto result = slot.endpointDevice
            ? slot.endpointDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(client.GetAddressOf()))
            : E_POINTER;
        WAVEFORMATEX* mixFormat = nullptr;
        if (SUCCEEDED(result))
        {
            result = client->GetMixFormat(&mixFormat);
        }
        if (SUCCEEDED(result))
        {
            constexpr REFERENCE_TIME kCaptureBufferDuration = 1000000; // 100 ms
            result = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_NOPERSIST, kCaptureBufferDuration, 0,
                mixFormat, nullptr);
        }
        if (mixFormat)
        {
            CoTaskMemFree(mixFormat);
        }

        ComPtr<IAudioCaptureClient> reader;
        if (SUCCEEDED(result))
        {
            result = client->GetService(IID_PPV_ARGS(&reader));
        }
        if (SUCCEEDED(result))
        {
            result = client->Start();
        }
        if (SUCCEEDED(result))
        {
            slot.captureMeterClient = std::move(client);
            slot.captureMeterReader = std::move(reader);
        }
        FB_TRACE("CAPTURE_METER_OPEN mode=shared hr=%08X active=%d",
            static_cast<unsigned>(result), slot.captureMeterReader ? 1 : 0);
    }

    void DrainCaptureMeterStream(Slot& slot) const
    {
        if (!slot.captureMeterReader)
        {
            return;
        }

        UINT32 packetFrames = 0;
        auto result = slot.captureMeterReader->GetNextPacketSize(&packetFrames);
        for (int packet = 0; SUCCEEDED(result) && packetFrames > 0 && packet < 64; ++packet)
        {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            result = slot.captureMeterReader->GetBuffer(
                &data, &frames, &flags, nullptr, nullptr);
            if (FAILED(result))
            {
                break;
            }
            result = slot.captureMeterReader->ReleaseBuffer(frames);
            if (SUCCEEDED(result))
            {
                result = slot.captureMeterReader->GetNextPacketSize(&packetFrames);
            }
        }
        if (FAILED(result))
        {
            FB_TRACE("CAPTURE_METER_DRAIN hr=%08X", static_cast<unsigned>(result));
            if (slot.captureMeterClient)
            {
                slot.captureMeterClient->Stop();
            }
            slot.captureMeterReader.Reset();
            slot.captureMeterClient.Reset();
            slot.captureMeterOpenAttempted = false;
        }
    }

    bool Initialize()
    {
        monoAudioSetting.Initialize();
        applicationRoutePolicy.Initialize();
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator))))
        {
            return false;
        }
        endpointNotifications.Attach(new EndpointNotifications(
            wakeEvent, changePending, discoveryPending));
        if (FAILED(enumerator->RegisterEndpointNotificationCallback(endpointNotifications.Get())))
        {
            return false;
        }
        return EnsureDefaultRenderManager();
    }

    bool EnsureDefaultRenderManager()
    {
        ComPtr<IMMDevice> current;
        if (!enumerator || FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &current)))
        {
            return false;
        }
        LPWSTR rawId = nullptr;
        if (FAILED(current->GetId(&rawId)) || !rawId)
        {
            return false;
        }
        const std::wstring currentId(rawId);
        CoTaskMemFree(rawId);
        if (manager && currentId == sessionEndpointId)
        {
            return true;
        }

        for (auto& slot : slots)
        {
            if (slot.kind == SlotKind::Application)
            {
                UnregisterSessions(slot.sessions);
                slot.sessions.clear();
            }
        }
        manager.Reset();
        endpoint.Reset();
        if (FAILED(current->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
            nullptr, reinterpret_cast<void**>(manager.GetAddressOf()))))
        {
            sessionEndpointId.clear();
            return false;
        }
        endpoint = std::move(current);
        sessionEndpointId = currentId;
        sessionMeterRoles = EndpointMeterRoles(endpoint.Get());
        FB_TRACE("SESSION_METER_FORMAT channels=%u",
            static_cast<unsigned>(sessionMeterRoles.size()));
        return true;
    }

    bool RefreshApplications()
    {
        struct SessionSource
        {
            std::wstring endpointId;
            ComPtr<IAudioSessionEnumerator> enumerator;
        };
        std::vector<SessionSource> sources;
        for (const auto& endpointSlot : slots)
        {
            if (endpointSlot.kind != SlotKind::RenderEndpoint ||
                !endpointSlot.endpointDevice)
            {
                continue;
            }
            ComPtr<IAudioSessionManager2> endpointManager;
            ComPtr<IAudioSessionEnumerator> sessionEnumerator;
            if (SUCCEEDED(endpointSlot.endpointDevice->Activate(
                    __uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                    reinterpret_cast<void**>(endpointManager.GetAddressOf()))) &&
                endpointManager &&
                SUCCEEDED(endpointManager->GetSessionEnumerator(&sessionEnumerator)) &&
                sessionEnumerator)
            {
                sources.push_back({ endpointSlot.endpointId, std::move(sessionEnumerator) });
            }
        }
        if (sources.empty()) return false;

        std::map<std::wstring, Application> applications;
        for (auto& source : sources)
        {
            int count = 0;
            if (FAILED(source.enumerator->GetCount(&count))) continue;
            for (int index = 0; index < count; ++index)
            {
            ComPtr<IAudioSessionControl> control;
            if (FAILED(source.enumerator->GetSession(index, &control)))
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
            const auto process = ProcessInformation(processId);
            if (process.name.empty())
            {
                continue;
            }
            const auto key = StableKey(process.name);

            ComPtr<ISimpleAudioVolume> volume;
            ComPtr<IChannelAudioVolume> channelVolume;
            ComPtr<IAudioMeterInformation> meter;
            if (FAILED(control.As(&volume)))
            {
                continue;
            }
            control.As(&meter);
            control.As(&channelVolume);

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
            app.name = process.name;
            if (app.focusExecutablePath.empty())
            {
                app.focusExecutablePath = process.executablePath;
            }
            if (app.focusPackageFamilyName.empty())
            {
                app.focusPackageFamilyName = process.packageFamilyName;
            }
            if (app.channelColor == AudioStripState::NoChannelColor)
            {
                app.channelColor = process.channelColor;
            }
            Session session;
            session.processId = processId;
            session.endpointId = source.endpointId;
            session.persistentAppIdentity = persistentAppIdentity;
            session.identifier = std::move(identifier);
            session.control = std::move(control);
            session.volume = std::move(volume);
            session.channelVolume = std::move(channelVolume);
            session.meter = std::move(meter);
            app.sessions.push_back(std::move(session));
            }
        }

        const auto now = std::chrono::steady_clock::now();
        for (auto& slot : slots)
        {
            if (slot.kind != SlotKind::Application)
            {
                continue;
            }
            const auto found = applications.find(slot.key);
            if (found != applications.end())
            {
                slot.name = found->second.name;
                slot.channelColor = found->second.channelColor;
                slot.focusExecutablePath = found->second.focusExecutablePath;
                slot.focusPackageFamilyName = found->second.focusPackageFamilyName;
                auto refreshedSessions = std::move(found->second.sessions);
                for (auto& refreshed : refreshedSessions)
                {
                    const auto previous = std::find_if(slot.sessions.begin(), slot.sessions.end(),
                        [&refreshed](const Session& session)
                        {
                            return !refreshed.identifier.empty() &&
                                session.identifier == refreshed.identifier &&
                                session.endpointId == refreshed.endpointId;
                        });
                    if (previous != slot.sessions.end())
                    {
                        // Keep the original COM interfaces and callback alive.
                        // Re-registering every discovery pass creates a small
                        // notification gap that is visible during fast moves.
                        const auto processId = refreshed.processId;
                        const auto persistentAppIdentity = refreshed.persistentAppIdentity;
                        const auto endpointId = refreshed.endpointId;
                        refreshed = std::move(*previous);
                        refreshed.processId = processId;
                        refreshed.persistentAppIdentity = persistentAppIdentity;
                        refreshed.endpointId = endpointId;
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
                [](const Slot& slot) { return slot.kind == SlotKind::Empty; });
            if (empty == slots.end())
            {
                break;
            }
            empty->kind = SlotKind::Application;
            empty->key = key;
            empty->name = application.name;
            empty->channelColor = application.channelColor;
            empty->focusExecutablePath = std::move(application.focusExecutablePath);
            empty->focusPackageFamilyName = std::move(application.focusPackageFamilyName);
            empty->sessions = std::move(application.sessions);
            for (auto& session : empty->sessions)
            {
                RegisterSession(session);
            }
            empty->lastSeen = now;
        }
        for (auto& slot : slots)
        {
            if (slot.kind != SlotKind::Application || slot.sessions.empty()) continue;
            const auto preferred = std::find_if(slot.sessions.begin(), slot.sessions.end(),
                [](const Session& session) { return session.persistentAppIdentity; });
            const auto processId = (preferred != slot.sessions.end() ? preferred :
                slot.sessions.begin())->processId;
            slot.outputRouteId = applicationRoutePolicy.Get(processId, eRender);
            slot.inputRouteId = applicationRoutePolicy.Get(processId, eCapture);
        }
        return true;
    }

    bool RefreshEndpoints()
    {
        struct EndpointDescription
        {
            SlotKind kind = SlotKind::Empty;
            std::wstring id;
            std::wstring name;
            bool isDefault = false;
            std::vector<AudioMeterRole> meterRoles;
            ComPtr<IMMDevice> device;
            ComPtr<IAudioEndpointVolume> volume;
            ComPtr<IAudioMeterInformation> meter;
        };

        const auto defaultId = [this](const EDataFlow flow)
        {
            std::wstring result;
            ComPtr<IMMDevice> device;
            LPWSTR raw = nullptr;
            if (enumerator && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device)) &&
                device && SUCCEEDED(device->GetId(&raw)) && raw)
            {
                result = raw;
                CoTaskMemFree(raw);
            }
            return result;
        };
        const auto defaultRender = defaultId(eRender);
        const auto defaultCapture = defaultId(eCapture);

        ComPtr<IMMDeviceCollection> collection;
        if (!enumerator || FAILED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE,
            &collection)))
        {
            return false;
        }
        UINT count = 0;
        if (FAILED(collection->GetCount(&count)))
        {
            return false;
        }
        std::map<std::wstring, EndpointDescription> available;
        for (UINT index = 0; index < count; ++index)
        {
            ComPtr<IMMDevice> device;
            if (FAILED(collection->Item(index, &device))) continue;
            LPWSTR rawId = nullptr;
            if (FAILED(device->GetId(&rawId)) || !rawId) continue;
            std::wstring id(rawId);
            CoTaskMemFree(rawId);

            ComPtr<IMMEndpoint> endpointInfo;
            EDataFlow flow = eAll;
            if (FAILED(device.As(&endpointInfo)) || FAILED(endpointInfo->GetDataFlow(&flow)))
            {
                continue;
            }
            ComPtr<IPropertyStore> properties;
            PROPVARIANT friendly;
            PropVariantInit(&friendly);
            std::wstring name = L"Windows audio device";
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) && properties &&
                SUCCEEDED(properties->GetValue(kDeviceFriendlyName, &friendly)) &&
                friendly.vt == VT_LPWSTR && friendly.pwszVal)
            {
                name = friendly.pwszVal;
            }
            PropVariantClear(&friendly);

            ComPtr<IAudioEndpointVolume> volume;
            if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(volume.GetAddressOf()))))
            {
                continue;
            }
            ComPtr<IAudioMeterInformation> meter;
            device->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(meter.GetAddressOf()));

            EndpointDescription description;
            description.kind = flow == eRender ? SlotKind::RenderEndpoint :
                SlotKind::CaptureEndpoint;
            description.id = id;
            description.name = std::move(name);
            description.isDefault = flow == eRender ? id == defaultRender : id == defaultCapture;
            description.meterRoles = EndpointMeterRoles(device.Get());
            description.device = std::move(device);
            description.volume = std::move(volume);
            description.meter = std::move(meter);
            available.emplace(id, std::move(description));
        }

        for (auto& slot : slots)
        {
            if (slot.kind != SlotKind::RenderEndpoint && slot.kind != SlotKind::CaptureEndpoint)
            {
                continue;
            }
            const auto found = available.find(slot.endpointId);
            if (found == available.end())
            {
                slot = Slot{};
                continue;
            }
            auto& source = found->second;
            slot.kind = source.kind;
            slot.name = source.name;
            slot.isDefault = source.isDefault;
            slot.meterRoles = source.meterRoles;
            EnsureCaptureMeterStream(slot);
            // Preserve the established endpoint interfaces across polling
            // passes. Re-activating them every 500 ms creates avoidable COM
            // churn and can interrupt a fast surface gesture.
            available.erase(found);
        }

        for (auto& [id, source] : available)
        {
            const auto empty = std::find_if(slots.begin(), slots.end(),
                [](const Slot& slot) { return slot.kind == SlotKind::Empty; });
            if (empty == slots.end()) break;
            empty->kind = source.kind;
            empty->endpointId = id;
            empty->key = (source.kind == SlotKind::RenderEndpoint ? L"ENDPOINT:RENDER:" :
                L"ENDPOINT:CAPTURE:") + id;
            empty->name = std::move(source.name);
            empty->channelColor = source.kind == SlotKind::RenderEndpoint
                ? 0x003E9BFFU : 0x0034C98FU;
            empty->isDefault = source.isDefault;
            empty->meterRoles = std::move(source.meterRoles);
            empty->endpointDevice = std::move(source.device);
            empty->endpointVolume = std::move(source.volume);
            empty->endpointMeter = std::move(source.meter);
            EnsureCaptureMeterStream(*empty);
        }
        const auto renderCount = std::count_if(slots.begin(), slots.end(), [](const Slot& slot)
        {
            return slot.kind == SlotKind::RenderEndpoint;
        });
        const auto captureCount = std::count_if(slots.begin(), slots.end(), [](const Slot& slot)
        {
            return slot.kind == SlotKind::CaptureEndpoint;
        });
        const auto signature = defaultRender + L"|" + defaultCapture + L"|" +
            std::to_wstring(renderCount) + L"|" + std::to_wstring(captureCount);
        if (signature != endpointDiagnosticSignature)
        {
            endpointDiagnosticSignature = signature;
            FB_TRACE("ENDPOINT_REFRESH render=%u capture=%u",
                static_cast<unsigned>(renderCount), static_cast<unsigned>(captureCount));
        }
        return true;
    }

    bool ApplyDefaultEndpoint(const int slotIndex)
    {
        FB_TRACE("DEFAULT_ENDPOINT_BEGIN slot=%d", slotIndex);
        if (slotIndex < 0 || slotIndex >= StripCount)
        {
            FB_TRACE("DEFAULT_ENDPOINT_REJECT slot=%d reason=range", slotIndex);
            return false;
        }
        const auto& slot = slots[slotIndex];
        if ((slot.kind != SlotKind::RenderEndpoint && slot.kind != SlotKind::CaptureEndpoint) ||
            slot.endpointId.empty())
        {
            FB_TRACE("DEFAULT_ENDPOINT_REJECT slot=%d reason=not-endpoint kind=%d id=%d",
                slotIndex, static_cast<int>(slot.kind), slot.endpointId.empty() ? 0 : 1);
            return false;
        }
        ComPtr<IPolicyConfig> policy;
        const auto createResult = CoCreateInstance(CLSID_PolicyConfigClient, nullptr,
            CLSCTX_ALL, __uuidof(IPolicyConfig),
            reinterpret_cast<void**>(policy.GetAddressOf()));
        if (FAILED(createResult))
        {
            FB_TRACE("DEFAULT_ENDPOINT_REJECT slot=%d reason=policy hr=%08X", slotIndex,
                static_cast<unsigned>(createResult));
            return false;
        }
        // Set every Windows role so the selected endpoint behaves consistently
        // in the modern Settings UI and in desktop applications.
        const auto console = policy->SetDefaultEndpoint(slot.endpointId.c_str(), eConsole);
        const auto multimedia = policy->SetDefaultEndpoint(slot.endpointId.c_str(), eMultimedia);
        const auto communications = policy->SetDefaultEndpoint(
            slot.endpointId.c_str(), eCommunications);
        const auto changed = SUCCEEDED(console) && SUCCEEDED(multimedia) &&
            SUCCEEDED(communications);
        FB_TRACE("DEFAULT_ENDPOINT slot=%d flow=%s console=%08X multimedia=%08X communications=%08X changed=%d",
            slotIndex, slot.kind == SlotKind::RenderEndpoint ? "render" : "capture",
            static_cast<unsigned>(console), static_cast<unsigned>(multimedia),
            static_cast<unsigned>(communications), changed ? 1 : 0);
        if (changed)
        {
            RefreshEndpoints();
            if (slot.kind == SlotKind::RenderEndpoint)
            {
                EnsureDefaultRenderManager();
                RefreshApplications();
            }
        }
        return changed;
    }

    bool ApplyApplicationRoute(const std::wstring& trackKey, const bool capture,
        const std::wstring& endpointId)
    {
        const auto application = std::find_if(slots.begin(), slots.end(),
            [&trackKey](const Slot& slot)
            {
                return slot.kind == SlotKind::Application && slot.key == trackKey;
            });
        if (application == slots.end() || application->sessions.empty())
        {
            FB_TRACE("APP_ROUTE_REJECT key=%ls reason=no-session", trackKey.c_str());
            return false;
        }
        if (!endpointId.empty())
        {
            const auto requiredKind = capture ? SlotKind::CaptureEndpoint :
                SlotKind::RenderEndpoint;
            const auto endpointFound = std::any_of(slots.begin(), slots.end(),
                [&endpointId, requiredKind](const Slot& slot)
                {
                    return slot.kind == requiredKind && slot.endpointId == endpointId;
                });
            if (!endpointFound)
            {
                FB_TRACE("APP_ROUTE_REJECT key=%ls reason=endpoint flow=%s id=%ls",
                    trackKey.c_str(), capture ? "capture" : "render", endpointId.c_str());
                return false;
            }
        }

        std::vector<DWORD> processIds;
        bool changed = false;
        for (const auto& session : application->sessions)
        {
            if (session.processId == 0U || std::find(processIds.begin(), processIds.end(),
                    session.processId) != processIds.end())
            {
                continue;
            }
            processIds.push_back(session.processId);
            changed = applicationRoutePolicy.Set(session.processId,
                capture ? eCapture : eRender, endpointId) || changed;
        }
        if (changed)
        {
            (capture ? application->inputRouteId : application->outputRouteId) = endpointId;
        }
        FB_TRACE("APP_ROUTE key=%ls flow=%s endpoint=%ls pids=%u changed=%d",
            trackKey.c_str(), capture ? "capture" : "render",
            endpointId.empty() ? L"default" : endpointId.c_str(),
            static_cast<unsigned>(processIds.size()), changed ? 1 : 0);
        return changed;
    }

    bool ApplyVolume(const int slotIndex, const float value)
    {
        if (slotIndex < 0 || slotIndex >= StripCount)
        {
            return false;
        }
        auto& slot = slots[slotIndex];
        if (slot.endpointVolume)
        {
            return SUCCEEDED(slot.endpointVolume->SetMasterVolumeLevelScalar(
                std::clamp(value, 0.0F, 1.0F), nullptr));
        }
        if (slot.sessions.empty()) return false;
        bool changed = false;
        for (const auto& session : slot.sessions)
        {
            changed = SUCCEEDED(session.volume->SetMasterVolume(
                std::clamp(value, 0.0F, 1.0F), nullptr)) || changed;
        }
        return changed;
    }

    bool ApplyPan(const int slotIndex, const float value)
    {
        if (slotIndex < 0 || slotIndex >= StripCount)
        {
            return false;
        }
        auto& slot = slots[slotIndex];
        const auto pan = std::clamp(value, -1.0F, 1.0F);
        if (slot.kind == SlotKind::RenderEndpoint && slot.endpointVolume)
        {
            UINT channelCount = 0U;
            float master = 0.0F;
            if (FAILED(slot.endpointVolume->GetChannelCount(&channelCount)) ||
                channelCount != 2U ||
                FAILED(slot.endpointVolume->GetMasterVolumeLevelScalar(&master)))
            {
                return false;
            }
            // Endpoint channel scalars are the absolute, audio-tapered values
            // displayed by Windows Settings. Keep the louder side at the
            // current Master ceiling so balance never raises device volume.
            const auto left = master * (pan <= 0.0F ? 1.0F : 1.0F - pan);
            const auto right = master * (pan >= 0.0F ? 1.0F : 1.0F + pan);
            const auto leftResult = slot.endpointVolume->SetChannelVolumeLevelScalar(
                0U, left, nullptr);
            const auto rightResult = slot.endpointVolume->SetChannelVolumeLevelScalar(
                1U, right, nullptr);
            const auto changed = SUCCEEDED(leftResult) && SUCCEEDED(rightResult);
            FB_TRACE("ENDPOINT_PAN_WRITE slot=%d pan=%.4f master=%.4f left=%.4f right=%.4f changed=%d",
                slotIndex, pan, master, left, right, changed ? 1 : 0);
            return changed;
        }
        if (slot.kind != SlotKind::Application || slot.sessions.empty())
        {
            return false;
        }

        // Balance law: center is 1/1, so moving through center never imposes
        // an equal-power -3 dB attenuation. The opposite channel alone is
        // attenuated as the control moves toward either edge.
        const auto left = pan <= 0.0F ? 1.0F : 1.0F - pan;
        const auto right = pan >= 0.0F ? 1.0F : 1.0F + pan;
        bool changed = false;
        for (const auto& session : slot.sessions)
        {
            UINT32 channelCount = 0U;
            if (!session.channelVolume ||
                FAILED(session.channelVolume->GetChannelCount(&channelCount)) ||
                channelCount != 2U)
            {
                continue;
            }
            const auto leftResult = session.channelVolume->SetChannelVolume(0U, left, nullptr);
            const auto rightResult = session.channelVolume->SetChannelVolume(1U, right, nullptr);
            changed = (SUCCEEDED(leftResult) && SUCCEEDED(rightResult)) || changed;
        }
        FB_TRACE("PAN_WRITE slot=%d pan=%.4f left=%.4f right=%.4f changed=%d",
            slotIndex, pan, left, right, changed ? 1 : 0);
        return changed;
    }

    bool ApplyMute(const int slotIndex, const bool muted)
    {
        if (slotIndex < 0 || slotIndex >= StripCount)
        {
            return false;
        }
        auto& slot = slots[slotIndex];
        if (slot.endpointVolume)
        {
            return SUCCEEDED(slot.endpointVolume->SetMute(muted, nullptr));
        }
        if (slot.sessions.empty()) return false;
        bool changed = false;
        for (const auto& session : slot.sessions)
        {
            changed = SUCCEEDED(session.volume->SetMute(muted, nullptr)) || changed;
        }
        return changed;
    }

    static bool ReadApplicationMute(Slot& slot, bool& muted)
    {
        if (slot.kind != SlotKind::Application || slot.sessions.empty())
        {
            return false;
        }
        auto selected = std::find_if(slot.sessions.begin(), slot.sessions.end(),
            [&slot](const Session& session)
            {
                return !slot.preferredMuteSession.empty() &&
                    session.identifier == slot.preferredMuteSession;
            });
        if (selected == slot.sessions.end())
        {
            selected = std::find_if(slot.sessions.begin(), slot.sessions.end(),
                [](const Session& session) { return session.persistentAppIdentity; });
        }
        if (selected == slot.sessions.end())
        {
            selected = slot.sessions.begin();
        }
        BOOL value = FALSE;
        if (!selected->volume || FAILED(selected->volume->GetMute(&value)))
        {
            return false;
        }
        muted = value != FALSE;
        return true;
    }

    static bool SetApplicationMute(Slot& slot, const bool muted)
    {
        if (slot.kind != SlotKind::Application || slot.sessions.empty())
        {
            return false;
        }
        bool changed = false;
        for (const auto& session : slot.sessions)
        {
            BOOL current = FALSE;
            if (session.volume && SUCCEEDED(session.volume->GetMute(&current)) &&
                (current != FALSE) != muted &&
                SUCCEEDED(session.volume->SetMute(muted, nullptr)))
            {
                changed = true;
            }
        }
        return changed;
    }

    bool ApplySoloPolicy()
    {
        if (!soloActive)
        {
            return false;
        }
        bool changed = false;
        for (auto& slot : slots)
        {
            if (slot.kind != SlotKind::Application || slot.sessions.empty())
            {
                continue;
            }
            if (preSoloMutes.find(slot.key) == preSoloMutes.end())
            {
                bool muted = false;
                if (ReadApplicationMute(slot, muted))
                {
                    preSoloMutes.emplace(slot.key, muted);
                }
            }
            // Windows has no native session Solo. Intercancel Solo is modeled
            // by making the target audible and muting every other application.
            changed = SetApplicationMute(slot, slot.key != soloTargetKey) || changed;
        }
        return changed;
    }

    bool ClearSolo()
    {
        if (!soloActive)
        {
            return false;
        }
        bool changed = false;
        for (auto& slot : slots)
        {
            if (slot.kind != SlotKind::Application || slot.sessions.empty())
            {
                continue;
            }
            const auto saved = preSoloMutes.find(slot.key);
            if (saved != preSoloMutes.end())
            {
                changed = SetApplicationMute(slot, saved->second) || changed;
            }
        }
        FB_TRACE("SOLO_CLEAR target=%ls restored=%u", soloTargetKey.c_str(),
            static_cast<unsigned>(preSoloMutes.size()));
        soloActive = false;
        soloTargetKey.clear();
        preSoloMutes.clear();
        return changed;
    }

    bool ToggleSolo(const std::wstring& trackKey)
    {
        const auto target = std::find_if(slots.begin(), slots.end(),
            [&trackKey](const Slot& slot)
            {
                return slot.kind == SlotKind::Application && !slot.sessions.empty() &&
                    slot.key == trackKey;
            });
        if (target == slots.end())
        {
            FB_TRACE("SOLO_REJECT missing_target=1");
            return false;
        }
        if (soloActive && soloTargetKey == trackKey)
        {
            ClearSolo();
            return true;
        }
        if (!soloActive)
        {
            preSoloMutes.clear();
            for (auto& slot : slots)
            {
                bool muted = false;
                if (slot.kind == SlotKind::Application && !slot.sessions.empty() &&
                    ReadApplicationMute(slot, muted))
                {
                    preSoloMutes.emplace(slot.key, muted);
                }
            }
        }
        soloActive = true;
        soloTargetKey = trackKey;
        FB_TRACE("SOLO_SET target=%ls saved=%u", soloTargetKey.c_str(),
            static_cast<unsigned>(preSoloMutes.size()));
        ApplySoloPolicy();
        return true;
    }

    std::unique_ptr<AudioFrame> MakeFrame()
    {
        auto frame = std::make_unique<AudioFrame>();
        monoAudioSetting.Get(frame->monoAudioEnabled);
        frame->anySolo = soloActive;
        for (const auto& slot : slots)
        {
            if ((slot.kind != SlotKind::RenderEndpoint &&
                    slot.kind != SlotKind::CaptureEndpoint) ||
                slot.endpointId.empty() || !slot.endpointVolume)
            {
                continue;
            }
            AudioRouteOption option;
            option.id = slot.endpointId;
            option.name = slot.name;
            option.color = slot.channelColor == AudioStripState::NoChannelColor
                ? 0x00FFFFFFU : slot.channelColor;
            (slot.kind == SlotKind::RenderEndpoint ? frame->outputRoutes :
                frame->inputRoutes).push_back(std::move(option));
        }
        frame->strips.reserve(StripCount);
        for (int index = 0; index < StripCount; ++index)
        {
            auto& slot = slots[index];
            AudioStripState strip;
            strip.slot = index;
            const auto endpointSlot = slot.kind == SlotKind::RenderEndpoint ||
                slot.kind == SlotKind::CaptureEndpoint;
            strip.active = endpointSlot ? slot.endpointVolume != nullptr : !slot.sessions.empty();
            strip.key = strip.active ? slot.key : L"";
            strip.name = strip.active ? (slot.kind == SlotKind::RenderEndpoint
                ? (slot.isDefault ? L"Master · " : L"OUT · ") + slot.name
                : slot.kind == SlotKind::CaptureEndpoint ? L"IN · " + slot.name
                : slot.name) : L"";
            strip.channelColor = strip.active
                ? slot.channelColor : AudioStripState::NoChannelColor;
            strip.defaultSelectable = endpointSlot;
            strip.isDefault = endpointSlot && slot.isDefault;
            strip.soloed = slot.kind == SlotKind::Application && soloActive &&
                slot.key == soloTargetKey;
            strip.sortGroup = slot.kind == SlotKind::RenderEndpoint ? 0 :
                slot.kind == SlotKind::CaptureEndpoint ? 1 : 2;
            strip.role = slot.kind == SlotKind::RenderEndpoint
                ? (slot.isDefault ? AudioStripRole::MasterOutput : AudioStripRole::OutputDevice)
                : slot.kind == SlotKind::CaptureEndpoint ? AudioStripRole::InputDevice
                : AudioStripRole::Application;
            if (slot.kind == SlotKind::Application)
            {
                strip.outputRouteId = slot.outputRouteId;
                strip.inputRouteId = slot.inputRouteId;
                strip.focusExecutablePath = slot.focusExecutablePath;
                strip.focusPackageFamilyName = slot.focusPackageFamilyName;
                strip.focusProcessIds.reserve(slot.sessions.size());
                for (const auto& session : slot.sessions)
                {
                    if (std::find(strip.focusProcessIds.begin(), strip.focusProcessIds.end(),
                            session.processId) == strip.focusProcessIds.end())
                    {
                        strip.focusProcessIds.push_back(session.processId);
                    }
                }
            }
            if (strip.active)
            {
                if (endpointSlot)
                {
                    float volume = 0.0F;
                    BOOL muted = FALSE;
                    if (slot.kind == SlotKind::CaptureEndpoint)
                    {
                        DrainCaptureMeterStream(slot);
                    }
                    if (SUCCEEDED(slot.endpointVolume->GetMasterVolumeLevelScalar(&volume)))
                    {
                        strip.volume = volume;
                    }
                    if (SUCCEEDED(slot.endpointVolume->GetMute(&muted)))
                    {
                        strip.muted = muted != FALSE;
                    }
                    if (slot.kind == SlotKind::RenderEndpoint)
                    {
                        UINT channelCount = 0U;
                        float left = 1.0F;
                        float right = 1.0F;
                        if (SUCCEEDED(slot.endpointVolume->GetChannelCount(&channelCount)) &&
                            channelCount == 2U &&
                            SUCCEEDED(slot.endpointVolume->GetChannelVolumeLevelScalar(0U, &left)) &&
                            SUCCEEDED(slot.endpointVolume->GetChannelVolumeLevelScalar(1U, &right)))
                        {
                            strip.panAvailable = true;
                            strip.pan = BalanceFromChannelLevels(left, right);
                        }
                    }
                    std::vector<float> peaks;
                    MergeMeterPeaks(slot.endpointMeter.Get(), peaks);
                    strip.meterDb.reserve(peaks.size());
                    for (const auto peak : peaks)
                    {
                        strip.meterDb.push_back(PeakToDb(peak));
                    }
                    strip.meterRoles = slot.meterRoles.size() >= strip.meterDb.size()
                        ? std::vector<AudioMeterRole>(slot.meterRoles.begin(),
                            slot.meterRoles.begin() + strip.meterDb.size())
                        : DefaultMeterRoles(static_cast<UINT>(strip.meterDb.size()));
                    strip.peakDb = strip.meterDb.empty() ? -120.0F :
                        *std::max_element(strip.meterDb.begin(), strip.meterDb.end());
                    frame->strips.push_back(std::move(strip));
                    continue;
                }
                struct Observation
                {
                    Session* session = nullptr;
                    AudioSessionState state = AudioSessionStateInactive;
                    float volume = 0.0F;
                    bool volumeValid = false;
                    bool muted = false;
                    bool panValid = false;
                    float pan = 0.0F;
                };

                std::vector<Observation> observations;
                observations.reserve(slot.sessions.size());
                int changedSession = -1;
                int changedMuteSession = -1;
                std::vector<float> peaks;

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
                    UINT32 channelCount = 0U;
                    if (session.channelVolume &&
                        SUCCEEDED(session.channelVolume->GetChannelCount(&channelCount)) &&
                        channelCount == 2U)
                    {
                        float left = 1.0F;
                        float right = 1.0F;
                        if (SUCCEEDED(session.channelVolume->GetChannelVolume(0U, &left)) &&
                            SUCCEEDED(session.channelVolume->GetChannelVolume(1U, &right)))
                        {
                            observation.panValid = true;
                            observation.pan = BalanceFromChannelLevels(left, right);
                            session.lastObservedPan = observation.pan;
                            session.panObserved = true;
                        }
                    }
                    MergeMeterPeaks(session.meter.Get(), peaks);
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
                    if (selected->panValid)
                    {
                        strip.panAvailable = true;
                        strip.pan = selected->pan;
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
                strip.meterDb.reserve(peaks.size());
                for (const auto peak : peaks)
                {
                    strip.meterDb.push_back(PeakToDb(peak));
                }
                strip.meterRoles = sessionMeterRoles.size() >= strip.meterDb.size()
                    ? std::vector<AudioMeterRole>(sessionMeterRoles.begin(),
                        sessionMeterRoles.begin() + strip.meterDb.size())
                    : DefaultMeterRoles(static_cast<UINT>(strip.meterDb.size()));
                strip.peakDb = strip.meterDb.empty() ? -120.0F :
                    *std::max_element(strip.meterDb.begin(), strip.meterDb.end());
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

bool NativeAudioController::QueuePan(const int slot, const float pan) noexcept
{
    if (!running_ || slot < 0 || slot >= StripCount)
    {
        return false;
    }
    pendingPans_[slot].store(std::clamp(pan, -1.0F, 1.0F), std::memory_order_release);
    panVersions_[slot].fetch_add(1, std::memory_order_acq_rel);
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

bool NativeAudioController::QueueSetDefault(const int slot) noexcept
{
    if (!running_ || slot < 0 || slot >= StripCount)
    {
        return false;
    }
    pendingDefaultSlot_.store(slot, std::memory_order_release);
    defaultVersion_.fetch_add(1, std::memory_order_acq_rel);
    SetEvent(wakeEvent_);
    return true;
}

bool NativeAudioController::QueueTrackControl(const AudioTrackControl control,
    const std::wstring& key, const float value) noexcept
{
    if (!running_) return false;
    try { if (!trackCommands_.Push(control, key, value)) return false; }
    catch (...) { return false; }
    SetEvent(wakeEvent_);
    return true;
}

bool NativeAudioController::QueueToggleMonoAudio() noexcept
{
    if (!running_)
    {
        return false;
    }
    monoToggleVersion_.fetch_add(1, std::memory_order_acq_rel);
    SetEvent(wakeEvent_);
    return true;
}

bool NativeAudioController::QueueToggleSolo(const std::wstring& trackKey)
{
    if (!running_ || trackKey.empty())
    {
        return false;
    }
    try
    {
        const std::scoped_lock lock(soloCommandMutex_);
        pendingSoloCommands_.push_back({ false, trackKey });
    }
    catch (...)
    {
        return false;
    }
    SetEvent(wakeEvent_);
    return true;
}

bool NativeAudioController::QueueClearSolo()
{
    if (!running_)
    {
        return false;
    }
    try
    {
        const std::scoped_lock lock(soloCommandMutex_);
        pendingSoloCommands_.push_back({ true, {} });
    }
    catch (...)
    {
        return false;
    }
    SetEvent(wakeEvent_);
    return true;
}

bool NativeAudioController::QueueApplicationRoute(const std::wstring& trackKey,
    const bool capture, const std::wstring& endpointId)
{
    if (!running_ || trackKey.empty()) return false;
    try
    {
        const std::scoped_lock lock(routeCommandMutex_);
        pendingRouteCommands_.push_back({ trackKey, capture, endpointId });
    }
    catch (...)
    {
        return false;
    }
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
    // At Windows sign-in, AudioSrv and the default endpoint can become ready
    // after this process. Keep the owner thread alive and retry initialization
    // instead of leaving a healthy-looking background process permanently
    // disconnected. This wait exists only before Core Audio is ready; it never
    // enters the real-time control or meter path.
    unsigned initializationAttempts = 0;
    while (running_)
    {
        impl_ = std::make_unique<Impl>();
        impl_->wakeEvent = wakeEvent_;
        impl_->changePending = &sessionChangePending_;
        impl_->discoveryPending = &sessionDiscoveryPending_;
        if (impl_->Initialize()) break;
        impl_.reset();
        ++initializationAttempts;
        FB_TRACE("AUDIO_INITIALIZATION_WAIT attempt=%u", initializationAttempts);
        WaitForSingleObject(wakeEvent_, 1000U);
    }
    if (!running_)
    {
        if (mmcssHandle) AvRevertMmThreadCharacteristics(mmcssHandle);
        CoUninitialize();
        return;
    }
    FB_TRACE("AUDIO_INITIALIZATION_READY attempts=%u", initializationAttempts + 1U);

    impl_->RefreshEndpoints();
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

        if (trackCommands_.HasPending()) for (const auto& command : trackCommands_.Take())
        {
            const auto slot = ResolveAudioTrackSlot(impl_->slots, command.key);
            bool accepted = false;
            if (slot >= 0)
            {
                switch (command.control)
                {
                case AudioTrackControl::Volume: accepted = impl_->ApplyVolume(slot, command.value); break;
                case AudioTrackControl::Pan: accepted = impl_->ApplyPan(slot, command.value); break;
                case AudioTrackControl::Mute: accepted = impl_->ApplyMute(slot, command.value > .5F); break;
                case AudioTrackControl::SetDefault: accepted = impl_->ApplyDefaultEndpoint(slot); break;
                }
            }
            if (accepted) sessionChangePending_.store(true, std::memory_order_release);
            FB_TRACE("TRACK_COMMAND key=%ls control=%d slot=%d accepted=%d",
                command.key.c_str(), static_cast<int>(command.control), slot, accepted);
        }

        for (int slot = 0; slot < StripCount; ++slot)
        {
            const auto volumeVersion = volumeVersions_[slot].load(std::memory_order_acquire);
            if (volumeVersion != impl_->appliedVolumeVersions[slot] &&
                impl_->ApplyVolume(slot, pendingVolumes_[slot].load(std::memory_order_acquire)))
            {
                impl_->appliedVolumeVersions[slot] = volumeVersion;
            }

            const auto panVersion = panVersions_[slot].load(std::memory_order_acquire);
            if (panVersion != impl_->appliedPanVersions[slot] &&
                impl_->ApplyPan(slot, pendingPans_[slot].load(std::memory_order_acquire)))
            {
                impl_->appliedPanVersions[slot] = panVersion;
            }

            const auto muteVersion = muteVersions_[slot].load(std::memory_order_acquire);
            if (muteVersion != impl_->appliedMuteVersions[slot] &&
                impl_->ApplyMute(slot, pendingMutes_[slot].load(std::memory_order_acquire) != 0))
            {
                impl_->appliedMuteVersions[slot] = muteVersion;
            }
        }

        const auto defaultVersion = defaultVersion_.load(std::memory_order_acquire);
        if (defaultVersion != impl_->appliedDefaultVersion)
        {
            const auto slot = pendingDefaultSlot_.load(std::memory_order_acquire);
            const auto changed = impl_->ApplyDefaultEndpoint(slot);
            impl_->appliedDefaultVersion = defaultVersion;
            if (changed)
            {
                sessionChangePending_.store(true, std::memory_order_release);
            }
        }

        const auto monoToggleVersion = monoToggleVersion_.load(std::memory_order_acquire);
        if (monoToggleVersion != impl_->appliedMonoToggleVersion)
        {
            bool current = false;
            const auto read = impl_->monoAudioSetting.Get(current);
            const auto changed = read && impl_->monoAudioSetting.Set(!current);
            impl_->appliedMonoToggleVersion = monoToggleVersion;
            if (changed)
            {
                sessionChangePending_.store(true, std::memory_order_release);
            }
        }

        std::vector<SoloCommand> soloCommands;
        {
            const std::scoped_lock lock(soloCommandMutex_);
            soloCommands.swap(pendingSoloCommands_);
        }
        for (const auto& command : soloCommands)
        {
            const auto accepted = command.clear
                ? (impl_->ClearSolo(), true)
                : impl_->ToggleSolo(command.trackKey);
            if (accepted)
            {
                sessionChangePending_.store(true, std::memory_order_release);
            }
        }

        std::vector<RouteCommand> routeCommands;
        {
            const std::scoped_lock lock(routeCommandMutex_);
            routeCommands.swap(pendingRouteCommands_);
        }
        for (const auto& command : routeCommands)
        {
            if (impl_->ApplyApplicationRoute(command.trackKey, command.capture,
                command.endpointId))
            {
                sessionChangePending_.store(true, std::memory_order_release);
                sessionDiscoveryPending_.store(true, std::memory_order_release);
            }
        }

        const auto afterCommands = std::chrono::steady_clock::now();
        const auto discoveryEvent = sessionDiscoveryPending_.exchange(false,
            std::memory_order_acq_rel);
        if (discoveryEvent || afterCommands >= nextDiscovery)
        {
            impl_->RefreshEndpoints();
            impl_->EnsureDefaultRenderManager();
            impl_->RefreshApplications();
            if (impl_->ApplySoloPolicy())
            {
                sessionChangePending_.store(true, std::memory_order_release);
            }
            nextDiscovery = afterCommands + kDiscoveryPeriod;
        }
        else if (impl_->ApplySoloPolicy())
        {
            sessionChangePending_.store(true, std::memory_order_release);
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
