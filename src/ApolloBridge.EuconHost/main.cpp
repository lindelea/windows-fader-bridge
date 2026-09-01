#include "ApolloEucon.h"
#include "ChannelWriter.h"
#include "MonitorWriter.h"
#include "ConfigWriter.h"
#include "Observer.h"
#include "Protocol.h"
#include "ApolloDesktop.h"
#include "DesktopSettings.h"
#include "../../tests/ApolloBridge.Tests/DesktopSettingsTests.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <dwmapi.h>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <shellapi.h>
#include <shlobj.h>
#include <sstream>
#include <string_view>
#include <thread>
#include <tlhelp32.h>
#include <windowsx.h>

namespace
{
constexpr wchar_t WindowClass[] = L"Lindelea.ApolloBridge.EUCON.Window";
constexpr wchar_t Title[] = L"UAD Console Bridge for EUCON";
constexpr UINT EuconEventMessage = WM_APP + 10;
constexpr int ConnectButton = 101, LogsButton = 102, LanguageButton = 103, ArmButton = 104,
              MonitorButton = 105, AllChannelsButton = 106, SafetyButton = 107, ConfigButton = 108;
constexpr COLORREF Background = RGB(13, 17, 23), Card = RGB(22, 28, 37), Border = RGB(43, 54, 68);
constexpr COLORREF Text = RGB(230, 237, 244), Muted = RGB(139, 157, 177), Accent = RGB(89, 218, 190);
void ButtonBounds(HWND button, int x, int y, int width, int height)
{
    RECT bounds{};
    GetWindowRect(button, &bounds);
    MapWindowPoints(nullptr, GetParent(button), reinterpret_cast<POINT *>(&bounds), 2);
    if (bounds.left != x || bounds.top != y || bounds.right - bounds.left != width ||
        bounds.bottom - bounds.top != height)
        MoveWindow(button, x, y, width, height, TRUE);
}
void ButtonCaption(HWND button, const wchar_t *caption)
{
    std::wstring current(static_cast<size_t>(GetWindowTextLengthW(button)) + 1, L'\0');
    current.resize(
        static_cast<size_t>(GetWindowTextW(button, current.data(), static_cast<int>(current.size()))));
    if (current != caption)
        SetWindowTextW(button, caption);
}
void ButtonEnabled(HWND button, bool enabled)
{
    if ((IsWindowEnabled(button) != FALSE) != enabled)
        EnableWindow(button, enabled);
}
void Print(const std::string &text)
{
    DWORD written = 0;
    const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle && handle != INVALID_HANDLE_VALUE)
        WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}
bool SdkExampleAdapterRunning()
{
    const HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes == INVALID_HANDLE_VALUE)
        return true;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(processes, &entry))
    {
        CloseHandle(processes);
        return true;
    }
    bool found = false;
    do
    {
        if (_wcsicmp(entry.szExeFile, L"EuConIO.exe") == 0 ||
            _wcsicmp(entry.szExeFile, L"EuConApp.exe") == 0)
        {
            found = true;
            break;
        }
    } while (Process32NextW(processes, &entry));
    CloseHandle(processes);
    return found;
}
int Observe(int seconds, bool configuration = false)
{
    apollo::Observer observer;
    if (configuration) observer.EnableConfiguration();
    observer.Start();
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    uint64_t maxFrames = 0;
    size_t connectedSamples = 0, changingMeters = 0, staleSamples = 0;
    long long maximumAgeMs = 0;
    std::optional<double> previous;
    while (std::chrono::steady_clock::now() < end)
    {
        const auto state = observer.Latest();
        if (state.connected)
        {
            ++connectedSamples;
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - state.receivedAt)
                                 .count();
            maximumAgeMs = std::max(maximumAgeMs, age);
            staleSamples += age >= 2000 ? 1 : 0;
            maxFrames = std::max(maxFrames, state.receivedFrames);
            if (!state.monitors.empty() && !state.monitors[0].meters.empty())
            {
                const auto level = state.monitors[0].meters[0].levelDb;
                if (level && previous && *level != *previous)
                    ++changingMeters;
                previous = level;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    const auto state = observer.Latest();
    observer.Stop();
    std::ostringstream result;
    size_t sends = 0, preamps = 0, inserts = 0, parameters = 0, outputSelectors = 0;
    size_t phantom = 0, references = 0, src = 0, recordModes = 0, auxModes = 0, talkChannels = 0;
    for (const auto &channel : state.channels)
    {
        sends += channel.sends.size();
        preamps += channel.preamps.size();
        inserts += channel.inserts.size();
        outputSelectors += channel.output && channel.output->choices.size() > 1 ? 1 : 0;
        references += channel.reference.has_value();
        src += channel.sampleRateConvert.has_value();
        recordModes += channel.recordPreEffects.has_value();
        auxModes += channel.sendPostFader.has_value() && channel.mono.has_value();
        talkChannels += channel.talk.has_value() && channel.talkToMonitor.has_value();
        for (const auto &preamp : channel.preamps)
            phantom += preamp.phantom.has_value();
        for (const auto &insert : channel.inserts)
            parameters += insert.parameters.size();
    }
    result << "Apollo observer: connected=" << state.connected << " onlineDevices=" << state.onlineDevices
           << " offlineDevices=" << state.offlineDevices << " channels=" << state.channels.size()
           << " skippedChannels=" << state.skippedChannels << " monitors=" << state.monitors.size()
           << " surfaceChannels=" << apollo::SurfaceChannels(state).size() << " sends=" << sends
           << " preamps=" << preamps << " inserts=" << inserts << " pluginParameters=" << parameters
           << " outputSelectors=" << outputSelectors << " monitorControlCandidates="
           << std::count_if(state.monitors.begin(), state.monitors.end(), apollo::MonitorEligible)
           << " dimControls="
           << std::count_if(state.monitors.begin(), state.monitors.end(),
                            [](const auto &m) {
                                return apollo::MonitorFieldAvailable(m, apollo::MonitorField::DimAmount);
                            })
           << " sourceControls="
           << std::count_if(state.monitors.begin(), state.monitors.end(),
                            [](const auto &m) {
                                return apollo::MonitorFieldAvailable(m, apollo::MonitorField::Source);
                            })
           << " talkControls="
           << std::count_if(
                  state.monitors.begin(), state.monitors.end(),
                  [](const auto &m) { return apollo::MonitorFieldAvailable(m, apollo::MonitorField::Talk); })
           << " phantomControls=" << phantom << " lineReferences=" << references << " srcControls=" << src
           << " recordModes=" << recordModes << " auxInputPages=" << auxModes
           << " talkInputPages=" << talkChannels << " frames=" << maxFrames << " samples=" << connectedSamples
           << " staleSamples=" << staleSamples << " maxSnapshotAgeMs=" << maximumAgeMs
           << " globalConfigValues="
           << std::count_if(state.globalConfig.begin(), state.globalConfig.end(),
                            [](const auto &entry) { return entry.second != "N/A"; })
           << " configSettings=" << (state.configuration ? state.configuration->settings.size() : 0)
           << " meterChanges=" << changingMeters << " generation=" << state.generation
           << " status=" << state.status << "\n"
           << "Safety: read-only transport; no EUCON initialization; no "
              "MIDI/audio/control writes.\n";
    Print(result.str());
    return state.connected && connectedSamples > 0 ? 0 : 2;
}
std::wstring Db(const std::optional<apollo::Parameter> &value)
{
    if (!value)
        return L"—";
    const double db = value->value.Number();
    if (db <= -143.9)
        return L"−∞";
    std::wostringstream text;
    text << std::fixed << std::setprecision(1) << db << L" dB";
    return text.str();
}
std::wstring Pan(const std::optional<apollo::Parameter> &p)
{
    if (!p)
        return L"—";
    const int value = static_cast<int>(std::lround(p->value.Number() * 100));
    return value == 0 ? L"C" : std::to_wstring(std::abs(value)) + (value < 0 ? L" L" : L" R");
}
std::wstring MeterDb(const std::vector<apollo::Meter> &meters, bool peakHold)
{
    const auto value = apollo::MeterMaximum(meters, peakHold);
    if (!value)
        return L"—";
    std::wostringstream text;
    text << std::fixed << std::setprecision(1) << *value;
    return text.str();
}
struct App
{
    HWND window = nullptr, connect = nullptr, logs = nullptr, language = nullptr, arm = nullptr,
         monitorArm = nullptr, allChannels = nullptr, safety = nullptr, configArm = nullptr;
    apollo::Observer observer;
    apollo::ChannelController controller{observer};
    apollo::MonitorController monitorController{observer};
    apollo::ConfigController configController{observer};
    std::unique_ptr<apollo::ApolloEucon> eucon;
    apollo::Snapshot snapshot;
    std::vector<apollo::Channel> channels;
    bool chinese = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE;
    bool adapterConflict = true;
    bool experimentalConfig = false;
    bool diagnostics = false, preview = false, autoConnectPaused = false, restoreAttempted = false;
    bool permissionsActivated = false;
    unsigned euconFailures = 0;
    std::chrono::steady_clock::time_point euconRetryAt{};
    apollo::Preferences preferences;
    std::unique_ptr<apollo::ApolloDesktop> desktop;
    std::wstring desktopNotice;
    int firstRow = 0;
    UINT dpi = 96;
    std::wstring error;
    std::string lastStatus;
    std::string selectedKey, lastControlError;
    uint64_t lastControlEpoch = 0, lastConfirmed = 0;
    uint64_t lastMonitorEpoch = 0, lastMonitorConfirmed = 0;
    std::string lastMonitorError;
    std::string lastConfigStatus;
    uint64_t lastGeneration = 0;
    HFONT normal = nullptr, large = nullptr, heading = nullptr, buttonFont = nullptr;
    HBRUSH cardBrush = CreateSolidBrush(Card);
    ~App()
    {
        desktop.reset();
        controller.Disarm();
        monitorController.Disarm();
        configController.Disarm();
        eucon.reset();
        observer.Stop();
        DeleteObject(normal);
        DeleteObject(large);
        DeleteObject(heading);
        DeleteObject(buttonFont);
        DeleteObject(cardBrush);
    }
    const wchar_t *T(const wchar_t *zh, const wchar_t *en) const
    {
        return chinese ? zh : en;
    }
    void SuspendControllers()
    {
        controller.Disarm();
        monitorController.Disarm();
        configController.Disarm();
    }
    void RevokeControls()
    {
        permissionsActivated = false;
        SuspendControllers();
        restoreAttempted = true;
    }
    void RecoverEucon(const std::exception &failure)
    {
        error = apollo::Wide(failure.what());
        // EUCON owns the surface connection. Tear down the failed adapter and
        // discard queued gestures, but retain the access profile the user
        // explicitly selected. No UAD write is retried or replayed.
        SuspendControllers();
        eucon.reset();
        const unsigned exponent = std::min(++euconFailures - 1, 3U);
        const auto delay = std::chrono::seconds(1U << exponent); // 1, 2, 4, then 8 seconds.
        euconRetryAt = std::chrono::steady_clock::now() + delay;
        apollo::Log(std::string("EUCON adapter recovery scheduled delay-ms=") +
                    std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(delay).count()) +
                    " error=" + failure.what());
    }
    bool FreshDesktopState() const
    {
        return !preview && snapshot.connected && snapshot.onlineDevices &&
               std::chrono::steady_clock::now() - snapshot.receivedAt < std::chrono::seconds(2);
    }
    void MaintainPermissions()
    {
        if (!permissionsActivated || !eucon || !FreshDesktopState())
            return;
        if (preferences.access.monitor && !monitorController.Epoch() && snapshot.monitors.size() == 1 &&
            apollo::MonitorEligible(snapshot.monitors.front()))
        {
            try
            {
                monitorController.Arm(snapshot.monitors.front().key, preferences.monitorCeiling);
            }
            catch (const std::exception &e)
            {
                apollo::Log(std::string("monitor permission waiting: ") + e.what());
            }
        }
        if (preferences.access.channels)
            for (const auto &channel : snapshot.channels)
                if (apollo::ControlEligible(channel))
                {
                    try
                    {
                        // A route/input change deliberately has no callback epoch
                        // while its new model is being confirmed. Keep tracking
                        // that transition instead of prematurely re-arming it.
                        if (!controller.Tracks(channel.key))
                            controller.Arm(channel.key);
                        if (preferences.access.sensitive && controller.Epoch(channel.key))
                            controller.UnlockSafety(channel.key);
                    }
                    catch (const std::exception &e)
                    {
                        apollo::Log("channel permission waiting key=" + channel.key + " error=" + e.what());
                    }
                }
        if (preferences.access.configuration && experimentalConfig && !configController.Epoch() &&
            !configController.Status().busy)
        {
            try
            {
                configController.Arm();
            }
            catch (const std::exception &e)
            {
                apollo::Log(std::string("config permission waiting: ") + e.what());
            }
        }
    }
    void ApplyPermissions()
    {
        RevokeControls();
        desktopNotice.clear();
        if (!preferences.access.Any())
        {
            desktopNotice = T(L"已切换为只读。", L"Read-only mode enabled.");
            return;
        }
        // An explicit Apply (or a verified startup restore) is session-long
        // authority. Individual cancelled/rejected operations may clear their
        // queues, but must not silently change the user's selected access.
        permissionsActivated = true;
        if (!eucon || !FreshDesktopState())
        {
            desktopNotice = T(L"设置已保存。", L"Settings saved.");
            return;
        }
        try
        {
            MaintainPermissions();
            if (preferences.access.configuration && !experimentalConfig)
                desktopNotice = T(L"CONFIG 更改将在重新启动后生效。",
                                   L"The CONFIG change takes effect after restart.");
        }
        catch (const std::exception &e)
        {
            apollo::Log(std::string("desktop permissions: ") + e.what());
            desktopNotice = T(L"等待连接状态恢复。", L"Waiting for the connection to recover.");
        }
    }
    std::wstring SavePreferences(const apollo::Preferences &requested, bool startup, bool activate)
    {
        if (preview) return L"UI preview does not persist settings";
        std::optional<std::wstring> oldStartup;
        bool startupChanged = false;
        try
        {
            auto next = requested;
            apollo::ValidatePreferences(next);
            if (activate && FreshDesktopState() && snapshot.configuration)
                next.trustedSystem = snapshot.configuration->systemIdentity;
            else next.trustedSystem = preferences.trustedSystem;
            oldStartup = apollo::DesktopStartupCommand();
            const auto nextStartup = startup ? std::make_optional(apollo::MakeStartupCommand(apollo::DesktopExecutable())) : std::nullopt;
            if (oldStartup != nextStartup) { apollo::RestoreDesktopStartup(nextStartup); startupChanged = true; }
            apollo::SaveDesktopPreferences(next);
            preferences = next;
            chinese = preferences.language == "zh-CN";
            desktopNotice.clear();
            if (activate) ApplyPermissions();
            return {};
        }
        catch (const std::exception &e)
        {
            bool startupRestored = true;
            if (startupChanged) try { apollo::RestoreDesktopStartup(oldStartup); } catch (...) { startupRestored = false; }
            apollo::Log(std::string("desktop settings: ") + e.what());
            if (!startupRestored)
            {
                apollo::Log("desktop settings: startup rollback failed");
                return T(L"设置未保存，且无法还原 Windows 启动项。请检查系统的启动应用设置。",
                         L"Settings were not saved and the startup entry could not be restored. Check Windows Startup apps.");
            }
            return T(L"设置未保存。请检查 Windows 启动项或当前用户文件访问。",
                     L"Settings were not saved. Check the Windows startup entry or current-user file access.");
        }
    }
    void UpdateDesktop()
    {
        if (!desktop) return;
        if (!preview && preferences.autoConnect && !autoConnectPaused && !eucon && FreshDesktopState() &&
            !adapterConflict && std::chrono::steady_clock::now() >= euconRetryAt)
        {
            Command(ConnectButton);
        }
        if (!restoreAttempted && eucon && FreshDesktopState())
        {
            restoreAttempted = true;
            const auto identity = snapshot.configuration ? snapshot.configuration->systemIdentity : std::string{};
            if (apollo::CanRestorePermissions(preferences, true, identity)) ApplyPermissions();
            else if (preferences.restorePermissions && preferences.access.Any())
                desktopNotice = T(L"已连接新的设备配置，请检查控制设置。",
                                   L"A different interface configuration is connected. Review Control settings.");
        }
        apollo::DesktopState state;
        state.snapshot = snapshot;
        state.published = eucon != nullptr;
        state.conflict = adapterConflict;
        state.preview = preview;
        state.configExtension = experimentalConfig;
        for (const auto &c : channels) if (controller.Epoch(c.key)) ++state.enabledChannels;
        state.monitorEnabled = monitorController.Epoch() != 0;
        state.configEnabled = configController.Epoch() != 0;
        state.activeCeiling = monitorController.Status().ceiling;
        state.notice = desktopNotice;
        const auto configState = configController.Status();
        if (!preview && state.notice.empty() && !configState.message.empty() && configState.message.rfind("CONFIG:", 0) != 0)
            state.notice = T(L"CONFIG 操作未完成。请检查 Console；详细原因已记录到日志。",
                              L"A CONFIG operation did not complete. Check Console; details are in the log.");
        if (!preview && state.notice.empty() && (!controller.Status().error.empty() || !monitorController.Status().error.empty()))
            state.notice = T(L"部分控制操作未完成。请检查 Console；详细原因已记录到日志。",
                             L"Some control operations did not complete. Check Console; details are in the log.");
        if (!error.empty())
            state.notice = preferences.autoConnect && !autoConnectPaused
                               ? T(L"EUCON 连接中断，正在自动恢复。",
                                   L"EUCON connection interrupted; recovering automatically.")
                               : T(L"EUCON 连接中断。可在连接设置中重试。",
                                   L"EUCON connection interrupted. Retry in Connection settings.");
        desktop->Update(std::move(state));
    }
    int Px(int value) const
    {
        return MulDiv(value, static_cast<int>(dpi), 96);
    }
    void Fonts()
    {
        DeleteObject(normal);
        DeleteObject(large);
        DeleteObject(heading);
        DeleteObject(buttonFont);
        normal = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Segoe UI");
        large = CreateFontW(-28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Segoe UI");
        heading = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Segoe UI");
        buttonFont = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                 CLEARTYPE_QUALITY, 0, L"Segoe UI");
        for (HWND button : {connect, logs, language, arm, monitorArm, allChannels, safety, configArm})
            if (button)
                SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(buttonFont), TRUE);
    }
    void Layout()
    {
        RECT r{};
        GetClientRect(window, &r);
        ButtonBounds(connect, Px(28), r.bottom - Px(61), Px(195), Px(34));
        ButtonBounds(arm, Px(235), r.bottom - Px(61), Px(170), Px(34));
        ButtonBounds(allChannels, Px(417), r.bottom - Px(61), Px(180), Px(34));
        ButtonBounds(safety, Px(609), r.bottom - Px(61), Px(180), Px(34));
        ButtonBounds(configArm, Px(801), r.bottom - Px(61), Px(150), Px(34));
        ShowWindow(configArm, experimentalConfig ? SW_SHOWNA : SW_HIDE);
        ButtonCaption(configArm, configController.Epoch() ? T(L"锁定 CONFIG", L"Lock CONFIG")
                                                          : T(L"开放 CONFIG…", L"Enable CONFIG…"));
        ButtonBounds(logs, r.right - Px(296), Px(30), Px(150), Px(34));
        ButtonBounds(language, r.right - Px(134), Px(30), Px(106), Px(34));
        ButtonBounds(monitorArm, r.right - Px(238), Px(210), Px(190), Px(30));
        ButtonCaption(connect,
                      eucon ? T(L"EUCON 已连接", L"EUCON connected") : T(L"连接 EUCON", L"Connect EUCON"));
        ButtonCaption(arm, controller.Epoch(selectedKey) ? T(L"锁定所选通道", L"Lock selected")
                                                         : T(L"开放所选通道", L"Enable selected"));
        ButtonCaption(allChannels, controller.Epoch() ? T(L"锁定全部通道", L"Lock all channels")
                                                      : T(L"开放全部通道…", L"Enable all channels…"));
        ButtonCaption(safety, T(L"敏感操作保护…", L"Sensitive controls…"));
        ButtonCaption(logs, T(L"打开日志目录", L"Open logs"));
        ButtonCaption(language, chinese ? L"English" : L"中文");
        ButtonCaption(monitorArm, monitorController.Epoch() ? T(L"锁定控制室", L"Lock control room")
                                                            : T(L"解锁控制室…", L"Unlock control room…"));
        ScrollInfo();
    }
    int PageRows() const
    {
        RECT r{};
        GetClientRect(window, &r);
        return std::max(1, (MulDiv(r.bottom, 96, static_cast<int>(dpi)) - 478) / 34);
    }
    void ScrollInfo()
    {
        firstRow = std::clamp(firstRow, 0, std::max(0, static_cast<int>(channels.size()) - PageRows()));
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMin = 0;
        info.nMax = std::max(0, static_cast<int>(channels.size()) - 1);
        info.nPage = std::min(PageRows(), info.nMax + 1);
        info.nPos = firstRow;
        SCROLLINFO current{sizeof(current), SIF_RANGE | SIF_PAGE | SIF_POS};
        if (!GetScrollInfo(window, SB_VERT, &current) || current.nMin != info.nMin ||
            current.nMax != info.nMax || current.nPage != info.nPage || current.nPos != info.nPos)
            SetScrollInfo(window, SB_VERT, &info, TRUE);
    }
    void Tick()
    {
        if (preview) { UpdateDesktop(); return; }
        snapshot = observer.Latest();
        if (snapshot.status != lastStatus || snapshot.generation != lastGeneration)
        {
            apollo::Log("observer status=" + snapshot.status +
                        " generation=" + std::to_string(snapshot.generation) +
                        " online=" + std::to_string(snapshot.onlineDevices) +
                        " channels=" + std::to_string(snapshot.channels.size()));
            lastStatus = snapshot.status;
            lastGeneration = snapshot.generation;
        }
        // Clear stale frames if the worker cannot produce a fresh publication.
        if (snapshot.connected &&
            std::chrono::steady_clock::now() - snapshot.receivedAt > std::chrono::seconds(4))
        {
            snapshot.connected = false;
            snapshot.channels.clear();
            snapshot.monitors.clear();
            snapshot.status = "Waiting for fresh state";
        }
        channels = apollo::SurfaceChannels(snapshot);
        controller.Validate();
        monitorController.Validate();
        // Preserve explicit Full/Custom access across transient observer
        // refreshes and per-operation failures. Rebinding uses only the latest
        // fresh model and never replays an old request.
        MaintainPermissions();
        const auto configStatus = configController.Status();
        const auto configLog = std::to_string(configStatus.epoch) + ":" + std::to_string(configStatus.confirmed) +
                               ":" + std::to_string(configStatus.busy) + ":" + configStatus.message;
        if (experimentalConfig && configLog != lastConfigStatus)
        {
            apollo::Log("config-control " + configLog);
            lastConfigStatus = configLog;
        }
        const auto monitoring = monitorController.Status();
        if (monitoring.epoch != lastMonitorEpoch || monitoring.confirmed != lastMonitorConfirmed ||
            monitoring.error != lastMonitorError)
        {
            apollo::Log("monitor-control epoch=" + std::to_string(monitoring.epoch) +
                        " ceiling-dB=" + std::to_string(monitoring.ceiling) +
                        " dispatched=" + std::to_string(monitoring.confirmed) + " " +
                        monitoring.lastOperation + " error=" + monitoring.error);
            lastMonitorEpoch = monitoring.epoch;
            lastMonitorConfirmed = monitoring.confirmed;
            lastMonitorError = monitoring.error;
            Layout();
        }
        const auto control = controller.Status();
        if (control.epoch != lastControlEpoch || control.confirmed != lastConfirmed ||
            control.error != lastControlError)
        {
            apollo::Log("control epoch=" + std::to_string(control.epoch) + " dispatched=" +
                        std::to_string(control.confirmed) + " pending=" + std::to_string(control.pending) +
                        " " + control.lastOperation + " error=" + control.error);
            lastControlEpoch = control.epoch;
            lastConfirmed = control.confirmed;
            lastControlError = control.error;
            Layout();
        }
        if (eucon)
        {
            try
            {
                eucon->Apply(snapshot);
            }
            catch (const std::exception &e)
            {
                RecoverEucon(e);
                Layout();
            }
        }
        static unsigned tick = 0;
        if (++tick % 30 == 0)
            adapterConflict = SdkExampleAdapterRunning();
        ButtonEnabled(connect,
                      snapshot.connected &&
                          (!channels.empty() || (snapshot.monitors.size() == 1 &&
                                                 apollo::MonitorEligible(snapshot.monitors.front()))) &&
                          !adapterConflict && !eucon);
        const auto selected = std::find_if(channels.begin(), channels.end(),
                                           [&](const auto &c) { return c.key == selectedKey; });
        ButtonEnabled(arm, controller.Epoch(selectedKey) ||
                               (eucon && snapshot.connected && selected != channels.end() &&
                                apollo::ControlEligible(*selected)));
        ButtonEnabled(monitorArm,
                      monitoring.epoch || (eucon && snapshot.connected && snapshot.monitors.size() == 1 &&
                                           apollo::MonitorEligible(snapshot.monitors.front())));
        ButtonEnabled(allChannels, control.epoch || (eucon && snapshot.connected && !channels.empty()));
        ButtonEnabled(safety, eucon && controller.Epoch(selectedKey));
        ButtonEnabled(configArm, experimentalConfig && eucon && snapshot.connected && !configController.Status().busy);
        Layout();
        if (diagnostics) InvalidateRect(window, nullptr, FALSE);
        UpdateDesktop();
    }
    void PumpEuconEvents()
    {
        if (!eucon)
            return;
        try
        {
            // EUCON callbacks post this owner-thread message immediately. Keep
            // audible control dispatch independent from the 33 ms desktop/UI
            // refresh timer while preserving the single SDK-owning thread.
            eucon->Apply(observer.Latest());
        }
        catch (const std::exception &e)
        {
            RecoverEucon(e);
            Layout();
        }
    }
    void Command(int id)
    {
        if (id == LanguageButton)
        {
            chinese = !chinese;
            Layout();
        }
        else if (id == ConnectButton && !eucon)
        {
            adapterConflict = SdkExampleAdapterRunning();
            if (adapterConflict || !snapshot.connected)
                return;
            try
            {
                const HWND owner = window;
                eucon = std::make_unique<apollo::ApolloEucon>(
                    snapshot, controller, monitorController, experimentalConfig, &configController,
                    [owner] { PostMessageW(owner, EuconEventMessage, 0, 0); });
                error.clear();
                euconFailures = 0;
                euconRetryAt = {};
            }
            catch (const std::exception &e)
            {
                RecoverEucon(e);
            }
            Layout();
        }
        else if (id == SafetyButton)
        {
            if (!eucon || !controller.Epoch(selectedKey))
                return;
            const auto message =
                T(L"仅为所选通道开放本次连接的 48V、对讲送监听和 UNISON 参数控制。\n\n"
                  L"UNISON 参数可能联动模拟增益、阻抗和幻象供电。\n"
                  L"请确认麦克风支持幻象供电，并降低监听音量以避免啸叫。不会立即改变任何音频设置。\n\n"
                  L"锁定通道、路由切换或断线后保护会恢复。",
                  L"Allow phantom power ON, talkback to monitor and UNISON control for the selected channel "
                  L"in this "
                  L"session.\n\nUNISON may change analog gain, impedance and phantom power.\n"
                  L"Confirm microphone compatibility and lower monitor volume to prevent feedback. No audio "
                  L"setting changes now.\n\n"
                  L"Channel lock, routing change or disconnection restores protection.");
            if (MessageBoxW(window, message, T(L"确认敏感操作", L"Confirm sensitive controls"),
                            MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK)
                try
                {
                    controller.UnlockSafety(selectedKey);
                    error.clear();
                }
                catch (const std::exception &e)
                {
                    error = apollo::Wide(e.what());
                }
        }
        else if (id == ConfigButton && experimentalConfig)
        {
            if (configController.Epoch()) configController.Disarm();
            else if (eucon && MessageBoxW(window,
                         T(L"允许 CONFIG 修改截图范围内的声卡配置、CUE 输出、插件和已有预置。\n\n"
                           L"请先降低监听及耳机音量并停止录音。采样率、时钟、余量、路由和插件变化可能中断音频或增大音量。\n\n"
                           L"旋钮只预选，按 In 确认；插件列表按 In 选用。不会保存或覆盖预置。断线或写入未确认时丢弃本次操作，连接恢复后继续保持已授权状态。",
                           L"Enable CONFIG for interface settings, cue outputs, plug-in selection and existing presets.\n\n"
                           L"Lower monitor/headphone volume and stop recording first. Clock, rate, headroom, routing and plug-in changes may interrupt audio or increase loudness.\n\n"
                           L"Turn to preview; press In to apply. In selects a listed plug-in/preset. Preset files are never overwritten. A disconnect or unconfirmed write discards that operation; granted access resumes on fresh state."),
                         T(L"确认 CONFIG 控制", L"Confirm CONFIG control"),
                         MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) == IDOK)
                try { configController.Arm(); error.clear(); }
                catch (const std::exception &e) { error = apollo::Wide(e.what()); }
            Layout();
        }
        else if (id == ArmButton || id == AllChannelsButton)
        {
            const bool all = id == AllChannelsButton;
            if (all && controller.Epoch())
                controller.Disarm();
            else if (!all && controller.Epoch(selectedKey))
                controller.Disarm(selectedKey);
            else if (eucon && (all || !selectedKey.empty()))
            {
                const auto it = std::find_if(channels.begin(), channels.end(),
                                             [&](const auto &c) { return c.key == selectedKey; });
                if (!all && (it == channels.end() || !apollo::ControlEligible(*it)))
                    return;
                const auto message =
                    (all ? std::wstring(T(L"当前在线的全部通道", L"All currently online channels"))
                         : apollo::Wide(it->name)) +
                    T(L"\n\n允许 EUCON 控制通道的推子、Mute、Solo、PAN、AUX/Cue "
                      L"发送、输出路由、前级和已加载插件。\n\n"
                      L"发送、路由、前级增益或插件开关都可能增大音量；请先降低监听音量"
                      L"。48V 开启和对讲送监听仍需单独确认。\n"
                      L"Rec 灯亮表示 UAD 插件效果录入，灯灭表示只监听效果。\n"
                      L"路由、连接或能力变化时丢弃旧操作并按最新状态继续控制。控制室权限独立设置。",
                      L"\n\nEnable this channel's fader, mute, solo, pan, AUX/Cue "
                      L"sends, output routing, preamp and loaded plug-ins.\n\n"
                      L"Sends, routing, preamp gain and plug-in power can increase "
                      L"loudness. Lower monitor volume first. Phantom ON and TB to monitors require separate "
                      L"safety confirmation.\n"
                      L"Rec LED on = UAD effects recorded; off = monitor effects only.\n"
                      L"Routing, connection or capability changes discard stale work and continue from fresh "
                      L"state. Control room permission is separate.");
                if (MessageBoxW(window, message.c_str(), T(L"开放通道控制", L"Enable channel control"),
                                MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
                    return;
                try
                {
                    if (all)
                        controller.ArmAll();
                    else
                        controller.Arm(selectedKey);
                    error.clear();
                }
                catch (const std::exception &e)
                {
                    error = apollo::Wide(e.what());
                }
            }
            Layout();
        }
        else if (id == MonitorButton)
        {
            if (monitorController.Epoch())
                monitorController.Disarm();
            else if (eucon && snapshot.monitors.size() == 1 &&
                     apollo::MonitorEligible(snapshot.monitors.front()))
            {
                const auto key = snapshot.monitors.front().key;
                const auto message = T(L"允许 EUCON 控制室改变主监听音量、Mute、Dim、Mono、DIM "
                                       L"衰减量、监听源和 TALK。\n\n"
                                       L"解锁时的当前监听音量将作为本次上限。普通通道推子不能控制主监听"
                                       L"。\n\n"
                                       L"取消 "
                                       L"Mute/"
                                       L"Dim、减小衰减量或切换监听源仍可能明显变响；软件上限不代表安全声"
                                       L"压。"
                                       L"TALK 会打开对讲麦克风；请先在 Console 确认发送目标和音量。"
                                       L"断线或配置变化会丢弃旧操作并在恢复后继续控制，但无法保证已开启的 TALK "
                                       L"自动关闭；必要时请在 Console 或硬件上关闭。",
                                       L"Enable EUCON level, Mute, Dim, Mono, dim depth, monitor source "
                                       L"and TALK.\n\n"
                                       L"The current monitor level at unlock becomes the ceiling for "
                                       L"this session. "
                                       L"Channel faders cannot control the monitor.\n\n"
                                       L"Removing Mute/Dim, reducing dim depth or changing source can "
                                       L"increase loudness; "
                                       L"this ceiling is not an SPL safeguard. TALK opens the talkback "
                                       L"microphone. Check its "
                                       L"destinations and levels in Console first. Disconnection or "
                                       L"configuration changes discard stale work and resume control, "
                                       L"but cannot guarantee TALK closes: use Console or hardware to "
                                       L"switch it off if needed.");
                if (MessageBoxW(window, message, T(L"解锁控制室", L"Unlock control room"),
                                MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
                    return;
                try
                {
                    monitorController.Arm(key);
                    error.clear();
                }
                catch (const std::exception &e)
                {
                    error = apollo::Wide(e.what());
                }
            }
            Layout();
        }
        else if (id == LogsButton)
        {
            PWSTR base = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)))
            {
                const auto folder = apollo::DesktopFolder() / L"logs";
                CoTaskMemFree(base);
                std::error_code ec;
                std::filesystem::create_directories(folder, ec);
                if (!ec)
                {
                    PIDLIST_ABSOLUTE item = nullptr;
                    if (SUCCEEDED(SHParseDisplayName(folder.c_str(), nullptr, &item, 0, nullptr)))
                    {
                        SHELLEXECUTEINFOW open{sizeof(open)};
                        open.fMask = SEE_MASK_IDLIST;
                        open.hwnd = window;
                        open.lpVerb = L"open";
                        open.lpIDList = item;
                        open.nShow = SW_SHOWNORMAL;
                        ShellExecuteExW(&open);
                        CoTaskMemFree(item);
                    }
                }
            }
        }
        InvalidateRect(window, nullptr, FALSE);
    }
    void Paint()
    {
        PAINTSTRUCT ps{};
        HDC target = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap =
            CreateCompatibleBitmap(target, std::max(1L, client.right), std::max(1L, client.bottom));
        const auto oldBitmap = SelectObject(dc, bitmap);
        HBRUSH bg = CreateSolidBrush(Background);
        FillRect(dc, &client, bg);
        DeleteObject(bg);
        SetMapMode(dc, MM_ANISOTROPIC);
        SetWindowExtEx(dc, 96, 96, nullptr);
        SetViewportExtEx(dc, dpi, dpi, nullptr);
        const int width = MulDiv(client.right, 96, static_cast<int>(dpi)),
                  height = MulDiv(client.bottom, 96, static_cast<int>(dpi));
        SetBkMode(dc, TRANSPARENT);
        const auto text = [&](std::wstring value, int x, int y, int w, int h, HFONT font, COLORREF color,
                              UINT flags = DT_LEFT) {
            SelectObject(dc, font);
            SetTextColor(dc, color);
            RECT r{x, y, x + w, y + h};
            DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &r,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | flags);
        };
        const auto card = [&](RECT r) {
            const auto pen = CreatePen(PS_SOLID, 1, Border);
            const auto oldPen = SelectObject(dc, pen);
            const auto oldBrush = SelectObject(dc, cardBrush);
            RoundRect(dc, r.left, r.top, r.right, r.bottom, 12, 12);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(pen);
        };
        text(L"Apollo Bridge", 28, 25, width - 190, 38, large, Text);
        text(L"for EUCON", 29, 62, 160, 22, normal, Accent);
        card({28, 101, width - 28, 178});
        const bool online = snapshot.connected && snapshot.onlineDevices;
        text(online ? L"●" : L"○", 48, 118, 22, 28, heading, online ? Accent : Muted);
        text(online ? T(L"Apollo 已连接", L"Apollo connected") : T(L"等待 Apollo", L"Waiting for Apollo"), 78,
             112, width - 300, 28, heading, Text);
        const auto control = controller.Status();
        text(control.epoch
                 ? std::wstring(T(L"控制已开放 · ", L"Control enabled · ")) + apollo::Wide(control.name) +
                       (control.key.empty() ? T(L" 条通道", L" channels") : L"")
                 : T(L"只读 · 选择一个通道以开放控制", L"Read only · Select a channel to enable control"),
             78, 140, width - 310, 24, normal, control.epoch ? RGB(237, 185, 96) : Muted);
        text(std::to_wstring(channels.size()), width - 150, 111, 98, 35, large, Accent, DT_RIGHT);
        text(T(L"可见通道", L"visible channels"), width - 175, 146, 123, 22, normal, Muted, DT_RIGHT);
        card({28, 196, width - 28, 295});
        text(T(L"控制室", L"CONTROL ROOM"), 48, 208, 180, 25, heading, Text);
        const auto monitoring = monitorController.Status();
        std::wstring monitorPolicy = T(L"已锁定", L"Locked");
        if (monitoring.epoch)
        {
            std::wostringstream ceiling;
            ceiling << T(L"上限 ", L"Ceiling ") << std::fixed << std::setprecision(1) << monitoring.ceiling
                    << L" dB";
            monitorPolicy = ceiling.str();
        }
        else if (snapshot.monitors.size() == 1 && !apollo::MonitorEligible(snapshot.monitors.front()))
            monitorPolicy = T(L"当前监听配置仅供查看", L"Current monitor configuration is read only");
        text(monitorPolicy, 250, 208, width - 504, 25, normal, monitoring.epoch ? RGB(237, 185, 96) : Muted);
        if (snapshot.monitors.size() == 1)
        {
            const auto &monitor = snapshot.monitors[0];
            text(Db(monitor.level), 48, 239, 178, 35, large, Text);
            text(apollo::Wide(monitor.deviceName), 250, 244, 200, 28, normal, Muted);
            const std::wstring states =
                std::wstring(L"MUTE ") + (monitor.mute && monitor.mute->value.Bool() ? L"●" : L"○") +
                L"     DIM " + (monitor.dim && monitor.dim->value.Bool() ? L"●" : L"○") + L"     MONO " +
                (monitor.mono && monitor.mono->value.Bool() ? L"●" : L"○");
            text(states, width - 330, 245, 282, 28, normal, Accent, DT_RIGHT);
        }
        else
            text(snapshot.monitors.empty() ? L"—"
                                           : T(L"检测到多个监听区", L"Multiple monitor sections detected"),
                 48, 244, width - 100, 28, normal, Muted);
        card({28, 313, width - 28, height - 105});
        text(T(L"通道", L"CHANNEL"), 48, 325, width - 715, 28, heading, Text);
        text(T(L"格式", L"FORMAT"), width - 650, 325, 70, 28, normal, Muted, DT_RIGHT);
        text(T(L"增益 dB", L"GAIN dB"), width - 575, 325, 86, 28, normal, Muted, DT_RIGHT);
        text(T(L"声像", L"PAN"), width - 479, 325, 125, 28, normal, Muted, DT_RIGHT);
        text(T(L"信号 dBFS", L"LEVEL dBFS"), width - 344, 325, 94, 28, normal, Muted, DT_RIGHT);
        text(T(L"峰值 dBFS", L"PEAK dBFS"), width - 240, 325, 94, 28, normal, Muted, DT_RIGHT);
        text(L"M / S", width - 136, 325, 84, 28, normal, Muted, DT_RIGHT);
        SaveDC(dc);
        IntersectClipRect(dc, 29, 360, width - 29, height - 118);
        for (int row = firstRow; row < static_cast<int>(channels.size()); ++row)
        {
            const int y = 360 + (row - firstRow) * 34;
            if (y >= height - 118)
                break;
            const auto &channel = channels[row];
            if (channel.key == selectedKey)
            {
                RECT selected{38, y, width - 38, y + 32};
                const auto brush = CreateSolidBrush(RGB(34, 55, 66));
                FillRect(dc, &selected, brush);
                DeleteObject(brush);
            }
            text(std::to_wstring(row + 1), 48, y, 38, 32, normal, Muted);
            text(apollo::Wide(channel.name), 93, y, width - 755, 32, normal, Text);
            text(channel.stereo ? L"Stereo" : L"Mono", width - 650, y, 70, 32, normal, Muted, DT_RIGHT);
            text(Db(channel.level), width - 575, y, 86, 32, normal, Text, DT_RIGHT);
            text(Pan(channel.pan) + (channel.panRight ? L" / " + Pan(channel.panRight) : L""), width - 479, y,
                 125, 32, normal, Muted, DT_RIGHT);
            text(MeterDb(channel.meters, false), width - 344, y, 94, 32, normal, Accent, DT_RIGHT);
            text(MeterDb(channel.meters, true), width - 240, y, 94, 32, normal, Accent, DT_RIGHT);
            const auto state =
                std::wstring(channel.mute ? (channel.mute->value.Bool() ? L"M" : L"·") : L"—") + L"   " +
                (channel.solo ? (channel.solo->value.Bool() ? L"S" : L"·") : L"—");
            text(state, width - 136, y, 84, 32, normal, Text, DT_RIGHT);
        }
        RestoreDC(dc, -1);
        std::wstring footer = error;
        const auto configuration = configController.Status();
        if (footer.empty() && !control.error.empty())
        {
            if (control.error == "Sensitive control locked; confirm safety for this channel in the app")
                footer = T(L"敏感操作受保护：请先为所选通道确认“敏感操作保护”。",
                           L"Sensitive control locked; confirm safety for the selected channel.");
            else if (control.error ==
                     "Channel identity, capabilities or connection changed; pending control cleared")
                footer = T(L"通道状态已变化，本次旧操作已丢弃；权限将按最新状态继续。",
                           L"Channel state changed; stale work was discarded and access will continue on fresh state.");
            else if (control.error == "Control gesture expired before write")
                footer = T(L"本次操作超时，未发送；通道权限保持开启。",
                           L"This operation timed out and was not sent; channel permission remains enabled.");
            else
                footer = apollo::Wide(control.error);
        }
        if (footer.empty() && !monitoring.error.empty())
            footer = monitoring.error == "Control gesture expired before write"
                         ? T(L"本次操作超时，未发送；控制室权限保持开启。",
                             L"This operation timed out and was not sent; Control Room permission remains enabled.")
                         : apollo::Wide(monitoring.error);
        if (footer.empty() && experimentalConfig && !configuration.message.empty())
            footer = apollo::Wide(configuration.message);
        if (footer.empty() && !snapshot.connected)
            footer = apollo::Wide(snapshot.status);
        if (footer.empty())
            footer = adapterConflict && !eucon ? T(L"请先退出正在运行的 EUCON SDK 示例。",
                                                   L"Exit the running EUCON SDK example first.")
                     : eucon                   ? T(L"EUCON 已连接", L"EUCON connected")
                                               : T(L"硬件连接尚未启用", L"Surface connection is not enabled");
        text(footer, 28, height - 90, width - 56, 23, normal,
             error.empty() && control.error.empty() ? Muted : RGB(255, 163, 147));
        SetMapMode(dc, MM_TEXT);
        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(window, &ps);
    }
};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    auto *app = reinterpret_cast<App *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app)
        return DefWindowProcW(window, message, w, l);
    switch (message)
    {
    case EuconEventMessage:
        app->PumpEuconEvents();
        return 0;
    case WM_APP + 9:
        if (app->desktop) app->desktop->Show();
        else { ShowWindow(window, SW_RESTORE); SetForegroundWindow(window); }
        return 0;
    case WM_CREATE:
        app->dpi = GetDpiForWindow(window);
        for (auto pair : {std::pair<HWND *, int>{&app->connect, ConnectButton},
                          {&app->logs, LogsButton},
                          {&app->arm, ArmButton},
                          {&app->monitorArm, MonitorButton},
                          {&app->allChannels, AllChannelsButton},
                          {&app->safety, SafetyButton},
                          {&app->configArm, ConfigButton},
                          {&app->language, LanguageButton}})
            *pair.first = CreateWindowExW(
                0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 1, 1, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(pair.second)), nullptr, nullptr);
        app->Fonts();
        app->adapterConflict = SdkExampleAdapterRunning();
        if (!app->preview)
        {
            if (app->experimentalConfig) app->observer.EnableConfiguration();
            app->observer.Start();
        }
        SetTimer(window, 1, 33, nullptr);
        app->Layout();
        return 0;
    case WM_TIMER:
        app->Tick();
        return 0;
    case WM_COMMAND:
        app->Command(LOWORD(w));
        return 0;
    case WM_LBUTTONDOWN: {
        const int x = MulDiv(GET_X_LPARAM(l), 96, static_cast<int>(app->dpi));
        const int y = MulDiv(GET_Y_LPARAM(l), 96, static_cast<int>(app->dpi));
        RECT r{};
        GetClientRect(window, &r);
        const int width = MulDiv(r.right, 96, static_cast<int>(app->dpi));
        const int height = MulDiv(r.bottom, 96, static_cast<int>(app->dpi));
        if (x >= 38 && x < width - 38 && y >= 360 && y < height - 118)
        {
            const auto row = app->firstRow + (y - 360) / 34;
            if (row < static_cast<int>(app->channels.size()))
                app->selectedKey = app->channels[row].key;
        }
        app->Tick();
        return 0;
    }
    case WM_PAINT:
        app->Paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        app->Layout();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED:
        app->dpi = HIWORD(w);
        app->Fonts();
        {
            const auto *rect = reinterpret_cast<RECT *>(l);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                         rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto *info = reinterpret_cast<MINMAXINFO *>(l);
        info->ptMinTrackSize = {app->Px(1000), app->Px(620)};
    }
        return 0;
    case WM_VSCROLL:
        switch (LOWORD(w))
        {
        case SB_LINEUP:
            --app->firstRow;
            break;
        case SB_LINEDOWN:
            ++app->firstRow;
            break;
        case SB_PAGEUP:
            app->firstRow -= app->PageRows();
            break;
        case SB_PAGEDOWN:
            app->firstRow += app->PageRows();
            break;
        case SB_THUMBTRACK: {
            SCROLLINFO info{sizeof(info), SIF_TRACKPOS};
            GetScrollInfo(window, SB_VERT, &info);
            app->firstRow = info.nTrackPos;
        }
        break;
        }
        app->ScrollInfo();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
        app->firstRow -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 3;
        app->ScrollInfo();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_DRAWITEM: {
        auto *item = reinterpret_cast<DRAWITEMSTRUCT *>(l);
        const bool disabled = (item->itemState & ODS_DISABLED) != 0;
        const auto brush = CreateSolidBrush((item->itemState & ODS_SELECTED) ? Border : Card);
        FillRect(item->hDC, &item->rcItem, brush);
        DeleteObject(brush);
        const auto penBrush = CreateSolidBrush(Border);
        FrameRect(item->hDC, &item->rcItem, penBrush);
        DeleteObject(penBrush);
        wchar_t label[160]{};
        GetWindowTextW(item->hwndItem, label, 160);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, disabled ? Muted : Text);
        SelectObject(item->hDC, app->buttonFont);
        DrawTextW(item->hDC, label, -1, &item->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (item->itemState & ODS_FOCUS)
        {
            RECT focus = item->rcItem;
            InflateRect(&focus, -3, -3);
            DrawFocusRect(item->hDC, &focus);
        }
        return TRUE;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        app->RevokeControls();
        app->eucon.reset();
        app->observer.Stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    int count = 0;
    auto **arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool smoke = false, sdkTextTest = false, desktopTest = false, experimentalConfig = false;
    bool diagnostics = false, preview = false, previewConnected = false, background = false;
    int observe = 0;
    for (int i = 1; i < count; ++i)
    {
        if (std::wstring_view(arguments[i]) == L"--smoke-test")
            smoke = true;
        else if (std::wstring_view(arguments[i]) == L"--sdk-text-test")
            sdkTextTest = true;
        else if (std::wstring_view(arguments[i]) == L"--desktop-self-test") desktopTest = true;
        else if (std::wstring_view(arguments[i]) == L"--experimental-config")
            experimentalConfig = true;
        else if (std::wstring_view(arguments[i]) == L"--diagnostics") diagnostics = true;
        else if (std::wstring_view(arguments[i]) == L"--ui-preview") preview = true;
        else if (std::wstring_view(arguments[i]) == L"--preview-connected") previewConnected = true;
        else if (std::wstring_view(arguments[i]) == L"--background") background = true;
        else if (std::wstring_view(arguments[i]) == L"--observe-seconds" && i + 1 < count)
            observe = std::clamp(_wtoi(arguments[++i]), 1, 60);
        else
        {
            LocalFree(arguments);
            Print("Unknown argument\n");
            return 2;
        }
    }
    LocalFree(arguments);
    if (int(smoke) + int(sdkTextTest) + int(desktopTest) + int(observe != 0) + int(preview) > 1 ||
        (previewConnected && !preview) || (preview && diagnostics))
    {
        Print("Select only one diagnostic mode\n");
        return 2;
    }
    if (smoke)
    {
        apollo::Observer observer;
        const auto empty = observer.Latest();
        apollo::ChannelQueue channels;
        apollo::MonitorQueue monitor;
        Print("Smoke: offline; no sockets, audio, MIDI, or EUCON initialized. "
              "Channel and control-room permissions "
              "locked.\n");
        return empty.connected || channels.Epoch() || monitor.Epoch() ? 1 : 0;
    }
    if (desktopTest)
    {
        try { Print(apollo::RunDesktopSettingsTests()); return 0; }
        catch (const std::exception &e) { Print(std::string("FAILED: ") + e.what() + "\n"); return 1; }
    }
    if (observe)
    {
        try
        {
            return Observe(observe, experimentalConfig);
        }
        catch (const std::exception &e)
        {
            Print(std::string(e.what()) + "\n");
            return 2;
        }
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto mutex = CreateMutexW(nullptr, TRUE, L"Local\\Lindelea.ApolloBridge.EUCON.v1");
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (sdkTextTest)
        {
            CloseHandle(mutex);
            Print("Close Apollo Bridge before running the SDK text test\n");
            return 2;
        }
        if (auto window = FindWindowW(WindowClass, nullptr))
        {
            if (!background) PostMessageW(window, WM_APP + 9, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (sdkTextTest)
    {
        int result = 0;
        try
        {
            if (SdkExampleAdapterRunning())
                throw std::runtime_error("Close other EUCON test adapters first");
            Print(apollo::RunEuconTextTests());
        }
        catch (const std::exception &error)
        {
            Print(std::string("SDK text regression FAILED: ") + error.what() + "\n");
            result = 1;
        }
        CoUninitialize();
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return result;
    }
    SetCurrentProcessExplicitAppUserModelID(L"Lindelea.ApolloBridge.EUCON");
    int result = 0;
    {
        App app;
        app.diagnostics = diagnostics;
        app.preview = preview;
        if (!diagnostics && !preview)
        {
            try { app.preferences = apollo::LoadDesktopPreferences(); }
            catch (const std::exception &e)
            {
                apollo::Log(std::string("settings load rejected: ") + e.what());
                app.desktopNotice = app.T(L"设置文件无法读取。已使用只读默认设置；原文件未更改。",
                                           L"Settings could not be loaded. Read-only defaults are in use; the original file is unchanged.");
            }
        }
        if (experimentalConfig) app.preferences.configExtension = true;
        if (!diagnostics) experimentalConfig = app.preferences.configExtension;
        app.chinese = app.preferences.language == "zh-CN";
        app.experimentalConfig = experimentalConfig;
        if (previewConnected)
        {
            app.snapshot.connected = true; app.snapshot.onlineDevices = 1;
            app.snapshot.globalConfig = {{"SampleRate", "48 kHz"}, {"ClockSource", "INTERNAL"}};
            for (int i = 0; i < 14; ++i)
            {
                apollo::Channel c; c.name = "Preview " + std::to_string(i + 1); c.deviceName = "Apollo x8";
                c.ioType = i < 4 ? "Mic" : i < 8 ? "Line" : i == 8 ? "S/PDIF" : i < 11 ? "Virtual" : i < 13 ? "Aux" : "TalkbackMic";
                c.auxiliary = i == 11 || i == 12;
                const char* names[] = {"MIC/LINE 1", "MIC/LINE 2", "MIC/LINE 3", "MIC/LINE 4", "LINE 5", "LINE 6", "LINE 7", "LINE 8", "S/PDIF 1/2", "VIRTUAL 1/2", "VIRTUAL 3/4", "AUX 1", "AUX 2", "TALKBACK"};
                c.name = names[i]; c.key = "preview-" + std::to_string(i);
                const auto value = [](const std::string& json) { apollo::Parameter p; p.value = apollo::Json::Parse(json); return p; };
                c.stereo = i >= 8 && i <= 12;
                c.level = value(std::to_string(-6 - i));
                c.pan = value(c.stereo ? "-1" : i % 2 ? "0.25" : "0");
                if (c.stereo) c.panRight = value("1");
                c.mute = value(i == 5 ? "true" : "false"); c.solo = value(i == 0 ? "true" : "false");
                if (i < 11 || i == 13) c.recordPreEffects = value(i == 0 ? "false" : "true");
                c.destination = i == 5 ? "LINE 3/4" : "MONITOR";
                if (i != 6) c.meters.push_back({-24.5 - i, -6.0 - i, false});
                if (i < 4) c.preamps.push_back({});
                app.snapshot.channels.push_back(c);
            }
            apollo::Monitor m; m.name = "Monitor"; m.deviceName = "Apollo x8"; m.source = "MONITOR";
            apollo::Parameter p; p.value = apollo::Json::Parse("-24.5"); m.level = p;
            p.value = apollo::Json::Parse("false"); m.mute = m.dim = m.mono = p;
            app.snapshot.monitors.push_back(m); app.channels = app.snapshot.channels;
        }
        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = WindowClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        const int dpi = static_cast<int>(GetDpiForSystem());
        HWND window = CreateWindowExW(
            0, WindowClass, diagnostics ? L"UAD Console Bridge for EUCON — Diagnostics" : Title,
            WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN, CW_USEDEFAULT,
            CW_USEDEFAULT, MulDiv(1060, dpi, 96), MulDiv(800, dpi, 96), nullptr, nullptr, instance, &app);
        if (!window)
            result = 1;
        else
        {
            const BOOL dark = TRUE;
            DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            if (diagnostics) ShowWindow(window, show);
            else
            {
                apollo::DesktopActions actions;
                actions.save = [&](const apollo::Preferences &p, bool startup, bool activate) { return app.SavePreferences(p, startup, activate); };
                actions.connect = [&] {
                    app.autoConnectPaused = false;
                    app.euconRetryAt = {};
                    app.Command(ConnectButton);
                    app.UpdateDesktop();
                };
                actions.disconnect = [&] { app.RevokeControls(); app.desktopNotice = app.T(L"控制已暂停。", L"Control suspended."); app.UpdateDesktop(); };
                actions.lock = [&] { app.RevokeControls(); app.desktopNotice.clear(); app.UpdateDesktop(); };
                actions.exit = [&] { DestroyWindow(window); };
                bool startup = false;
                if (!preview)
                    try { startup = apollo::DesktopStartupEnabled(); }
                    catch (...) { app.desktopNotice = app.T(L"无法读取 Windows 启动项。程序仍可手动运行。", L"Unable to read Windows startup registration. Manual launch is still available."); }
                app.desktop = std::make_unique<apollo::ApolloDesktop>(app.preferences, startup, std::move(actions));
                if (!app.desktop->Create(instance, preview)) { DestroyWindow(window); result = 1; }
                else
                {
                    app.UpdateDesktop();
                    if ((!background && !app.preferences.startMinimized) || !app.desktop->TrayAvailable()) app.desktop->Show();
                }
            }
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0)
            {
                const HWND dialog = app.desktop ? app.desktop->Window() : window;
                if (!IsDialogMessageW(dialog, &message))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
        }
    }
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result;
}
